#include "doctest.h"

#include <string>
#include <thread>
#include <vector>

#include "common/log.h"
#include "core/config.h"
#include "core/trace_event.h"
#include "core/trace_sink.h"
#include "platform/paths.h"

using namespace eosr;

namespace {

std::vector<std::string> sink_logs;

void EOS_CALL capture_sink_log(const EOS_LogMessage* message) {
    sink_logs.push_back(message->Message);
}

void begin_log_capture() {
    sink_logs.clear();
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Verbose);
    logger::instance().set_callback(capture_sink_log);
}

void end_log_capture() {
    logger::instance().set_callback(0);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Warning);
}

// A deterministic envelope source, so the sink's own records are reproducible.
struct fake_meta : trace_meta_source {
    u64 seq;
    fake_meta() : seq(0) {}
    trace_envelope next_envelope() {
        trace_envelope env;
        env.schema_version = 1;
        env.seq = seq++;
        env.t_mono_ns = 1000;
        env.pid = 99;
        env.inst = "test";
        env.tid = "t#0";
        return env;
    }
};

std::string slurp(const std::string& path) {
    std::string out;
    platform::read_file_capped(path, 16 * 1024 * 1024, out);
    return out;
}

bool file_exists(const std::string& path) {
    std::string out;
    return platform::read_file_capped(path, 16 * 1024 * 1024, out) == platform::file_read::ok;
}

// A ~59-byte record line, comfortably above the sink's own rotate record so a tiny cap still holds
// one, yet large enough that a couple of writes overflow a small cap.
std::string big_line(int i) {
    return "{\"k\":\"" + std::string(48, 'x') + std::to_string(i % 10) + "\"}";
}

std::size_t count(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        n++;
        pos += needle.size();
    }
    return n;
}

void write_thread_records(trace_sink* sink, int worker) {
    for (int i = 0; i < 25; i++) {
        const std::string line = "{\"worker\":" + std::to_string(worker) +
                                 ",\"record\":" + std::to_string(i) + "}";
        sink->write(trace_level::full, line);
    }
}

struct sink_fixture {
    std::string dir;
    fake_meta meta;

    explicit sink_fixture(const std::string& name) {
        dir = std::string(EOSR_TEST_PROFILE_DIR) + "/sink-" + name;
        platform::make_directories(dir);
        platform::remove_file(dir + "/trace.jsonl");
        for (int i = 1; i <= 12; i++) {
            platform::remove_file(dir + "/trace." + std::to_string(i) + ".jsonl");
        }
    }
    std::string file(const std::string& name) { return dir + "/" + name; }
};

} // namespace

TEST_CASE("an off sink creates nothing and is inactive") {
    sink_fixture fx("off");
    trace_sink sink;
    CHECK_FALSE(sink.open(fx.dir, trace_level::off, 65536, 8, &fx.meta));
    CHECK_FALSE(sink.active());
    sink.write(trace_level::errors, "{\"x\":1}");
    sink.flush();
    CHECK_FALSE(file_exists(fx.file("trace.jsonl")));
}

TEST_CASE("opening writes a run_start record and a line persists on flush") {
    sink_fixture fx("open");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));
    CHECK(sink.active());
    sink.write(trace_level::lifecycle, "{\"line\":1}");
    sink.flush();
    const std::string content = slurp(fx.file("trace.jsonl"));
    CHECK(content.find("run_start") != std::string::npos);
    CHECK(content.find("{\"line\":1}") != std::string::npos);
    // Whole lines: two records plus their newlines.
    CHECK(count(content, "\n") == 2);
    sink.close();
}

TEST_CASE("an errors-level record is flushed at once") {
    sink_fixture fx("errors-flush");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));
    sink.write(trace_level::errors, "{\"err\":1}");
    // No explicit flush: an errors record is already on disk.
    CHECK(slurp(fx.file("trace.jsonl")).find("{\"err\":1}") != std::string::npos);
    sink.close();
}

TEST_CASE("records above the configured level are dropped") {
    sink_fixture fx("level");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::lifecycle, 65536, 8, &fx.meta));
    sink.write(trace_level::full, "{\"full\":1}");        // above lifecycle -> dropped
    sink.write(trace_level::lifecycle, "{\"life\":1}");   // at lifecycle -> kept
    sink.flush();
    const std::string content = slurp(fx.file("trace.jsonl"));
    CHECK(content.find("{\"full\":1}") == std::string::npos);
    CHECK(content.find("{\"life\":1}") != std::string::npos);
    sink.close();
}

