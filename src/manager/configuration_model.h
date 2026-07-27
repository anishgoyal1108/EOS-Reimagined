#ifndef EOSR_MANAGER_CONFIGURATION_MODEL_H
#define EOSR_MANAGER_CONFIGURATION_MODEL_H

#include <string>
#include <vector>

#include "common/types.h"
#include "manager/install_transaction.h"
#include "manager/json.h"

namespace eosr {
namespace manager {

enum class configuration_field_kind {
    text,
    boolean,
    integer,
    port_range,
    string_list,
    choice
};

struct configuration_field {
    std::string key;
    std::string environment;
    std::string group;
    configuration_field_kind kind;
    bool available;
};

const std::vector<configuration_field>& configuration_fields();

struct configuration_import_issue {
    std::string field;
    std::string reason;
    std::string action;
};

struct manager_configuration {
    std::string display_name;
    std::string locale;
    u16 discovery_first;
    u16 discovery_last;
    std::vector<std::string> peer_seeds;
    bool enable_lan;
    std::string log_level;
    std::string trace_level;
    std::string trace_dir;
    i64 trace_max_bytes;
    i64 trace_max_rotated_files;
    std::string instance_label;
    bool enable_overlay;
    bool unlock_dlcs;
    json_value source_document;
    std::vector<configuration_import_issue> import_issues;
};

struct configuration_diagnostic {
    std::string field;
    std::string reason;
    std::string action;
};

struct configuration_validation {
    configuration_validation();
    bool valid;
    manager_configuration normalized;
    std::vector<configuration_diagnostic> diagnostics;
};

manager_configuration default_manager_configuration();
bool parse_manager_configuration(const std::string& bytes, manager_configuration& out,
                                 std::string& error);
configuration_validation validate_manager_configuration(const manager_configuration& config);
std::string serialize_manager_configuration(const manager_configuration& config);

struct configuration_save_request {
    configuration_save_request();
    std::string path;
    std::string temporary_path;
    std::string backup_path;
    std::string expected_sha256;
    std::string owned_backup_sha256;
    bool existing_was_malformed;
    bool replace_confirmed;
};

enum class configuration_save_code {
    saved,
    invalid_configuration,
    invalid_request,
    malformed_confirmation_required,
    externally_changed,
    backup_exists,
    backup_failed,
    temporary_exists,
    write_failed,
    replace_failed
};

struct configuration_save_result {
    configuration_save_result();
    configuration_save_code code;
    std::string saved_sha256;
    std::string backup_sha256;
    std::string detail;
};

configuration_save_result save_manager_configuration(
    const configuration_save_request& request, const manager_configuration& configuration,
    transaction_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
