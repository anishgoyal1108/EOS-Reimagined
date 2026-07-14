#include "core/config.h"

#include <cstddef>
#include <limits>

#include "platform/paths.h"

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

const std::size_t max_locale_chars = 16;

std::string lowered(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

// An environment flag: the JSON spellings plus the ones people actually type.
bool parse_flag_text(const std::string& text, bool& out) {
    const std::string value = lowered(text);
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        out = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        out = false;
        return true;
    }
    return false;
}

// The log levels the Epic emulator ecosystem already uses, so a config written for another emulator
// means the same thing here. We accept both "err" and "error", and both "warn" and "warning".
bool parse_log_level(const std::string& text, log_level& out) {
    const std::string value = lowered(text);
    if (value == "off") { out = log_level::off; return true; }
    if (value == "fatal") { out = log_level::fatal; return true; }
    if (value == "err" || value == "error") { out = log_level::error; return true; }
    if (value == "warn" || value == "warning") { out = log_level::warn; return true; }
    if (value == "info") { out = log_level::info; return true; }
    if (value == "debug") { out = log_level::debug; return true; }
    if (value == "trace") { out = log_level::trace; return true; }
    return false;
}

// An ISO-639 language tag, optionally with a region: en, en-US, pt-BR. Letters and one separator only,
// so it can never carry a path or a control character into the files or the wire.
bool valid_locale(const std::string& text) {
    if (text.empty() || text.size() > max_locale_chars) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

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

// One boolean field, environment then file then the default already in `out`. An unparseable value at
// either layer is discarded with a diagnostic and yields to the next, exactly like every other field.
// Returns which layer actually selected the value, or null when the default stood -- a later
// diagnostic about the field has to name the layer it really came from.
const char* resolve_flag(const config_source& source, const char* env_name, const char* file_key,
                         std::vector<config_diagnostic>& diagnostics, bool& out) {
    std::string raw;
    if (env_value(source, env_name, raw)) {
        bool value = false;
        if (parse_flag_text(raw, value)) {
            out = value;
            return "environment";
        }
        add_diag(diagnostics, file_key, "environment", "not a boolean", "ignored");
    }
    bool value = false;
    const lookup found = source.file_bool(file_key, value);
    if (found == lookup::ok) {
        out = value;
        return "file";
    }
    if (found == lookup::wrong_type) {
        add_diag(diagnostics, file_key, "file", "wrong type", "ignored");
    }
    return 0;
}

// A key we accept so a config written for another Epic emulator loads cleanly, but which we cannot
// honour. Reporting it is the point: silently ignoring it would leave the user believing it took.
void reject_unsupported(const config_source& source, const char* key, const char* reason,
                        std::vector<config_diagnostic>& diagnostics) {
    std::string raw;
    if (source.file_string(key, raw) != lookup::missing) {
        add_diag(diagnostics, key, "file", reason, "ignored");
    }
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
    config.locale = "en";
    config.logging = log_level::off;
    config.enable_lan = true;      // the peer mesh is the whole point; it is on unless turned off
    config.enable_overlay = false;
    config.unlock_dlcs = false;

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
            // `username` is the spelling the rest of the Epic-emulator ecosystem uses; we accept it as
            // an alias so a config written for one of those works here, with `display_name` preferred.
            lookup found = source.file_string("display_name", raw);
            if (found == lookup::missing) {
                found = source.file_string("username", raw);
            }
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
        config.trace_dir =
            platform::path_is_absolute(dir) ? dir : (config.data_dir + "/" + dir);
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

    // locale: the language the game reports back through EOS_UserInfo. `language` is the ecosystem's
    // spelling; `locale` is ours, and is preferred when both are present.
    {
        const char* used = 0;
        std::string candidate;
        if (env_value(source, "EOSR_LOCALE", raw)) {
            candidate = raw;
            used = "environment";
        } else {
            lookup found = source.file_string("locale", raw);
            if (found == lookup::missing) {
                found = source.file_string("language", raw);
            }
            if (found == lookup::ok && !raw.empty()) {
                candidate = raw;
                used = "file";
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "locale", "file", "wrong type", "ignored");
            }
        }
        if (used != 0) {
            if (valid_locale(candidate)) {
                config.locale = candidate;
            } else {
                add_diag(diagnostics, "locale", used, "not a language tag", "ignored");
            }
        }
    }

    // log_level: how much the ordinary EOS logger emits.
    {
        const char* used = 0;
        std::string candidate;
        if (env_value(source, "EOSR_LOG_LEVEL", raw)) {
            candidate = raw;
            used = "environment";
        } else {
            const lookup found = source.file_string("log_level", raw);
            if (found == lookup::ok && !raw.empty()) {
                candidate = raw;
                used = "file";
            } else if (found == lookup::wrong_type) {
                add_diag(diagnostics, "log_level", "file", "wrong type", "ignored");
            }
        }
        if (used != 0) {
            log_level level = log_level::off;
            if (parse_log_level(candidate, level)) {
                config.logging = level;
            } else {
                add_diag(diagnostics, "log_level", used, "unrecognized value", "ignored");
            }
        }
    }

    resolve_flag(source, "EOSR_ENABLE_LAN", "enable_lan", diagnostics, config.enable_lan);
    const char* overlay_source =
        resolve_flag(source, "EOSR_ENABLE_OVERLAY", "enable_overlay", diagnostics,
                     config.enable_overlay);
    const char* dlcs_source =
        resolve_flag(source, "EOSR_UNLOCK_DLCS", "unlock_dlcs", diagnostics, config.unlock_dlcs);

    // Options we accept so an ecosystem config loads, but cannot honour. Identity is derived from the
    // profile key and recomputed by every peer from the key the handshake proves (wiki/internals/adr/0001), so an
    // id we merely claimed would be rejected by the peers it is meant to reach.
    reject_unsupported(source, "epicid", "identity is derived from the profile key", diagnostics);
    reject_unsupported(source, "productuserid", "identity is derived from the profile key",
                       diagnostics);
    // Only a layer that actually asked for these can have turned them on -- both default to false --
    // so the diagnostic names that layer rather than assuming the file.
    if (config.enable_overlay && overlay_source != 0) {
        add_diag(diagnostics, "enable_overlay", overlay_source, "no overlay to show", "ignored");
    }
    if (config.unlock_dlcs && dlcs_source != 0) {
        add_diag(diagnostics, "unlock_dlcs", dlcs_source, "no ecom interface yet", "ignored");
    }

    return config;
}

} // namespace eosr
