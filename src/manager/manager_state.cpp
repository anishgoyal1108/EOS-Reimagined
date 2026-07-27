#include "manager/manager_state.h"

#include <set>

#include "manager/json.h"

namespace eosr {
namespace manager {

namespace {

const int manager_state_version = 1;

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

bool string_has_nul(const std::string& value) {
    return value.find('\0') != std::string::npos;
}

bool optional_hash(const std::string& value) {
    if (value.empty()) return true;
    if (value.size() != 64) return false;
    for (std::size_t i = 0; i < value.size(); i++)
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

bool valid_label(const std::string& value, std::size_t max_bytes, bool allow_empty) {
    return (allow_empty || !value.empty()) && value.size() <= max_bytes && !string_has_nul(value);
}

bool valid_release_id(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    if (value.size() > 128) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-' ||
              c == '@')) {
            return false;
        }
    }
    return true;
}

bool decimal_app_id(const std::string& value) {
    if (value.empty() || value.size() > 10) {
        return false;
    }
    unsigned long long app_id = 0;
    for (std::size_t i = 0; i < value.size(); i++) {
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
        app_id = app_id * 10 + static_cast<unsigned long long>(value[i] - '0');
        if (app_id > 4294967295ULL) {
            return false;
        }
    }
    return app_id != 0;
}

const char* install_kind_name(steam_install_kind kind) {
    if (kind == steam_install_kind::native_windows) {
        return "native_windows";
    }
    if (kind == steam_install_kind::native_linux) {
        return "native_linux";
    }
    return "flatpak_linux";
}

bool parse_install_kind(const std::string& name, steam_install_kind& out) {
    if (name == "native_windows") {
        out = steam_install_kind::native_windows;
    } else if (name == "native_linux") {
        out = steam_install_kind::native_linux;
    } else if (name == "flatpak_linux") {
        out = steam_install_kind::flatpak_linux;
    } else {
        return false;
    }
    return true;
}

const char* binary_kind_name(eos_binary_kind kind) {
    if (kind == eos_binary_kind::windows_x86_64) {
        return "windows_x86_64";
    }
    if (kind == eos_binary_kind::linux_x86_64) {
        return "linux_x86_64";
    }
    return "unknown";
}

bool parse_binary_kind(const std::string& name, eos_binary_kind& out) {
    if (name == "windows_x86_64") {
        out = eos_binary_kind::windows_x86_64;
    } else if (name == "linux_x86_64") {
        out = eos_binary_kind::linux_x86_64;
    } else {
        return false;
    }
    return true;
}

bool exact_object(const json_value& value, std::size_t fields, std::string& error) {
    if (value.kind != json_kind::object) {
        return fail(error, "state value is not an object");
    }
    if (value.members.size() != fields) {
        return fail(error, "state object has missing or unknown fields");
    }
    return true;
}

bool required(const json_value& object, const char* name, json_kind kind,
              const json_value*& out, std::string& error) {
    out = json_member(object, name);
    if (out == 0 || out->kind != kind) {
        error = std::string("state field has wrong type: ") + name;
        return false;
    }
    return true;
}

json_value target_json(const managed_target& target) {
    json_value out = json_object();
    out.members["id"] = json_string(target.id);
    out.members["kind"] = json_string(binary_kind_name(target.kind));
    out.members["path"] = json_string(target.path);
    return out;
}

json_value instance_json(const managed_instance& instance) {
    json_value out = json_object();
    out.members["data_dir"] = json_string(instance.data_dir);
    out.members["display_name"] = json_string(instance.display_name);
    out.members["config_backup_sha256"] = json_string(instance.config_backup_sha256);
    out.members["config_sha256"] = json_string(instance.config_sha256);
    out.members["id"] = json_string(instance.id);
    out.members["proton_mapped_host_root"] = json_string(instance.proton_mapped_host_root);
    out.members["proton_mapping_name"] = json_string(instance.proton_mapping_name);
    out.members["proton_prefix"] = json_string(instance.proton_prefix);
    out.members["slug"] = json_string(instance.slug);
    out.members["target_data_dir"] = json_string(instance.target_data_dir);
    return out;
}

bool parse_target(const json_value& value, managed_target& out, std::string& error) {
    if (!exact_object(value, 3, error)) {
        return false;
    }
    const json_value* id = 0;
    const json_value* kind = 0;
    const json_value* path = 0;
    if (!required(value, "id", json_kind::string, id, error) ||
        !required(value, "kind", json_kind::string, kind, error) ||
        !required(value, "path", json_kind::string, path, error)) {
        return false;
    }
    if (!valid_manager_id(id->text)) {
        return fail(error, "invalid target id");
    }
    if (!manager_absolute_path(path->text)) {
        return fail(error, "target path must be absolute");
    }
    if (!parse_binary_kind(kind->text, out.kind)) {
        return fail(error, "invalid target binary kind");
    }
    out.id = id->text;
    out.path = path->text;
    return true;
}

bool parse_instance(const json_value& value, managed_instance& out, std::string& error) {
    if (!exact_object(value, 10, error)) {
        return false;
    }
    const json_value* id = 0;
    const json_value* slug = 0;
    const json_value* name = 0;
    const json_value* data_dir = 0;
    const json_value* target_data_dir = 0;
    const json_value* proton_prefix = 0;
    const json_value* proton_mapping_name = 0;
    const json_value* proton_mapped_host_root = 0;
    const json_value* config_sha256 = 0;
    const json_value* config_backup_sha256 = 0;
    if (!required(value, "id", json_kind::string, id, error) ||
        !required(value, "slug", json_kind::string, slug, error) ||
        !required(value, "display_name", json_kind::string, name, error) ||
        !required(value, "data_dir", json_kind::string, data_dir, error) ||
        !required(value, "config_sha256", json_kind::string, config_sha256, error) ||
        !required(value, "config_backup_sha256", json_kind::string,
                  config_backup_sha256, error) ||
        !required(value, "target_data_dir", json_kind::string, target_data_dir, error) ||
        !required(value, "proton_prefix", json_kind::string, proton_prefix, error) ||
        !required(value, "proton_mapping_name", json_kind::string, proton_mapping_name, error) ||
        !required(value, "proton_mapped_host_root", json_kind::string,
                  proton_mapped_host_root, error)) {
        return false;
    }
    if (!valid_manager_id(id->text)) {
        return fail(error, "invalid instance id");
    }
    if (!valid_instance_slug(slug->text)) {
        return fail(error, "invalid instance slug");
    }
    if (!valid_label(name->text, 256, false)) {
        return fail(error, "invalid instance display name");
    }
    if (!manager_absolute_path(data_dir->text)) {
        return fail(error, "instance data path must be absolute");
    }
    if (!manager_absolute_path(target_data_dir->text)) {
        return fail(error, "instance target-runtime data path must be absolute");
    }
    const bool no_proton = proton_prefix->text.empty() && proton_mapping_name->text.empty() &&
                           proton_mapped_host_root->text.empty();
    const bool proton = manager_absolute_path(proton_prefix->text) &&
                        manager_absolute_path(proton_mapped_host_root->text) &&
                        proton_mapping_name->text.size() == 2 &&
                        proton_mapping_name->text[1] == ':';
    if (!no_proton && !proton) return fail(error, "instance Proton mapping snapshot is invalid");
    if (!optional_hash(config_sha256->text) || !optional_hash(config_backup_sha256->text))
        return fail(error, "instance configuration ownership hash is invalid");
    out.id = id->text;
    out.slug = slug->text;
    out.display_name = name->text;
    out.data_dir = data_dir->text;
    out.target_data_dir = target_data_dir->text;
    out.proton_prefix = proton_prefix->text;
    out.proton_mapping_name = proton_mapping_name->text;
    out.proton_mapped_host_root = proton_mapped_host_root->text;
    out.config_sha256 = config_sha256->text;
    out.config_backup_sha256 = config_backup_sha256->text;
    return true;
}

} // namespace

