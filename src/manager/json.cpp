#include "manager/json.h"

#include <cstddef>
#include <limits>
#include <sstream>

namespace eosr {
namespace manager {

namespace {

const std::size_t max_input_bytes = 1024 * 1024;
const std::size_t max_token_bytes = 64 * 1024;
const std::size_t max_depth = 32;
const std::size_t max_values = 100000;

void append_utf8(std::string& out, u32 codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xc0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3f));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xe0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (codepoint & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
}

class parser {
public:
    explicit parser(const std::string& bytes) : bytes_(bytes), position_(0), values_(0) {}

    bool parse(json_value& out) {
        if (bytes_.size() >= 3 && static_cast<unsigned char>(bytes_[0]) == 0xef &&
            static_cast<unsigned char>(bytes_[1]) == 0xbb &&
            static_cast<unsigned char>(bytes_[2]) == 0xbf) {
            position_ = 3;
        }
        skip_spacing();
        if (!parse_value(out, 0)) {
            return false;
        }
        skip_spacing();
        return position_ == bytes_.size() ? true : fail("trailing JSON data");
    }

    const std::string& error() const { return error_; }

private:
    bool fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }

    void skip_spacing() {
        while (position_ < bytes_.size() &&
               (bytes_[position_] == ' ' || bytes_[position_] == '\t' ||
                bytes_[position_] == '\r' || bytes_[position_] == '\n')) {
            position_++;
        }
    }

    bool parse_value(json_value& out, std::size_t depth) {
        if (depth > max_depth) {
            return fail("JSON nesting is too deep");
        }
        if (++values_ > max_values) {
            return fail("JSON has too many values");
        }
        skip_spacing();
        if (position_ >= bytes_.size()) {
            return fail("expected JSON value");
        }
        const char current = bytes_[position_];
        if (current == '{') {
            return parse_object(out, depth);
        }
        if (current == '[') {
            return parse_array(out, depth);
        }
        if (current == '"') {
            out = json_value();
            out.kind = json_kind::string;
            return parse_string(out.text);
        }
        if (current == '-' || (current >= '0' && current <= '9')) {
            return parse_number(out);
        }
        if (match("true")) {
            out = json_bool(true);
            return true;
        }
        if (match("false")) {
            out = json_bool(false);
            return true;
        }
        if (match("null")) {
            out = json_value();
            return true;
        }
        return fail("invalid JSON value");
    }

    bool match(const char* literal) {
        std::size_t length = 0;
        while (literal[length] != '\0') {
            length++;
        }
        if (position_ + length > bytes_.size() ||
            bytes_.compare(position_, length, literal) != 0) {
            return false;
        }
        position_ += length;
        return true;
    }

    bool parse_object(json_value& out, std::size_t depth) {
        out = json_object();
        position_++;
        skip_spacing();
        if (position_ < bytes_.size() && bytes_[position_] == '}') {
            position_++;
            return true;
        }
        while (position_ < bytes_.size()) {
            if (bytes_[position_] != '"') {
                return fail("JSON object key must be a string");
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            if (out.members.find(key) != out.members.end()) {
                return fail("duplicate JSON object key");
            }
            skip_spacing();
            if (position_ >= bytes_.size() || bytes_[position_] != ':') {
                return fail("JSON object key has no value");
            }
            position_++;
            json_value value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.members.insert(std::make_pair(key, value));
            skip_spacing();
            if (position_ < bytes_.size() && bytes_[position_] == '}') {
                position_++;
                return true;
            }
            if (position_ >= bytes_.size() || bytes_[position_] != ',') {
                return fail("unterminated JSON object");
            }
            position_++;
            skip_spacing();
        }
        return fail("unterminated JSON object");
    }

    bool parse_array(json_value& out, std::size_t depth) {
        out = json_array();
        position_++;
        skip_spacing();
        if (position_ < bytes_.size() && bytes_[position_] == ']') {
            position_++;
            return true;
        }
        while (position_ < bytes_.size()) {
            json_value value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.elements.push_back(value);
            skip_spacing();
            if (position_ < bytes_.size() && bytes_[position_] == ']') {
                position_++;
                return true;
            }
            if (position_ >= bytes_.size() || bytes_[position_] != ',') {
                return fail("unterminated JSON array");
            }
            position_++;
            skip_spacing();
        }
        return fail("unterminated JSON array");
    }

    bool read_hex4(u32& value) {
        if (position_ + 4 > bytes_.size()) {
            return fail("truncated JSON unicode escape");
        }
        value = 0;
        for (int i = 0; i < 4; i++) {
            const char c = bytes_[position_++];
            unsigned int digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned int>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<unsigned int>(c - 'A' + 10);
            } else {
                return fail("invalid JSON unicode escape");
            }
            value = (value << 4) | digit;
        }
        return true;
    }

