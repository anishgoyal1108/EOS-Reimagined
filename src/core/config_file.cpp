#include "core/config_file.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <set>

namespace eosr {

lookup config_file::get_string(const std::string& key, std::string& out) const {
    std::map<std::string, node>::const_iterator it = nodes_.find(key);
    if (it == nodes_.end()) {
        return lookup::missing;
    }
    if (it->second.type != node::k_string) {
        return lookup::wrong_type;
    }
    out = it->second.str;
    return lookup::ok;
}

lookup config_file::get_int(const std::string& key, i64& out) const {
    std::map<std::string, node>::const_iterator it = nodes_.find(key);
    if (it == nodes_.end()) {
        return lookup::missing;
    }
    if (it->second.type != node::k_int) {
        return lookup::wrong_type;
    }
    out = it->second.integer;
    return lookup::ok;
}

lookup config_file::get_int_pair(const std::string& key, i64& first, i64& second) const {
    std::map<std::string, node>::const_iterator it = nodes_.find(key);
    if (it == nodes_.end()) {
        return lookup::missing;
    }
    if (it->second.type != node::k_int_array || it->second.ints.size() != 2) {
        return lookup::wrong_type;
    }
    first = it->second.ints[0];
    second = it->second.ints[1];
    return lookup::ok;
}

lookup config_file::get_bool(const std::string& key, bool& out) const {
    std::map<std::string, node>::const_iterator it = nodes_.find(key);
    if (it == nodes_.end()) {
        return lookup::missing;
    }
    if (it->second.type != node::k_bool) {
        return lookup::wrong_type;
    }
    out = it->second.flag;
    return lookup::ok;
}

lookup config_file::get_string_array(const std::string& key,
                                     std::vector<std::string>& out) const {
    std::map<std::string, node>::const_iterator it = nodes_.find(key);
    if (it == nodes_.end()) {
        return lookup::missing;
    }
    if (it->second.type != node::k_string_array) {
        return lookup::wrong_type;
    }
    out = it->second.strings;
    return lookup::ok;
}

void config_file::set_string(const std::string& key, const std::string& value) {
    node& n = nodes_[key];
    n.type = node::k_string;
    n.str = value;
}

void config_file::set_bool(const std::string& key, bool value) {
    node& n = nodes_[key];
    n.type = node::k_bool;
    n.flag = value;
}

void config_file::set_int(const std::string& key, i64 value) {
    node& n = nodes_[key];
    n.type = node::k_int;
    n.integer = value;
}

void config_file::set_int_array(const std::string& key, const std::vector<i64>& values) {
    node& n = nodes_[key];
    n.type = node::k_int_array;
    n.ints = values;
}

void config_file::set_string_array(const std::string& key,
                                   const std::vector<std::string>& values) {
    node& n = nodes_[key];
    n.type = node::k_string_array;
    n.strings = values;
}

void config_file::set_other(const std::string& key) {
    node& n = nodes_[key];
    n.type = node::k_other;
}

namespace {

const std::size_t max_input = 65536;   // 64 KiB
const std::size_t max_token = 4096;    // 4 KiB per string or number token
const std::size_t max_array = 64;
const std::size_t max_depth = 8;

// One JSON value, kept only as far as config needs to classify it. Objects are validated but their
// members are discarded; arrays keep their elements so an all-integer array can be recognized.
struct value_node {
    enum kind { v_string, v_int, v_number, v_bool, v_null, v_array, v_object };
    kind type;
    std::string str;
    i64 integer;
    std::vector<value_node> elements;
    value_node() : type(v_null), integer(0) {}
};

bool token_to_i64(const std::string& text, i64& out) {
    std::size_t i = 0;
    const bool negative = (text[0] == '-');
    if (negative) {
        i = 1;
    }
    // Accumulate magnitude as unsigned. A negative token may reach 2^63 (INT64_MIN); a positive one
    // only 2^63 - 1, so the limit depends on the sign.
    const u64 int64_min_magnitude = 9223372036854775808ULL;
    const u64 limit = negative ? int64_min_magnitude : (int64_min_magnitude - 1);
    u64 value = 0;
    for (; i < text.size(); i++) {
        const u64 digit = static_cast<u64>(text[i] - '0');
        if (value > (limit - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    if (negative) {
        out = (value == int64_min_magnitude) ? std::numeric_limits<i64>::min()
                                             : -static_cast<i64>(value);
    } else {
        out = static_cast<i64>(value);
    }
    return true;
}

void append_utf8(std::string& out, u32 codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

// A bounded recursive-descent reader for the flat config object. Every helper returns false and sets
// the (first) error on any malformed input, and the depth counter caps recursion so a hostile file
// cannot blow the stack.
class parser {
public:
    explicit parser(const std::string& text) : s_(text), pos_(0), depth_(0) {}

    bool parse(config_file& out);
    const std::string& error() const { return error_; }

private:
    bool fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }
    bool enter() { return (++depth_ <= max_depth) ? true : fail("nesting too deep"); }
    void leave() { --depth_; }
    void skip_ws();
    bool parse_value(value_node& out);
    bool parse_object(std::map<std::string, value_node>* collect);
    bool parse_array(value_node& out);
    bool parse_string(std::string& out);
    bool parse_number(value_node& out);
    bool parse_literal(const char* literal, value_node::kind kind, value_node& out);
    bool read_hex4(u32& out);

    const std::string& s_;
    std::size_t pos_;
    std::size_t depth_;
    std::string error_;
};

void parser::skip_ws() {
    while (pos_ < s_.size()) {
        const char c = s_[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pos_++;
        } else {
            break;
        }
    }
}

bool parser::read_hex4(u32& out) {
    if (pos_ + 4 > s_.size()) {
        return fail("truncated unicode escape");
    }
    u32 value = 0;
    for (int i = 0; i < 4; i++) {
        const char c = s_[pos_ + i];
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return fail("bad unicode escape");
        }
        value = (value << 4) | static_cast<u32>(digit);
    }
    pos_ += 4;
    out = value;
    return true;
}

bool parser::parse_string(std::string& out) {
    pos_++; // opening quote
    const std::size_t content_start = pos_;
    out.clear();
    while (pos_ < s_.size()) {
        const unsigned char c = static_cast<unsigned char>(s_[pos_]);
        if (c == '"') {
            pos_++;
            return true;
        }
        if (c < 0x20) {
            return fail("unescaped control character in string");
        }
        if (c != '\\') {
            // An unescaped byte must be a valid UTF-8 scalar, copied whole. The contract is UTF-8
            // only, so a stray or overlong byte is rejected rather than smuggled through.
            if (c < 0x80) {
                out += static_cast<char>(c);
                pos_++;
            } else {
                std::size_t length;
                u32 codepoint;
                if ((c >> 5) == 0x6) {
                    length = 2;
                    codepoint = c & 0x1F;
                } else if ((c >> 4) == 0xE) {
                    length = 3;
                    codepoint = c & 0x0F;
                } else if ((c >> 3) == 0x1E) {
                    length = 4;
                    codepoint = c & 0x07;
                } else {
                    return fail("invalid UTF-8 in string");
                }
                if (pos_ + length > s_.size()) {
                    return fail("invalid UTF-8 in string");
                }
                for (std::size_t k = 1; k < length; k++) {
                    const unsigned char cont = static_cast<unsigned char>(s_[pos_ + k]);
                    if ((cont >> 6) != 0x2) {
                        return fail("invalid UTF-8 in string");
                    }
                    codepoint = (codepoint << 6) | (cont & 0x3F);
                }
                if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
                    (length == 4 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
                    (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
                    return fail("invalid UTF-8 in string");
                }
                out.append(s_, pos_, length);
                pos_ += length;
            }
        } else {
            pos_++;
            if (pos_ >= s_.size()) {
                return fail("unterminated escape");
            }
            const char e = s_[pos_];
            pos_++;
            if (e == '"') {
                out += '"';
            } else if (e == '\\') {
                out += '\\';
            } else if (e == '/') {
                out += '/';
            } else if (e == 'b') {
                out += '\b';
            } else if (e == 'f') {
                out += '\f';
            } else if (e == 'n') {
                out += '\n';
            } else if (e == 'r') {
                out += '\r';
            } else if (e == 't') {
                out += '\t';
            } else if (e == 'u') {
                u32 cp;
                if (!read_hex4(cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos_ + 1 >= s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u') {
                        return fail("lone high surrogate");
                    }
                    pos_ += 2;
                    u32 low;
                    if (!read_hex4(low)) {
                        return false;
                    }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        return fail("invalid low surrogate");
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("lone low surrogate");
                }
                append_utf8(out, cp);
            } else {
                return fail("invalid escape");
            }
        }
        // Bound both the decoded output and the source span, so an escape-heavy token cannot slip
        // past the cap by decoding to fewer bytes than it occupies.
        if (out.size() > max_token || (pos_ - content_start) > max_token) {
            return fail("string too long");
        }
    }
    return fail("unterminated string");
}

bool parser::parse_number(value_node& out) {
    const std::size_t start = pos_;
    if (s_[pos_] == '-') {
        pos_++;
    }
    if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') {
        return fail("invalid number");
    }
    if (s_[pos_] == '0') {
        pos_++;
        if (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
            return fail("leading zero in number");
        }
    } else {
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
            pos_++;
        }
    }
    bool is_fractional = false;
    if (pos_ < s_.size() && s_[pos_] == '.') {
        is_fractional = true;
        pos_++;
        if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') {
            return fail("invalid fraction");
        }
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
            pos_++;
        }
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
        is_fractional = true;
        pos_++;
        if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) {
            pos_++;
        }
        if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') {
            return fail("invalid exponent");
        }
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
            pos_++;
        }
    }
    const std::string token = s_.substr(start, pos_ - start);
    if (token.size() > max_token) {
        return fail("number too long");
    }
    // A fractional or exponent number, or an integer too large for i64, is a number but not an integer.
    i64 value;
    if (!is_fractional && token_to_i64(token, value)) {
        out.type = value_node::v_int;
        out.integer = value;
    } else {
        out.type = value_node::v_number;
    }
    return true;
}

