#include "platform/proton_paths.h"

namespace eosr {
namespace platform {

bool translate_host_path_for_proton(const std::string&, const std::string&,
                                    proton_path_translation& out, proton_path_error& error) {
    out = proton_path_translation();
    error = proton_path_error::unsupported;
    return false;
}

bool validate_proton_path_translation(const std::string&,
                                      const proton_path_translation&,
                                      proton_path_error& error) {
    error = proton_path_error::unsupported;
    return false;
}

bool flatpak_steam_manager_roots(const std::string&, flatpak_steam_path_roots& out,
                                 proton_path_error& error) {
    out = flatpak_steam_path_roots();
    error = proton_path_error::unsupported;
    return false;
}

bool flatpak_steam_runtime_path(const std::string&, const std::string&,
                                std::string& runtime_path, proton_path_error& error) {
    runtime_path.clear();
    error = proton_path_error::unsupported;
    return false;
}

bool translate_flatpak_host_path_for_proton(
    const std::string&, const std::string&, const std::string&,
    proton_path_translation& out, proton_path_error& error) {
    out = proton_path_translation();
    error = proton_path_error::unsupported;
    return false;
}

bool validate_flatpak_proton_path_translation(
    const std::string&, const std::string&, const proton_path_translation&,
    proton_path_error& error) {
    error = proton_path_error::unsupported;
    return false;
}

} // namespace platform
} // namespace eosr