TEST_CASE("concurrent writers preserve every complete line") {
    sink_fixture fx("concurrent");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));

    std::vector<std::thread> workers;
    for (int i = 0; i < 4; i++) {
        workers.push_back(std::thread(write_thread_records, &sink, i));
    }
    for (std::size_t i = 0; i < workers.size(); i++) {
        workers[i].join();
    }
    sink.flush();

    const std::string content = slurp(fx.file("trace.jsonl"));
    CHECK(count(content, "\"worker\"") == 100);
    CHECK(count(content, "\n") == 101); // run_start plus 100 complete records
    sink.close();
}

TEST_CASE("an empty line is skipped") {
    sink_fixture fx("empty");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));
    sink.write(trace_level::full, ""); // a refused record
    sink.flush();
    // Only run_start and its single newline: no blank line.
    CHECK(count(slurp(fx.file("trace.jsonl")), "\n") == 1);
    sink.close();
}

TEST_CASE("the file rotates when a line would overflow the cap") {
    sink_fixture fx("rotate");
    trace_sink sink;
    // A small cap: run_start plus one line fits, the second forces rotation while the file is live.
    REQUIRE(sink.open(fx.dir, trace_level::full, 200, 8, &fx.meta));
    sink.write(trace_level::full, big_line(1));
    sink.write(trace_level::full, big_line(2));
    sink.flush();
    CHECK(file_exists(fx.file("trace.1.jsonl")));                    // the previous file was kept
    const std::string current = slurp(fx.file("trace.jsonl"));
    CHECK(current.find("\"event\":\"rotate\"") != std::string::npos); // fresh file starts with rotate
    sink.close();
}

TEST_CASE("rotation and shutdown never leave any file above the byte cap") {
    sink_fixture fx("hard-cap");
    const u64 cap = 200;
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, cap, 3, &fx.meta));
    for (int i = 0; i < 8; i++) {
        sink.write(trace_level::full, big_line(i));
    }
    sink.close();

    const char* const names[] = {
        "trace.jsonl", "trace.1.jsonl", "trace.2.jsonl", "trace.3.jsonl"
    };
    for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (file_exists(fx.file(names[i]))) {
            CHECK(slurp(fx.file(names[i])).size() <= cap);
        }
    }
}

TEST_CASE("a sink exclusively owns a new trace file") {
    sink_fixture fx("exclusive-file");
    const std::string sentinel = "pre-existing-owner\n";
    REQUIRE(platform::write_private_file(fx.file("trace.jsonl"), sentinel));

    trace_sink sink;
    CHECK_FALSE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));
    CHECK_FALSE(sink.active());
    sink.flush();
    CHECK(slurp(fx.file("trace.jsonl")) == sentinel);
}

TEST_CASE("an active sink always has a source for its lifecycle records") {
    sink_fixture fx("missing-meta");
    trace_sink sink;
    CHECK_FALSE(sink.open(fx.dir, trace_level::full, 65536, 8, 0));
    CHECK_FALSE(sink.active());
    sink.write(trace_level::lifecycle, "{\"line\":1}");
    sink.flush();
    CHECK_FALSE(file_exists(fx.file("trace.jsonl")));
}

TEST_CASE("rotated files are bounded and the drop is recorded") {
    sink_fixture fx("bounded");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 200, 1, &fx.meta)); // keep one rotated file
    for (int i = 0; i < 12; i++) {
        sink.write(trace_level::full, big_line(i));
    }
    sink.flush();
    CHECK(file_exists(fx.file("trace.1.jsonl")));
    CHECK_FALSE(file_exists(fx.file("trace.2.jsonl"))); // never more than one rotated file
    // A later rotate record reports having dropped a file.
    CHECK(slurp(fx.file("trace.jsonl")).find("\"dropped_files\":1") != std::string::npos);
    sink.close();
}

TEST_CASE("zero rotated files keeps only the live file") {
    sink_fixture fx("zero-rotated");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 200, 0, &fx.meta));
    for (int i = 0; i < 10; i++) {
        sink.write(trace_level::full, big_line(i));
    }
    sink.flush();
    CHECK_FALSE(file_exists(fx.file("trace.1.jsonl"))); // no history kept
    CHECK(slurp(fx.file("trace.jsonl")).find("\"dropped_files\":1") != std::string::npos);
    sink.close();
}

TEST_CASE("close writes a shutdown record") {
    sink_fixture fx("shutdown");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));
    sink.close();
    CHECK(slurp(fx.file("trace.jsonl")).find("shutdown") != std::string::npos);
    sink.close(); // idempotent
}

