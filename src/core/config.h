#ifndef EOSR_CORE_CONFIG_H
#define EOSR_CORE_CONFIG_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// How much the trace sink records. Ordered: each level is a strict superset of the one before it.
// Spec: wiki/developers/internals/alpha-tracing.qmd §4.
enum class trace_level {
    off,
    errors,
    lifecycle,
    full
};

// How much the ordinary EOS logger emits. Ordered like EOS_ELogLevel, and named for the values the
// emulator ecosystem already uses (off/fatal/err/warn/info/debug/trace), so a config written for
// another Epic emulator means the same thing here.
enum class log_level {
    off,
    fatal,
    error,
    warn,
    info,
    debug,
    trace
};

// The inclusive discovery-port range an instance binds and advertises to.
struct discovery_range {
    u16 first;
    u16 last;
};

// The outcome of a typed file lookup. A parsed key can be absent, present with the expected type, or
// present with a different JSON type -- and the last is a config error the contract must report, so
// it cannot be folded into "absent".
enum class lookup {
    missing,
    ok,
    wrong_type
};

enum class config_origin {
    unknown,
    default_value,
    file,
    environment
};

const char* config_origin_name(config_origin origin);

struct resolved_config_sources {
    resolved_config_sources();
    config_origin display_name;
    config_origin trace_level;
    config_origin trace_max_bytes;
    config_origin trace_max_rotated_files;
    config_origin discovery_ports;
    config_origin peer_seeds;
    config_origin instance_label;
    config_origin trace_dir;
    config_origin locale;
    config_origin log_level;
    config_origin enable_lan;
    config_origin enable_overlay;
    config_origin unlock_dlcs;
};

// A structured note about a field resolution had to reject, clamp, or truncate. Stable fields so the
// alpha tooling can compare runs across games by machine, not by matching prose; `message` is an
// optional human sentence. These become meta/config trace records once the sink exists.
struct config_diagnostic {
    std::string field;     // the config field, e.g. "trace_max_bytes"
    std::string source;    // "environment" or "file"
    std::string reason;    // e.g. "wrong type", "not an integer", "embedded NUL", "out of range"
    std::string action;    // "ignored", "clamped", or "truncated"
    std::string message;   // optional human-readable detail
};

// The fully resolved run configuration. Pure data: resolve_config produces it with no I/O, so it can
// be built and checked in tests without an environment or a filesystem.
// Spec: wiki/developers/internals/alpha-tracing.qmd §2.
// Every scalar carries its default in-class, so a default-built resolved_config (a test's, or the
// process-global one before EOS_Initialize resolves it) is well-defined rather than indeterminate.
struct resolved_config {
    std::string display_name;              // validated UTF-8, bounded to the EOS caps
    std::string data_dir;                  // profile/key storage (given, already env-resolved)
    std::string run_dir;                   // EOSR_RUN_DIR, or empty for the auto <trace_dir>/<run_id>
    std::string trace_dir;                 // resolved against data_dir when relative
    trace_level level = trace_level::off;
    u64 trace_max_bytes = 0;               // per-file cap before rotation, clamped to its range
    u32 trace_max_rotated_files = 0;       // rotated files kept, not counting the live one
    discovery_range discovery_ports = discovery_range();
    std::vector<u32> peer_seeds;           // validated unicast IPv4 addresses, in host byte order
    std::string instance_label;            // path-safe slug, or empty when unset

    // Emulator behaviour the game itself does not set. These drive the platform, not the trace.
    std::string locale;                    // ISO-639 language, e.g. "en"
    log_level logging = log_level::off;    // the ordinary EOS logger's threshold
    bool enable_lan = true;                // false runs the platform with no peer network at all
    bool enable_overlay = false;           // accepted; we have no overlay to show (see docs)
    bool unlock_dlcs = false;              // accepted; we have no Ecom interface yet (see docs)

    // Authority for every manager-editable directive after invalid higher-priority values fall
    // through. Manually assembled configs retain unknown origins rather than inventing provenance.
    resolved_config_sources sources;

    // One entry per field that was rejected, clamped, or truncated -- what resolution did and why.
    std::vector<config_diagnostic> diagnostics;
};

// The environment and the parsed config file, abstracted so resolution touches neither directly.
// The real source wraps getenv and the JSON-parsed file (built later); tests supply a fake.
class config_source {
public:
    virtual ~config_source() {}

    // The value of an environment variable. True if it is present -- even when set to the empty
    // string, which resolution then treats as unset. `out` is untouched when false.
    virtual bool env(const std::string& name, std::string& out) const = 0;

    // Typed reads of the parsed config file. `ok` fills `out`; `wrong_type` means the key exists with
    // a different JSON type (a config error to report); `missing` means it is absent. Resolution
    // distinguishes the last two so a mistyped key produces a diagnostic rather than a silent default.
    virtual lookup file_string(const std::string& key, std::string& out) const = 0;
    virtual lookup file_int(const std::string& key, i64& out) const = 0;
    virtual lookup file_int_pair(const std::string& key, i64& first, i64& second) const = 0;
    virtual lookup file_bool(const std::string& key, bool& out) const = 0;
    virtual lookup file_string_array(const std::string& key,
                                     std::vector<std::string>& out) const = 0;
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
// Spec: wiki/developers/internals/alpha-tracing.qmd §2.
resolved_config resolve_config(const config_source& source, const config_defaults& defaults);

} // namespace eosr

#endif
