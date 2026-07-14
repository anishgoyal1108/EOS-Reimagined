#include "core/tracer.h"

#include <string>
#include <vector>

#include "common/json_writer.h"
#include "common/log.h"
#include "core/peer_fp.h"
#include "core/trace_event.h"
#include "platform/paths.h"
#include "platform/rng.h"

namespace eosr {

namespace {

const char* const emulator_build = "eosr 0.1.0-alpha";
const int max_run_id_attempts = 16; // bump the suffix this many times before giving up on a collision

#if defined(_WIN32)
const char* const os_name = "windows";
#else
const char* const os_name = "linux";
#endif

std::string two_digits(int value) {
    std::string out;
    out += static_cast<char>('0' + (value / 10) % 10);
    out += static_cast<char>('0' + value % 10);
    return out;
}

std::string four_digits(int value) {
    return two_digits(value / 100) + two_digits(value % 100);
}

// 20260713T004500Z -- filesystem-safe, sorts chronologically, no separators to escape.
std::string compact_stamp(const platform::utc_time& t) {
    return four_digits(t.year) + two_digits(t.month) + two_digits(t.day) + "T" + two_digits(t.hour) +
           two_digits(t.minute) + two_digits(t.second) + "Z";
}

// 2026-07-13T00:45:00Z -- ISO-8601 for runtime.json.
std::string iso_stamp(const platform::utc_time& t) {
    return four_digits(t.year) + "-" + two_digits(t.month) + "-" + two_digits(t.day) + "T" +
           two_digits(t.hour) + ":" + two_digits(t.minute) + ":" + two_digits(t.second) + "Z";
}

// A short random suffix so two copies started in the same second under the same clock and pid still
// get distinct run ids. Not a secret -- just a collision breaker -- so a missing RNG falls back to a
// fixed pair rather than failing the run.
std::string random_suffix() {
    static const char* const alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    u8 bytes[2] = {0, 0};
    platform::random_bytes(bytes, sizeof(bytes));
    std::string out;
    out += alphabet[bytes[0] % 36];
    out += alphabet[bytes[1] % 36];
    return out;
}

std::string level_name(trace_level level) {
    switch (level) {
        case trace_level::off: return "off";
        case trace_level::errors: return "errors";
        case trace_level::lifecycle: return "lifecycle";
        case trace_level::full: return "full";
    }
    return "off";
}

std::string base_name(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

} // namespace

tracer::tracer()
    : seq_(0), pid_(0), level_(trace_level::off), started_(false), next_thread_label_(0) {}

tracer::~tracer() {
    stop();
}

std::string tracer::logical_thread_label() {
    const std::thread::id self = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(label_mutex_);
    std::map<std::thread::id, std::string>::iterator it = thread_labels_.find(self);
    if (it != thread_labels_.end()) {
        return it->second;
    }
    const std::string label = "t#" + std::to_string(next_thread_label_++);
    thread_labels_[self] = label;
    return label;
}

trace_envelope tracer::next_envelope() {
    trace_envelope env;
    env.schema_version = 1;
    env.seq = seq_.fetch_add(1);
    const std::chrono::steady_clock::duration since =
        std::chrono::steady_clock::now() - epoch_;
    env.t_mono_ns =
        static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
    env.pid = pid_;
    env.inst = instance_label_;
    env.tid = logical_thread_label();
    return env;
}

std::string tracer::resolve_manual_run_dir(const std::string& trace_dir) {
    platform::utc_time now;
    const std::string stamp = platform::utc_now(now) ? compact_stamp(now) : "00000000T000000Z";
    const std::string base = "run-" + stamp + "-" + std::to_string(pid_) + "-";
    for (int attempt = 0; attempt < max_run_id_attempts; attempt++) {
        const std::string candidate = base + random_suffix();
        const std::string dir = trace_dir + "/" + candidate;
        if (!platform::directory_exists(dir)) {
            run_id_ = candidate;
            return dir;
        }
    }
    // Every candidate collided (astronomically unlikely). Take the last one anyway; the sink's
    // exclusive create of trace.jsonl is the final guard against clobbering a live run.
    run_id_ = base + random_suffix();
    return trace_dir + "/" + run_id_;
}

void tracer::write_runtime_json(const std::string& run_dir, const resolved_config& config) const {
    platform::utc_time now;
    const std::string created = platform::utc_now(now) ? iso_stamp(now) : std::string();

    json_writer writer;
    writer.begin_object();
    writer.field_uint("schema_version", 1);
    writer.field_string("emulator_build", emulator_build);
    if (created.empty()) {
        writer.field_null("created_utc");
    } else {
        writer.field_string("created_utc", created);
    }
    writer.field_string("run_id", run_id_);
    if (config.instance_label.empty()) {
        writer.field_null("instance_label");
    } else {
        writer.field_string("instance_label", config.instance_label);
    }
    writer.key("os");
    writer.begin_object();
    writer.field_string("name", os_name);
    writer.field_null("version"); // best-effort: the OS version and Wine detection come later
    writer.field_null("wine");
    writer.end_object();
    writer.key("config");
    writer.begin_object();
    writer.field_string("display_name", config.display_name);
    writer.field_string("trace_level", level_name(config.level));
    writer.key("discovery_ports");
    writer.begin_array();
    writer.value_uint(config.discovery_ports.first);
    writer.value_uint(config.discovery_ports.last);
    writer.end_array();
    writer.end_object();
    writer.end_object();

    if (!writer.ok()) {
        return; // never persist a malformed document
    }
    // Exclusively claim the file, then fill it: like trace.jsonl, the library owns runtime.json and
    // must not clobber a colliding one.
    const std::string path = run_dir + "/runtime.json";
    if (platform::create_new_file(path)) {
        if (!platform::append_file(path, writer.str())) {
            log_warn("tracer: could not write runtime.json");
        }
    }
}

void tracer::route_diagnostics(const std::vector<config_diagnostic>& diagnostics) const {
    // Config diagnostics carry free text (which field, human reason), which the typed trace schema
    // deliberately cannot represent yet, so for now every diagnostic is a best-effort logger line
    // rather than a lossy meta/config record. A faithful meta/config record awaits a schema field for
    // the config field name and action.
    for (std::size_t i = 0; i < diagnostics.size(); i++) {
        const config_diagnostic& d = diagnostics[i];
        std::string line = "config: " + d.field + " (" + d.source + ") " + d.action + " -- " + d.reason;
        if (!d.message.empty()) {
            line += ": " + d.message;
        }
        log_warn(line);
    }
}

void tracer::start(const resolved_config& config) {
    if (started_) {
        return; // a second EOS_Initialize keeps the first run
    }
    started_ = true;

    // Fresh run identity and counters.
    pid_ = platform::process_id();
    epoch_ = std::chrono::steady_clock::now();
    seq_.store(0);
    instance_label_ = config.instance_label;
    level_ = config.level;
    run_id_.clear();
    run_dir_.clear();
    {
        std::lock_guard<std::mutex> lock(label_mutex_);
        thread_labels_.clear();
        next_thread_label_ = 0;
    }

    if (config.level == trace_level::off) {
        route_diagnostics(config.diagnostics); // nothing on disk; diagnostics go to the logger
        return;
    }

    std::string run_dir;
    trace_sink::dir_mode mode;
    if (!config.run_dir.empty()) {
        // Runner mode: the launcher already created this directory; we only open it.
        run_dir = config.run_dir;
        run_id_ = base_name(config.run_dir);
        mode = trace_sink::dir_mode::must_exist;
    } else {
        run_dir = resolve_manual_run_dir(config.trace_dir);
        mode = trace_sink::dir_mode::create;
    }

    run_dir_ = run_dir;
    sink_.open(run_dir, config.level, config.trace_max_bytes, config.trace_max_rotated_files, this,
               mode);
    if (sink_.active()) {
        write_runtime_json(run_dir, config);
    }
    route_diagnostics(config.diagnostics);
}

void tracer::on_profile(const std::string& product_user_id) {
    if (!sink_.active()) {
        return;
    }
    const std::string fingerprint = peer_fingerprint(product_user_id);
    if (fingerprint.empty()) {
        return; // an id we cannot fingerprint yields no record rather than a bad one
    }
    std::vector<trace_field> fields;
    fields.push_back(make_field(field_id::peer_fp, tv_fingerprint(fingerprint)));
    sink_.write(trace_level::lifecycle, serialize_meta(next_envelope(), "profile", fields));
}

void tracer::flush() {
    sink_.flush();
}

void tracer::stop() {
    sink_.close();
    started_ = false;
}

bool tracer::active() const {
    return sink_.active();
}

} // namespace eosr
