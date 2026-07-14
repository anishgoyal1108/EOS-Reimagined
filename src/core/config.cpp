#include "core/config.h"

#include <cstddef>
#include <limits>

namespace eosr {

namespace {

const u64 default_max_bytes = 67108864;   // 64 MiB
const u64 min_max_bytes = 65536;          // 64 KiB: one full record plus the rotate record always fit
const u64 max_max_bytes = 1073741824;     // 1 GiB
const u32 default_rotated = 8;
const u32 max_rotated = 64;
const std::size_t max_display_chars = 16;
const std::size_t max_display_bytes = 64;
const std::size_t max_label_chars = 32;
const i64 max_port = 65535;
const i64 max_port_span = 64;

void add_diag(std::vector<config_diagnostic>& diagnostics, const std::string& field,
              const char* source, const char* reason, const char* action) {
    config_diagnostic diagnostic;
    diagnostic.field = field;
    diagnostic.source = source;
    diagnostic.reason = reason;
    diagnostic.action = action;
    diagnostics.push_back(diagnostic);
}

// Present and non-empty. An environment variable set to the empty string is unset, per the contract.
bool env_value(const config_source& source, const char* name, std::string& out) {
    std::string value;
    if (!source.env(name, value) || value.empty()) {
        return false;
    }
    out = value;
    return true;
}

// Parse a base-10 integer with no leading/trailing slop, rejecting overflow.
bool parse_int(const std::string& text, i64& out) {
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    const bool negative = (text[0] == '-');
    if (negative) {
        if (text.size() == 1) {
            return false;
        }
        i = 1;
    }
    i64 value = 0;
    for (; i < text.size(); i++) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        const i64 digit = c - '0';
        if (value > (std::numeric_limits<i64>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    out = negative ? -value : value;
    return true;
}

bool utf8_valid(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length;
        u32 codepoint;
        if (lead < 0x80) {
            length = 1;
            codepoint = lead;
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
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; k++) {
            const unsigned char cont = static_cast<unsigned char>(text[i + k]);
            if ((cont >> 6) != 0x2) {
                return false;
            }
            codepoint = (codepoint << 6) | (cont & 0x3F);
        }
        // Reject overlong encodings, surrogates, and anything past the Unicode maximum.
        if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
            (length == 4 && codepoint < 0x10000)) {
            return false;
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

bool has_nul(const std::string& text) {
    return text.find('\0') != std::string::npos;
}

// Why a string is unfit for a C-string field, or empty if it is fit. An embedded NUL truncates the
// value at the C ABI, so a name or path that carries one is not the value it appears to be.
const char* text_reject_reason(const std::string& text) {
    if (has_nul(text)) {
        return "embedded NUL";
    }
    if (!utf8_valid(text)) {
        return "invalid UTF-8";
    }
    return 0;
}

// Paths need not be UTF-8, but an embedded NUL still means the path the filesystem sees differs from
// the configured one.
const char* path_reject_reason(const std::string& text) {
    return has_nul(text) ? "embedded NUL" : 0;
}

// Cut `text` (already valid UTF-8) to at most `max_chars` codepoints and `max_bytes` bytes, never
// splitting a codepoint.
std::string utf8_truncate(const std::string& text, std::size_t max_chars, std::size_t max_bytes) {
    std::size_t i = 0;
    std::size_t chars = 0;
    while (i < text.size() && chars < max_chars) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if (lead >= 0xF0) {
            length = 4;
        } else if (lead >= 0xE0) {
            length = 3;
        } else if (lead >= 0xC0) {
            length = 2;
        }
        if (i + length > max_bytes || i + length > text.size()) {
            break;
        }
        i += length;
        chars++;
    }
    return text.substr(0, i);
}

bool is_absolute_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    // A Windows drive-absolute path needs a slash after the colon (C:\ or C:/). C:traces is
    // drive-relative -- it depends on the current directory on drive C -- so it is not absolute.
    const char c = path[0];
    return path.size() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\') &&
           ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

bool valid_label(const std::string& label) {
    if (label.empty() || label.size() > max_label_chars || label == "." || label == "..") {
        return false;
    }
    for (std::size_t i = 0; i < label.size(); i++) {
        const char c = label[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool parse_level(const std::string& text, trace_level& out) {
    if (text == "off") {
        out = trace_level::off;
    } else if (text == "errors") {
        out = trace_level::errors;
    } else if (text == "lifecycle") {
        out = trace_level::lifecycle;
    } else if (text == "full") {
        out = trace_level::full;
    } else {
        return false;
    }
    return true;
}

// Parse the env form "first-last" into two integers.
bool parse_port_range(const std::string& text, i64& first, i64& last) {
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= text.size()) {
        return false;
    }
    return parse_int(text.substr(0, dash), first) && parse_int(text.substr(dash + 1), last);
}

bool valid_ports(i64 first, i64 last, discovery_range& out) {
    if (first <= 0 || last <= 0 || first > max_port || last > max_port || first > last ||
        (last - first + 1) > max_port_span) {
        return false;
    }
    out.first = static_cast<u16>(first);
    out.last = static_cast<u16>(last);
    return true;
}

u64 clamp_bytes(i64 value, std::vector<config_diagnostic>& diagnostics, const char* source) {
    if (value < static_cast<i64>(min_max_bytes)) {
        add_diag(diagnostics, "trace_max_bytes", source, "below minimum", "clamped");
        return min_max_bytes;
    }
    if (static_cast<u64>(value) > max_max_bytes) {
        add_diag(diagnostics, "trace_max_bytes", source, "above maximum", "clamped");
        return max_max_bytes;
    }
    return static_cast<u64>(value);
}

u32 clamp_rotated(i64 value, std::vector<config_diagnostic>& diagnostics, const char* source) {
    if (value < 0) {
        add_diag(diagnostics, "trace_max_rotated_files", source, "negative", "clamped");
        return 0;
    }
    if (value > static_cast<i64>(max_rotated)) {
        add_diag(diagnostics, "trace_max_rotated_files", source, "above maximum", "clamped");
        return max_rotated;
    }
    return static_cast<u32>(value);
}

} // namespace

resolved_config resolve_config(const config_source& source, const config_defaults& defaults) {
    resolved_config config;
    config.data_dir = defaults.data_dir;
    config.display_name = "Player";
    config.level = trace_level::off;
    config.trace_max_bytes = default_max_bytes;
    config.trace_max_rotated_files = default_rotated;
    config.discovery_ports = defaults.default_ports;

    std::vector<config_diagnostic>& diagnostics = config.diagnostics;
    std::string raw;

    // display_name: valid UTF-8 with no embedded NUL, bounded to both EOS caps.
    {
        std::string candidate;
        const char* used = 0;
        if (env_value(source, "EOSR_DISPLAY_NAME", raw)) {
            const char* bad = text_reject_reason(raw);
            if (bad == 0) {
                candidate = raw;
                used = "environment";
            } else {
                add_diag(diagnostics, "display_name", "environment", bad, "ignored");
            }
        }
        if (used == 0) {
            const lookup found = source.file_string("display_name", raw);
            if (found == lookup::ok && !raw.empty()) {
                const char* bad = text_reject_reason(raw);
                if (bad == 0) {
                    candidate = raw;
                    used = "file";
                } else {
                    add_diag(diagnostics, "display_name", "file", bad, "ignored");
                }
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "display_name", "file", "wrong type", "ignored");
            }
        }
        if (used != 0) {
            const std::string bounded = utf8_truncate(candidate, max_display_chars, max_display_bytes);
            if (bounded.size() != candidate.size()) {
                add_diag(diagnostics, "display_name", used, "exceeds length cap", "truncated");
            }
            config.display_name = bounded;
        }
    }

    // trace_level.
    {
        trace_level level;
        bool have = false;
        if (env_value(source, "EOSR_TRACE", raw)) {
            if (parse_level(raw, level)) {
                have = true;
            } else {
                add_diag(diagnostics, "trace_level", "environment", "unrecognized value", "ignored");
            }
        }
        if (!have) {
            const lookup found = source.file_string("trace_level", raw);
            if (found == lookup::ok) {
                if (parse_level(raw, level)) {
                    have = true;
                } else {
                    add_diag(diagnostics, "trace_level", "file", "unrecognized value", "ignored");
                }
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "trace_level", "file", "wrong type", "ignored");
            }
        }
        if (have) {
            config.level = level;
        }
    }

    // trace_max_bytes.
    {
        i64 value;
        const char* used = 0;
        if (env_value(source, "EOSR_TRACE_MAX_BYTES", raw)) {
            if (parse_int(raw, value)) {
                used = "environment";
            } else {
                add_diag(diagnostics, "trace_max_bytes", "environment", "not an integer", "ignored");
            }
        }
        if (used == 0) {
            const lookup found = source.file_int("trace_max_bytes", value);
            if (found == lookup::ok) {
                used = "file";
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "trace_max_bytes", "file", "wrong type", "ignored");
            }
        }
        if (used != 0) {
            config.trace_max_bytes = clamp_bytes(value, diagnostics, used);
        }
    }

    // trace_max_rotated_files.
    {
        i64 value;
        const char* used = 0;
        if (env_value(source, "EOSR_TRACE_MAX_ROTATED", raw)) {
            if (parse_int(raw, value)) {
                used = "environment";
            } else {
                add_diag(diagnostics, "trace_max_rotated_files", "environment", "not an integer",
                         "ignored");
            }
        }
        if (used == 0) {
            const lookup found = source.file_int("trace_max_rotated_files", value);
            if (found == lookup::ok) {
                used = "file";
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "trace_max_rotated_files", "file", "wrong type", "ignored");
            }
        }
        if (used != 0) {
            config.trace_max_rotated_files = clamp_rotated(value, diagnostics, used);
        }
    }

