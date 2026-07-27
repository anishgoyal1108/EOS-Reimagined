#include "platform/proton_paths.h"

#include <cstddef>

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace eosr {
namespace platform {

namespace {

bool canonical_directory(const std::string& path, std::string& out) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == 0) {
        return false;
    }
    struct stat info;
    if (stat(resolved, &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false;
    }
    out = resolved;
    return true;
}

bool drive_mapping_name(const std::string& name) {
    if (name.size() != 2 || name[1] != ':') {
        return false;
    }
    const char drive = name[0];
    return (drive >= 'a' && drive <= 'z') || (drive >= 'A' && drive <= 'Z');
}

char upper_drive(char drive) {
    return (drive >= 'a' && drive <= 'z') ? static_cast<char>(drive - 'a' + 'A') : drive;
}

bool path_contains(const std::string& root, const std::string& path) {
    if (root == "/") {
        return !path.empty() && path[0] == '/';
    }
    return path == root ||
           (path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
            path[root.size()] == '/');
}

std::string relative_to(const std::string& root, const std::string& path) {
    if (root == "/") {
        return path.size() > 1 ? path.substr(1) : std::string();
    }
    if (path == root) {
        return std::string();
    }
    return path.substr(root.size() + 1);
}

std::string windows_path(char drive, const std::string& relative) {
    std::string out;
    out += upper_drive(drive);
    out += ":\\";
    for (std::size_t i = 0; i < relative.size(); i++) {
        out += relative[i] == '/' ? '\\' : relative[i];
    }
    return out;
}

bool canonical_mapping(const std::string& dosdevices, const std::string& name,
                       std::string& target) {
    const std::string path = dosdevices + "/" + name;
    struct stat link_info;
    if (lstat(path.c_str(), &link_info) != 0 || !S_ISLNK(link_info.st_mode)) {
        return false;
    }
    return canonical_directory(path, target);
}

bool verify_round_trip(const std::string& mapped_root, const std::string& host_path) {
    if (!path_contains(mapped_root, host_path)) {
        return false;
    }
    const std::string relative = relative_to(mapped_root, host_path);
    const std::string reconstructed = relative.empty() ? mapped_root : mapped_root + "/" + relative;
    std::string canonical;
    return canonical_directory(reconstructed, canonical) && canonical == host_path;
}

bool flatpak_runtime_for_host(const std::string& steam_root,
                              const std::string& host_path,
                              flatpak_steam_path_roots& roots,
                              std::string& canonical_host,
                              std::string& runtime_path,
                              proton_path_error& error) {
    canonical_host.clear();
    runtime_path.clear();
    if (!flatpak_steam_manager_roots(steam_root, roots, error)) return false;
    if (!canonical_directory(host_path, canonical_host)) {
        error = proton_path_error::host_missing;
        return false;
    }
    std::string canonical_manager_root;
    if (!canonical_directory(roots.manager_host_root, canonical_manager_root) ||
        canonical_manager_root != roots.manager_host_root ||
        !path_contains(canonical_manager_root, canonical_host)) {
        error = proton_path_error::flatpak_path_outside;
        return false;
    }
    const std::string relative = relative_to(canonical_manager_root, canonical_host);
    runtime_path = relative.empty() ? roots.manager_runtime_root :
                                      roots.manager_runtime_root + "/" + relative;
    return true;
}

bool select_runtime_mapping(const std::string& prefix, const std::string& runtime_path,
                            std::string& canonical_prefix, std::string& best_name,
                            std::string& best_target, char& best_drive,
                            proton_path_error& error) {
    canonical_prefix.clear();
    best_name.clear();
    best_target.clear();
    best_drive = 0;
    if (!canonical_directory(prefix, canonical_prefix)) {
        error = proton_path_error::prefix_missing;
        return false;
    }
    const std::string dosdevices = canonical_prefix + "/dosdevices";
    std::string canonical_dosdevices;
    if (!canonical_directory(dosdevices, canonical_dosdevices)) {
        error = proton_path_error::prefix_missing;
        return false;
    }
    DIR* directory = opendir(canonical_dosdevices.c_str());
    if (directory == 0) {
        error = proton_path_error::prefix_missing;
        return false;
    }
    struct dirent* entry = 0;
    while ((entry = readdir(directory)) != 0) {
        const std::string name = entry->d_name;
        if (!drive_mapping_name(name)) continue;
        std::string target;
        if (!canonical_mapping(canonical_dosdevices, name, target) ||
            !path_contains(target, runtime_path)) continue;
        const char drive = upper_drive(name[0]);
        if (best_name.empty() || target.size() > best_target.size() ||
            (target.size() == best_target.size() && drive < best_drive)) {
            best_name = name;
            best_target = target;
            best_drive = drive;
        }
    }
    closedir(directory);
    if (best_name.empty()) {
        error = proton_path_error::no_mapping;
        return false;
    }
    const std::string relative = relative_to(best_target, runtime_path);
    const std::string reconstructed = relative.empty() ? best_target :
        (best_target == "/" ? best_target + relative : best_target + "/" + relative);
    if (reconstructed != runtime_path) {
        error = proton_path_error::round_trip_failed;
        return false;
    }
    std::string current_target;
    if (!canonical_mapping(canonical_dosdevices, best_name, current_target) ||
        current_target != best_target) {
        error = proton_path_error::mapping_changed;
        return false;
    }
    return true;
}

} // namespace

bool flatpak_steam_manager_roots(const std::string& steam_root,
                                 flatpak_steam_path_roots& out,
                                 proton_path_error& error) {
    out = flatpak_steam_path_roots();
    error = proton_path_error::none;
    std::string canonical_steam_root;
    if (!canonical_directory(steam_root, canonical_steam_root)) {
        error = proton_path_error::flatpak_root_invalid;
        return false;
    }
    const std::string marker = "/.var/app/com.valvesoftware.Steam/";
    const std::string::size_type marker_at = canonical_steam_root.find(marker);
    if (marker_at == std::string::npos || marker_at == 0) {
        error = proton_path_error::flatpak_root_invalid;
        return false;
    }
    const std::string home = canonical_steam_root.substr(0, marker_at);
    const std::string app_root = home + "/.var/app/com.valvesoftware.Steam";
    std::string canonical_app_root;
    if (!canonical_directory(app_root, canonical_app_root) ||
        canonical_app_root != app_root ||
        !path_contains(canonical_app_root, canonical_steam_root)) {
        error = proton_path_error::flatpak_root_invalid;
        return false;
    }
    out.app_host_root = canonical_app_root;
    out.manager_host_root = canonical_app_root +
                            "/.local/share/eos-reimagined-manager";
    out.manager_runtime_root = home + "/.local/share/eos-reimagined-manager";
    return true;
}

bool flatpak_steam_runtime_path(const std::string& steam_root,
                                const std::string& host_path,
                                std::string& runtime_path,
                                proton_path_error& error) {
    flatpak_steam_path_roots roots;
    std::string canonical_host;
    return flatpak_runtime_for_host(steam_root, host_path, roots, canonical_host,
                                    runtime_path, error);
}

bool translate_flatpak_host_path_for_proton(
    const std::string& steam_root, const std::string& prefix,
    const std::string& host_path, proton_path_translation& out,
    proton_path_error& error) {
    out = proton_path_translation();
    error = proton_path_error::none;
    flatpak_steam_path_roots roots;
    std::string canonical_host;
    std::string runtime_path;
    if (!flatpak_runtime_for_host(steam_root, host_path, roots, canonical_host,
                                  runtime_path, error)) return false;
    std::string canonical_prefix;
    std::string mapping_name;
    std::string mapped_root;
    char drive = 0;
    if (!select_runtime_mapping(prefix, runtime_path, canonical_prefix, mapping_name,
                                mapped_root, drive, error)) return false;

    out.prefix_path = canonical_prefix;
    out.host_path = canonical_host;
    out.mapped_host_root = mapped_root;
    out.mapping_name = mapping_name;
    out.drive = drive;
    out.windows_path = windows_path(drive, relative_to(mapped_root, runtime_path));
    return true;
}

bool validate_flatpak_proton_path_translation(
    const std::string& steam_root, const std::string& prefix,
    const proton_path_translation& translation, proton_path_error& error) {
    error = proton_path_error::none;
    flatpak_steam_path_roots roots;
    std::string canonical_host;
    std::string runtime_path;
    if (!flatpak_runtime_for_host(steam_root, translation.host_path, roots,
                                  canonical_host, runtime_path, error)) return false;
    if (canonical_host != translation.host_path) {
        error = proton_path_error::flatpak_path_outside;
        return false;
    }
    std::string canonical_prefix;
    if (!canonical_directory(prefix, canonical_prefix)) {
        error = proton_path_error::prefix_missing;
        return false;
    }
    if (canonical_prefix != translation.prefix_path ||
        !drive_mapping_name(translation.mapping_name) || translation.drive == 0 ||
        upper_drive(translation.mapping_name[0]) != upper_drive(translation.drive)) {
        error = proton_path_error::mapping_changed;
        return false;
    }
    const std::string dosdevices = canonical_prefix + "/dosdevices";
    std::string mapped_root;
    if (!canonical_mapping(dosdevices, translation.mapping_name, mapped_root) ||
        mapped_root != translation.mapped_host_root) {
        error = proton_path_error::mapping_changed;
        return false;
    }
    if (!path_contains(mapped_root, runtime_path)) {
        error = proton_path_error::round_trip_failed;
        return false;
    }
    const std::string expected = windows_path(
        translation.drive, relative_to(mapped_root, runtime_path));
    if (expected != translation.windows_path) {
        error = proton_path_error::round_trip_failed;
        return false;
    }
    return true;
}

bool translate_host_path_for_proton(const std::string& prefix, const std::string& host_path,
                                    proton_path_translation& out, proton_path_error& error) {
    out = proton_path_translation();
    error = proton_path_error::none;

    std::string canonical_prefix;
    if (!canonical_directory(prefix, canonical_prefix)) {
        error = proton_path_error::prefix_missing;
        return false;
    }
    const std::string dosdevices = canonical_prefix + "/dosdevices";
    std::string canonical_dosdevices;
    if (!canonical_directory(dosdevices, canonical_dosdevices)) {
        error = proton_path_error::prefix_missing;
        return false;
    }

    std::string canonical_host;
    if (!canonical_directory(host_path, canonical_host)) {
        error = proton_path_error::host_missing;
        return false;
    }

    DIR* directory = opendir(canonical_dosdevices.c_str());
    if (directory == 0) {
        error = proton_path_error::prefix_missing;
        return false;
    }

    std::string best_name;
    std::string best_target;
    char best_drive = 0;
    struct dirent* entry = 0;
    while ((entry = readdir(directory)) != 0) {
        const std::string name = entry->d_name;
        if (!drive_mapping_name(name)) {
            continue;
        }
        std::string target;
        if (!canonical_mapping(canonical_dosdevices, name, target) ||
            !path_contains(target, canonical_host)) {
            continue;
        }
        const char drive = upper_drive(name[0]);
        if (best_name.empty() || target.size() > best_target.size() ||
            (target.size() == best_target.size() && drive < best_drive)) {
            best_name = name;
            best_target = target;
            best_drive = drive;
        }
    }
    closedir(directory);

    if (best_name.empty()) {
        error = proton_path_error::no_mapping;
        return false;
    }
    if (!verify_round_trip(best_target, canonical_host)) {
        error = proton_path_error::round_trip_failed;
        return false;
    }

    // Re-read the symlink after selection so a mapping changed during enumeration cannot be recorded
    // as proven. The manager repeats this validation just before use as well.
    std::string current_target;
    if (!canonical_mapping(canonical_dosdevices, best_name, current_target) ||
        current_target != best_target) {
        error = proton_path_error::mapping_changed;
        return false;
    }

    out.prefix_path = canonical_prefix;
    out.host_path = canonical_host;
    out.mapped_host_root = best_target;
    out.mapping_name = best_name;
    out.drive = best_drive;
    out.windows_path = windows_path(best_drive, relative_to(best_target, canonical_host));
    return true;
}

bool validate_proton_path_translation(const std::string& prefix,
                                      const proton_path_translation& translation,
                                      proton_path_error& error) {
    error = proton_path_error::none;
    std::string canonical_prefix;
    if (!canonical_directory(prefix, canonical_prefix)) {
        error = proton_path_error::prefix_missing;
        return false;
    }
    if (canonical_prefix != translation.prefix_path ||
        !drive_mapping_name(translation.mapping_name) || translation.drive == 0) {
        error = proton_path_error::mapping_changed;
        return false;
    }

    const std::string dosdevices = canonical_prefix + "/dosdevices";
    std::string target;
    if (!canonical_mapping(dosdevices, translation.mapping_name, target) ||
        target != translation.mapped_host_root) {
        error = proton_path_error::mapping_changed;
        return false;
    }

    std::string host;
    if (!canonical_directory(translation.host_path, host)) {
        error = proton_path_error::host_missing;
        return false;
    }
    if (host != translation.host_path || !verify_round_trip(target, host)) {
        error = proton_path_error::round_trip_failed;
        return false;
    }
    const std::string expected = windows_path(translation.drive, relative_to(target, host));
    if (expected != translation.windows_path) {
        error = proton_path_error::round_trip_failed;
        return false;
    }
    return true;
}

} // namespace platform
} // namespace eosr
