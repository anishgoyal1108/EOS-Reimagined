#include "doctest.h"

#include <string>
#include <vector>

#include "common/log.h"
#include "core/config.h"
#include "core/peer_fp.h"
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
    CHECK(runtime.find("\"version\":null") == std::string::npos);
    CHECK(runtime.find("\"wine\":") != std::string::npos);

    t.stop();
    CHECK_FALSE(t.active());
    CHECK(slurp(dir + "/trace.jsonl").find("shutdown") != std::string::npos);
}

TEST_CASE("the platform reports a runtime OS version") {
    std::string os_version;
    std::string wine_version;
    CHECK(platform::system_versions(os_version, wine_version));
    CHECK_FALSE(os_version.empty());
#if !defined(_WIN32)
    CHECK(wine_version.empty());
#endif
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

// --- Correlation: an async call, its return, and the callback that completes it. ---

TEST_CASE("a call, its return, and its callback share one correlation id") {
    tracer_fixture fx("corr");
    const std::string run = fx.make_run("run-corr");
    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    REQUIRE(t.enabled());

    std::vector<trace_field> args;
    args.push_back(make_field(field_id::cred_type, tv_enum("EOS_ECT_DEVICEID_ACCESS_TOKEN")));
    const std::string corr = t.begin_async_call("EOS_Connect_Login", 2, args);
    CHECK(corr == "c#0");
    CHECK(t.pending_corr() == corr); // live for the duration of the call
    CHECK(t.pending_fn() == "EOS_Connect_Login");
    t.end_async_call("EOS_Connect_Login", corr);
    CHECK(t.pending_corr().empty()); // and cleared afterwards

    trace_result_code ok;
    ok.code = 0;
    ok.name = "EOS_Success";
    std::vector<trace_field> payload;
    payload.push_back(make_field(field_id::puid, tv_label(t.label(label_kind::puid, "abc"))));
    t.record_callback("EOS_Connect_Login", corr, ok, payload);
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"kind\":\"call\",\"fn\":\"EOS_Connect_Login\",\"api\":2,\"corr\":\"c#0\"") !=
          std::string::npos);
    CHECK(trace.find("\"cred_type\":\"EOS_ECT_DEVICEID_ACCESS_TOKEN\"") != std::string::npos);
    CHECK(trace.find("\"kind\":\"return\",\"fn\":\"EOS_Connect_Login\",\"corr\":\"c#0\",\"void\":true") !=
          std::string::npos);
    CHECK(trace.find("\"kind\":\"callback\",\"fn\":\"EOS_Connect_Login\",\"corr\":\"c#0\"") !=
          std::string::npos);
    CHECK(trace.find("\"puid\":\"puid#0\"") != std::string::npos);

    // A second async call gets its own correlation id.
    CHECK(t.begin_async_call("EOS_Connect_Login", 2, args) == "c#1");
    t.stop();
}

TEST_CASE("correlation ids and labels reset with the run") {
    tracer_fixture fx("corr-reset");
    const std::string run_a = fx.make_run("runA");
    const std::string run_b = fx.make_run("runB");
    tracer t;

    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run_a));
    CHECK(t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>()) == "c#0");
    CHECK(t.label(label_kind::puid, "abc") == "puid#0");
    t.stop();

    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run_b));
    CHECK(t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>()) == "c#0");
    CHECK(t.label(label_kind::puid, "xyz") == "puid#0"); // a wholly new run
    t.stop();
}

TEST_CASE("tracing off costs nothing and mints no correlation") {
    tracer_fixture fx("corr-off");
    tracer t;
    t.start(make_config(trace_level::off, fx.sub("traces"), std::string()));
    CHECK_FALSE(t.enabled());
    CHECK(t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>()).empty());
    CHECK(t.pending_corr().empty());
    t.end_async_call("EOS_Connect_Login", std::string()); // a safe no-op
    t.stop();
}

TEST_CASE("ending a nested async call restores the outer ambient context") {
    tracer_fixture fx("corr-nested");
    const std::string run = fx.make_run("run-nested");
    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    REQUIRE(t.enabled());

    const std::string outer =
        t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>());
    CHECK(t.pending_fn() == "EOS_Connect_Login");
    CHECK(t.pending_corr() == outer);

    // A synchronous EOS log callback can re-enter the SDK before the outer trampoline returns.
    // Finishing that nested call must reveal the outer frame again, so a result queued afterwards
    // still correlates to the operation that actually created it.
    const std::string inner =
        t.begin_async_call("EOS_Auth_Login", 3, std::vector<trace_field>());
    CHECK(t.pending_fn() == "EOS_Auth_Login");
    CHECK(t.pending_corr() == inner);
    t.end_async_call("EOS_Auth_Login", inner);

    CHECK(t.pending_fn() == "EOS_Connect_Login");
    CHECK(t.pending_corr() == outer);
    t.end_async_call("EOS_Connect_Login", outer);
    CHECK(t.pending_corr().empty());
    t.stop();
}

