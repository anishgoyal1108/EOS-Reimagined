#include "manager/steam_discovery.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>

#include "common/types.h"
#include "manager/vdf.h"

namespace eosr {
namespace manager {

namespace {

const std::size_t max_steam_metadata_bytes = 1024 * 1024;

struct app_manifest {
    std::string app_id;
    std::string name;
    std::string install_dir;
};

std::string join_path(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    const char last = parent[parent.size() - 1];
    return (last == '/' || last == '\\') ? parent + child : parent + "/" + child;
}

bool decimal_app_id(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    const u64 maximum = 4294967295ULL;
    u64 value = 0;
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10 + static_cast<u64>(text[i] - '0');
        if (value > maximum) {
            return false;
        }
    }
    return value != 0;
}

bool safe_install_dir(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\' ||
        (path.size() >= 2 && path[1] == ':')) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = start;
        while (end < path.size() && path[end] != '/' && path[end] != '\\') {
            const unsigned char c = static_cast<unsigned char>(path[end]);
            if (c == 0 || c < 0x20) {
                return false;
            }
            end++;
        }
        const std::string component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == path.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool ascii_equal_case_insensitive(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); i++) {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool unique_scalar(const std::vector<vdf_entry>& entries, const std::string& name,
                   std::string& value) {
    const vdf_entry* found = 0;
    for (std::size_t i = 0; i < entries.size(); i++) {
        const vdf_entry* entry = &entries[i];
        if (!ascii_equal_case_insensitive(entry->key, name)) {
            continue;
        }
        if (found != 0 || entry->is_object) {
            return false;
        }
        found = entry;
    }
    if (found == 0) {
        return false;
    }
    value = found->value;
    return true;
}

bool parse_manifest(const std::string& bytes, app_manifest& out, std::string& error) {
    out = app_manifest();
    vdf_document document;
    if (!parse_vdf(bytes, document, error)) {
        return false;
    }
    const vdf_entry* state = find_vdf_entry_case_insensitive(document.entries, "appstate");
    if (state == 0 || !state->is_object) {
        error = "missing AppState object";
        return false;
    }
    if (!unique_scalar(state->children, "appid", out.app_id) ||
        !unique_scalar(state->children, "name", out.name) ||
        !unique_scalar(state->children, "installdir", out.install_dir)) {
        error = "missing, duplicate, or mistyped manifest field";
        return false;
    }
    if (!decimal_app_id(out.app_id)) {
        error = "invalid app id";
        return false;
    }
    if (!safe_install_dir(out.install_dir)) {
        error = "unsafe install directory";
        return false;
    }
    return true;
}

bool manifest_file_name(const std::string& name, std::string& app_id) {
    const std::string prefix = "appmanifest_";
    const std::string suffix = ".acf";
    if (name.size() <= prefix.size() + suffix.size() ||
        name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    app_id = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    return decimal_app_id(app_id);
}

void diagnostic(steam_discovery_result& result, const char* code, const std::string& path,
                const std::string& detail = std::string()) {
    steam_discovery_diagnostic value;
    value.code = code;
    value.path = path;
    value.detail = detail;
    result.diagnostics.push_back(value);
}

bool numbered_key(const std::string& key) {
    if (key.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < key.size(); i++) {
        if (key[i] < '0' || key[i] > '9') {
            return false;
        }
    }
    return true;
}

std::vector<std::string> library_paths(const std::string& root, const std::string& bytes,
                                       std::string& error) {
    std::vector<std::string> paths;
    paths.push_back(root);
    if (bytes.empty()) {
        return paths;
    }
    vdf_document document;
    if (!parse_vdf(bytes, document, error)) {
        return paths;
    }
    const vdf_entry* folders = find_vdf_entry_case_insensitive(document.entries, "libraryfolders");
    if (folders == 0 || !folders->is_object) {
        error = "missing libraryfolders object";
        return paths;
    }
    for (std::size_t i = 0; i < folders->children.size(); i++) {
        const vdf_entry& child = folders->children[i];
        if (!numbered_key(child.key)) {
            continue;
        }
        if (!child.is_object) {
            if (!child.value.empty()) {
                paths.push_back(child.value);
            }
            continue;
        }
        const vdf_entry* path = find_vdf_entry_case_insensitive(child.children, "path");
        if (path != 0 && !path->is_object && !path->value.empty()) {
            paths.push_back(path->value);
        }
    }
    return paths;
}

std::string flatpak_host_library_path(const steam_installation& installation,
                                      const std::string& candidate) {
    if (installation.kind != steam_install_kind::flatpak_linux) return candidate;
    const std::string marker = "/.var/app/com.valvesoftware.Steam/";
    const std::string::size_type marker_at = installation.root.find(marker);
    if (marker_at == std::string::npos) return candidate;

    std::string normalized = candidate;
    while (normalized.size() > 1 && normalized[normalized.size() - 1] == '/') {
        normalized.resize(normalized.size() - 1);
    }
    const std::string home = installation.root.substr(0, marker_at);
    if (normalized == home + "/.local/share/Steam" ||
        normalized == home + "/.steam/steam" || normalized == home + "/.steam/root") {
        // Flatpak Steam can persist the path visible inside its sandbox. On the host that spelling
        // may name an unrelated native Steam tree; the candidate root is the verified host mapping.
        return installation.root;
    }
    return candidate;
}

void scan_library(const steam_installation& installation, const std::string& library,
                  discovery_filesystem& filesystem, std::set<std::string>& seen_games,
                  steam_discovery_result& result) {
    const std::string steamapps = join_path(library, "steamapps");
    std::vector<std::string> names;
    if (!filesystem.list_file_names(steamapps, names)) {
        diagnostic(result, "steamapps_unreadable", steamapps);
        return;
    }
    std::sort(names.begin(), names.end());
    for (std::size_t i = 0; i < names.size(); i++) {
        std::string file_app_id;
        if (!manifest_file_name(names[i], file_app_id)) {
            continue;
        }
        const std::string manifest_path = join_path(steamapps, names[i]);
        std::string bytes;
        const discovery_read read = filesystem.read_file(
            manifest_path, max_steam_metadata_bytes, bytes);
        if (read != discovery_read::ok) {
            diagnostic(result, read == discovery_read::too_large ? "manifest_too_large" :
                       "manifest_unreadable", manifest_path);
            continue;
        }
        app_manifest manifest;
        std::string error;
        if (!parse_manifest(bytes, manifest, error)) {
            diagnostic(result, error == "unsafe install directory" ? "unsafe_install_dir" :
                       "manifest_invalid", manifest_path, error);
            continue;
        }
        if (manifest.app_id != file_app_id) {
            diagnostic(result, "appid_mismatch", manifest_path);
            continue;
        }
        const std::string requested = join_path(join_path(steamapps, "common"),
                                                manifest.install_dir);
        std::string install_root;
        if (!filesystem.canonical_directory(requested, install_root)) {
            diagnostic(result, "install_missing", requested);
            continue;
        }
        const std::string identity = manifest.app_id + "\n" + install_root;
        if (!seen_games.insert(identity).second) {
            continue;
        }
        steam_game_installation game;
        game.app_id = manifest.app_id;
        game.name = manifest.name;
        game.install_root = install_root;
        game.library_root = library;
        game.steam_root = installation.root;
        game.kind = installation.kind;
        result.games.push_back(game);
    }
}

} // namespace

std::vector<steam_root_candidate> linux_steam_root_candidates(const std::string& home) {
    std::vector<steam_root_candidate> roots;
    if (home.empty()) {
        return roots;
    }
    const char* native_paths[] = {
        "/.steam/steam", "/.steam/root", "/.local/share/Steam"
    };
    for (std::size_t i = 0; i < sizeof(native_paths) / sizeof(native_paths[0]); i++) {
        steam_root_candidate candidate;
        candidate.path = home + native_paths[i];
        candidate.kind = steam_install_kind::native_linux;
        roots.push_back(candidate);
    }
    const char* flatpak_paths[] = {
        "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
        "/.var/app/com.valvesoftware.Steam/data/Steam",
        "/.var/app/com.valvesoftware.Steam/.steam/steam"
    };
    for (std::size_t i = 0; i < sizeof(flatpak_paths) / sizeof(flatpak_paths[0]); i++) {
        steam_root_candidate candidate;
        candidate.path = home + flatpak_paths[i];
        candidate.kind = steam_install_kind::flatpak_linux;
        roots.push_back(candidate);
    }
    return roots;
}

steam_discovery_result discover_steam_games(const std::vector<steam_root_candidate>& roots,
                                            discovery_filesystem& filesystem) {
    steam_discovery_result result;
    std::set<std::string> seen_roots;
    std::set<std::string> seen_games;
    for (std::size_t root_index = 0; root_index < roots.size(); root_index++) {
        std::string canonical_root;
        if (!filesystem.canonical_directory(roots[root_index].path, canonical_root)) {
            continue;
        }
        if (!seen_roots.insert(canonical_root).second) {
            continue;
        }

        steam_installation installation;
        installation.root = canonical_root;
        installation.kind = roots[root_index].kind;
        const std::string folders_path = join_path(
            join_path(canonical_root, "steamapps"), "libraryfolders.vdf");
        std::string bytes;
        const discovery_read folders_read = filesystem.read_file(
            folders_path, max_steam_metadata_bytes, bytes);
        std::string parse_error;
        std::vector<std::string> candidates;
        if (folders_read == discovery_read::ok) {
            candidates = library_paths(canonical_root, bytes, parse_error);
            if (!parse_error.empty()) {
                diagnostic(result, "libraryfolders_invalid", folders_path, parse_error);
            }
        } else {
            candidates.push_back(canonical_root);
            if (folders_read == discovery_read::too_large) {
                diagnostic(result, "libraryfolders_too_large", folders_path);
            } else if (folders_read == discovery_read::unreadable) {
                diagnostic(result, "libraryfolders_unreadable", folders_path);
            }
        }

        std::set<std::string> seen_libraries;
        for (std::size_t i = 0; i < candidates.size(); i++) {
            std::string library;
            const std::string host_candidate =
                flatpak_host_library_path(installation, candidates[i]);
            if (!filesystem.canonical_directory(host_candidate, library)) {
                diagnostic(result, "library_missing", candidates[i]);
                continue;
            }
            if (seen_libraries.insert(library).second) {
                installation.libraries.push_back(library);
            }
        }
        result.installations.push_back(installation);
        const steam_installation& stored = result.installations.back();
        for (std::size_t i = 0; i < stored.libraries.size(); i++) {
            scan_library(stored, stored.libraries[i], filesystem, seen_games, result);
        }
    }
    return result;
}

} // namespace manager
} // namespace eosr
