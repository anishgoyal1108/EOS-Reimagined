#include "doctest.h"

#include <string>
#include <vector>

#include "common/log.h"
#include "core/config.h"
#include "core/tracer.h"
#include "platform/paths.h"

using namespace eosr;

namespace {

std::vector<std::string> tracer_logs;
tracer* reentry_target = 0;

void EOS_CALL capture_log(const EOS_LogMessage* message) {
    tracer_logs.push_back(message->Message);
}

// A log callback that re-enters the tracer, to prove diagnostics are delivered with no tracer lock
// held: an EOS log callback is synchronous and may call back into the SDK.
void EOS_CALL reentrant_log(const EOS_LogMessage* message) {
    tracer_logs.push_back(message->Message);
    if (reentry_target != 0) {
        reentry_target->active();
        reentry_target->flush();
        reentry_target->next_envelope();
    }
}

void begin_capture(EOS_LogMessageFunc callback) {
    tracer_logs.clear();
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Verbose);
    logger::instance().set_callback(callback);
}

void end_capture() {
    logger::instance().set_callback(0);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Warning);
}

std::string slurp(const std::string& path) {
    std::string out;
    platform::read_file_capped(path, 16 * 1024 * 1024, out);
    return out;
}

bool file_exists(const std::string& path) {
    std::string out;
    return platform::read_file_capped(path, 16 * 1024 * 1024, out) == platform::file_read::ok;
}

resolved_config make_config(trace_level level, const std::string& trace_dir,
                            const std::string& run_dir) {
    resolved_config c;
    c.display_name = "Marlowe";
    c.data_dir = trace_dir;
    c.run_dir = run_dir;
    c.trace_dir = trace_dir;
    c.level = level;
    c.trace_max_bytes = 65536;
    c.trace_max_rotated_files = 8;
    c.discovery_ports.first = 55789;
    c.discovery_ports.last = 55798;
    c.instance_label = "alice";
    return c;
}

struct tracer_fixture {
    std::string base;

    explicit tracer_fixture(const std::string& name) {
        base = std::string(EOSR_TEST_PROFILE_DIR) + "/tracer-" + name;
        platform::make_directories(base);
    }
    std::string sub(const std::string& name) { return base + "/" + name; }

    // Pre-create a runner directory and clear any owned files left from a previous run.
    std::string make_run(const std::string& name) {
        const std::string dir = sub(name);
        platform::make_directories(dir);
        platform::remove_file(dir + "/trace.jsonl");
        platform::remove_file(dir + "/runtime.json");
        for (int i = 1; i <= 12; i++) {
            platform::remove_file(dir + "/trace." + std::to_string(i) + ".jsonl");
        }
        return dir;
    }
};

} // namespace

TEST_CASE("an off run creates nothing on disk") {
    tracer_fixture fx("off");
    const std::string trace_dir = fx.sub("traces"); // deliberately not created
    tracer t;
    t.start(make_config(trace_level::off, trace_dir, std::string()));
    CHECK_FALSE(t.active());
    CHECK(t.run_directory().empty());
    CHECK_FALSE(platform::directory_exists(trace_dir)); // the library created no run directory
    t.stop();
}

TEST_CASE("manual mode creates the run directory and its owned files") {
    tracer_fixture fx("manual");
    const std::string trace_dir = fx.sub("traces");
    platform::make_directories(trace_dir);

    tracer t;
    t.start(make_config(trace_level::lifecycle, trace_dir, std::string()));
    CHECK(t.active());
    const std::string dir = t.run_directory();
    CHECK(dir.find("/run-") != std::string::npos);
    CHECK(t.run_id().find("run-") == 0);
    t.flush();

    CHECK(file_exists(dir + "/trace.jsonl"));
    CHECK(file_exists(dir + "/runtime.json"));
    CHECK(slurp(dir + "/trace.jsonl").find("run_start") != std::string::npos);

    const std::string runtime = slurp(dir + "/runtime.json");
    CHECK(runtime.find("\"schema_version\":1") != std::string::npos);
    CHECK(runtime.find("\"run_id\":\"" + t.run_id() + "\"") != std::string::npos);
    CHECK(runtime.find("\"trace_level\":\"lifecycle\"") != std::string::npos);
    CHECK(runtime.find("\"instance_label\":\"alice\"") != std::string::npos);
    CHECK(runtime.find("\"discovery_ports\":[55789,55798]") != std::string::npos);

    t.stop();
    CHECK_FALSE(t.active());
    CHECK(slurp(dir + "/trace.jsonl").find("shutdown") != std::string::npos);
}

TEST_CASE("runner mode opens the given directory") {
    tracer_fixture fx("runner");
    const std::string run = fx.make_run("run-xyz");

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    CHECK(t.active());
    CHECK(t.run_directory() == run);
    CHECK(t.run_id() == "run-xyz");
    t.flush();
    CHECK(file_exists(run + "/trace.jsonl"));
    CHECK(file_exists(run + "/runtime.json"));
    t.stop();
}

TEST_CASE("a trailing separator does not erase the runner run id") {
    tracer_fixture fx("runner-trailing-separator");
    const std::string run = fx.make_run("run-trailing");

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run + "/"));
    REQUIRE(t.active());
    CHECK(t.run_id() == "run-trailing");
    t.stop();
}