TEST_CASE("stop disables the tracer hot path before a later off run") {
    tracer_fixture fx("corr-stop-gate");
    const std::string run = fx.make_run("run-enabled");
    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    REQUIRE(t.enabled());

    t.stop();
    CHECK_FALSE(t.enabled());
    CHECK(t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>()).empty());

    // The component's documented isolated stop/start cycle must not carry an enabled gate from the
    // previous run when the new configuration explicitly turns tracing off.
    t.start(make_config(trace_level::off, fx.sub("traces"), std::string()));
    CHECK_FALSE(t.enabled());
    CHECK(t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>()).empty());
    t.stop();
}

TEST_CASE("the level decides which half of a pair is kept") {
    tracer_fixture fx("corr-levels");
    const std::string run = fx.make_run("run-errors");
    tracer t;
    t.start(make_config(trace_level::errors, fx.sub("traces"), run)); // errors only

    const std::string corr = t.begin_async_call("EOS_Connect_Login", 2, std::vector<trace_field>());
    t.end_async_call("EOS_Connect_Login", corr);

    trace_result_code ok;
    ok.code = 0;
    ok.name = "EOS_Success";
    t.record_callback("EOS_Connect_Login", corr, ok, std::vector<trace_field>());

    trace_result_code bad;
    bad.code = 2;
    bad.name = "EOS_InvalidParameters";
    t.record_callback("EOS_Connect_Login", corr, bad, std::vector<trace_field>());
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"kind\":\"call\"") == std::string::npos);     // a lifecycle record: dropped
    CHECK(trace.find("\"EOS_Success\"") == std::string::npos);       // a successful completion: dropped
    CHECK(trace.find("\"EOS_InvalidParameters\"") != std::string::npos); // the failure is kept
    t.stop();
}

TEST_CASE("no raw id or credential can reach the trace through a call or callback") {
    tracer_fixture fx("corr-privacy");
    const std::string run = fx.make_run("run-privacy");
    const std::string raw_puid = "00112233445566778899aabbccddeeff";
    const std::string token = "a-real-credential-token";

    tracer t;
    t.start(make_config(trace_level::full, fx.sub("traces"), run));

    std::vector<trace_field> args;
    args.push_back(make_field(field_id::cred_type, tv_enum(token))); // dashes: not a valid enum
    const std::string corr = t.begin_async_call("EOS_Connect_Login", 2, args);
    t.end_async_call("EOS_Connect_Login", corr);

    trace_result_code ok;
    ok.code = 0;
    ok.name = "EOS_Success";
    std::vector<trace_field> payload;
    payload.push_back(make_field(field_id::puid, tv_label(raw_puid))); // no '#': not a valid label
    t.record_callback("EOS_Connect_Login", corr, ok, payload);
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find(raw_puid) == std::string::npos); // the id never reaches the file
    CHECK(trace.find(token) == std::string::npos);    // nor the credential
    CHECK(trace.find("\"kind\":\"call\"") != std::string::npos); // the records themselves still land
    t.stop();
}

// --- The call scope: a synchronous call is not an asynchronous one. ---

