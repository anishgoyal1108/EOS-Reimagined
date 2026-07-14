#include "common/json_writer.h"

#include <cstddef>
#include <string>

namespace eosr {

namespace {

const char* const hex_digits = "0123456789abcdef";

void append_u_escape(std::string& out, u32 codepoint) {
    out += "\\u";
    out += hex_digits[(codepoint >> 12) & 0xF];
    out += hex_digits[(codepoint >> 8) & 0xF];
    out += hex_digits[(codepoint >> 4) & 0xF];
    out += hex_digits[codepoint & 0xF];
}

// Validate the UTF-8 sequence starting at `i`. On success, set `length` to its byte count. A byte that
// does not begin a valid, minimally-encoded, in-range, non-surrogate scalar returns false.
bool decode_utf8(const std::string& s, std::size_t i, std::size_t& length) {
    const unsigned char lead = static_cast<unsigned char>(s[i]);
    u32 codepoint;
    if (lead < 0x80) {
        length = 1;
        return true;
    } else if ((lead >> 5) == 0x6) {
        length = 2;
        codepoint = lead & 0x1F;
    } else if ((lead >> 4) == 0xE) {
        length = 3;
        codepoint = lead & 0x0F;
    } else if ((lead >> 3) == 0x1E) {
        length = 4;
        codepoint = lead & 0x07;
    } else {
        return false;
    }
    if (i + length > s.size()) {
        return false;
    }
    for (std::size_t k = 1; k < length; k++) {
        const unsigned char cont = static_cast<unsigned char>(s[i + k]);
        if ((cont >> 6) != 0x2) {
            return false;
        }
        codepoint = (codepoint << 6) | (cont & 0x3F);
    }
    if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
        (length == 4 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return false;
    }
    return true;
}

} // namespace

json_writer::json_writer() : after_key_(false), valid_(true) {}

void json_writer::pre_value() {
    if (!valid_) {
        return;
    }
    if (levels_.empty()) {
        // A value at the top level is allowed only as the single whole document.
        if (!out_.empty()) {
            valid_ = false;
        }
        return;
    }
    level& current = levels_.back();
    if (current.is_object) {
        // A value inside an object must follow a key.
        if (!after_key_) {
            valid_ = false;
            return;
        }
        after_key_ = false;
        return;
    }
    if (!current.first) {
        out_ += ',';
    }
    current.first = false;
}

void json_writer::write_string(const std::string& value) {
    out_ += '"';
    std::size_t i = 0;
    while (i < value.size()) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '"') {
            out_ += "\\\"";
            i++;
        } else if (c == '\\') {
            out_ += "\\\\";
            i++;
        } else if (c == '\n') {
            out_ += "\\n";
            i++;
        } else if (c == '\r') {
            out_ += "\\r";
            i++;
        } else if (c == '\t') {
            out_ += "\\t";
            i++;
        } else if (c == '\b') {
            out_ += "\\b";
            i++;
        } else if (c == '\f') {
            out_ += "\\f";
            i++;
        } else if (c < 0x20) {
            append_u_escape(out_, c);
            i++;
        } else if (c < 0x80) {
            out_ += static_cast<char>(c);
            i++;
        } else {
            std::size_t length;
            if (decode_utf8(value, i, length)) {
                out_.append(value, i, length);
                i += length;
            } else {
                // Never emit an invalid byte: stand U+FFFD in for it and advance one byte.
                out_ += "\xEF\xBF\xBD";
                i++;
            }
        }
    }
    out_ += '"';
}

void json_writer::begin_object() {
    pre_value();
    if (!valid_) {
        return;
    }
    out_ += '{';
    const level opened = {true, true};
    levels_.push_back(opened);
}

void json_writer::end_object() {
    if (!valid_) {
        return;
    }
    // Closing must match an open object, and never with a key left dangling.
    if (levels_.empty() || !levels_.back().is_object || after_key_) {
        valid_ = false;
        return;
    }
    out_ += '}';
    levels_.pop_back();
}

void json_writer::begin_array() {
    pre_value();
    if (!valid_) {
        return;
    }
    out_ += '[';
    const level opened = {false, true};
    levels_.push_back(opened);
}

void json_writer::end_array() {
    if (!valid_) {
        return;
    }
    if (levels_.empty() || levels_.back().is_object) {
        valid_ = false;
        return;
    }
    out_ += ']';
    levels_.pop_back();
}

void json_writer::key(const std::string& name) {
    if (!valid_) {
        return;
    }
    // A key belongs only inside an object, and only where a key is expected.
    if (levels_.empty() || !levels_.back().is_object || after_key_) {
        valid_ = false;
        return;
    }
    level& current = levels_.back();
    if (!current.first) {
        out_ += ',';
    }
    current.first = false;
    write_string(name);
    out_ += ':';
    after_key_ = true;
}

void json_writer::value_string(const std::string& value) {
    pre_value();
    if (!valid_) {
        return;
    }
    write_string(value);
}

void json_writer::value_int(i64 value) {
    pre_value();
    if (!valid_) {
        return;
    }
    out_ += std::to_string(value);
}

void json_writer::value_uint(u64 value) {
    pre_value();
    if (!valid_) {
        return;
    }
    out_ += std::to_string(value);
}

void json_writer::value_bool(bool value) {
    pre_value();
    if (!valid_) {
        return;
    }
    out_ += value ? "true" : "false";
}

void json_writer::value_null() {
    pre_value();
    if (!valid_) {
        return;
    }
    out_ += "null";
}

void json_writer::field_string(const std::string& name, const std::string& value) {
    key(name);
    value_string(value);
}

void json_writer::field_int(const std::string& name, i64 value) {
    key(name);
    value_int(value);
}

void json_writer::field_uint(const std::string& name, u64 value) {
    key(name);
    value_uint(value);
}

void json_writer::field_bool(const std::string& name, bool value) {
    key(name);
    value_bool(value);
}

void json_writer::field_null(const std::string& name) {
    key(name);
    value_null();
}

} // namespace eosr