TEST_CASE("a runtime file collision rejects the run instead of pairing stale metadata with it") {
    tracer_fixture fx("runtime-collision");
    const std::string run = fx.make_run("run-collision");
    const std::string sentinel = "{\"owner\":\"someone-else\"}";
    REQUIRE(platform::write_private_file(run + "/runtime.json", sentinel));

    begin_capture(capture_log);
    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    end_capture();

    CHECK_FALSE(t.active());
    CHECK(slurp(run + "/runtime.json") == sentinel);
    REQUIRE(tracer_logs.size() == 1);
    CHECK(tracer_logs[0].find("runtime.json") != std::string::npos);
    t.stop();
}

TEST_CASE("runner mode degrades to off when the directory is missing") {
    tracer_fixture fx("runner-missing");
    const std::string missing = fx.sub("no-such-run");

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), missing));
    CHECK_FALSE(t.active());
    CHECK_FALSE(platform::directory_exists(missing)); // the library never creates the runner's dir
    CHECK_FALSE(file_exists(missing + "/trace.jsonl"));
    t.stop();
}

TEST_CASE("a second start keeps the first run") {
    tracer_fixture fx("repeat");
    const std::string run_a = fx.make_run("runA");
    const std::string run_b = fx.make_run("runB");

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run_a));
    const std::string first = t.run_directory();
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run_b)); // must be ignored
    CHECK(t.run_directory() == first);
    CHECK(t.run_id() == "runA");
    t.flush();
    CHECK_FALSE(file_exists(run_b + "/trace.jsonl")); // the second run never opened
    t.stop();
}

TEST_CASE("stopping and restarting opens a wholly new run") {
    tracer_fixture fx("reset");
    const std::string run_a = fx.make_run("runA");
    const std::string run_b = fx.make_run("runB");

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run_a));
    CHECK(t.active());
    t.stop();
    CHECK_FALSE(t.active());
    const std::string trace_a = slurp(run_a + "/trace.jsonl");
    CHECK(trace_a.find("run_start") != std::string::npos);
    CHECK(trace_a.find("shutdown") != std::string::npos);

    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run_b));
    CHECK(t.active());
    CHECK(t.run_id() == "runB");
    t.flush();
    CHECK(file_exists(run_b + "/trace.jsonl"));
    t.stop();
}

TEST_CASE("config diagnostics are delivered without deadlocking a re-entrant log callback") {
    tracer_fixture fx("reentrant");
    resolved_config cfg = make_config(trace_level::off, fx.sub("traces"), std::string());
    config_diagnostic d;
    d.field = "trace_level";
    d.source = "environment";
    d.reason = "unknown value";
    d.action = "ignored";
    d.message = "bad";
    cfg.diagnostics.push_back(d);

    tracer t;
    reentry_target = &t;
    begin_capture(reentrant_log);
    t.start(cfg); // off: routes the diagnostic to the logger, whose callback re-enters the tracer
    end_capture();
    reentry_target = 0;

    REQUIRE(tracer_logs.size() >= 1);
    CHECK(tracer_logs[0].find("trace_level") != std::string::npos);
    t.stop();
}

TEST_CASE("an enabled trace retains config diagnostics without needing a log callback") {
    tracer_fixture fx("config-record");
    const std::string run = fx.make_run("run-config");
    resolved_config cfg = make_config(trace_level::lifecycle, fx.sub("traces"), run);
    config_diagnostic d;
    d.field = "trace_max_bytes";
    d.source = "environment";
    d.reason = "below minimum";
    d.action = "clamped";
    cfg.diagnostics.push_back(d);

    tracer t;
    t.start(cfg); // no logger callback is installed, so the trace is the durable record
    t.flush();
    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"event\":\"config\"") != std::string::npos);
    CHECK(trace.find("trace_max_bytes") != std::string::npos);
    CHECK(trace.find("clamped") != std::string::npos);
    t.stop();
}

TEST_CASE("the envelope carries a monotonic sequence, a pid, and a stable thread label") {
    tracer_fixture fx("envelope");
    tracer t;
    t.start(make_config(trace_level::off, fx.sub("traces"), std::string()));

    const trace_envelope a = t.next_envelope();
    const trace_envelope b = t.next_envelope();
    CHECK(a.seq == 0);
    CHECK(b.seq == 1);
    CHECK(a.tid == "t#0");
    CHECK(b.tid == "t#0"); // same thread keeps its label
    CHECK(a.pid != 0);
    CHECK(b.t_mono_ns >= a.t_mono_ns);
    CHECK(a.inst == "alice");
    t.stop();
}

TEST_CASE("a profile record carries the local fingerprint") {
    tracer_fixture fx("profile");
    const std::string run = fx.make_run("run-profile");

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    t.on_profile("00112233445566778899aabbccddeeff");
    t.flush();
    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"event\":\"profile\"") != std::string::npos);
    CHECK(trace.find("\"peer_fp\":\"ebf65ed621ba531b\"") != std::string::npos);
    t.stop();
}

TEST_CASE("on_profile is a safe no-op when inactive or given a bad id") {
    tracer_fixture fx("profile-noop");
    tracer inactive;
    inactive.on_profile("00112233445566778899aabbccddeeff"); // never started: no crash
    CHECK_FALSE(inactive.active());

    const std::string run = fx.make_run("run-badid");
    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    t.on_profile("not-a-valid-product-user-id");
    t.flush();
    CHECK(slurp(run + "/trace.jsonl").find("profile") == std::string::npos);
    t.stop();
}
