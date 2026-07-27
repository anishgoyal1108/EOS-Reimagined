#ifndef EOSR_MANAGER_STEAM_DISCOVERY_H
#define EOSR_MANAGER_STEAM_DISCOVERY_H

#include <cstddef>
#include <string>
#include <vector>

namespace eosr {
namespace manager {

enum class steam_install_kind {
    native_windows,
    native_linux,
    flatpak_linux
};

struct steam_root_candidate {
    std::string path;
    steam_install_kind kind;
};

enum class discovery_read {
    ok,
    missing,
    unreadable,
    too_large
};

class discovery_filesystem {
public:
    virtual ~discovery_filesystem() {}
    virtual bool canonical_directory(const std::string& path, std::string& out) = 0;
    virtual discovery_read read_file(const std::string& path, std::size_t max_bytes,
                                     std::string& out) = 0;
    virtual bool list_file_names(const std::string& path, std::vector<std::string>& out) = 0;
};

struct steam_discovery_diagnostic {
    std::string code;
    std::string path;
    std::string detail;
};

struct steam_installation {
    std::string root;
    steam_install_kind kind;
    std::vector<std::string> libraries;
};

struct steam_game_installation {
    std::string app_id;
    std::string name;
    std::string install_root;
    std::string library_root;
    std::string steam_root;
    steam_install_kind kind;
};

struct steam_discovery_result {
    std::vector<steam_installation> installations;
    std::vector<steam_game_installation> games;
    std::vector<steam_discovery_diagnostic> diagnostics;
};

std::vector<steam_root_candidate> linux_steam_root_candidates(const std::string& home);

steam_discovery_result discover_steam_games(const std::vector<steam_root_candidate>& roots,
                                            discovery_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
