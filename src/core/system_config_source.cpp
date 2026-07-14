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

void add_config_diag(std::vector<config_diagnostic>& diagnostics, const char* source,
                     const std::string& reason) {
    config_diagnostic diagnostic;
    diagnostic.field = "config";
    diagnostic.source = source;
    diagnostic.reason = reason;
    diagnostic.action = "ignored";
    diagnostics.push_back(diagnostic);
}

} // namespace

system_config_source::system_config_source(const std::string& data_dir) {
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
}

void system_config_source::load_file(const std::string& data_dir) {
    std::string path;
    bool explicit_path = false;
    const char* configured = std::getenv("EOSR_CONFIG");
    if (configured != 0 && configured[0] != '\0') {
        path = configured;
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
            add_config_diag(diagnostics_, source, "config file not found");
        }
        return;
    }
    if (result == platform::file_read::unreadable) {
        add_config_diag(diagnostics_, source, "config file could not be read");
        return;
    }
    if (result == platform::file_read::too_large) {
        add_config_diag(diagnostics_, source, "config file exceeds the size limit");
        return;
    }
    std::string error;
    if (!parse_config_file(bytes, file_, error)) {
        // parse_config_file cleared file_ on failure, so no partial values survive.
        add_config_diag(diagnostics_, source, "config file is not valid JSON: " + error);
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

} // namespace eosr
