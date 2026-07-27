#include "manager/steam_launch.h"

#include <cstddef>

#include "common/types.h"

namespace eosr {
namespace manager {

namespace {

bool valid_app_id(const std::string& app_id) {
    if (app_id.empty()) {
        return false;
    }
    const u64 max_app_id = 4294967295ULL;
    u64 value = 0;
    for (std::size_t i = 0; i < app_id.size(); i++) {
        const char digit = app_id[i];
        if (digit < '0' || digit > '9') {
            return false;
        }
        value = value * 10 + static_cast<u64>(digit - '0');
        if (value > max_app_id) {
            return false;
        }
    }
    return value != 0;
}

} // namespace

bool build_steam_launch_request(steam_launch_adapter adapter, const std::string& steam_executable,
                                const std::string& app_id, steam_launch_request& out,
                                std::string& error) {
    out = steam_launch_request();
    error.clear();
    if (!valid_app_id(app_id)) {
        error = "Steam app id must be a nonzero decimal 32-bit value";
        return false;
    }

    const std::string uri = "steam://run/" + app_id;
    if (adapter == steam_launch_adapter::windows_client ||
        adapter == steam_launch_adapter::linux_client) {
        if (steam_executable.empty()) {
            error = "Steam executable is missing";
            return false;
        }
        out.executable = steam_executable;
        out.arguments.push_back(uri);
        return true;
    }
    if (adapter == steam_launch_adapter::flatpak_linux) {
        out.executable = "flatpak";
        out.arguments.push_back("run");
        out.arguments.push_back("com.valvesoftware.Steam");
        out.arguments.push_back(uri);
        return true;
    }
    if (adapter == steam_launch_adapter::windows_uri) {
        out.arguments.push_back(uri);
        return true;
    }
    if (adapter == steam_launch_adapter::desktop_uri) {
        out.executable = "xdg-open";
        out.arguments.push_back(uri);
        return true;
    }

    error = "unsupported Steam launch adapter";
    return false;
}

} // namespace manager
} // namespace eosr