    bool parse_string(std::string& out) {
        position_++;
        const std::size_t source_start = position_;
        out.clear();
        while (position_ < bytes_.size()) {
            const unsigned char current = static_cast<unsigned char>(bytes_[position_++]);
            if (current == '"') {
                return true;
            }
            if (current < 0x20) {
                return fail("control byte in JSON string");
            }
            if (current == '\\') {
                if (position_ >= bytes_.size()) {
                    return fail("unterminated JSON escape");
                }
                const char escaped = bytes_[position_++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') {
                    out += escaped;
                } else if (escaped == 'b') {
                    out += '\b';
                } else if (escaped == 'f') {
                    out += '\f';
                } else if (escaped == 'n') {
                    out += '\n';
                } else if (escaped == 'r') {
                    out += '\r';
                } else if (escaped == 't') {
                    out += '\t';
                } else if (escaped == 'u') {
                    u32 codepoint = 0;
                    if (!read_hex4(codepoint)) {
                        return false;
                    }
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                        if (position_ + 2 > bytes_.size() || bytes_[position_] != '\\' ||
                            bytes_[position_ + 1] != 'u') {
                            return fail("lone high surrogate in JSON string");
                        }
                        position_ += 2;
                        u32 low = 0;
                        if (!read_hex4(low) || low < 0xdc00 || low > 0xdfff) {
                            return fail("invalid low surrogate in JSON string");
                        }
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                        return fail("lone low surrogate in JSON string");
                    }
                    append_utf8(out, codepoint);
                } else {
                    return fail("invalid JSON escape");
                }
            } else if (current < 0x80) {
                out += static_cast<char>(current);
            } else {
                std::size_t length = 0;
                u32 codepoint = 0;
                if ((current >> 5) == 0x6) {
                    length = 2;
                    codepoint = current & 0x1f;
                } else if ((current >> 4) == 0xe) {
                    length = 3;
                    codepoint = current & 0x0f;
                } else if ((current >> 3) == 0x1e) {
                    length = 4;
                    codepoint = current & 0x07;
                } else {
                    return fail("invalid UTF-8 in JSON string");
                }
                if (position_ - 1 + length > bytes_.size()) {
                    return fail("invalid UTF-8 in JSON string");
                }
                const std::size_t sequence_start = position_ - 1;
                for (std::size_t i = 1; i < length; i++) {
                    const unsigned char next = static_cast<unsigned char>(bytes_[position_++]);
                    if ((next >> 6) != 0x2) {
                        return fail("invalid UTF-8 in JSON string");
                    }
                    codepoint = (codepoint << 6) | (next & 0x3f);
                }
                if ((length == 2 && codepoint < 0x80) ||
                    (length == 3 && codepoint < 0x800) ||
                    (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                    return fail("invalid UTF-8 in JSON string");
                }
                out.append(bytes_, sequence_start, length);
            }
            if (out.size() > max_token_bytes || position_ - source_start > max_token_bytes) {
                return fail("JSON string is too long");
            }
        }
        return fail("unterminated JSON string");
    }

    bool parse_number(json_value& out) {
        const std::size_t start = position_;
        if (bytes_[position_] == '-') {
            position_++;
        }
        if (position_ >= bytes_.size() || bytes_[position_] < '0' || bytes_[position_] > '9') {
            return fail("invalid JSON number");
        }
        if (bytes_[position_] == '0') {
            position_++;
            if (position_ < bytes_.size() && bytes_[position_] >= '0' && bytes_[position_] <= '9') {
                return fail("leading zero in JSON number");
            }
        } else {
            while (position_ < bytes_.size() && bytes_[position_] >= '0' &&
                   bytes_[position_] <= '9') {
                position_++;
            }
        }
        bool integer = true;
        if (position_ < bytes_.size() && bytes_[position_] == '.') {
            integer = false;
            position_++;
            if (position_ >= bytes_.size() || bytes_[position_] < '0' || bytes_[position_] > '9') {
                return fail("invalid JSON fraction");
            }
            while (position_ < bytes_.size() && bytes_[position_] >= '0' &&
                   bytes_[position_] <= '9') {
                position_++;
            }
        }
        if (position_ < bytes_.size() && (bytes_[position_] == 'e' || bytes_[position_] == 'E')) {
            integer = false;
            position_++;
            if (position_ < bytes_.size() && (bytes_[position_] == '+' || bytes_[position_] == '-')) {
                position_++;
            }
            if (position_ >= bytes_.size() || bytes_[position_] < '0' || bytes_[position_] > '9') {
                return fail("invalid JSON exponent");
            }
            while (position_ < bytes_.size() && bytes_[position_] >= '0' &&
                   bytes_[position_] <= '9') {
                position_++;
            }
        }
        const std::string token = bytes_.substr(start, position_ - start);
        if (token.size() > max_token_bytes) {
            return fail("JSON number is too long");
        }
        out = json_value();
        out.text = token;
        if (!integer) {
            out.kind = json_kind::number;
            return true;
        }
        std::istringstream stream(token);
        i64 value = 0;
        stream >> value;
        if (!stream || !stream.eof()) {
            return fail("JSON integer is out of range");
        }
        out.kind = json_kind::integer;
        out.integer = value;
        return true;
    }