bool parser::parse_literal(const char* literal, value_node::kind kind, value_node& out) {
    const std::size_t n = std::strlen(literal);
    if (pos_ + n > s_.size() || s_.compare(pos_, n, literal) != 0) {
        return fail("invalid literal");
    }
    pos_ += n;
    out.type = kind;
    return true;
}

bool parser::parse_array(value_node& out) {
    if (!enter()) {
        return false;
    }
    out.type = value_node::v_array;
    pos_++; // '['
    skip_ws();
    if (pos_ < s_.size() && s_[pos_] == ']') {
        pos_++;
        leave();
        return true;
    }
    while (true) {
        if (out.elements.size() >= max_array) {
            return fail("too many array elements");
        }
        value_node element;
        if (!parse_value(element)) {
            return false;
        }
        out.elements.push_back(element);
        skip_ws();
        if (pos_ >= s_.size()) {
            return fail("unterminated array");
        }
        const char c = s_[pos_];
        if (c == ',') {
            pos_++;
        } else if (c == ']') {
            pos_++;
            leave();
            return true;
        } else {
            return fail("expected ',' or ']'");
        }
    }
}

bool parser::parse_object(std::map<std::string, value_node>* collect) {
    if (!enter()) {
        return false;
    }
    pos_++; // '{'
    skip_ws();
    if (pos_ < s_.size() && s_[pos_] == '}') {
        pos_++;
        leave();
        return true;
    }
    std::set<std::string> seen;
    while (true) {
        skip_ws();
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            return fail("expected string key");
        }
        std::string key;
        if (!parse_string(key)) {
            return false;
        }
        if (seen.count(key) != 0) {
            return fail("duplicate key");
        }
        seen.insert(key);
        skip_ws();
        if (pos_ >= s_.size() || s_[pos_] != ':') {
            return fail("expected ':'");
        }
        pos_++;
        value_node value;
        if (!parse_value(value)) {
            return false;
        }
        if (collect != 0) {
            (*collect)[key] = value;
        }
        skip_ws();
        if (pos_ >= s_.size()) {
            return fail("unterminated object");
        }
        const char c = s_[pos_];
        if (c == ',') {
            pos_++;
        } else if (c == '}') {
            pos_++;
            leave();
            return true;
        } else {
            return fail("expected ',' or '}'");
        }
    }
}