TEST_CASE("reopening starts a fresh run and resets state") {
    sink_fixture a("reset-a");
    sink_fixture b("reset-b");
    trace_sink sink;

    // A first run that rotates, then shuts down.
    REQUIRE(sink.open(a.dir, trace_level::full, 200, 8, &a.meta));
    for (int i = 0; i < 6; i++) {
        sink.write(trace_level::full, big_line(i));
    }
    sink.close();
    CHECK_FALSE(sink.active());

    // Reopening -- a new run directory, as a fresh run_id would give -- clears closed/disabled and the
    // rotation bookkeeping: the new file is a clean run_start with no leftover from the first run.
    REQUIRE(sink.open(b.dir, trace_level::full, 65536, 8, &b.meta));
    CHECK(sink.active());
    sink.write(trace_level::lifecycle, "{\"fresh\":1}");
    sink.flush();
    const std::string content = slurp(b.file("trace.jsonl"));
    CHECK(content.find("run_start") != std::string::npos);
    CHECK(content.find("rotate") == std::string::npos); // no carried-over rotation state
    CHECK(count(content, "\n") == 2);                    // exactly run_start + the one fresh line
    CHECK_FALSE(file_exists(b.file("trace.1.jsonl")));
    sink.close();
}

TEST_CASE("a run directory that cannot be created leaves the sink inactive") {
    sink_fixture fx("uncreatable");
    // A regular file where a directory component is expected: make_directories must fail.
    REQUIRE(platform::write_private_file(fx.file("blocker"), "x"));
    trace_sink sink;
    CHECK_FALSE(sink.open(fx.file("blocker/run"), trace_level::full, 65536, 8, &fx.meta));
    CHECK_FALSE(sink.active());
    sink.write(trace_level::errors, "{\"x\":1}"); // a no-op, no crash
    sink.flush();
}

TEST_CASE("an open failure produces one best-effort diagnostic") {
    sink_fixture fx("open-diagnostic");
    REQUIRE(platform::write_private_file(fx.file("blocker"), "x"));
    begin_log_capture();
    trace_sink sink;
    const bool opened = sink.open(fx.file("blocker/run"), trace_level::full, 65536, 8, &fx.meta);
    end_log_capture();

    CHECK_FALSE(opened);
    REQUIRE(sink_logs.size() == 1);
    CHECK(sink_logs[0].find("trace") != std::string::npos);
}

TEST_CASE("a write failure disables the sink once, without crashing") {
    sink_fixture fx("write-failure");
    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta));
    sink.flush();
    // The run directory disappears mid-run; the next flush cannot write and must disable the sink.
    REQUIRE(platform::rename_file(fx.dir, fx.dir + "-gone"));
    sink.write(trace_level::full, "{\"after\":1}");
    sink.flush();
    CHECK_FALSE(sink.active());
    sink.write(trace_level::full, "{\"more\":1}"); // still a safe no-op
    sink.close();
    platform::rename_file(fx.dir + "-gone", fx.dir); // restore for a re-run
}

TEST_CASE("a rename failure during rotation disables the sink") {
    sink_fixture fx("rename-failure");
    // Occupy trace.1.jsonl with a non-empty directory so the rotation rename onto it must fail on both
    // platforms. This exercises the multi-step rename path, which mutates the filesystem before it
    // updates the sink's bookkeeping.
    REQUIRE(platform::make_directories(fx.file("trace.1.jsonl")));
    REQUIRE(platform::write_private_file(fx.file("trace.1.jsonl/keep"), "x"));

    trace_sink sink;
    REQUIRE(sink.open(fx.dir, trace_level::full, 200, 8, &fx.meta));
    sink.write(trace_level::full, big_line(1));
    sink.write(trace_level::full, big_line(2)); // forces the rotation whose rename cannot succeed
    sink.flush();
    CHECK_FALSE(sink.active()); // disabled cleanly, no crash
    sink.write(trace_level::full, big_line(3)); // a safe no-op afterwards
    sink.close();
}

TEST_CASE("runner mode uses an existing directory and refuses a missing one") {
    sink_fixture fx("runner");
    // must_exist against the directory the runner already created: opens normally.
    {
        trace_sink sink;
        REQUIRE(sink.open(fx.dir, trace_level::full, 65536, 8, &fx.meta,
                          trace_sink::dir_mode::must_exist));
        CHECK(sink.active());
        sink.close();
    }
    // must_exist against a missing directory: refused, and the library must not fabricate it.
    const std::string missing = fx.file("no-such-run");
    trace_sink sink;
    CHECK_FALSE(sink.open(missing, trace_level::full, 65536, 8, &fx.meta,
                          trace_sink::dir_mode::must_exist));
    CHECK_FALSE(sink.active());
    CHECK_FALSE(platform::directory_exists(missing));
    CHECK_FALSE(file_exists(missing + "/trace.jsonl"));
}
