#ifndef EOSR_PLATFORM_PROTON_PATHS_H
#define EOSR_PLATFORM_PROTON_PATHS_H

#include <string>

namespace eosr {
namespace platform {

enum class proton_path_error {
    none,
    unsupported,
    prefix_missing,
    host_missing,
    no_mapping,
    mapping_changed,
    round_trip_failed,
    flatpak_root_invalid,
    flatpak_path_outside
};

struct flatpak_steam_path_roots {
    std::string app_host_root;
    std::string manager_host_root;
    std::string manager_runtime_root;
};

// A translation is a snapshot, not a timeless promise. The manager stores both path forms and the
// exact mapping it proved, then revalidates immediately before committing or launching.
struct proton_path_translation {
    proton_path_translation() : drive(0) {}

    std::string prefix_path;
    std::string host_path;
    std::string windows_path;
    std::string mapped_host_root;
    std::string mapping_name;
    char drive;
};

bool translate_host_path_for_proton(const std::string& prefix, const std::string& host_path,
                                    proton_path_translation& out, proton_path_error& error);

bool validate_proton_path_translation(const std::string& prefix,
                                      const proton_path_translation& translation,
                                      proton_path_error& error);

// Flatpak Steam exposes its persistent app directory as the user's home inside the sandbox. These
// helpers prove that namespace hop before applying a Proton dosdevices mapping. A host path that
// merely has the same sandbox-visible spelling is deliberately refused.
bool flatpak_steam_manager_roots(const std::string& steam_root,
                                 flatpak_steam_path_roots& out,
                                 proton_path_error& error);

bool flatpak_steam_runtime_path(const std::string& steam_root,
                                const std::string& host_path,
                                std::string& runtime_path,
                                proton_path_error& error);

bool translate_flatpak_host_path_for_proton(
    const std::string& steam_root, const std::string& prefix,
    const std::string& host_path, proton_path_translation& out,
    proton_path_error& error);

bool validate_flatpak_proton_path_translation(
    const std::string& steam_root, const std::string& prefix,
    const proton_path_translation& translation, proton_path_error& error);

} // namespace platform
} // namespace eosr

#endif