TEST_CASE("a synchronous call mints no correlation and lands only at full") {
    tracer_fixture fx("scope-sync");
    const std::string lifecycle_run = fx.make_run("run-sync-lifecycle");
    const std::string full_run = fx.make_run("run-sync-full");

    {
        tracer t;
        t.start(make_config(trace_level::lifecycle, fx.sub("traces"), lifecycle_run));
        trace_scope scope(t, "EOS_Connect_GetLoginStatus", 1, std::vector<trace_field>(),
                          call_mode::sync);
        CHECK(scope.corr().empty());        // nothing to correlate with: there is no completion
        CHECK(t.pending_corr().empty());    // and no ambient context for a nested result to inherit
        t.flush();
        t.stop();
        // A plain call/return is a `full` record, so a lifecycle run must not carry it.
        const std::string trace = slurp(lifecycle_run + "/trace.jsonl");
        CHECK(trace.find("EOS_Connect_GetLoginStatus") == std::string::npos);
    }
    {
        tracer t;
        t.start(make_config(trace_level::full, fx.sub("traces"), full_run));
        {
            trace_scope scope(t, "EOS_Connect_GetLoginStatus", 1, std::vector<trace_field>(),
                              call_mode::sync);
            scope.returns(return_count(3));
        }
        t.flush();
        t.stop();
        const std::string trace = slurp(full_run + "/trace.jsonl");
        CHECK(trace.find("\"kind\":\"call\",\"fn\":\"EOS_Connect_GetLoginStatus\"") !=
              std::string::npos);
        CHECK(trace.find("\"kind\":\"return\",\"fn\":\"EOS_Connect_GetLoginStatus\"") !=
              std::string::npos);
        CHECK(trace.find("\"corr\"") == std::string::npos); // no correlation anywhere in the run
        CHECK(trace.find("\"value\":{\"type\":\"count\",\"v\":3}") != std::string::npos);
    }
}

TEST_CASE("an asynchronous scope still correlates its call and return") {
    tracer_fixture fx("scope-async");
    const std::string run = fx.make_run("run-async");
    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));

    std::string corr;
    {
        trace_scope scope(t, "EOS_Connect_Login", 2, std::vector<trace_field>(), call_mode::async);
        corr = scope.corr();
        CHECK_FALSE(corr.empty());
        CHECK(t.pending_corr() == corr); // a result queued here inherits it
    }
    CHECK(t.pending_corr().empty()); // and the scope closed cleanly

    trace_result_code ok;
    ok.code = 0;
    ok.name = "EOS_Success";
    t.record_callback("EOS_Connect_Login", corr, ok, std::vector<trace_field>());
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"kind\":\"call\",\"fn\":\"EOS_Connect_Login\",\"api\":2,\"corr\":\"" + corr +
                     "\"") != std::string::npos);
    CHECK(trace.find("\"kind\":\"return\",\"fn\":\"EOS_Connect_Login\",\"corr\":\"" + corr + "\"") !=
          std::string::npos);
    CHECK(trace.find("\"kind\":\"callback\",\"fn\":\"EOS_Connect_Login\",\"corr\":\"" + corr + "\"") !=
          std::string::npos);
    t.stop();
}

TEST_CASE("an early return records what it actually returned, not void") {
    tracer_fixture fx("scope-early");
    const std::string run = fx.make_run("run-early");
    tracer t;
    t.start(make_config(trace_level::full, fx.sub("traces"), run));

    // A synchronous trampoline that rejects its handle and returns a real result, exactly as a
    // getter does: the scope must record the result it gave back, not the default void.
    {
        trace_scope scope(t, "EOS_Connect_GetProductUserIdMapping", 1, std::vector<trace_field>(),
                          call_mode::sync);
        scope.returns(return_result(2, "EOS_InvalidParameters"));
        // ...the trampoline returns here, and the scope closes on the way out.
    }
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"result\":{\"code\":2,\"name\":\"EOS_InvalidParameters\"}") !=
          std::string::npos);
    CHECK(trace.find("\"void\":true") == std::string::npos); // never the default
    t.stop();
}

TEST_CASE("a failing return is kept even at the errors level") {
    tracer_fixture fx("scope-errors");
    const std::string run = fx.make_run("run-scope-errors");
    tracer t;
    t.start(make_config(trace_level::errors, fx.sub("traces"), run)); // errors only

    {
        trace_scope ok(t, "EOS_Connect_GetLoginStatus", 1, std::vector<trace_field>(),
                       call_mode::sync);
        ok.returns(return_count(1)); // a success: a full-level record, dropped here
    }
    {
        trace_scope bad(t, "EOS_Connect_GetProductUserIdMapping", 1, std::vector<trace_field>(),
                        call_mode::sync);
        bad.returns(return_result(2, "EOS_InvalidParameters")); // a failure: kept at every level
    }
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("EOS_Connect_GetLoginStatus") == std::string::npos);
    CHECK(trace.find("\"EOS_InvalidParameters\"") != std::string::npos);
    t.stop();
}

// --- Network lifecycle: whether two instances ever found each other. ---

