#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/config.h"
#include "core/system_config_source.h"
#include "platform/paths.h"

using namespace eosr;

namespace {

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// Each test gets its own directory so files never leak between runs, and starts with the config
// environment variables cleared so a stray one from another test cannot bleed in.
struct source_fixture {
    std::string dir;

    explicit source_fixture(const std::string& name) {
        dir = std::string(EOSR_TEST_PROFILE_DIR) + "/config-" + name;
        clear_env();
        std::remove((dir + "/eosr.json").c_str()); // drop a leftover from a prior run
        platform::make_directories(dir);
    }
    ~source_fixture() { clear_env(); }

    void clear_env() {
        const char* names[] = {"EOSR_CONFIG",          "EOSR_DISPLAY_NAME", "EOSR_TRACE",
                               "EOSR_DISCOVERY_PORTS",  "EOSR_TRACE_MAX_BYTES",
                               "EOSR_TRACE_MAX_ROTATED", "EOSR_TRACE_DIR", "EOSR_INSTANCE_LABEL",
                               "EOSR_RUN_DIR"};
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            unset_env(names[i]);
        }
    }

    std::string write(const std::string& name, const std::string& content) {
        const std::string path = dir + "/" + name;
        REQUIRE(platform::write_private_file(path, content));
        return path;
    }
};

bool has_field(const std::vector<config_diagnostic>& diagnostics, const std::string& field) {
    for (std::size_t i = 0; i < diagnostics.size(); i++) {
        if (diagnostics[i].field == field) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("the default eosr.json in the data directory is read") {
    source_fixture fx("default-present");
    fx.write("eosr.json", "{ \"display_name\": \"FromDefault\" }");

    system_config_source source(fx.dir);
    std::string value;
    CHECK(source.file_string("display_name", value) == lookup::ok);
    CHECK(value == "FromDefault");
    CHECK(source.diagnostics().empty());
}

TEST_CASE("a missing default file is normal and silent") {
    source_fixture fx("default-missing");
    system_config_source source(fx.dir);
    std::string value;
    CHECK(source.file_string("display_name", value) == lookup::missing);
    CHECK(source.diagnostics().empty());
}

TEST_CASE("EOSR_CONFIG selects the file explicitly") {
    source_fixture fx("explicit");
    const std::string path = fx.write("custom.json", "{ \"display_name\": \"FromExplicit\" }");
    set_env("EOSR_CONFIG", path.c_str());

    system_config_source source(fx.dir);
    std::string value;
    CHECK(source.file_string("display_name", value) == lookup::ok);
    CHECK(value == "FromExplicit");
    CHECK(source.diagnostics().empty());
}

TEST_CASE("an explicitly selected file that is missing is diagnosed") {
    source_fixture fx("explicit-missing");
    set_env("EOSR_CONFIG", (fx.dir + "/does-not-exist.json").c_str());

    system_config_source source(fx.dir);
    std::string value;
    CHECK(source.file_string("display_name", value) == lookup::missing);
    CHECK(has_field(source.diagnostics(), "config"));
}

TEST_CASE("an unreadable file is diagnosed") {
    source_fixture fx("unreadable");
    // A directory is not a readable file: open-then-read fails on POSIX, open fails on Windows.
    set_env("EOSR_CONFIG", fx.dir.c_str());

    system_config_source source(fx.dir);
    CHECK(has_field(source.diagnostics(), "config"));
}

TEST_CASE("an oversized file is diagnosed and not parsed") {
    source_fixture fx("oversized");
    fx.write("eosr.json", "{ \"pad\": \"" + std::string(70000, 'a') + "\" }");

    system_config_source source(fx.dir);
    std::string value;
    CHECK(source.file_string("pad", value) == lookup::missing);
    CHECK(has_field(source.diagnostics(), "config"));
}

TEST_CASE("a malformed file is diagnosed and leaves no values") {
    source_fixture fx("parse-error");
    fx.write("eosr.json", "{ malformed ");

    system_config_source source(fx.dir);
    std::string value;
    CHECK(source.file_string("display_name", value) == lookup::missing);
    CHECK(has_field(source.diagnostics(), "config"));
}

TEST_CASE("the environment is snapshotted at construction") {
    source_fixture fx("env-snapshot");
    set_env("EOSR_DISPLAY_NAME", "Snapshot");
    system_config_source source(fx.dir);
    set_env("EOSR_DISPLAY_NAME", "ChangedLater"); // after construction

    std::string value;
    CHECK(source.env("EOSR_DISPLAY_NAME", value));
    CHECK(value == "Snapshot"); // the later change does not move the snapshot
}

TEST_CASE("a wrong-typed file value reaches the resolver as a diagnostic") {
    source_fixture fx("e2e-wrong-type");
    fx.write("eosr.json", "{ \"trace_max_bytes\": \"not-an-integer\" }");

    system_config_source source(fx.dir);
    config_defaults defaults;
    defaults.data_dir = "/data";
    defaults.default_ports.first = 55789;
    defaults.default_ports.last = 55798;
    const resolved_config config = resolve_config(source, defaults);

    CHECK(config.trace_max_bytes == 67108864u); // the wrong-typed value was ignored
    CHECK(has_field(config.diagnostics, "trace_max_bytes"));
}