    // discovery_ports.
    {
        discovery_range range;
        bool have = false;
        if (env_value(source, "EOSR_DISCOVERY_PORTS", raw)) {
            i64 first;
            i64 last;
            if (parse_port_range(raw, first, last) && valid_ports(first, last, range)) {
                have = true;
            } else {
                add_diag(diagnostics, "discovery_ports", "environment", "invalid range", "ignored");
            }
        }
        if (!have) {
            i64 first;
            i64 last;
            const lookup found = source.file_int_pair("discovery_ports", first, last);
            if (found == lookup::ok) {
                if (valid_ports(first, last, range)) {
                    have = true;
                } else {
                    add_diag(diagnostics, "discovery_ports", "file", "invalid range", "ignored");
                }
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "discovery_ports", "file", "wrong type", "ignored");
            }
        }
        if (have) {
            config.discovery_ports = range;
        }
    }

    // instance_label: a path-safe slug or unset. An invalid environment value still yields to the file.
    {
        bool have = false;
        if (env_value(source, "EOSR_INSTANCE_LABEL", raw)) {
            if (valid_label(raw)) {
                config.instance_label = raw;
                have = true;
            } else {
                add_diag(diagnostics, "instance_label", "environment", "not a path-safe slug",
                         "ignored");
            }
        }
        if (!have) {
            const lookup found = source.file_string("instance_label", raw);
            if (found == lookup::ok && !raw.empty()) {
                if (valid_label(raw)) {
                    config.instance_label = raw;
                } else {
                    add_diag(diagnostics, "instance_label", "file", "not a path-safe slug", "ignored");
                }
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "instance_label", "file", "wrong type", "ignored");
            }
        }
    }

    // trace_dir: relative resolves against data_dir; absolute is used as-is.
    {
        std::string dir = "traces";
        const char* used = 0;
        std::string candidate;
        if (env_value(source, "EOSR_TRACE_DIR", raw)) {
            candidate = raw;
            used = "environment";
        } else {
            const lookup found = source.file_string("trace_dir", raw);
            if (found == lookup::ok && !raw.empty()) {
                candidate = raw;
                used = "file";
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "trace_dir", "file", "wrong type", "ignored");
            }
        }
        if (used != 0) {
            const char* bad = path_reject_reason(candidate);
            if (bad == 0) {
                dir = candidate;
            } else {
                add_diag(diagnostics, "trace_dir", used, bad, "ignored");
            }
        }
        config.trace_dir = is_absolute_path(dir) ? dir : (config.data_dir + "/" + dir);
    }

    // run_dir: the runner override, or empty for the auto <trace_dir>/<run_id>.
    if (env_value(source, "EOSR_RUN_DIR", raw)) {
        const char* bad = path_reject_reason(raw);
        if (bad == 0) {
            config.run_dir = raw;
        } else {
            add_diag(diagnostics, "run_dir", "environment", bad, "ignored");
        }
    }

    return config;
}

} // namespace eosr
