#ifndef EOSR_CORE_SYSTEM_CONFIG_SOURCE_H
#define EOSR_CORE_SYSTEM_CONFIG_SOURCE_H

#include <map>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/config.h"
#include "core/config_file.h"

namespace eosr {

// The real config_source. It touches the OS exactly once, up front: a snapshot of the recognized
// EOSR_* environment variables taken at construction, plus one bounded read of eosr.json. So
// resolution over it is deterministic for the life of the object -- the environment changing later,
// or the file being edited, does not move a value out from under a run. File-loading problems (an
// unreadable, oversized, or malformed file, or an explicitly selected EOSR_CONFIG that is missing)
// become diagnostics; an absent *default* file is normal and silent.
// Spec: docs/alpha-tracing.md §2, §7.
class system_config_source : public config_source {
public:
    explicit system_config_source(const std::string& data_dir);

    bool env(const std::string& name, std::string& out) const;
    lookup file_string(const std::string& key, std::string& out) const;
    lookup file_int(const std::string& key, i64& out) const;
    lookup file_int_pair(const std::string& key, i64& first, i64& second) const;

    // Problems met while loading the config file. The caller folds these into resolution's own
    // diagnostics so the run's meta/config records carry both.
    const std::vector<config_diagnostic>& diagnostics() const { return diagnostics_; }

private:
    void snapshot_env();
    void load_file(const std::string& data_dir);

    std::map<std::string, std::string> env_;
    config_file file_;
    std::vector<config_diagnostic> diagnostics_;
};

} // namespace eosr

#endif
