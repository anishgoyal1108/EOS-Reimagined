#include "core/bootstrap_descriptor.h"

#include <cstddef>
#include <cstdlib>

#include "core/config_file.h"
#include "platform/paths.h"

namespace eosr {

namespace {

const std::size_t max_bootstrap_bytes = 4096;
const char* const bootstrap_name = "eosr-bootstrap.json";

void reject(bootstrap_descriptor& out, std::string& error, const char* message) {
    out = bootstrap_descriptor();
    error = message;
}

std::string sibling_path(const std::string& module_path) {
    const std::size_t separator = module_path.find_last_of("/\\");
    if (separator == std::string::npos) {
        return std::string();
    }
    return module_path.substr(0, separator + 1) + bootstrap_name;
}

void add_diagnostic(data_directory_selection& selection, const char* reason,
                    const std::string& message) {
    config_diagnostic diagnostic;
    diagnostic.field = "data_dir";
    diagnostic.source = "bootstrap";
    diagnostic.reason = reason;
    diagnostic.action = "ignored";
    diagnostic.message = message;
    selection.diagnostics.push_back(diagnostic);
}

data_directory_selection fallback_selection() {
    data_directory_selection selection;
    selection.path = platform::default_user_data_directory();
    selection.source = data_directory_source::platform_default;
    return selection;
}

} // namespace

bool parse_bootstrap_descriptor(const std::string& bytes, bootstrap_descriptor& out,
                                std::string& error) {
    out = bootstrap_descriptor();
    error.clear();

    config_file file;
    if (!parse_config_file(bytes, file, error)) {
        return false;
    }
    if (file.size() != 2) {
        reject(out, error, "descriptor must contain only version and data_dir");
        return false;
    }

    i64 version = 0;
    if (file.get_int("version", version) != lookup::ok) {
        reject(out, error, "version must be an integer");
        return false;
    }
    if (version != 1) {
        reject(out, error, "unsupported descriptor version");
        return false;
    }

    std::string data_dir;
    if (file.get_string("data_dir", data_dir) != lookup::ok) {
        reject(out, error, "data_dir must be a string");
        return false;
    }
    if (data_dir.find('\0') != std::string::npos) {
        reject(out, error, "data_dir contains a NUL byte");
        return false;
    }
    if (!platform::path_is_fully_qualified(data_dir)) {
        reject(out, error, "data_dir must be an absolute path");
        return false;
    }

    out.version = 1;
    out.data_dir = data_dir;
    return true;
}

data_directory_selection select_data_directory() {
    const char* environment = std::getenv("EOSR_DATA_DIR");
    if (environment != 0 && environment[0] != '\0') {
        data_directory_selection selection;
        selection.path = environment;
        selection.source = data_directory_source::environment;
        return selection;
    }

    data_directory_selection selection = fallback_selection();
    std::string module_path;
    if (!platform::loaded_module_path(module_path)) {
        return selection;
    }
    const std::string path = sibling_path(module_path);
    if (path.empty()) {
        return selection;
    }

    std::string bytes;
    const platform::file_read read = platform::read_file_capped(path, max_bootstrap_bytes, bytes);
    if (read == platform::file_read::missing) {
        return selection;
    }
    if (read == platform::file_read::unreadable) {
        add_diagnostic(selection, "unreadable", std::string());
        return selection;
    }
    if (read == platform::file_read::too_large) {
        add_diagnostic(selection, "too large", std::string());
        return selection;
    }

    bootstrap_descriptor descriptor;
    std::string error;
    if (!parse_bootstrap_descriptor(bytes, descriptor, error)) {
        add_diagnostic(selection, "invalid descriptor", error);
        return selection;
    }
    selection.path = descriptor.data_dir;
    selection.source = data_directory_source::bootstrap;
    return selection;
}

} // namespace eosr
