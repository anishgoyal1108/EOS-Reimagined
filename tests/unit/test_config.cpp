#include "doctest.h"

#include <map>
#include <string>
#include <utility>

#include "common/types.h"
#include "core/config.h"

using namespace eosr;

namespace {

// A config_source backed by plain maps, so a test states exactly what the environment and the parsed
// file hold and nothing touches the real environment or disk.
struct fake_source : config_source {
    std::map<std::string, std::string> envs;
    std::map<std::string, std::string> file_strings;
    std::map<std::string, i64> file_ints;
    std::map<std::string, std::pair<i64, i64> > file_pairs;

    bool env(const std::string& name, std::string& out) const {
        std::map<std::string, std::string>::const_iterator it = envs.find(name);
        if (it == envs.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    bool file_string(const std::string& key, std::string& out) const {
        std::map<std::string, std::string>::const_iterator it = file_strings.find(key);
        if (it == file_strings.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    bool file_int(const std::string& key, i64& out) const {
        std::map<std::string, i64>::const_iterator it = file_ints.find(key);
        if (it == file_ints.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    bool file_int_pair(const std::string& key, i64& first, i64& second) const {
        std::map<std::string, std::pair<i64, i64> >::const_iterator it = file_pairs.find(key);
        if (it == file_pairs.end()) {
            return false;
        }
        first = it->second.first;
        second = it->second.second;
        return true;
    }
};

config_defaults make_defaults() {
    config_defaults defaults;
    defaults.data_dir = "/data";
    defaults.default_ports.first = 55789;
    defaults.default_ports.last = 55798;
    return defaults;
}

bool has_diagnostic(const resolved_config& config, const std::string& needle) {
    for (std::size_t i = 0; i < config.diagnostics.size(); i++) {
        if (config.diagnostics[i].find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const std::size_t max_display_bytes = 64;
const char* grinning = "\xF0\x9F\x98\x80"; // U+1F600, four UTF-8 bytes

std::string repeat(const std::string& unit, std::size_t times) {
    std::string out;
    for (std::size_t i = 0; i < times; i++) {
        out += unit;
    }
    return out;
}

} // namespace

TEST_CASE("defaults apply when nothing is configured") {
    fake_source source;
    const resolved_config config = resolve_config(source, make_defaults());
    CHECK(config.display_name == "Player");
    CHECK(config.data_dir == "/data");
    CHECK(config.run_dir.empty());
    CHECK(config.trace_dir == "/data/traces");
    CHECK(config.level == trace_level::off);
    CHECK(config.trace_max_bytes == 67108864u);
    CHECK(config.trace_max_rotated_files == 8u);
    CHECK(config.discovery_ports.first == 55789);
    CHECK(config.discovery_ports.last == 55798);
    CHECK(config.instance_label.empty());
    CHECK(config.diagnostics.empty());
}

TEST_CASE("environment overrides the file overrides the default") {
    fake_source source;
    source.file_strings["display_name"] = "FromFile";
    resolved_config config = resolve_config(source, make_defaults());
    CHECK(config.display_name == "FromFile"); // file beats default

    source.envs["EOSR_DISPLAY_NAME"] = "FromEnv";
    config = resolve_config(source, make_defaults());
    CHECK(config.display_name == "FromEnv"); // env beats file
}

TEST_CASE("an empty environment variable is unset") {
    fake_source source;
    source.file_strings["display_name"] = "FromFile";
    source.envs["EOSR_DISPLAY_NAME"] = ""; // present but empty -> unset
    const resolved_config config = resolve_config(source, make_defaults());
    CHECK(config.display_name == "FromFile");
}

TEST_CASE("an invalid environment override falls through to the file value") {
    fake_source source;
    source.file_strings["trace_level"] = "lifecycle";
    source.envs["EOSR_TRACE"] = "verbose"; // not a level -> discard, fall to file
    const resolved_config config = resolve_config(source, make_defaults());
    CHECK(config.level == trace_level::lifecycle);
    CHECK(has_diagnostic(config, "trace"));
}

TEST_CASE("the display name is bounded to both EOS caps") {
    config_defaults defaults = make_defaults();

    fake_source seventeen;
    seventeen.envs["EOSR_DISPLAY_NAME"] = "abcdefghijklmnopq"; // 17 ASCII chars
    resolved_config config = resolve_config(seventeen, defaults);
    CHECK(config.display_name == "abcdefghijklmnop"); // truncated to 16 characters
    CHECK(has_diagnostic(config, "display"));

    fake_source wide;
    wide.envs["EOSR_DISPLAY_NAME"] = repeat(grinning, 16); // 16 codepoints, 64 bytes
    config = resolve_config(wide, defaults);
    CHECK(config.display_name == repeat(grinning, 16)); // both caps satisfied, kept whole
    CHECK(config.display_name.size() == max_display_bytes);

    fake_source too_wide;
    too_wide.envs["EOSR_DISPLAY_NAME"] = repeat(grinning, 17); // 17 codepoints, 68 bytes
    config = resolve_config(too_wide, defaults);
    CHECK(config.display_name == repeat(grinning, 16)); // cut to 16 codepoints on a boundary
    CHECK(config.display_name.size() == max_display_bytes);

    fake_source invalid;
    invalid.envs["EOSR_DISPLAY_NAME"] = "\xFF\xFE"; // not valid UTF-8
    invalid.file_strings["display_name"] = "GoodName";
    config = resolve_config(invalid, defaults);
    CHECK(config.display_name == "GoodName"); // invalid rejected, falls through
}

TEST_CASE("trace level parses its four values and rejects others") {
    config_defaults defaults = make_defaults();
    fake_source source;

    source.envs["EOSR_TRACE"] = "off";
    CHECK(resolve_config(source, defaults).level == trace_level::off);
    source.envs["EOSR_TRACE"] = "errors";
    CHECK(resolve_config(source, defaults).level == trace_level::errors);
    source.envs["EOSR_TRACE"] = "lifecycle";
    CHECK(resolve_config(source, defaults).level == trace_level::lifecycle);
    source.envs["EOSR_TRACE"] = "full";
    CHECK(resolve_config(source, defaults).level == trace_level::full);

    source.envs["EOSR_TRACE"] = "nonsense";
    const resolved_config config = resolve_config(source, defaults);
    CHECK(config.level == trace_level::off); // rejected, default
    CHECK(has_diagnostic(config, "trace"));
}

TEST_CASE("trace_max_bytes clamps to its range and rejects non-integers") {
    config_defaults defaults = make_defaults();

    fake_source too_small;
    too_small.envs["EOSR_TRACE_MAX_BYTES"] = "1024"; // below the 64 KiB minimum
    resolved_config config = resolve_config(too_small, defaults);
    CHECK(config.trace_max_bytes == 65536u);
    CHECK(has_diagnostic(config, "trace_max_bytes"));

    fake_source too_big;
    too_big.envs["EOSR_TRACE_MAX_BYTES"] = "9999999999"; // above the 1 GiB maximum
    config = resolve_config(too_big, defaults);
    CHECK(config.trace_max_bytes == 1073741824u);

    fake_source not_int;
    not_int.envs["EOSR_TRACE_MAX_BYTES"] = "64MiB"; // not an integer
    not_int.file_ints["trace_max_bytes"] = 131072;
    config = resolve_config(not_int, defaults);
    CHECK(config.trace_max_bytes == 131072u); // env rejected, file value used
}

TEST_CASE("trace_max_rotated_files clamps to its range") {
    config_defaults defaults = make_defaults();

    fake_source zero;
    zero.envs["EOSR_TRACE_MAX_ROTATED"] = "0";
    CHECK(resolve_config(zero, defaults).trace_max_rotated_files == 0u); // zero is valid

    fake_source over;
    over.envs["EOSR_TRACE_MAX_ROTATED"] = "500"; // above 64
    const resolved_config config = resolve_config(over, defaults);
    CHECK(config.trace_max_rotated_files == 64u);
    CHECK(has_diagnostic(config, "rotated"));
}

TEST_CASE("discovery ports validate range and span") {
    config_defaults defaults = make_defaults();

    fake_source from_env;
    from_env.envs["EOSR_DISCOVERY_PORTS"] = "40000-40007";
    resolved_config config = resolve_config(from_env, defaults);
    CHECK(config.discovery_ports.first == 40000);
    CHECK(config.discovery_ports.last == 40007);

    fake_source from_file;
    from_file.file_pairs["discovery_ports"] = std::pair<i64, i64>(41000, 41003);
    config = resolve_config(from_file, defaults);
    CHECK(config.discovery_ports.first == 41000);
    CHECK(config.discovery_ports.last == 41003);

    fake_source inverted;
    inverted.envs["EOSR_DISCOVERY_PORTS"] = "50000-49000"; // first > last
    config = resolve_config(inverted, defaults);
    CHECK(config.discovery_ports.first == 55789); // rejected -> default
    CHECK(has_diagnostic(config, "discovery"));

    fake_source huge;
    huge.envs["EOSR_DISCOVERY_PORTS"] = "1000-2000"; // span > 64
    config = resolve_config(huge, defaults);
    CHECK(config.discovery_ports.first == 55789); // rejected -> default

    fake_source zero;
    zero.envs["EOSR_DISCOVERY_PORTS"] = "0-10"; // zero is not a valid port
    config = resolve_config(zero, defaults);
    CHECK(config.discovery_ports.first == 55789); // rejected -> default
}

TEST_CASE("instance label accepts a slug and rejects unsafe values") {
    config_defaults defaults = make_defaults();

    fake_source good;
    good.envs["EOSR_INSTANCE_LABEL"] = "alice-2.v_3";
    CHECK(resolve_config(good, defaults).instance_label == "alice-2.v_3");

    const char* unsafe[] = {"../etc", "a/b", "a\\b", ".", "..",
                            "waaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaay-too-long", ""};
    for (std::size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++) {
        fake_source source;
        source.envs["EOSR_INSTANCE_LABEL"] = unsafe[i];
        const resolved_config config = resolve_config(source, defaults);
        CHECK(config.instance_label.empty()); // rejected back to unset
    }
}

TEST_CASE("a relative trace dir resolves against data dir, an absolute one is used as-is") {
    config_defaults defaults = make_defaults();

    fake_source relative;
    relative.envs["EOSR_TRACE_DIR"] = "my-traces";
    CHECK(resolve_config(relative, defaults).trace_dir == "/data/my-traces");

    fake_source absolute;
    absolute.envs["EOSR_TRACE_DIR"] = "/var/log/eosr";
    CHECK(resolve_config(absolute, defaults).trace_dir == "/var/log/eosr");

    fake_source windows;
    windows.envs["EOSR_TRACE_DIR"] = "C:\\traces";
    CHECK(resolve_config(windows, defaults).trace_dir == "C:\\traces"); // drive-letter absolute
}

TEST_CASE("the run directory comes from EOSR_RUN_DIR or is left for the auto path") {
    config_defaults defaults = make_defaults();

    fake_source none;
    CHECK(resolve_config(none, defaults).run_dir.empty());

    fake_source runner;
    runner.envs["EOSR_RUN_DIR"] = "/runs/alice-run-1";
    CHECK(resolve_config(runner, defaults).run_dir == "/runs/alice-run-1");
}