bool parser::parse_value(value_node& out) {
    skip_ws();
    if (pos_ >= s_.size()) {
        return fail("unexpected end of input");
    }
    const char c = s_[pos_];
    if (c == '{') {
        if (!parse_object(0)) {
            return false;
        }
        out.type = value_node::v_object;
        return true;
    }
    if (c == '[') {
        return parse_array(out);
    }
    if (c == '"') {
        out.type = value_node::v_string;
        return parse_string(out.str);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_number(out);
    }
    if (c == 't') {
        if (!parse_literal("true", value_node::v_bool, out)) {
            return false;
        }
        out.integer = 1; // a bool carries its value in `integer`; the node kind says how to read it
        return true;
    }
    if (c == 'f') {
        if (!parse_literal("false", value_node::v_bool, out)) {
            return false;
        }
        out.integer = 0;
        return true;
    }
    if (c == 'n') {
        return parse_literal("null", value_node::v_null, out);
    }
    return fail("unexpected character");
}

// Reduce a parsed top-level value to the shape config reads.
void store(config_file& out, const std::string& key, const value_node& value) {
    if (value.type == value_node::v_string) {
        out.set_string(key, value.str);
    } else if (value.type == value_node::v_int) {
        out.set_int(key, value.integer);
    } else if (value.type == value_node::v_bool) {
        out.set_bool(key, value.integer != 0);
    } else if (value.type == value_node::v_array) {
        std::vector<i64> ints;
        std::vector<std::string> strings;
        bool all_int = true;
        bool all_string = true;
        for (std::size_t i = 0; i < value.elements.size(); i++) {
            if (value.elements[i].type != value_node::v_int) {
                all_int = false;
            } else {
                ints.push_back(value.elements[i].integer);
            }
            if (value.elements[i].type != value_node::v_string) {
                all_string = false;
            } else {
                strings.push_back(value.elements[i].str);
            }
        }
        if (all_string) {
            out.set_string_array(key, strings);
        } else if (all_int) {
            out.set_int_array(key, ints);
        } else {
            out.set_other(key);
        }
    } else {
        out.set_other(key);
    }
}

