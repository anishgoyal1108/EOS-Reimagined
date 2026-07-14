#ifndef EOSR_CORE_CONFIG_H
#define EOSR_CORE_CONFIG_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// How much the trace sink records. Ordered: each level is a strict superset of the one before it.
// Spec: docs/alpha-tracing.md §4.
enum class trace_level {
    off,
    errors,
    lifecycle,
    full
};

// The inclusive discovery-port range an instance binds and advertises to.
struct discovery_range {
    u16 first;
    u16 last;
};

// The fully resolved run configuration. Pure data: resolve_config produces it with no I/O, so it can
// be built and checked in tests without an environment or a filesystem.
// Spec: docs/alpha-tracing.md §2.
struct resolved_config {
    std::string display_name;              // validated UTF-8, bounded to the EOS caps
    std::string data_dir;                  // profile/key storage (given, already env-resolved)
    std::string run_dir;                   // EOSR_RUN_DIR, or empty for the auto <trace_dir>/<run_id>
    std::string trace_dir;                 // resolved against data_dir when relative
    trace_level level;
    u64 trace_max_bytes;                   // per-file cap before rotation, clamped to its range
    u32 trace_max_rotated_files;           // rotated files kept, not counting the live one
    discovery_range discovery_ports;
    std::string instance_label;            // path-safe slug, or empty when unset
    // One human-readable note per field that was rejected, clamped, or truncated. These become
    // meta/config trace records once the sink exists; here they just record what resolution did.
    std::vector<std::string> diagnostics;
};

// The environment and the parsed config file, abstracted so resolution touches neither directly.
// The real source wraps getenv and the JSON-parsed file (built later); tests supply a fake.
class config_source {
public:
    virtual ~config_source() {}

    // The value of an environment variable. True if it is present -- even when set to the empty
    // string, which resolution then treats as unset. `out` is untouched when false.
    virtual bool env(const std::string& name, std::string& out) const = 0;

    // Typed reads of the parsed config file. Each returns true only when the key exists with that
    // exact shape; a missing key, or one of another shape, returns false, and the field falls through.
    virtual bool file_string(const std::string& key, std::string& out) const = 0;
    virtual bool file_int(const std::string& key, i64& out) const = 0;
    virtual bool file_int_pair(const std::string& key, i64& first, i64& second) const = 0;
};

// The non-config inputs resolution needs: the platform's already-resolved data directory (which is
// where EOSR_DATA_DIR is honoured), and the built-in discovery-port range.
struct config_defaults {
    std::string data_dir;
    discovery_range default_ports;
};

// Resolve the configuration from `source`, highest precedence first: environment, then file, then
// default. An override that fails validation at any layer is discarded and resolution falls through
// to the next; an environment variable set to empty is unset. No I/O, no globals -- given the same
// inputs it returns the same result.
// Spec: docs/alpha-tracing.md §2.
resolved_config resolve_config(const config_source& source, const config_defaults& defaults);

} // namespace eosr

#endif
