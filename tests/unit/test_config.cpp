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
    std::map<std::string, bool> file_flags;

    // A key lives in at most one of the typed maps, so "present in some other map" is exactly the
    // wrong-type case the real JSON source will report.
    bool present(const std::string& key) const {
        return file_strings.count(key) != 0 || file_ints.count(key) != 0 ||
               file_pairs.count(key) != 0 || file_flags.count(key) != 0;
    }

    bool env(const std::string& name, std::string& out) const {
        std::map<std::string, std::string>::const_iterator it = envs.find(name);
        if (it == envs.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    lookup file_string(const std::string& key, std::string& out) const {
        std::map<std::string, std::string>::const_iterator it = file_strings.find(key);
        if (it != file_strings.end()) {
            out = it->second;
            return lookup::ok;
        }
        return present(key) ? lookup::wrong_type : lookup::missing;
    }
    lookup file_int(const std::string& key, i64& out) const {
        std::map<std::string, i64>::const_iterator it = file_ints.find(key);
        if (it != file_ints.end()) {
            out = it->second;
            return lookup::ok;
        }
        return present(key) ? lookup::wrong_type : lookup::missing;
    }
    lookup file_int_pair(const std::string& key, i64& first, i64& second) const {
        std::map<std::string, std::pair<i64, i64> >::const_iterator it = file_pairs.find(key);
        if (it != file_pairs.end()) {
            first = it->second.first;
            second = it->second.second;
            return lookup::ok;
        }
        return present(key) ? lookup::wrong_type : lookup::missing;
    }
    lookup file_bool(const std::string& key, bool& out) const {
        std::map<std::string, bool>::const_iterator it = file_flags.find(key);
        if (it != file_flags.end()) {
            out = it->second;
            return lookup::ok;
        }
        return present(key) ? lookup::wrong_type : lookup::missing;
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
        const config_diagnostic& diagnostic = config.diagnostics[i];
        const std::string joined = diagnostic.field + " " + diagnostic.source + " " +
                                   diagnostic.reason + " " + diagnostic.action + " " +
                                   diagnostic.message;
        if (joined.find(needle) != std::string::npos) {
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

TEST_CASE("parsed strings containing an embedded NUL do not reach C or filesystem APIs") {
    config_defaults defaults = make_defaults();

    fake_source display;
    display.file_strings["display_name"] = std::string("Ali\0ce", 6);
    resolved_config config = resolve_config(display, defaults);
    CHECK(config.display_name == "Player");
    CHECK(has_diagnostic(config, "display_name"));

    fake_source path;
    path.file_strings["trace_dir"] = std::string("visible\0hidden", 14);
    config = resolve_config(path, defaults);
    CHECK(config.trace_dir == "/data/traces");
    CHECK(has_diagnostic(config, "trace_dir"));
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

TEST_CASE("an invalid environment label falls through to the file label") {
    config_defaults defaults = make_defaults();
    fake_source source;
    source.envs["EOSR_INSTANCE_LABEL"] = "../unsafe";
    source.file_strings["instance_label"] = "from-file";

    const resolved_config config = resolve_config(source, defaults);
    CHECK(config.instance_label == "from-file");
    CHECK(has_diagnostic(config, "instance_label"));
}

TEST_CASE("a known file key with the wrong type produces a diagnostic") {
    config_defaults defaults = make_defaults();
    fake_source source;
    // This models {"trace_max_bytes":"not-an-integer"}. The typed source can currently report only
    // "not an integer value was returned", which is indistinguishable from a missing key.
    source.file_strings["trace_max_bytes"] = "not-an-integer";

    const resolved_config config = resolve_config(source, defaults);
    CHECK(config.trace_max_bytes == 67108864u);
    CHECK(has_diagnostic(config, "trace_max_bytes"));
}

TEST_CASE("a relative trace dir resolves against data dir, an absolute one is used as-is") {
    config_defaults defaults = make_defaults();

    fake_source relative;
    relative.envs["EOSR_TRACE_DIR"] = "my-traces";
    CHECK(resolve_config(relative, defaults).trace_dir == "/data/my-traces");

    fake_source absolute;
    absolute.envs["EOSR_TRACE_DIR"] = "/var/log/eosr";
    CHECK(resolve_config(absolute, defaults).trace_dir == "/var/log/eosr");

    // Absoluteness follows the host platform's path rules, since that is where the paths are used.
#if defined(_WIN32)
    fake_source drive_absolute;
    drive_absolute.envs["EOSR_TRACE_DIR"] = "C:\\traces";
    CHECK(resolve_config(drive_absolute, defaults).trace_dir == "C:\\traces");

    fake_source drive_relative;
    drive_relative.envs["EOSR_TRACE_DIR"] = "C:traces"; // drive-relative, not absolute
    CHECK(resolve_config(drive_relative, defaults).trace_dir == "/data/C:traces");
#else
    fake_source backslash;
    backslash.envs["EOSR_TRACE_DIR"] = "C:\\traces"; // ordinary bytes on POSIX, so relative
    CHECK(resolve_config(backslash, defaults).trace_dir == "/data/C:\\traces");
#endif
}

TEST_CASE("the run directory comes from EOSR_RUN_DIR or is left for the auto path") {
    config_defaults defaults = make_defaults();

    fake_source none;
    CHECK(resolve_config(none, defaults).run_dir.empty());

    fake_source runner;
    runner.envs["EOSR_RUN_DIR"] = "/runs/alice-run-1";
    CHECK(resolve_config(runner, defaults).run_dir == "/runs/alice-run-1");
}

// --- Emulator options the game never supplies (parity with the wider Epic-emulator ecosystem). ---

TEST_CASE("the defaults are a working LAN emulator with logging and tracing off") {
    fake_source none;
    const resolved_config config = resolve_config(none, make_defaults());
    CHECK(config.display_name == "Player");
    CHECK(config.locale == "en");
    CHECK(config.logging == log_level::off);
    CHECK(config.enable_lan);
    CHECK_FALSE(config.enable_overlay);
    CHECK_FALSE(config.unlock_dlcs);
}

TEST_CASE("username is accepted as an alias for display_name") {
    fake_source source;
    source.file_strings["username"] = "Marlowe";
    CHECK(resolve_config(source, make_defaults()).display_name == "Marlowe");

    // display_name wins when a file carries both, and the environment still beats the file.
    fake_source both;
    both.file_strings["username"] = "FromUsername";
    both.file_strings["display_name"] = "FromDisplayName";
    CHECK(resolve_config(both, make_defaults()).display_name == "FromDisplayName");
    both.envs["EOSR_DISPLAY_NAME"] = "FromEnv";
    CHECK(resolve_config(both, make_defaults()).display_name == "FromEnv");
}

TEST_CASE("language is accepted as an alias for locale, and a bad tag falls through") {
    fake_source source;
    source.file_strings["language"] = "pt-BR";
    CHECK(resolve_config(source, make_defaults()).locale == "pt-BR");

    fake_source env;
    env.envs["EOSR_LOCALE"] = "fr";
    CHECK(resolve_config(env, make_defaults()).locale == "fr");

    fake_source bad;
    bad.envs["EOSR_LOCALE"] = "../etc/passwd"; // not a language tag
    const resolved_config config = resolve_config(bad, make_defaults());
    CHECK(config.locale == "en"); // falls back to the default
    REQUIRE(config.diagnostics.size() == 1);
    CHECK(config.diagnostics[0].field == "locale");
    CHECK(config.diagnostics[0].action == "ignored");
}

TEST_CASE("log_level accepts the ecosystem spellings") {
    fake_source off;
    CHECK(resolve_config(off, make_defaults()).logging == log_level::off);

    fake_source err;
    err.file_strings["log_level"] = "ERR"; // the spelling the other emulators use
    CHECK(resolve_config(err, make_defaults()).logging == log_level::error);

    fake_source warning;
    warning.envs["EOSR_LOG_LEVEL"] = "warning";
    CHECK(resolve_config(warning, make_defaults()).logging == log_level::warn);

    fake_source trace;
    trace.file_strings["log_level"] = "trace";
    CHECK(resolve_config(trace, make_defaults()).logging == log_level::trace);

    fake_source bad;
    bad.file_strings["log_level"] = "chatty";
    const resolved_config config = resolve_config(bad, make_defaults());
    CHECK(config.logging == log_level::off);
    REQUIRE(config.diagnostics.size() == 1);
    CHECK(config.diagnostics[0].reason == "unrecognized value");
}

TEST_CASE("boolean options resolve from the file and the environment") {
    fake_source file;
    file.file_flags["enable_lan"] = false;
    CHECK_FALSE(resolve_config(file, make_defaults()).enable_lan);

    // The environment beats the file, and accepts what people actually type.
    fake_source env;
    env.file_flags["enable_lan"] = false;
    env.envs["EOSR_ENABLE_LAN"] = "yes";
    CHECK(resolve_config(env, make_defaults()).enable_lan);

    fake_source zero;
    zero.envs["EOSR_ENABLE_LAN"] = "0";
    CHECK_FALSE(resolve_config(zero, make_defaults()).enable_lan);

    // A non-boolean environment value is discarded and yields to the file, like every other field.
    fake_source bad;
    bad.envs["EOSR_ENABLE_LAN"] = "maybe";
    bad.file_flags["enable_lan"] = false;
    const resolved_config config = resolve_config(bad, make_defaults());
    CHECK_FALSE(config.enable_lan);
    REQUIRE(config.diagnostics.size() == 1);
    CHECK(config.diagnostics[0].reason == "not a boolean");
}

TEST_CASE("a wrong-typed boolean is a diagnostic, not a silent default") {
    fake_source source;
    source.file_strings["enable_lan"] = "true"; // a string where a bool belongs
    const resolved_config config = resolve_config(source, make_defaults());
    CHECK(config.enable_lan); // the default stands
    REQUIRE(config.diagnostics.size() == 1);
    CHECK(config.diagnostics[0].field == "enable_lan");
    CHECK(config.diagnostics[0].reason == "wrong type");
}

TEST_CASE("options we cannot honour are reported rather than silently ignored") {
    // Identity is derived from the profile key and recomputed by every peer from the key the
    // handshake proves, so an id we merely claimed would be rejected by the peers it must reach.
    fake_source ids;
    ids.file_strings["epicid"] = "00112233445566778899aabbccddeeff";
    ids.file_strings["productuserid"] = "ffeeddccbbaa99887766554433221100";
    const resolved_config id_config = resolve_config(ids, make_defaults());
    REQUIRE(id_config.diagnostics.size() == 2);
    CHECK(id_config.diagnostics[0].field == "epicid");
    CHECK(id_config.diagnostics[1].field == "productuserid");

    fake_source overlay;
    overlay.file_flags["enable_overlay"] = true;
    overlay.file_flags["unlock_dlcs"] = true;
    const resolved_config config = resolve_config(overlay, make_defaults());
    CHECK(config.enable_overlay); // recorded, so runtime.json reports what was asked for
    CHECK(config.unlock_dlcs);
    REQUIRE(config.diagnostics.size() == 2);
    CHECK(config.diagnostics[0].field == "enable_overlay");
    CHECK(config.diagnostics[1].field == "unlock_dlcs");
}
