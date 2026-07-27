#include "manager/configuration_model.h"

#include <set>

#include "manager/manager_state.h"
#include "manager/sha256.h"

namespace eosr {
namespace manager {

namespace {

const std::size_t max_config_bytes = 64 * 1024;

configuration_field field(const char* key, const char* environment, const char* group,
                          configuration_field_kind kind, bool available = true) {
    configuration_field value;
    value.key = key;
    value.environment = environment;
    value.group = group;
    value.kind = kind;
    value.available = available;
    return value;
}

void import_issue(std::vector<configuration_import_issue>& issues, const char* key) {
    configuration_import_issue issue;
    issue.field = key;
    issue.reason = "wrong type";
    issue.action = "ignored; SDK default retained";
    issues.push_back(issue);
}

bool get_string(const json_value& root, const char* key, std::string& out, bool& present,
                std::vector<configuration_import_issue>& issues) {
    const json_value* value = json_member(root, key);
    present = value != 0;
    if (value == 0) {
        return true;
    }
    if (value->kind != json_kind::string) {
        import_issue(issues, key);
        return true;
    }
    out = value->text;
    return true;
}

bool get_bool(const json_value& root, const char* key, bool& out,
              std::vector<configuration_import_issue>& issues) {
    const json_value* value = json_member(root, key);
    if (value == 0) {
        return true;
    }
    if (value->kind != json_kind::boolean) {
        import_issue(issues, key);
        return true;
    }
    out = value->boolean;
    return true;
}

bool get_int(const json_value& root, const char* key, i64& out,
             std::vector<configuration_import_issue>& issues) {
    const json_value* value = json_member(root, key);
    if (value == 0) {
        return true;
    }
    if (value->kind != json_kind::integer) {
        import_issue(issues, key);
        return true;
    }
    out = value->integer;
    return true;
}

bool get_ports(const json_value& root, u16& first, u16& last,
               std::vector<configuration_import_issue>& issues) {
    const json_value* value = json_member(root, "discovery_ports");
    if (value == 0) {
        return true;
    }
    if (value->kind != json_kind::array || value->elements.size() != 2 ||
        value->elements[0].kind != json_kind::integer ||
        value->elements[1].kind != json_kind::integer || value->elements[0].integer < 0 ||
        value->elements[0].integer > 65535 || value->elements[1].integer < 0 ||
        value->elements[1].integer > 65535) {
        import_issue(issues, "discovery_ports");
        return true;
    }
    first = static_cast<u16>(value->elements[0].integer);
    last = static_cast<u16>(value->elements[1].integer);
    return true;
}

bool get_seeds(const json_value& root, std::vector<std::string>& out,
               std::vector<configuration_import_issue>& issues) {
    const json_value* value = json_member(root, "peer_seeds");
    if (value == 0) {
        return true;
    }
    if (value->kind != json_kind::array) {
        import_issue(issues, "peer_seeds");
        return true;
    }
    std::vector<std::string> seeds;
    for (std::size_t i = 0; i < value->elements.size(); i++) {
        if (value->elements[i].kind != json_kind::string) {
            import_issue(issues, "peer_seeds");
            return true;
        }
        seeds.push_back(value->elements[i].text);
    }
    out = seeds;
    return true;
}

void diagnostic(configuration_validation& out, const char* field_name, const char* reason,
                const char* action, bool invalid = false) {
    configuration_diagnostic value;
    value.field = field_name;
    value.reason = reason;
    value.action = action;
    out.diagnostics.push_back(value);
    if (invalid) {
        out.valid = false;
    }
}

bool utf8_next(const std::string& text, std::size_t offset, std::size_t& length) {
    if (offset >= text.size()) {
        return false;
    }
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    u32 codepoint = 0;
    if (lead < 0x80) {
        length = 1;
        codepoint = lead;
    } else if ((lead >> 5) == 0x6) {
        length = 2;
        codepoint = lead & 0x1f;
    } else if ((lead >> 4) == 0xe) {
        length = 3;
        codepoint = lead & 0x0f;
    } else if ((lead >> 3) == 0x1e) {
        length = 4;
        codepoint = lead & 0x07;
    } else {
        return false;
    }
    if (offset + length > text.size()) {
        return false;
    }
    for (std::size_t i = 1; i < length; i++) {
        const unsigned char next = static_cast<unsigned char>(text[offset + i]);
        if ((next >> 6) != 0x2) {
            return false;
        }
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    return !((length == 2 && codepoint < 0x80) ||
             (length == 3 && codepoint < 0x800) ||
             (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff ||
             (codepoint >= 0xd800 && codepoint <= 0xdfff));
}

bool valid_utf8(const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        std::size_t length = 0;
        if (!utf8_next(text, offset, length)) {
            return false;
        }
        offset += length;
    }
    return true;
}

std::string truncate_display(const std::string& text) {
    std::size_t offset = 0;
    std::size_t characters = 0;
    while (offset < text.size() && characters < 16) {
        std::size_t length = 0;
        if (!utf8_next(text, offset, length) || offset + length > 64) {
            break;
        }
        offset += length;
        characters++;
    }
    return text.substr(0, offset);
}

bool valid_locale(const std::string& value) {
    if (value.empty() || value.size() > 16) {
        return false;
    }
    int separators = 0;
    for (std::size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter) {
            if ((c != '-' && c != '_') || ++separators > 1 || i == 0 ||
                i + 1 == value.size()) {
                return false;
            }
        }
    }
    return true;
}

bool valid_slug(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    if (value.size() > 32 || value == "." || value == "..") {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool ipv4(const std::string& value) {
    std::size_t position = 0;
    unsigned int address = 0;
    for (int part = 0; part < 4; part++) {
        const std::size_t start = position;
        unsigned int octet = 0;
        while (position < value.size() && value[position] >= '0' && value[position] <= '9') {
            octet = octet * 10 + static_cast<unsigned int>(value[position] - '0');
            if (octet > 255) {
                return false;
            }
            position++;
        }
        if (position == start || (position - start > 1 && value[start] == '0')) {
            return false;
        }
        address = (address << 8) | octet;
        if (part != 3) {
            if (position >= value.size() || value[position++] != '.') {
                return false;
            }
        }
    }
    return position == value.size() && address != 0 && (address >> 24) < 224;
}

std::string normalize_log_level(const std::string& value) {
    if (value == "error") {
        return "err";
    }
    if (value == "warning") {
        return "warn";
    }
    return value;
}

bool choice(const std::string& value, const char* const* choices, std::size_t count) {
    for (std::size_t i = 0; i < count; i++) {
        if (value == choices[i]) {
            return true;
        }
    }
    return false;
}

bool hash_transaction_file(const std::string& path, transaction_filesystem& filesystem,
                           std::string& out) {
    out.clear();
    transaction_handle handle = 0;
    if (filesystem.open_read(path, handle) != transaction_io_result::ok) {
        return false;
    }
    sha256_hasher hasher;
    unsigned char buffer[4096];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(handle, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) {
            break;
        }
        if (read != transaction_io_result::ok || count == 0) {
            ok = false;
            break;
        }
        hasher.update(buffer, count);
    }
    if (filesystem.close(handle) != transaction_io_result::ok) {
        ok = false;
    }
    if (ok) {
        out = hasher.final_hex();
    }
    return ok;
}

bool write_transaction_file(const std::string& path, const std::string& bytes,
                            transaction_filesystem& filesystem) {
    transaction_handle handle = 0;
    if (filesystem.create_new(path, handle) != transaction_io_result::ok) {
        return false;
    }
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        std::size_t written = 0;
        if (filesystem.write(handle,
                reinterpret_cast<const unsigned char*>(bytes.data()) + offset,
                bytes.size() - offset, written) != transaction_io_result::ok || written == 0 ||
            written > bytes.size() - offset) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && filesystem.flush(handle) != transaction_io_result::ok) {
        ok = false;
    }
    if (filesystem.close(handle) != transaction_io_result::ok) {
        ok = false;
    }
    return ok;
}

bool copy_transaction_file(const std::string& from, const std::string& to,
                           transaction_filesystem& filesystem) {
    transaction_handle input = 0;
    transaction_handle output = 0;
    if (filesystem.open_read(from, input) != transaction_io_result::ok) {
        return false;
    }
    if (filesystem.create_new(to, output) != transaction_io_result::ok) {
        filesystem.close(input);
        return false;
    }
    unsigned char buffer[4096];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = filesystem.read(input, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) {
            break;
        }
        if (read != transaction_io_result::ok || count == 0) {
            ok = false;
            break;
        }
        std::size_t offset = 0;
        while (offset < count) {
            std::size_t written = 0;
            if (filesystem.write(output, buffer + offset, count - offset, written) !=
                    transaction_io_result::ok || written == 0 || written > count - offset) {
                ok = false;
                break;
            }
            offset += written;
        }
    }
    if (ok && filesystem.flush(output) != transaction_io_result::ok) {
        ok = false;
    }
    if (filesystem.close(input) != transaction_io_result::ok) {
        ok = false;
    }
    if (filesystem.close(output) != transaction_io_result::ok) {
        ok = false;
    }
    return ok;
}

bool path_is_missing(const std::string& path, transaction_filesystem& filesystem) {
    transaction_file_info info;
    return filesystem.inspect(path, info) == transaction_io_result::missing;
}

configuration_save_result save_result(configuration_save_code code, const std::string& detail) {
    configuration_save_result out;
    out.code = code;
    out.detail = detail;
    return out;
}

} // namespace

const std::vector<configuration_field>& configuration_fields() {
    static std::vector<configuration_field> fields;
    if (fields.empty()) {
        fields.push_back(field("display_name", "EOSR_DISPLAY_NAME", "Identity",
                               configuration_field_kind::text));
        fields.push_back(field("locale", "EOSR_LOCALE", "Language",
                               configuration_field_kind::text));
        fields.push_back(field("discovery_ports", "EOSR_DISCOVERY_PORTS", "LAN",
                               configuration_field_kind::port_range));
        fields.push_back(field("peer_seeds", "EOSR_PEER_SEEDS", "LAN",
                               configuration_field_kind::string_list));
        fields.push_back(field("enable_lan", "EOSR_ENABLE_LAN", "LAN",
                               configuration_field_kind::boolean));
        fields.push_back(field("log_level", "EOSR_LOG_LEVEL", "Diagnostics",
                               configuration_field_kind::choice));
        fields.push_back(field("trace_level", "EOSR_TRACE", "Diagnostics",
                               configuration_field_kind::choice));
        fields.push_back(field("trace_dir", "EOSR_TRACE_DIR", "Diagnostics",
                               configuration_field_kind::text));
        fields.push_back(field("trace_max_bytes", "EOSR_TRACE_MAX_BYTES", "Diagnostics",
                               configuration_field_kind::integer));
        fields.push_back(field("trace_max_rotated_files", "EOSR_TRACE_MAX_ROTATED",
                               "Diagnostics", configuration_field_kind::integer));
        fields.push_back(field("instance_label", "EOSR_INSTANCE_LABEL", "Identity",
                               configuration_field_kind::text));
        fields.push_back(field("enable_overlay", "EOSR_ENABLE_OVERLAY", "Compatibility",
                               configuration_field_kind::boolean, false));
        fields.push_back(field("unlock_dlcs", "EOSR_UNLOCK_DLCS", "Compatibility",
                               configuration_field_kind::boolean, false));
    }
    return fields;
}

configuration_validation::configuration_validation() : valid(true) {}

configuration_save_request::configuration_save_request()
    : existing_was_malformed(false), replace_confirmed(false) {}

configuration_save_result::configuration_save_result()
    : code(configuration_save_code::invalid_request) {}

manager_configuration default_manager_configuration() {
    manager_configuration config;
    config.display_name = "Player";
    config.locale = "en";
    config.discovery_first = 55789;
    config.discovery_last = 55798;
    config.enable_lan = true;
    config.log_level = "off";
    config.trace_level = "off";
    config.trace_dir = "traces";
    config.trace_max_bytes = 67108864;
    config.trace_max_rotated_files = 8;
    config.instance_label.clear();
    config.enable_overlay = false;
    config.unlock_dlcs = false;
    config.source_document = json_object();
    return config;
}

bool parse_manager_configuration(const std::string& bytes, manager_configuration& out,
                                 std::string& error) {
    out = default_manager_configuration();
    error.clear();
    if (bytes.size() > max_config_bytes) {
        error = "configuration input is too large";
        return false;
    }
    json_value root;
    if (!parse_json(bytes, root, error)) {
        return false;
    }
    if (root.kind != json_kind::object) {
        error = "configuration must be a JSON object";
        return false;
    }
    manager_configuration parsed = default_manager_configuration();
    parsed.source_document = root;
    bool display_present = false;
    if (!get_string(root, "display_name", parsed.display_name, display_present,
                    parsed.import_issues)) {
        return false;
    }
    if (!display_present) {
        bool alias_present = false;
        if (!get_string(root, "username", parsed.display_name, alias_present,
                        parsed.import_issues)) {
            return false;
        }
    }
    bool locale_present = false;
    if (!get_string(root, "locale", parsed.locale, locale_present, parsed.import_issues)) {
        return false;
    }
    if (!locale_present) {
        bool alias_present = false;
        if (!get_string(root, "language", parsed.locale, alias_present, parsed.import_issues)) {
            return false;
        }
    }
    bool present = false;
    if (!get_ports(root, parsed.discovery_first, parsed.discovery_last, parsed.import_issues) ||
        !get_seeds(root, parsed.peer_seeds, parsed.import_issues) ||
        !get_bool(root, "enable_lan", parsed.enable_lan, parsed.import_issues) ||
        !get_string(root, "log_level", parsed.log_level, present, parsed.import_issues) ||
        !get_string(root, "trace_level", parsed.trace_level, present, parsed.import_issues) ||
        !get_string(root, "trace_dir", parsed.trace_dir, present, parsed.import_issues) ||
        !get_int(root, "trace_max_bytes", parsed.trace_max_bytes, parsed.import_issues) ||
        !get_int(root, "trace_max_rotated_files", parsed.trace_max_rotated_files,
                 parsed.import_issues) ||
        !get_string(root, "instance_label", parsed.instance_label, present,
                    parsed.import_issues) ||
        !get_bool(root, "enable_overlay", parsed.enable_overlay, parsed.import_issues) ||
        !get_bool(root, "unlock_dlcs", parsed.unlock_dlcs, parsed.import_issues)) {
        return false;
    }
    out = parsed;
    return true;
}

configuration_validation validate_manager_configuration(const manager_configuration& config) {
    configuration_validation out;
    out.normalized = config;
    if (config.display_name.find('\0') != std::string::npos || !valid_utf8(config.display_name)) {
        diagnostic(out, "display_name", "invalid UTF-8 or embedded NUL", "reject", true);
    } else if (config.display_name.empty()) {
        out.normalized.display_name = "Player";
        diagnostic(out, "display_name", "empty value uses the SDK default", "defaulted");
    } else {
        const std::string bounded = truncate_display(config.display_name);
        if (bounded != config.display_name) {
            out.normalized.display_name = bounded;
            diagnostic(out, "display_name", "exceeds SDK character or byte cap", "truncated");
        }
    }
    if (!valid_locale(config.locale)) {
        diagnostic(out, "locale", "not a supported language tag", "reject", true);
    }
    if (config.discovery_first == 0 || config.discovery_last == 0 ||
        config.discovery_first > config.discovery_last ||
        static_cast<unsigned int>(config.discovery_last - config.discovery_first) + 1 > 64) {
        diagnostic(out, "discovery_ports", "invalid or over-wide port range", "reject", true);
    }
    if (config.peer_seeds.size() > 16) {
        diagnostic(out, "peer_seeds", "more than 16 addresses", "reject", true);
    } else {
        std::set<std::string> seeds;
        bool duplicate = false;
        for (std::size_t i = 0; i < config.peer_seeds.size(); i++) {
            if (!ipv4(config.peer_seeds[i])) {
                diagnostic(out, "peer_seeds", "invalid unicast IPv4 literal", "reject", true);
                break;
            }
            if (!seeds.insert(config.peer_seeds[i]).second) {
                diagnostic(out, "peer_seeds", "duplicate address", "removed");
                duplicate = true;
            }
        }
        if (duplicate) {
            out.normalized.peer_seeds.assign(seeds.begin(), seeds.end());
        }
    }
    const char* trace_choices[] = {"off", "errors", "lifecycle", "full"};
    if (!choice(config.trace_level, trace_choices, 4)) {
        diagnostic(out, "trace_level", "unrecognized trace level", "reject", true);
    }
    const char* log_choices[] = {"off", "fatal", "err", "error", "warn", "warning",
                                 "info", "debug", "trace"};
    if (!choice(config.log_level, log_choices, 9)) {
        diagnostic(out, "log_level", "unrecognized log level", "reject", true);
    } else {
        out.normalized.log_level = normalize_log_level(config.log_level);
    }
    if (config.trace_dir.find('\0') != std::string::npos || config.trace_dir.empty()) {
        diagnostic(out, "trace_dir", "empty path or embedded NUL", "reject", true);
    }
    if (config.trace_max_bytes < 65536) {
        out.normalized.trace_max_bytes = 65536;
        diagnostic(out, "trace_max_bytes", "below 64 KiB", "clamped");
    } else if (config.trace_max_bytes > 1073741824LL) {
        out.normalized.trace_max_bytes = 1073741824LL;
        diagnostic(out, "trace_max_bytes", "above 1 GiB", "clamped");
    }
    if (config.trace_max_rotated_files < 0) {
        out.normalized.trace_max_rotated_files = 0;
        diagnostic(out, "trace_max_rotated_files", "negative value", "clamped");
    } else if (config.trace_max_rotated_files > 64) {
        out.normalized.trace_max_rotated_files = 64;
        diagnostic(out, "trace_max_rotated_files", "above 64", "clamped");
    }
    if (!valid_slug(config.instance_label)) {
        diagnostic(out, "instance_label", "not a path-safe SDK label", "reject", true);
    }
    if (config.enable_overlay) {
        diagnostic(out, "enable_overlay", "the SDK has no overlay", "unavailable");
    }
    if (config.unlock_dlcs) {
        diagnostic(out, "unlock_dlcs", "the SDK has no Ecom entitlement implementation",
                   "unavailable");
    }
    return out;
}

std::string serialize_manager_configuration(const manager_configuration& config) {
    json_value root = config.source_document.kind == json_kind::object
                          ? config.source_document : json_object();
    root.members["display_name"] = json_string(config.display_name);
    root.members["locale"] = json_string(config.locale);
    json_value ports = json_array();
    ports.elements.push_back(json_int(config.discovery_first));
    ports.elements.push_back(json_int(config.discovery_last));
    root.members["discovery_ports"] = ports;
    json_value seeds = json_array();
    for (std::size_t i = 0; i < config.peer_seeds.size(); i++) {
        seeds.elements.push_back(json_string(config.peer_seeds[i]));
    }
    root.members["peer_seeds"] = seeds;
    root.members["enable_lan"] = json_bool(config.enable_lan);
    root.members["log_level"] = json_string(config.log_level);
    root.members["trace_level"] = json_string(config.trace_level);
    root.members["trace_dir"] = json_string(config.trace_dir);
    root.members["trace_max_bytes"] = json_int(config.trace_max_bytes);
    root.members["trace_max_rotated_files"] = json_int(config.trace_max_rotated_files);
    root.members["instance_label"] = json_string(config.instance_label);
    root.members["enable_overlay"] = json_bool(config.enable_overlay);
    root.members["unlock_dlcs"] = json_bool(config.unlock_dlcs);
    return serialize_json(root);
}

configuration_save_result save_manager_configuration(
    const configuration_save_request& request, const manager_configuration& configuration,
    transaction_filesystem& filesystem) {
    const configuration_validation validation = validate_manager_configuration(configuration);
    if (!validation.valid) {
        return save_result(configuration_save_code::invalid_configuration,
                           "configuration has rejected fields");
    }
    if (!manager_absolute_path(request.path) ||
        !manager_absolute_path(request.temporary_path) ||
        !manager_absolute_path(request.backup_path) || request.path == request.temporary_path ||
        request.path == request.backup_path || request.temporary_path == request.backup_path) {
        return save_result(configuration_save_code::invalid_request,
                           "configuration transaction paths are invalid");
    }
    if (request.existing_was_malformed && !request.replace_confirmed) {
        return save_result(configuration_save_code::malformed_confirmation_required,
                           "explicit Replace confirmation is required for malformed JSON");
    }
    if (!path_is_missing(request.temporary_path, filesystem)) {
        return save_result(configuration_save_code::temporary_exists,
                           "configuration staging path already exists");
    }
    const std::string bytes = serialize_manager_configuration(validation.normalized);
    const std::string saved_hash = sha256_hex(bytes);
    transaction_file_info current_info;
    const transaction_io_result current_status = filesystem.inspect(request.path, current_info);
    configuration_save_result out;
    if (current_status == transaction_io_result::missing) {
        if (!request.expected_sha256.empty() || !request.owned_backup_sha256.empty() ||
            !path_is_missing(request.backup_path, filesystem)) {
            return save_result(configuration_save_code::backup_exists,
                               "an unowned configuration backup already exists");
        }
        if (!write_transaction_file(request.temporary_path, bytes, filesystem) ||
            filesystem.set_mode(request.temporary_path, 0600) != transaction_io_result::ok) {
            return save_result(configuration_save_code::write_failed,
                               "configuration staging write failed");
        }
        if (filesystem.rename_no_replace(request.temporary_path, request.path) !=
                transaction_io_result::ok ||
            filesystem.flush_parent(request.path) != transaction_io_result::ok ||
            !hash_transaction_file(request.path, filesystem, out.saved_sha256) ||
            out.saved_sha256 != saved_hash) {
            return save_result(configuration_save_code::replace_failed,
                               "configuration publication did not verify");
        }
        out.code = configuration_save_code::saved;
        return out;
    }
    if (current_status != transaction_io_result::ok || !current_info.regular ||
        current_info.symlink || !current_info.writable || request.expected_sha256.size() != 64) {
        return save_result(configuration_save_code::externally_changed,
                           "existing configuration is unsafe or lacks an expected hash");
    }
    std::string current_hash;
    if (!hash_transaction_file(request.path, filesystem, current_hash) ||
        current_hash != request.expected_sha256) {
        return save_result(configuration_save_code::externally_changed,
                           "configuration changed since it was opened");
    }
    transaction_file_info backup_info;
    const transaction_io_result backup_status = filesystem.inspect(request.backup_path, backup_info);
    if (backup_status == transaction_io_result::missing) {
        if (!request.owned_backup_sha256.empty() ||
            !copy_transaction_file(request.path, request.backup_path, filesystem) ||
            filesystem.set_mode(request.backup_path, 0600) != transaction_io_result::ok ||
            filesystem.flush_parent(request.backup_path) != transaction_io_result::ok ||
            !hash_transaction_file(request.backup_path, filesystem, out.backup_sha256) ||
            out.backup_sha256 != current_hash) {
            return save_result(configuration_save_code::backup_failed,
                               "existing configuration backup did not verify");
        }
    } else {
        if (backup_status != transaction_io_result::ok || request.owned_backup_sha256.size() != 64 ||
            !hash_transaction_file(request.backup_path, filesystem, out.backup_sha256) ||
            out.backup_sha256 != request.owned_backup_sha256) {
            return save_result(configuration_save_code::backup_exists,
                               "configuration backup is unowned or changed");
        }
    }
    if (!write_transaction_file(request.temporary_path, bytes, filesystem) ||
        filesystem.set_mode(request.temporary_path, current_info.mode) != transaction_io_result::ok ||
        !hash_transaction_file(request.temporary_path, filesystem, out.saved_sha256) ||
        out.saved_sha256 != saved_hash ||
        !hash_transaction_file(request.path, filesystem, current_hash) ||
        current_hash != request.expected_sha256) {
        return save_result(configuration_save_code::write_failed,
                           "configuration staging or final ownership check failed");
    }
    if (filesystem.rename_replace(request.temporary_path, request.path) != transaction_io_result::ok ||
        filesystem.flush_parent(request.path) != transaction_io_result::ok ||
        !hash_transaction_file(request.path, filesystem, out.saved_sha256) ||
        out.saved_sha256 != saved_hash) {
        return save_result(configuration_save_code::replace_failed,
                           "configuration replacement did not verify");
    }
    out.code = configuration_save_code::saved;
    return out;
}

} // namespace manager
} // namespace eosr