    const std::string& bytes_;
    std::size_t position_;
    std::size_t values_;
    std::string error_;
};

void append_escaped(std::string& out, const std::string& text) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (std::size_t i = 0; i < text.size(); i++) {
        const unsigned char value = static_cast<unsigned char>(text[i]);
        if (value == '"' || value == '\\') {
            out += '\\';
            out += static_cast<char>(value);
        } else if (value == '\b') {
            out += "\\b";
        } else if (value == '\f') {
            out += "\\f";
        } else if (value == '\n') {
            out += "\\n";
        } else if (value == '\r') {
            out += "\\r";
        } else if (value == '\t') {
            out += "\\t";
        } else if (value < 0x20) {
            out += "\\u00";
            out += hex[value >> 4];
            out += hex[value & 0x0f];
        } else {
            out += static_cast<char>(value);
        }
    }
    out += '"';
}

void append_json(std::string& out, const json_value& value) {
    if (value.kind == json_kind::null_value) {
        out += "null";
    } else if (value.kind == json_kind::boolean) {
        out += value.boolean ? "true" : "false";
    } else if (value.kind == json_kind::integer) {
        std::ostringstream stream;
        stream << value.integer;
        out += stream.str();
    } else if (value.kind == json_kind::number) {
        out += value.text;
    } else if (value.kind == json_kind::string) {
        append_escaped(out, value.text);
    } else if (value.kind == json_kind::array) {
        out += '[';
        for (std::size_t i = 0; i < value.elements.size(); i++) {
            if (i != 0) {
                out += ',';
            }
            append_json(out, value.elements[i]);
        }
        out += ']';
    } else {
        out += '{';
        bool first = true;
        for (std::map<std::string, json_value>::const_iterator it = value.members.begin();
             it != value.members.end(); ++it) {
            if (!first) {
                out += ',';
            }
            first = false;
            append_escaped(out, it->first);
            out += ':';
            append_json(out, it->second);
        }
        out += '}';
    }
}

} // namespace

json_value::json_value() : kind(json_kind::null_value), boolean(false), integer(0) {}

bool parse_json(const std::string& bytes, json_value& out, std::string& error) {
    out = json_value();
    error.clear();
    if (bytes.size() > max_input_bytes) {
        error = "JSON input is too large";
        return false;
    }
    parser reader(bytes);
    json_value parsed;
    if (!reader.parse(parsed)) {
        error = reader.error();
        return false;
    }
    out = parsed;
    return true;
}

std::string serialize_json(const json_value& value) {
    std::string out;
    append_json(out, value);
    out += '\n';
    return out;
}

const json_value* json_member(const json_value& object, const std::string& name) {
    if (object.kind != json_kind::object) {
        return 0;
    }
    const std::map<std::string, json_value>::const_iterator it = object.members.find(name);
    return it == object.members.end() ? 0 : &it->second;
}

json_value json_bool(bool value) {
    json_value out;
    out.kind = json_kind::boolean;
    out.boolean = value;
    return out;
}

json_value json_int(i64 value) {
    json_value out;
    out.kind = json_kind::integer;
    out.integer = value;
    return out;
}

json_value json_string(const std::string& value) {
    json_value out;
    out.kind = json_kind::string;
    out.text = value;
    return out;
}

json_value json_array() {
    json_value out;
    out.kind = json_kind::array;
    return out;
}

json_value json_object() {
    json_value out;
    out.kind = json_kind::object;
    return out;
}

} // namespace manager
} // namespace eosr
