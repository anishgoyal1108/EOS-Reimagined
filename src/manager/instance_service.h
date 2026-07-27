#ifndef EOSR_MANAGER_INSTANCE_SERVICE_H
#define EOSR_MANAGER_INSTANCE_SERVICE_H

#include <string>

#include "manager/configuration_model.h"
#include "manager/manager_state.h"
#include "manager/run_service.h"

namespace eosr {
namespace manager {

struct instance_create_request {
    std::string id;
    std::string slug;
    std::string display_name;
    std::string instances_root;
};

enum class identity_disposition {
    unspecified,
    retain_directory,
    exported_then_destroy,
    destroy_confirmed
};

struct instance_delete_request {
    instance_delete_request();
    std::string id;
    bool confirmed;
    bool profile_key_exists;
    identity_disposition identity;
};

enum class instance_operation_code {
    created,
    renamed,
    selected,
    removed,
    removed_retained,
    invalid_request,
    id_exists,
    slug_exists,
    path_exists,
    not_found,
    confirmation_required,
    identity_decision_required
};

struct instance_operation {
    instance_operation();
    instance_operation_code code;
    std::string detail;
    std::string instance_id;
};

instance_operation create_instance_model(game_state& game,
                                         const instance_create_request& request);
instance_operation rename_instance_model(game_state& game, const std::string& id,
                                         const std::string& display_name);
instance_operation select_instance_model(game_state& game, const std::string& id);
instance_operation delete_instance_model(game_state& game,
                                         const instance_delete_request& request);

manager_configuration new_instance_configuration(const std::string& display_name,
                                                  const std::string& slug, bool alpha_build);
void apply_configuration_preset(manager_configuration& target,
                                const manager_configuration& preset);

// Records hashes owned by a successful eosr.json transaction. The managed instance display name
// is a cosmetic manager label and is intentionally independent of configuration.display_name.
void apply_saved_configuration_metadata(managed_instance& instance,
                                        const configuration_save_result& saved);

struct instance_provision_request {
    instance_create_request model;
    manager_configuration configuration;
};

enum class instance_provision_code {
    provisioned,
    invalid_model,
    invalid_configuration,
    root_missing,
    path_exists,
    directory_failed,
    configuration_failed,
    cleanup_incomplete
};

struct instance_provision_result {
    instance_provision_result();
    instance_provision_code code;
    std::string instance_id;
    std::string config_path;
    std::string config_sha256;
    std::string detail;
};

// Creates <slug>/data/eosr.json completely before publishing the instance in game state. Any
// failed create removes only paths exclusively claimed by this operation; an unexpected file is
// retained and reported as cleanup_incomplete rather than guessed to be ours.
instance_provision_result provision_instance(game_state& game,
                                             const instance_provision_request& request,
                                             run_filesystem& directories,
                                             transaction_filesystem& files);

struct identity_export_request {
    std::string source_path;
    std::string destination_path;
};

enum class identity_export_code {
    exported,
    invalid_request,
    source_missing,
    source_unsafe,
    destination_exists,
    copy_failed,
    source_changed,
    verification_failed
};

struct identity_export_result {
    identity_export_result();
    identity_export_code code;
    std::string sha256;
    std::string detail;
};

identity_export_result export_instance_identity(const identity_export_request& request,
                                                transaction_filesystem& files);

} // namespace manager
} // namespace eosr

#endif
