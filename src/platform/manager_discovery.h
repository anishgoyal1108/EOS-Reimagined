#ifndef EOSR_PLATFORM_MANAGER_DISCOVERY_H
#define EOSR_PLATFORM_MANAGER_DISCOVERY_H

#include <string>
#include <vector>

#include "manager/steam_discovery.h"
#include "manager/target_inspection.h"

namespace eosr {
namespace platform {

// The real filesystem adapter for Steam discovery. The portable discovery service owns all VDF
// interpretation; this class only performs bounded OS reads and canonical path resolution.
class manager_discovery_filesystem : public manager::discovery_filesystem,
                                     public manager::target_filesystem {
public:
    bool canonical_directory(const std::string& path, std::string& out);
    manager::discovery_read read_file(const std::string& path, std::size_t max_bytes,
                                      std::string& out);
    bool list_file_names(const std::string& path, std::vector<std::string>& out);
    bool list_directory(const std::string& path,
                        std::vector<manager::target_directory_entry>& out);
    manager::discovery_read read_prefix(const std::string& path, std::size_t max_bytes,
                                        std::string& out);
    bool canonical_file(const std::string& path, std::string& out);
};

// Candidate Steam roots supplied by the host OS. Discovery validates and canonicalizes each one;
// a registry value or environment variable is never trusted merely because it exists.
std::vector<manager::steam_root_candidate> platform_steam_root_candidates();

} // namespace platform
} // namespace eosr

#endif
