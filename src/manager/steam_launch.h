#ifndef EOSR_MANAGER_STEAM_LAUNCH_H
#define EOSR_MANAGER_STEAM_LAUNCH_H

#include <string>
#include <vector>

namespace eosr {
namespace manager {

enum class steam_launch_adapter {
    windows_client,
    windows_uri,
    linux_client,
    flatpak_linux,
    desktop_uri
};

struct steam_launch_request {
    steam_launch_request() : shell_command(false) {}

    std::string executable;
    std::vector<std::string> arguments;
    bool shell_command;
};

// Construct argv for a normal Steam URI launch. Nothing is concatenated into a shell command, and
// the app id is bounded to the decimal u32 syntax Steam uses before it reaches the URI.
bool build_steam_launch_request(steam_launch_adapter adapter, const std::string& steam_executable,
                                const std::string& app_id, steam_launch_request& out,
                                std::string& error);

} // namespace manager
} // namespace eosr

#endif