TEST_CASE("net records carry opaque peers, and a drop survives the errors level") {
    tracer_fixture fx("net");
    const std::string run = fx.make_run("run-net");
    const std::string raw_peer = "00112233445566778899aabbccddeeff";

    tracer t;
    t.start(make_config(trace_level::errors, fx.sub("traces"), run)); // errors only

    std::vector<trace_field> found;
    found.push_back(make_field(field_id::peer, tv_label(t.label(label_kind::puid, raw_peer))));
    found.push_back(make_field(field_id::port, tv_uint(55790)));
    t.record_net("discover", found); // a lifecycle record: dropped at errors

    std::vector<trace_field> lost;
    lost.push_back(make_field(field_id::peer, tv_label(t.label(label_kind::puid, raw_peer))));
    lost.push_back(make_field(field_id::peer_fp, tv_fingerprint(peer_fingerprint(raw_peer))));
    lost.push_back(make_field(field_id::reason, tv_enum("timeout")));
    t.record_net("drop", lost, true); // a failure: kept at every level above off
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"event\":\"discover\"") == std::string::npos);
    CHECK(trace.find("\"kind\":\"net\",\"event\":\"drop\"") != std::string::npos);
    CHECK(trace.find("\"peer\":\"puid#0\"") != std::string::npos);
    CHECK(trace.find("\"peer_fp\":\"ebf65ed621ba531b\"") != std::string::npos); // the golden vector
    CHECK(trace.find("\"reason\":\"timeout\"") != std::string::npos);
    CHECK(trace.find(raw_peer) == std::string::npos); // the raw id never reaches the file
    t.stop();
}

TEST_CASE("the mesh lifecycle reads as listen, discover, handshake, adopt, drop") {
    tracer_fixture fx("net-lifecycle");
    const std::string run = fx.make_run("run-net-life");
    const std::string peer = "0123456789abcdef0123456789abcdef";

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));

    std::vector<trace_field> bound;
    bound.push_back(make_field(field_id::port, tv_uint(55789)));
    bound.push_back(make_field(field_id::port_first, tv_uint(55789)));
    bound.push_back(make_field(field_id::port_last, tv_uint(55798)));
    t.record_net("listen", bound);

    std::vector<trace_field> one;
    one.push_back(make_field(field_id::peer, tv_label(t.label(label_kind::puid, peer))));
    t.record_net("discover", one);
    one.push_back(make_field(field_id::peer_fp, tv_fingerprint(peer_fingerprint(peer))));
    one.push_back(make_field(field_id::reason, tv_enum("complete")));
    t.record_net("handshake", one);
    one.pop_back();
    t.record_net("adopt", one);
    one.push_back(make_field(field_id::reason, tv_enum("local_shutdown")));
    t.record_net("drop", one);
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    const std::size_t at_listen = trace.find("\"event\":\"listen\"");
    const std::size_t at_discover = trace.find("\"event\":\"discover\"");
    const std::size_t at_handshake = trace.find("\"event\":\"handshake\"");
    const std::size_t at_adopt = trace.find("\"event\":\"adopt\"");
    const std::size_t at_drop = trace.find("\"event\":\"drop\"");
    CHECK(at_listen != std::string::npos);
    CHECK(at_listen < at_discover);
    CHECK(at_discover < at_handshake);
    CHECK(at_handshake < at_adopt);
    CHECK(at_adopt < at_drop);
    // Which discovery slot we took: two copies on one machine landing on the same one is the whole
    // explanation for why they never met.
    CHECK(trace.find("\"port\":55789,\"port_first\":55789,\"port_last\":55798") != std::string::npos);
    // A claimed id has a label; only a key-proved id has a fingerprint.
    CHECK(trace.find("\"event\":\"discover\",\"peer\":\"puid#0\"}") != std::string::npos);
    CHECK(trace.find("\"event\":\"adopt\",\"peer\":\"puid#0\",\"peer_fp\":") != std::string::npos);
    t.stop();
}

TEST_CASE("search records expose stage and counts without raw peer ids") {
    tracer_fixture fx("search-lifecycle");
    const std::string run = fx.make_run("run-search");
    const std::string peer = "0123456789abcdef0123456789abcdef";

    tracer t;
    t.start(make_config(trace_level::lifecycle, fx.sub("traces"), run));
    t.record_search("sessions_response", peer, 3);
    t.flush();

    const std::string trace = slurp(run + "/trace.jsonl");
    CHECK(trace.find("\"kind\":\"net\",\"event\":\"search\"") != std::string::npos);
    CHECK(trace.find("\"peer\":\"puid#0\"") != std::string::npos);
    CHECK(trace.find("\"count\":3") != std::string::npos);
    CHECK(trace.find("\"reason\":\"sessions_response\"") != std::string::npos);
    CHECK(trace.find(peer) == std::string::npos);
    t.stop();
}
