#include <cstdlib>
#include <iostream>
#include <string>

#include "eos_init.h"
#include "eos_sdk.h"

#include "platform/dynlib.h"
#include "platform/paths.h"

using eosr::platform::dynamic_library;

namespace {

typedef EOS_EResult (EOS_CALL *initialize_fn)(const EOS_InitializeOptions*);
typedef EOS_EResult (EOS_CALL *shutdown_fn)();
typedef EOS_HPlatform (EOS_CALL *platform_create_fn)(const EOS_Platform_Options*);
typedef void (EOS_CALL *platform_release_fn)(EOS_HPlatform);

char separator() {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

std::string parent_path(const std::string& path) {
    const std::size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? std::string() : path.substr(0, at);
}

std::string join(const std::string& parent, const std::string& child) {
    return parent + separator() + child;
}

std::string json_string(const std::string& value) {
    std::string out = "\"";
    for (std::size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        if (c == '\\' || c == '"') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void unset_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

bool file_exists(const std::string& path) {
    std::string bytes;
    return eosr::platform::read_file_capped(path, 65536, bytes) == eosr::platform::file_read::ok;
}

bool write_new(const std::string& path, const std::string& bytes) {
    if (!eosr::platform::create_new_file(path)) {
        return false;
    }
    if (!eosr::platform::append_file(path, bytes)) {
        eosr::platform::remove_file(path);
        return false;
    }
    return true;
}

class remove_on_exit {
public:
    explicit remove_on_exit(const std::string& path) : path_(path) {}
    ~remove_on_exit() { eosr::platform::remove_file(path_); }

private:
    std::string path_;
};

template <typename Function>
Function resolve(dynamic_library& library, const char* name) {
    return reinterpret_cast<Function>(library.symbol(name));
}

bool exercise_sdk(const std::string& library_path) {
    dynamic_library library;
    if (!library.open(library_path.c_str())) {
        std::cerr << "could not load SDK: " << library_path << '\n';
        return false;
    }
    initialize_fn initialize = resolve<initialize_fn>(library, "EOS_Initialize");
    shutdown_fn shutdown = resolve<shutdown_fn>(library, "EOS_Shutdown");
    platform_create_fn create = resolve<platform_create_fn>(library, "EOS_Platform_Create");
    platform_release_fn release = resolve<platform_release_fn>(library, "EOS_Platform_Release");
    if (initialize == 0 || shutdown == 0 || create == 0 || release == 0) {
        std::cerr << "SDK bootstrap exports are missing\n";
        return false;
    }

    EOS_InitializeOptions initialize_options = {};
    initialize_options.ApiVersion = EOS_INITIALIZE_API_LATEST;
    initialize_options.ProductName = "BootstrapDescriptorProbe";
    initialize_options.ProductVersion = "1";
    if (initialize(&initialize_options) != EOS_EResult::EOS_Success) {
        std::cerr << "EOS_Initialize failed\n";
        return false;
    }

    EOS_Platform_Options platform_options = {};
    platform_options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform_options.ProductId = "bootstrap-product";
    platform_options.SandboxId = "bootstrap-sandbox";
    platform_options.DeploymentId = "bootstrap-deployment";
    platform_options.ClientCredentials.ClientId = "client";
    platform_options.ClientCredentials.ClientSecret = "secret";
    EOS_HPlatform platform = create(&platform_options);
    if (platform == 0) {
        shutdown();
        std::cerr << "EOS_Platform_Create failed\n";
        return false;
    }
    release(platform);
    if (shutdown() != EOS_EResult::EOS_Success) {
        std::cerr << "EOS_Shutdown failed\n";
        return false;
    }
    library.close();
    return true;
}

void clear_probe_environment() {
    const char* names[] = {
        "EOSR_DATA_DIR", "EOSR_CONFIG", "EOSR_RUN_DIR", "EOSR_TRACE", "EOSR_TRACE_DIR",
        "EOSR_DISPLAY_NAME", "EOSR_INSTANCE_LABEL"
    };
    for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        unset_env(names[i]);
    }
}

bool prepare_directory(const std::string& path) {
    if (!eosr::platform::make_directories(path)) {
        return false;
    }
    eosr::platform::remove_file(join(path, "profile.key"));
    eosr::platform::remove_file(join(path, "profile.lock"));
    eosr::platform::remove_file(join(path, "eosr.json"));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: bootstrap_descriptor_probe <library> "
                     "<descriptor|environment|invalid>\n"
                     "   or: bootstrap_descriptor_probe <library> manager <data-dir> <run-dir>\n";
        return 2;
    }
    clear_probe_environment();

    std::string executable_path;
    if (!eosr::platform::loaded_module_path(executable_path)) {
        std::cerr << "could not obtain the probe's loaded module path\n";
        return 1;
    }
    const std::string module_dir = parent_path(executable_path);
    const std::string mode = argv[2];
    if (mode == "manager") {
        if (argc != 5) {
            std::cerr << "manager mode requires data and run directories\n";
            return 2;
        }
        const std::string expected_dir = argv[3];
        const std::string run_dir = argv[4];
        if (!eosr::platform::make_directories(expected_dir) ||
            !eosr::platform::make_directories(run_dir)) {
            std::cerr << "could not prepare manager workflow directories\n";
            return 1;
        }
        set_env("EOSR_RUN_DIR", run_dir);
        set_env("EOSR_TRACE", "lifecycle");
        set_env("EOSR_INSTANCE_LABEL", "manager-e2e");
        if (!exercise_sdk(argv[1])) {
            return 1;
        }
        std::string runtime;
        if (!file_exists(join(expected_dir, "profile.key")) ||
            eosr::platform::read_file_capped(join(run_dir, "runtime.json"), 1024 * 1024,
                                             runtime) != eosr::platform::file_read::ok ||
            runtime.find("\"instance_label\":\"manager-e2e\"") == std::string::npos) {
            std::cerr << "manager-installed library did not select its descriptor/run identity\n";
            return 1;
        }
        clear_probe_environment();
        return 0;
    }
    if (argc != 3) {
        std::cerr << "unexpected arguments for descriptor probe mode\n";
        return 2;
    }
    const std::string root = join(module_dir, "bootstrap-probe-" + mode);
    const std::string descriptor_dir = join(root, "descriptor-data");
    const std::string environment_dir = join(root, "environment-data");
    const std::string fallback_base = join(root, "fallback-base");
#if defined(_WIN32)
    const std::string fallback_dir = join(fallback_base, "eos-reimagined");
    set_env("LOCALAPPDATA", fallback_base);
#else
    const std::string fallback_dir = join(fallback_base, "eos-reimagined");
    set_env("XDG_DATA_HOME", fallback_base);
#endif
    if (!prepare_directory(descriptor_dir) || !prepare_directory(environment_dir) ||
        !prepare_directory(fallback_dir)) {
        std::cerr << "could not prepare probe directories\n";
        return 1;
    }

    const std::string descriptor_path = join(module_dir, "eosr-bootstrap.json");
    std::string descriptor_bytes;
    std::string expected_dir;
    if (mode == "descriptor") {
        descriptor_bytes = "{\"version\":1,\"data_dir\":" + json_string(descriptor_dir) + "}";
        expected_dir = descriptor_dir;
    } else if (mode == "environment") {
        descriptor_bytes = "{\"version\":1,\"data_dir\":" + json_string(descriptor_dir) + "}";
        set_env("EOSR_DATA_DIR", environment_dir);
        expected_dir = environment_dir;
    } else if (mode == "invalid") {
        descriptor_bytes = "{\"version\":1,\"data_dir\":\"relative\"}";
        const std::string run_dir = join(root, "diagnostic-run");
        if (!eosr::platform::make_directories(run_dir)) {
            std::cerr << "could not prepare diagnostic run\n";
            return 1;
        }
        eosr::platform::remove_file(join(run_dir, "trace.jsonl"));
        eosr::platform::remove_file(join(run_dir, "runtime.json"));
        set_env("EOSR_RUN_DIR", run_dir);
        set_env("EOSR_TRACE", "lifecycle");
        expected_dir = fallback_dir;
    } else {
        std::cerr << "unknown probe mode\n";
        return 2;
    }

    if (!write_new(descriptor_path, descriptor_bytes)) {
        std::cerr << "refusing to overwrite an existing descriptor: " << descriptor_path << '\n';
        return 1;
    }
    remove_on_exit descriptor_cleanup(descriptor_path);

    if (!exercise_sdk(argv[1])) {
        return 1;
    }
    if (!file_exists(join(expected_dir, "profile.key"))) {
        std::cerr << "selected data directory did not receive profile.key: " << expected_dir << '\n';
        return 1;
    }
    if (expected_dir != descriptor_dir && file_exists(join(descriptor_dir, "profile.key"))) {
        std::cerr << "lower-precedence descriptor unexpectedly won\n";
        return 1;
    }

    if (mode == "invalid") {
        std::string trace;
        const std::string trace_path = join(join(root, "diagnostic-run"), "trace.jsonl");
        if (eosr::platform::read_file_capped(trace_path, 1024 * 1024, trace) !=
            eosr::platform::file_read::ok) {
            std::cerr << "invalid descriptor did not produce a diagnostic trace\n";
            return 1;
        }
        if (trace.find("\"event\":\"config\"") == std::string::npos ||
            trace.find("\"source\":\"bootstrap\"") == std::string::npos ||
            trace.find("\"reason\":\"invalid_descriptor\"") == std::string::npos) {
            std::cerr << "bootstrap diagnostic is missing or unstructured\n";
            return 1;
        }
    }
    clear_probe_environment();
    return 0;
}
