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

json_writer::json_writer(std::size_t max_bytes)
    : max_bytes_(max_bytes), after_key_(false), valid_(true), root_done_(false) {}

bool json_writer::reserve(std::size_t n) {
    if (!valid_) {
        return false;
    }
    if (max_bytes_ != 0 && out_.size() + n > max_bytes_) {
        valid_ = false;
        return false;
    }
    return true;
}

void json_writer::pre_value() {
    if (!valid_) {
        return;
    }
    if (levels_.empty()) {
        // A value at the top level is allowed only as the single whole document.
        if (root_done_ || !out_.empty()) {
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
        if (!reserve(1)) {
            return;
        }
        out_ += ',';
    }
    current.first = false;
}

void json_writer::write_string(const std::string& value) {
    if (!reserve(1)) {
        return;
    }
    out_ += '"';
    std::size_t i = 0;
    while (i < value.size()) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t' || c == '\b' || c == '\f') {
            if (!reserve(2)) {
                return;
            }
            out_ += '\\';
            out_ += (c == '"') ? '"' : (c == '\\') ? '\\' : (c == '\n') ? 'n' : (c == '\r') ? 'r'
                    : (c == '\t') ? 't' : (c == '\b') ? 'b' : 'f';
            i++;
        } else if (c < 0x20) {
            if (!reserve(6)) {
                return;
            }
            append_u_escape(out_, c);
            i++;
        } else if (c < 0x80) {
            if (!reserve(1)) {
                return;
            }
            out_ += static_cast<char>(c);
            i++;
        } else {
            std::size_t length;
            if (decode_utf8(value, i, length)) {
                if (!reserve(length)) {
                    return;
                }
                out_.append(value, i, length);
                i += length;
            } else {
                // Never emit an invalid byte: stand U+FFFD in for it and advance one byte.
                if (!reserve(3)) {
                    return;
                }
                out_ += "\xEF\xBF\xBD";
                i++;
            }
        }
    }
    if (!reserve(1)) {
        return;
    }
    out_ += '"';
}

void json_writer::begin_object() {
    pre_value();
    if (!valid_ || !reserve(1)) {
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
    if (levels_.empty()) {
        root_done_ = true;
    }
}

void json_writer::begin_array() {
    pre_value();
    if (!valid_ || !reserve(1)) {
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
    if (levels_.empty()) {
        root_done_ = true;
    }
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
        if (!reserve(1)) {
            return;
        }
        out_ += ',';
    }
    current.first = false;
    write_string(name);
    if (!reserve(1)) {
        return;
    }
    out_ += ':';
    after_key_ = true;
}

void json_writer::mark_root_if_top() {
    if (levels_.empty()) {
        root_done_ = true;
    }
}

void json_writer::value_string(const std::string& value) {
    pre_value();
    if (!valid_) {
        return;
    }
    write_string(value);
    mark_root_if_top();
}

void json_writer::value_int(i64 value) {
    pre_value();
    const std::string text = std::to_string(value);
    if (!valid_ || !reserve(text.size())) {
        return;
    }
    out_ += text;
    mark_root_if_top();
}

void json_writer::value_uint(u64 value) {
    pre_value();
    const std::string text = std::to_string(value);
    if (!valid_ || !reserve(text.size())) {
        return;
    }
    out_ += text;
    mark_root_if_top();
}

void json_writer::value_bool(bool value) {
    pre_value();
    if (!valid_ || !reserve(value ? 4 : 5)) {
        return;
    }
    out_ += value ? "true" : "false";
    mark_root_if_top();
}

void json_writer::value_null() {
    pre_value();
    if (!valid_ || !reserve(4)) {
        return;
    }
    out_ += "null";
    mark_root_if_top();
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
