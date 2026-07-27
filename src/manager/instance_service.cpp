#include "manager/instance_service.h"

#include "manager/sha256.h"

namespace eosr {
namespace manager {

namespace {

instance_operation operation(instance_operation_code code, const std::string& detail,
                             const std::string& id = std::string()) {
    instance_operation out;
    out.code = code;
    out.detail = detail;
    out.instance_id = id;
    return out;
}

std::string join_path(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    const char separator = parent.find('\\') != std::string::npos &&
                           parent.find('/') == std::string::npos ? '\\' : '/';
    const char last = parent[parent.size() - 1];
    return last == '/' || last == '\\' ? parent + child : parent + separator + child;
}

bool valid_display_name(const std::string& value) {
    if (value.empty() || value.size() > 256 || value.find('\0') != std::string::npos) {
        return false;
    }
    manager_configuration config = default_manager_configuration();
    config.display_name = value;
    return validate_manager_configuration(config).valid;
}

std::size_t find_instance(const game_state& game, const std::string& id) {
    for (std::size_t i = 0; i < game.instances.size(); i++) {
        if (game.instances[i].id == id) {
            return i;
        }
    }
    return game.instances.size();
}

std::string parent_path(const std::string& path) {
    const std::string::size_type at = path.find_last_of("/\\");
    if (at == std::string::npos) return std::string();
    return at == 0 ? path.substr(0, 1) : path.substr(0, at);
}

bool hash_file(const std::string& path, transaction_filesystem& files, std::string& out) {
    out.clear();
    transaction_handle handle = 0;
    if (files.open_read(path, handle) != transaction_io_result::ok) return false;
    sha256_hasher hash;
    unsigned char buffer[4096];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = files.read(handle, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) break;
        if (read != transaction_io_result::ok || count == 0) {
            ok = false;
            break;
        }
        hash.update(buffer, count);
    }
    if (files.close(handle) != transaction_io_result::ok) ok = false;
    if (ok) out = hash.final_hex();
    return ok;
}

bool removed_or_missing(transaction_io_result result) {
    return result == transaction_io_result::ok || result == transaction_io_result::missing;
}

bool removed_or_missing(run_io_result result) {
    return result == run_io_result::ok || result == run_io_result::missing;
}

instance_provision_result provision_result(instance_provision_code code,
                                           const std::string& detail) {
    instance_provision_result out;
    out.code = code;
    out.detail = detail;
    return out;
}

} // namespace

instance_delete_request::instance_delete_request()
    : confirmed(false), profile_key_exists(false), identity(identity_disposition::unspecified) {}

instance_operation::instance_operation() : code(instance_operation_code::invalid_request) {}

instance_provision_result::instance_provision_result()
    : code(instance_provision_code::invalid_model) {}

identity_export_result::identity_export_result()
    : code(identity_export_code::invalid_request) {}

instance_operation create_instance_model(game_state& game,
                                         const instance_create_request& request) {
    if (!valid_manager_id(game.id) || !valid_manager_id(request.id) ||
        !valid_instance_slug(request.slug) || !valid_display_name(request.display_name) ||
        !manager_absolute_path(request.instances_root)) {
        return operation(instance_operation_code::invalid_request,
                         "instance identity, slug, name, or root is invalid");
    }
    const std::string data_dir = join_path(join_path(request.instances_root, request.slug), "data");
    for (std::size_t i = 0; i < game.instances.size(); i++) {
        if (game.instances[i].id == request.id) {
            return operation(instance_operation_code::id_exists,
                             "instance manager id already exists");
        }
        if (game.instances[i].slug == request.slug) {
            return operation(instance_operation_code::slug_exists,
                             "instance slug already exists");
        }
        if (game.instances[i].data_dir == data_dir) {
            return operation(instance_operation_code::path_exists,
                             "instance data directory is already assigned");
        }
    }
    managed_instance instance;
    instance.id = request.id;
    instance.slug = request.slug;
    instance.display_name = request.display_name;
    instance.data_dir = data_dir;
    instance.target_data_dir = data_dir;
    game.instances.push_back(instance);
    if (game.active_instance_id.empty()) {
        game.active_instance_id = instance.id;
    }
    return operation(instance_operation_code::created, std::string(), instance.id);
}

instance_operation rename_instance_model(game_state& game, const std::string& id,
                                         const std::string& display_name) {
    const std::size_t index = find_instance(game, id);
    if (index == game.instances.size()) {
        return operation(instance_operation_code::not_found, "instance does not exist");
    }
    if (!valid_display_name(display_name)) {
        return operation(instance_operation_code::invalid_request,
                         "instance display name is invalid");
    }
    game.instances[index].display_name = display_name;
    return operation(instance_operation_code::renamed, std::string(), id);
}

instance_operation select_instance_model(game_state& game, const std::string& id) {
    if (find_instance(game, id) == game.instances.size()) {
        return operation(instance_operation_code::not_found, "instance does not exist");
    }
    game.active_instance_id = id;
    return operation(instance_operation_code::selected, std::string(), id);
}

instance_operation delete_instance_model(game_state& game,
                                         const instance_delete_request& request) {
    const std::size_t index = find_instance(game, request.id);
    if (index == game.instances.size()) {
        return operation(instance_operation_code::not_found, "instance does not exist");
    }
    if (!request.confirmed) {
        return operation(instance_operation_code::confirmation_required,
                         "instance deletion requires confirmation");
    }
    if (request.profile_key_exists && request.identity == identity_disposition::unspecified) {
        return operation(instance_operation_code::identity_decision_required,
                         "choose export, retain, or confirmed identity destruction");
    }
    const bool retained = request.profile_key_exists &&
                          request.identity == identity_disposition::retain_directory;
    game.instances.erase(game.instances.begin() + static_cast<std::ptrdiff_t>(index));
    if (game.active_instance_id == request.id) {
        game.active_instance_id = game.instances.empty() ? std::string() : game.instances[0].id;
    }
    return operation(retained ? instance_operation_code::removed_retained
                              : instance_operation_code::removed,
                     std::string(), request.id);
}

manager_configuration new_instance_configuration(const std::string& display_name,
                                                  const std::string& slug, bool alpha_build) {
    manager_configuration config = default_manager_configuration();
    config.display_name = display_name;
    config.instance_label = slug;
    config.trace_level = alpha_build ? "lifecycle" : "off";
    return config;
}

void apply_configuration_preset(manager_configuration& target,
                                const manager_configuration& preset) {
    target.locale = preset.locale;
    target.discovery_first = preset.discovery_first;
    target.discovery_last = preset.discovery_last;
    target.peer_seeds = preset.peer_seeds;
    target.enable_lan = preset.enable_lan;
    target.log_level = preset.log_level;
    target.trace_level = preset.trace_level;
    target.trace_dir = preset.trace_dir;
    target.trace_max_bytes = preset.trace_max_bytes;
    target.trace_max_rotated_files = preset.trace_max_rotated_files;
    target.enable_overlay = preset.enable_overlay;
    target.unlock_dlcs = preset.unlock_dlcs;
}

void apply_saved_configuration_metadata(managed_instance& instance,
                                        const configuration_save_result& saved) {
    instance.config_sha256 = saved.saved_sha256;
    if (!saved.backup_sha256.empty()) instance.config_backup_sha256 = saved.backup_sha256;
}

instance_provision_result provision_instance(game_state& game,
                                             const instance_provision_request& request,
                                             run_filesystem& directories,
                                             transaction_filesystem& files) {
    game_state proposed = game;
    const instance_operation modeled = create_instance_model(proposed, request.model);
    if (modeled.code != instance_operation_code::created) {
        const bool collision = modeled.code == instance_operation_code::path_exists ||
                               modeled.code == instance_operation_code::slug_exists;
        return provision_result(collision ? instance_provision_code::path_exists :
                                            instance_provision_code::invalid_model,
                                modeled.detail);
    }
    const configuration_validation validation =
        validate_manager_configuration(request.configuration);
    if (!validation.valid || validation.normalized.display_name != request.model.display_name ||
        validation.normalized.instance_label != request.model.slug) {
        return provision_result(instance_provision_code::invalid_configuration,
                                "configuration identity must match the new instance");
    }
    std::vector<run_entry> root_entries;
    if (directories.list_directory(request.model.instances_root, root_entries) !=
        run_io_result::ok) {
        return provision_result(instance_provision_code::root_missing,
                                "instances root does not exist or is unreadable");
    }
    for (std::size_t i = 0; i < root_entries.size(); i++) {
        if (root_entries[i].name == request.model.slug) {
            return provision_result(instance_provision_code::path_exists,
                                    "instance directory already exists and is not adopted");
        }
    }

    const managed_instance& instance = proposed.instances[proposed.instances.size() - 1];
    const std::string instance_dir = parent_path(instance.data_dir);
    const std::string config_path = join_path(instance.data_dir, "eosr.json");
    const std::string temporary_path =
        join_path(instance.data_dir, ".eosr.json.eosr-stage-" + request.model.id);
    const std::string backup_path = join_path(instance.data_dir, "eosr.json.eosr-import-backup");
    if (directories.create_private_directory(instance_dir) != run_io_result::ok) {
        return provision_result(instance_provision_code::directory_failed,
                                "instance directory could not be exclusively created");
    }
    bool cleanup_ok = true;
    if (directories.flush_parent(instance_dir) != run_io_result::ok ||
        directories.create_private_directory(instance.data_dir) != run_io_result::ok ||
        directories.flush_parent(instance.data_dir) != run_io_result::ok) {
        cleanup_ok = removed_or_missing(directories.remove_empty_directory(instance.data_dir)) &&
                     cleanup_ok;
        cleanup_ok = removed_or_missing(directories.remove_empty_directory(instance_dir)) &&
                     cleanup_ok;
        directories.flush_parent(instance_dir);
        return provision_result(cleanup_ok ? instance_provision_code::directory_failed :
                                             instance_provision_code::cleanup_incomplete,
                                "private instance data directory creation failed");
    }

    configuration_save_request save;
    save.path = config_path;
    save.temporary_path = temporary_path;
    save.backup_path = backup_path;
    const configuration_save_result saved =
        save_manager_configuration(save, validation.normalized, files);
    if (saved.code != configuration_save_code::saved) {
        // The stage name did not exist before this operation and is process-unique, so a partial
        // staging file remains ours even when its write failed.
        cleanup_ok = removed_or_missing(files.remove(temporary_path)) && cleanup_ok;
        transaction_file_info live;
        const transaction_io_result inspected = files.inspect(config_path, live);
        if (inspected == transaction_io_result::ok) {
            std::string hash;
            const std::string expected = sha256_hex(serialize_manager_configuration(
                validation.normalized));
            if (live.regular && !live.symlink && hash_file(config_path, files, hash) &&
                hash == expected) {
                cleanup_ok = removed_or_missing(files.remove(config_path)) && cleanup_ok;
            } else {
                cleanup_ok = false;
            }
        } else if (inspected != transaction_io_result::missing) {
            cleanup_ok = false;
        }
        cleanup_ok = removed_or_missing(directories.remove_empty_directory(instance.data_dir)) &&
                     cleanup_ok;
        cleanup_ok = removed_or_missing(directories.remove_empty_directory(instance_dir)) &&
                     cleanup_ok;
        files.flush_parent(config_path);
        directories.flush_parent(instance_dir);
        return provision_result(cleanup_ok ? instance_provision_code::configuration_failed :
                                             instance_provision_code::cleanup_incomplete,
                                saved.detail.empty() ? "instance configuration save failed" :
                                                       saved.detail);
    }

    game = proposed;
    apply_saved_configuration_metadata(game.instances[game.instances.size() - 1], saved);
    instance_provision_result out;
    out.code = instance_provision_code::provisioned;
    out.instance_id = request.model.id;
    out.config_path = config_path;
    out.config_sha256 = saved.saved_sha256;
    return out;
}

identity_export_result export_instance_identity(const identity_export_request& request,
                                                transaction_filesystem& files) {
    identity_export_result out;
    if (!manager_absolute_path(request.source_path) ||
        !manager_absolute_path(request.destination_path) ||
        request.source_path == request.destination_path) {
        out.detail = "identity source and destination must be distinct absolute paths";
        return out;
    }
    transaction_file_info source;
    const transaction_io_result inspected = files.inspect(request.source_path, source);
    if (inspected == transaction_io_result::missing) {
        out.code = identity_export_code::source_missing;
        out.detail = "profile identity does not exist";
        return out;
    }
    if (inspected != transaction_io_result::ok || !source.regular || source.symlink) {
        out.code = identity_export_code::source_unsafe;
        out.detail = "profile identity is not a safe regular file";
        return out;
    }
    transaction_file_info destination;
    if (files.inspect(request.destination_path, destination) != transaction_io_result::missing) {
        out.code = identity_export_code::destination_exists;
        out.detail = "identity export destination already exists";
        return out;
    }
    transaction_handle input = 0;
    transaction_handle output = 0;
    if (files.open_read(request.source_path, input) != transaction_io_result::ok ||
        files.create_new(request.destination_path, output) != transaction_io_result::ok) {
        if (input != 0) files.close(input);
        out.code = identity_export_code::copy_failed;
        out.detail = "identity export streams could not be opened";
        return out;
    }
    sha256_hasher copied;
    unsigned char buffer[4096];
    bool ok = true;
    while (ok) {
        std::size_t count = 0;
        const transaction_io_result read = files.read(input, buffer, sizeof(buffer), count);
        if (read == transaction_io_result::end_of_file) break;
        if (read != transaction_io_result::ok || count == 0) {
            ok = false;
            break;
        }
        copied.update(buffer, count);
        std::size_t offset = 0;
        while (offset < count) {
            std::size_t written = 0;
            if (files.write(output, buffer + offset, count - offset, written) !=
                    transaction_io_result::ok || written == 0 || written > count - offset) {
                ok = false;
                break;
            }
            offset += written;
        }
    }
    if (ok && files.flush(output) != transaction_io_result::ok) ok = false;
    if (files.close(input) != transaction_io_result::ok) ok = false;
    if (files.close(output) != transaction_io_result::ok) ok = false;
    if (ok && files.set_mode(request.destination_path, 0600) != transaction_io_result::ok) ok = false;
    if (ok && files.flush_parent(request.destination_path) != transaction_io_result::ok) ok = false;
    if (!ok) {
        files.remove(request.destination_path);
        out.code = identity_export_code::copy_failed;
        out.detail = "identity export write failed";
        return out;
    }
    const std::string copied_hash = copied.final_hex();
    std::string source_hash;
    std::string destination_hash;
    if (!hash_file(request.source_path, files, source_hash) || source_hash != copied_hash) {
        files.remove(request.destination_path);
        out.code = identity_export_code::source_changed;
        out.detail = "profile identity changed while it was exported";
        return out;
    }
    if (!hash_file(request.destination_path, files, destination_hash) ||
        destination_hash != copied_hash) {
        files.remove(request.destination_path);
        out.code = identity_export_code::verification_failed;
        out.detail = "identity export did not verify";
        return out;
    }
    out.code = identity_export_code::exported;
    out.sha256 = copied_hash;
    return out;
}

} // namespace manager
} // namespace eosr
