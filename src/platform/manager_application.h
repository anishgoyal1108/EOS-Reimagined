#ifndef EOSR_PLATFORM_MANAGER_APPLICATION_H
#define EOSR_PLATFORM_MANAGER_APPLICATION_H

#include <string>

#include "common/types.h"
#include "manager/steam_launch.h"

namespace eosr {
namespace platform {

struct manager_action_result {
    manager_action_result();
    bool ok;
    u64 process_id;
    std::string code;
    std::string detail;
};

std::string manager_data_root();
std::string manager_executable_path();
bool manager_ensure_private_directory(const std::string& path, std::string& error);
std::string manager_random_id(std::string& error); // 128 random bits as 32 lowercase hex
std::string manager_utc_now();

manager_action_result manager_launch_steam(const manager::steam_launch_request& request);
manager_action_result manager_open_location(const std::string& path, bool select_file);

} // namespace platform
} // namespace eosr

#endif