bool parser::parse(config_file& out) {
    // A leading UTF-8 BOM is skipped; no other encoding is accepted.
    if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF &&
        static_cast<unsigned char>(s_[1]) == 0xBB && static_cast<unsigned char>(s_[2]) == 0xBF) {
        pos_ = 3;
    }
    skip_ws();
    if (pos_ >= s_.size() || s_[pos_] != '{') {
        return fail("top-level value must be an object");
    }
    std::map<std::string, value_node> top;
    if (!parse_object(&top)) {
        return false;
    }
    skip_ws();
    if (pos_ != s_.size()) {
        return fail("trailing content after top-level object");
    }
    std::map<std::string, value_node>::const_iterator it = top.begin();
    for (; it != top.end(); ++it) {
        store(out, it->first, it->second);
    }
    return true;
}

} // namespace

bool parse_config_file(const std::string& bytes, config_file& out, std::string& error) {
    // The contract is that failure leaves `out` empty, so clear any prior contents up front -- a
    // caller reusing the object must never mistake stale values for the malformed file's.
    out = config_file();
    if (bytes.size() > max_input) {
        error = "input too large";
        return false;
    }
    parser reader(bytes);
    config_file result;
    if (!reader.parse(result)) {
        error = reader.error();
        return false;
    }
    out = result;
    return true;
}

} // namespace eosr