manager_index::manager_index() : version(manager_state_version) {}

game_state::game_state()
    : version(manager_state_version), install_kind(steam_install_kind::native_linux), manual(false) {}

bool valid_manager_id(const std::string& value) {
    if (value.size() != 32) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); i++) {
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool valid_instance_slug(const std::string& value) {
    if (value.empty() || value.size() > 32 || value == "." || value == "..") {
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

bool manager_absolute_path(const std::string& value) {
    if (value.empty() || string_has_nul(value)) {
        return false;
    }
    if (value[0] == '/') {
        return true;
    }
    if (value.size() >= 3 && ((value[0] >= 'a' && value[0] <= 'z') ||
        (value[0] >= 'A' && value[0] <= 'Z')) && value[1] == ':' &&
        (value[2] == '/' || value[2] == '\\')) {
        return true;
    }
    return value.size() >= 3 && (value[0] == '/' || value[0] == '\\') &&
           (value[1] == '/' || value[1] == '\\');
}

std::string serialize_manager_index(const manager_index& state) {
    json_value root = json_object();
    root.members["version"] = json_int(state.version);
    root.members["selected_release_id"] = json_string(state.selected_release_id);
    json_value games = json_array();
    for (std::size_t i = 0; i < state.games.size(); i++) {
        json_value game = json_object();
        game.members["id"] = json_string(state.games[i].id);
        game.members["state_path"] = json_string(state.games[i].state_path);
        games.elements.push_back(game);
    }
    root.members["games"] = games;
    return serialize_json(root);
}

bool parse_manager_index(const std::string& bytes, manager_index& out, std::string& error) {
    out = manager_index();
    json_value root;
    if (!parse_json(bytes, root, error) || !exact_object(root, 3, error)) {
        return false;
    }
    const json_value* version = 0;
    const json_value* release = 0;
    const json_value* games = 0;
    if (!required(root, "version", json_kind::integer, version, error) ||
        !required(root, "selected_release_id", json_kind::string, release, error) ||
        !required(root, "games", json_kind::array, games, error)) {
        return false;
    }
    if (version->integer != manager_state_version) {
        return fail(error, "unsupported manager state version");
    }
    if (!valid_release_id(release->text)) {
        return fail(error, "invalid selected release id");
    }
    std::set<std::string> ids;
    std::set<std::string> paths;
    manager_index parsed;
    parsed.selected_release_id = release->text;
    for (std::size_t i = 0; i < games->elements.size(); i++) {
        if (!exact_object(games->elements[i], 2, error)) {
            return false;
        }
        const json_value* id = 0;
        const json_value* path = 0;
        if (!required(games->elements[i], "id", json_kind::string, id, error) ||
            !required(games->elements[i], "state_path", json_kind::string, path, error)) {
            return false;
        }
        if (!valid_manager_id(id->text) || !ids.insert(id->text).second) {
            return fail(error, "invalid or duplicate game id");
        }
        if (!manager_absolute_path(path->text) || !paths.insert(path->text).second) {
            return fail(error, "game state path must be absolute and unique");
        }
        game_reference reference;
        reference.id = id->text;
        reference.state_path = path->text;
        parsed.games.push_back(reference);
    }
    out = parsed;
    return true;
}

std::string serialize_game_state(const game_state& state) {
    json_value root = json_object();
    root.members["active_instance_id"] = json_string(state.active_instance_id);
    root.members["active_target_id"] = json_string(state.active_target_id);
    root.members["display_name"] = json_string(state.display_name);
    root.members["id"] = json_string(state.id);
    root.members["install_kind"] = json_string(install_kind_name(state.install_kind));
    root.members["install_root"] = json_string(state.install_root);
    root.members["library_root"] = json_string(state.library_root);
    json_value instances = json_array();
    for (std::size_t i = 0; i < state.instances.size(); i++) {
        instances.elements.push_back(instance_json(state.instances[i]));
    }
    root.members["instances"] = instances;
    root.members["manual"] = json_bool(state.manual);
    root.members["steam_app_id"] = json_string(state.steam_app_id);
    root.members["steam_root"] = json_string(state.steam_root);
    json_value targets = json_array();
    for (std::size_t i = 0; i < state.targets.size(); i++) {
        targets.elements.push_back(target_json(state.targets[i]));
    }
    root.members["targets"] = targets;
    root.members["version"] = json_int(state.version);
    return serialize_json(root);
}

bool parse_game_state(const std::string& bytes, game_state& out, std::string& error) {
    out = game_state();
    json_value root;
    if (!parse_json(bytes, root, error) || !exact_object(root, 13, error)) {
        return false;
    }
    const json_value* version = 0;
    const json_value* id = 0;
    const json_value* app_id = 0;
    const json_value* name = 0;
    const json_value* install_root = 0;
    const json_value* steam_root = 0;
    const json_value* library_root = 0;
    const json_value* install_kind = 0;
    const json_value* manual = 0;
    const json_value* active = 0;
    const json_value* active_target = 0;
    const json_value* targets = 0;
    const json_value* instances = 0;
    if (!required(root, "version", json_kind::integer, version, error) ||
        !required(root, "id", json_kind::string, id, error) ||
        !required(root, "steam_app_id", json_kind::string, app_id, error) ||
        !required(root, "display_name", json_kind::string, name, error) ||
        !required(root, "install_root", json_kind::string, install_root, error) ||
        !required(root, "steam_root", json_kind::string, steam_root, error) ||
        !required(root, "library_root", json_kind::string, library_root, error) ||
        !required(root, "install_kind", json_kind::string, install_kind, error) ||
        !required(root, "manual", json_kind::boolean, manual, error) ||
        !required(root, "active_instance_id", json_kind::string, active, error) ||
        !required(root, "active_target_id", json_kind::string, active_target, error) ||
        !required(root, "targets", json_kind::array, targets, error) ||
        !required(root, "instances", json_kind::array, instances, error)) {
        return false;
    }
    if (version->integer != manager_state_version) {
        return fail(error, "unsupported game state version");
    }
    game_state parsed;
    if (!valid_manager_id(id->text)) {
        return fail(error, "invalid game id");
    }
    if (!valid_label(name->text, 256, false)) {
        return fail(error, "invalid game display name");
    }
    if (!manager_absolute_path(install_root->text)) {
        return fail(error, "game install root must be absolute");
    }
    if (!parse_install_kind(install_kind->text, parsed.install_kind)) {
        return fail(error, "invalid Steam install kind");
    }
    if ((manual->boolean && !app_id->text.empty()) ||
        (!manual->boolean && !decimal_app_id(app_id->text))) {
        return fail(error, "manual and Steam app identity disagree");
    }
    if ((manual->boolean && (!steam_root->text.empty() || !library_root->text.empty())) ||
        (!manual->boolean && (!manager_absolute_path(steam_root->text) ||
                             !manager_absolute_path(library_root->text)))) {
        return fail(error, "manual and Steam installation paths disagree");
    }
    if (!active->text.empty() && !valid_manager_id(active->text)) {
        return fail(error, "invalid active instance id");
    }
    if (!active_target->text.empty() && !valid_manager_id(active_target->text))
        return fail(error, "invalid active target id");
    parsed.id = id->text;
    parsed.steam_app_id = app_id->text;
    parsed.display_name = name->text;
    parsed.install_root = install_root->text;
    parsed.steam_root = steam_root->text;
    parsed.library_root = library_root->text;
    parsed.manual = manual->boolean;
    parsed.active_instance_id = active->text;
    parsed.active_target_id = active_target->text;
    std::set<std::string> target_ids;
    std::set<std::string> target_paths;
    for (std::size_t i = 0; i < targets->elements.size(); i++) {
        managed_target target;
        if (!parse_target(targets->elements[i], target, error) ||
            !target_ids.insert(target.id).second || !target_paths.insert(target.path).second) {
            if (error.empty()) {
                error = "duplicate target identity or path";
            }
            return false;
        }
        parsed.targets.push_back(target);
    }
    if (!parsed.active_target_id.empty() &&
        target_ids.find(parsed.active_target_id) == target_ids.end())
        return fail(error, "active target does not exist");
    std::set<std::string> instance_ids;
    std::set<std::string> instance_slugs;
    bool active_found = parsed.active_instance_id.empty();
    for (std::size_t i = 0; i < instances->elements.size(); i++) {
        managed_instance instance;
        if (!parse_instance(instances->elements[i], instance, error)) {
            return false;
        }
        if (!instance_ids.insert(instance.id).second || !instance_slugs.insert(instance.slug).second) {
            return fail(error, "duplicate instance identity or slug");
        }
        active_found = active_found || instance.id == parsed.active_instance_id;
        parsed.instances.push_back(instance);
    }
    if (!active_found) {
        return fail(error, "active instance does not exist");
    }
    out = parsed;
    return true;
}

} // namespace manager
} // namespace eosr
