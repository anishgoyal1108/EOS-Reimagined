#ifndef EOSR_MANAGER_RELEASE_CATALOG_H
#define EOSR_MANAGER_RELEASE_CATALOG_H

#include <string>
#include <vector>

#include "manager/target_inspection.h"

namespace eosr {
namespace manager {

struct release_catalog {
    std::string version;
    std::string commit;
    std::string release_id;
    std::string platform;
    std::string architecture;
    std::vector<release_artifact> artifacts;
};

// Reads the deterministic MANIFEST.json emitted by tools/package_manager.py. Only the two exact SDK
// member names become installable artifacts; arbitrary manifest paths are never interpreted as one.
bool parse_release_catalog(const std::string& bytes, const std::string& package_root,
                           release_catalog& out, std::string& error);
bool catalog_artifact(const release_catalog& catalog, eos_binary_kind kind,
                      release_artifact& out);

} // namespace manager
} // namespace eosr

#endif
