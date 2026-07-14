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
// Spec: wiki/developers/internals/alpha-tracing.qmd §2, §7.
class system_config_source : public config_source {
public:
    explicit system_config_source(const std::string& data_dir);

    bool env(const std::string& name, std::string& out) const;
    lookup file_string(const std::string& key, std::string& out) const;
    lookup file_int(const std::string& key, i64& out) const;
    lookup file_int_pair(const std::string& key, i64& first, i64& second) const;
    lookup file_bool(const std::string& key, bool& out) const;

    // Problems met while loading the config file. The caller folds these into resolution's own
    // diagnostics so the run's meta/config records carry both.
    const std::vector<config_diagnostic>& diagnostics() const { return diagnostics_; }

private:
    void snapshot_env();
    void load_file(const std::string& data_dir);

    std::map<std::string, std::string> env_;
    // The config-file selector is captured with the other variables so file selection and the value
    // reads come from one coherent snapshot, not two separate getenv calls.
    bool config_env_present_;
    std::string config_env_value_;
    config_file file_;
    std::vector<config_diagnostic> diagnostics_;
};

// Load and resolve the run configuration in one step: construct the system source (env snapshot plus
// the bounded file read), resolve over it, and merge the source's file-loading diagnostics into the
// result exactly once. This is the single entry point EOS_Initialize *will* call once the lifecycle
// slice wires tracing in -- until then the pipeline is complete but dormant -- so that no caller can
// forget to fold in a malformed/missing/unreadable-file diagnostic.
resolved_config load_resolved_config(const std::string& data_dir,
                                     const discovery_range& default_ports);

} // namespace eosr

#endif
