#include "manager/vdf.h"

#include <cstddef>

namespace eosr {
namespace manager {

namespace {

const std::size_t max_input_bytes = 1024 * 1024;
const std::size_t max_token_bytes = 4096;
const std::size_t max_depth = 16;
const std::size_t max_entries = 100000;

bool ascii_equal_case_insensitive(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); i++) {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

class vdf_parser {
public:
    explicit vdf_parser(const std::string& text) : text_(text), position_(0), entry_count_(0) {}

    bool parse(vdf_document& out) {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            position_ = 3;
        }
        return parse_entries(out.entries, false, 0);
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
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                position_++;
                continue;
            }
            if (c == '/' && position_ + 1 < text_.size() && text_[position_ + 1] == '/') {
                position_ += 2;
                while (position_ < text_.size() && text_[position_] != '\n') {
                    position_++;
                }
                continue;
            }
            break;
        }
    }

    bool parse_entries(std::vector<vdf_entry>& entries, bool expect_close, std::size_t depth) {
        if (depth > max_depth) {
            return fail("VDF nesting is too deep");
        }
        while (true) {
            skip_spacing();
            if (position_ >= text_.size()) {
                return expect_close ? fail("unterminated VDF object") : true;
            }
            if (text_[position_] == '}') {
                if (!expect_close) {
                    return fail("unexpected closing brace");
                }
                position_++;
                return true;
            }
            if (text_[position_] == '{') {
                return fail("VDF object has no key");
            }

            vdf_entry entry;
            if (!parse_token(entry.key)) {
                return false;
            }
            skip_spacing();
            if (position_ >= text_.size() || text_[position_] == '}') {
                return fail("VDF key has no value");
            }
            if (++entry_count_ > max_entries) {
                return fail("VDF has too many entries");
            }
            if (text_[position_] == '{') {
                position_++;
                entry.is_object = true;
                if (!parse_entries(entry.children, true, depth + 1)) {
                    return false;
                }
            } else if (!parse_token(entry.value)) {
                return false;
            }
            entries.push_back(entry);
        }
    }

    bool parse_token(std::string& out) {
        if (position_ >= text_.size()) {
            return fail("expected VDF token");
        }
        if (text_[position_] == '"') {
            return parse_quoted(out);
        }
        const std::size_t start = position_;
        while (position_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[position_]);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}') {
                break;
            }
            if (c == 0 || c < 0x20) {
                return fail("control byte in VDF token");
            }
            position_++;
            if (position_ - start > max_token_bytes) {
                return fail("VDF token is too long");
            }
        }
        if (position_ == start) {
            return fail("expected VDF token");
        }
        out.assign(text_, start, position_ - start);
        return true;
    }

    bool parse_quoted(std::string& out) {
        position_++;
        const std::size_t source_start = position_;
        out.clear();
        while (position_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') {
                return true;
            }
            if (c == 0 || (c < 0x20 && c != '\t')) {
                return fail("control byte in quoted VDF token");
            }
            if (c != '\\') {
                out += static_cast<char>(c);
            } else {
                if (position_ >= text_.size()) {
                    return fail("unterminated VDF escape");
                }
                const char escaped = text_[position_++];
                if (escaped == '"' || escaped == '\\') {
                    out += escaped;
                } else if (escaped == 'n') {
                    out += '\n';
                } else if (escaped == 'r') {
                    out += '\r';
                } else if (escaped == 't') {
                    out += '\t';
                } else {
                    // Steam paths from older tools occasionally contain a single backslash. Keep an
                    // unknown escape verbatim so D:\Steam never silently becomes D:Steam.
                    out += '\\';
                    out += escaped;
                }
            }
            if (out.size() > max_token_bytes || position_ - source_start > max_token_bytes) {
                return fail("VDF token is too long");
            }
        }
        return fail("unterminated quoted VDF token");
    }

    const std::string& text_;
    std::size_t position_;
    std::size_t entry_count_;
    std::string error_;
};

} // namespace

bool parse_vdf(const std::string& bytes, vdf_document& out, std::string& error) {
    out = vdf_document();
    error.clear();
    if (bytes.size() > max_input_bytes) {
        error = "VDF input is too large";
        return false;
    }
    vdf_parser parser(bytes);
    vdf_document result;
    if (!parser.parse(result)) {
        error = parser.error();
        return false;
    }
    out = result;
    return true;
}

const vdf_entry* find_vdf_entry(const std::vector<vdf_entry>& entries, const std::string& key) {
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].key == key) {
            return &entries[i];
        }
    }
    return 0;
}

const vdf_entry* find_vdf_entry_case_insensitive(const std::vector<vdf_entry>& entries,
                                                 const std::string& key) {
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (ascii_equal_case_insensitive(entries[i].key, key)) {
            return &entries[i];
        }
    }
    return 0;
}

} // namespace manager
} // namespace eosr
