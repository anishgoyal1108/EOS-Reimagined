#include "core/system_config_source.h"

#include <cstddef>
#include <cstdlib>

#include "platform/paths.h"

namespace eosr {

namespace {

// Matches the reader's input cap: the platform read stops one byte past this, so an oversized file is
// caught at the read rather than after slurping it whole.
const std::size_t max_config_bytes = 65536;

// The EOSR_* variables resolution consults. Snapshotting exactly these keeps the source from holding a
// copy of the whole environment.
const char* const recognized_env[] = {
    "EOSR_DISPLAY_NAME", "EOSR_RUN_DIR",       "EOSR_TRACE_DIR",       "EOSR_TRACE",
    "EOSR_TRACE_MAX_BYTES", "EOSR_TRACE_MAX_ROTATED", "EOSR_DISCOVERY_PORTS", "EOSR_INSTANCE_LABEL"
};

// A stable machine-readable reason; the free-text detail (a parser message, say) goes in `message` so
// tooling can aggregate failures without matching prose.
void add_config_diag(std::vector<config_diagnostic>& diagnostics, const char* source,
                     const char* reason, const std::string& message) {
    config_diagnostic diagnostic;
    diagnostic.field = "config";
    diagnostic.source = source;
    diagnostic.reason = reason;
    diagnostic.action = "ignored";
    diagnostic.message = message;
    diagnostics.push_back(diagnostic);
}

} // namespace

system_config_source::system_config_source(const std::string& data_dir)
    : config_env_present_(false) {
    snapshot_env();
    load_file(data_dir);
}

void system_config_source::snapshot_env() {
    for (std::size_t i = 0; i < sizeof(recognized_env) / sizeof(recognized_env[0]); i++) {
        const char* name = recognized_env[i];
        const char* value = std::getenv(name);
        if (value != 0) {
            // Present even when empty; resolution treats an empty value as unset.
            env_[name] = value;
        }
    }
    // The file selector is captured here too, so selecting the file and reading its values come from
    // one coherent snapshot rather than a second getenv the environment could change under.
    const char* selector = std::getenv("EOSR_CONFIG");
    if (selector != 0) {
        config_env_present_ = true;
        config_env_value_ = selector;
    }
}

void system_config_source::load_file(const std::string& data_dir) {
    std::string path;
    bool explicit_path = false;
    if (config_env_present_ && !config_env_value_.empty()) {
        // A relative EOSR_CONFIG resolves against data_dir, never the process cwd -- which varies
        // wildly between launchers -- so the same setting means the same file everywhere.
        path = platform::path_is_absolute(config_env_value_)
                   ? config_env_value_
                   : (data_dir + "/" + config_env_value_);
        explicit_path = true;
    } else {
        path = data_dir + "/eosr.json";
    }
    const char* source = explicit_path ? "environment" : "file";

    std::string bytes;
    const platform::file_read result = platform::read_file_capped(path, max_config_bytes, bytes);
    if (result == platform::file_read::missing) {
        // A missing default file is normal; a missing explicitly-selected one is worth reporting.
        if (explicit_path) {
            add_config_diag(diagnostics_, source, "file not found", std::string());
        }
        return;
    }
    if (result == platform::file_read::unreadable) {
        add_config_diag(diagnostics_, source, "unreadable", std::string());
        return;
    }
    if (result == platform::file_read::too_large) {
        add_config_diag(diagnostics_, source, "too large", std::string());
        return;
    }
    std::string error;
    if (!parse_config_file(bytes, file_, error)) {
        // parse_config_file cleared file_ on failure, so no partial values survive. The parser's
        // detail is the message; the reason stays stable.
        add_config_diag(diagnostics_, source, "parse error", error);
    }
}

bool system_config_source::env(const std::string& name, std::string& out) const {
    std::map<std::string, std::string>::const_iterator it = env_.find(name);
    if (it == env_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

lookup system_config_source::file_string(const std::string& key, std::string& out) const {
    return file_.get_string(key, out);
}

lookup system_config_source::file_int(const std::string& key, i64& out) const {
    return file_.get_int(key, out);
}

lookup system_config_source::file_int_pair(const std::string& key, i64& first, i64& second) const {
    return file_.get_int_pair(key, first, second);
}

resolved_config load_resolved_config(const std::string& data_dir,
                                     const discovery_range& default_ports) {
    system_config_source source(data_dir);
    config_defaults defaults;
    defaults.data_dir = data_dir;
    defaults.default_ports = default_ports;
    resolved_config config = resolve_config(source, defaults);
    // Fold the file-loading diagnostics in after resolution's own, exactly once, so a malformed or
    // unreadable file is never silently lost.
    const std::vector<config_diagnostic>& loading = source.diagnostics();
    config.diagnostics.insert(config.diagnostics.end(), loading.begin(), loading.end());
    return config;
}

} // namespace eosr
