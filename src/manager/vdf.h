#ifndef EOSR_MANAGER_VDF_H
#define EOSR_MANAGER_VDF_H

#include <string>
#include <vector>

namespace eosr {
namespace manager {

struct vdf_entry {
    vdf_entry() : is_object(false) {}

    std::string key;
    bool is_object;
    std::string value;
    std::vector<vdf_entry> children;
};

struct vdf_document {
    std::vector<vdf_entry> entries;
};

// Parse Valve KeyValues text with bounds suitable for Steam metadata. Duplicate keys are retained so
// discovery policy can diagnose or deduplicate them rather than the parser silently choosing one.
bool parse_vdf(const std::string& bytes, vdf_document& out, std::string& error);

const vdf_entry* find_vdf_entry(const std::vector<vdf_entry>& entries, const std::string& key);
const vdf_entry* find_vdf_entry_case_insensitive(const std::vector<vdf_entry>& entries,
                                                 const std::string& key);

} // namespace manager
} // namespace eosr

#endif
