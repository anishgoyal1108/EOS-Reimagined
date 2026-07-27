#ifndef EOSR_MANAGER_MANAGER_STATE_H
#define EOSR_MANAGER_MANAGER_STATE_H

#include <string>
#include <vector>

#include "manager/steam_discovery.h"
#include "manager/target_inspection.h"

namespace eosr {
namespace manager {

struct game_reference {
    std::string id;
    std::string state_path;
};

struct manager_index {
    manager_index();
    int version;
    std::string selected_release_id;
    std::vector<game_reference> games;
};

struct managed_target {
    std::string id;
    std::string path;
    eos_binary_kind kind;
};

struct managed_instance {
    std::string id;
    std::string slug;
    std::string display_name;
    std::string data_dir;
    std::string target_data_dir;
    std::string proton_prefix;
    std::string proton_mapping_name;
    std::string proton_mapped_host_root;
    std::string config_sha256;
    std::string config_backup_sha256;
};

struct game_state {
    game_state();
    int version;
    std::string id;
    std::string steam_app_id;
    std::string display_name;
    std::string install_root;
    std::string steam_root;
    std::string library_root;
    steam_install_kind install_kind;
    bool manual;
    std::string active_target_id;
    std::string active_instance_id;
    std::vector<managed_target> targets;
    std::vector<managed_instance> instances;
};

std::string serialize_manager_index(const manager_index& state);
bool parse_manager_index(const std::string& bytes, manager_index& out, std::string& error);

std::string serialize_game_state(const game_state& state);
bool parse_game_state(const std::string& bytes, game_state& out, std::string& error);

bool valid_manager_id(const std::string& value);
bool valid_instance_slug(const std::string& value);
bool manager_absolute_path(const std::string& value);

} // namespace manager
} // namespace eosr

#endif
