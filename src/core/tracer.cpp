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

// The exact artifact identity, injected by CMake so two traces can be tied to the precise build.
// The fallback keeps a non-CMake compile (an IDE indexer, say) building.
#if defined(EOSR_BUILD_ID)
const char* const emulator_build = EOSR_BUILD_ID;
#else
const char* const emulator_build = "eosr (unknown build)";
#endif

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

std::string log_level_name(log_level level) {
    switch (level) {
        case log_level::off: return "off";
        case log_level::fatal: return "fatal";
        case log_level::error: return "error";
        case log_level::warn: return "warn";
        case log_level::info: return "info";
        case log_level::debug: return "debug";
        case log_level::trace: return "trace";
    }
    return "off";
}

bool is_separator(char c) {
    return c == '/' || c == '\\';
}

// Drop any trailing path separators so a runner directory given as ".../run-3/" is treated as
// ".../run-3" -- otherwise both the derived run id and the joined file paths carry the stray slash.
std::string trim_trailing_separators(const std::string& path) {
    std::size_t end = path.size();
    while (end > 0 && is_separator(path[end - 1])) {
        end--;
    }
    return path.substr(0, end);
}

std::string base_name(const std::string& path) {
    const std::string trimmed = trim_trailing_separators(path);
    const std::size_t slash = trimmed.find_last_of("/\\");
    return (slash == std::string::npos) ? trimmed : trimmed.substr(slash + 1);
}

// A stable code from a diagnostic string: lower-case, with every non-identifier byte folded to '_',
// so a spaced reason like "below minimum" becomes the enum token "below_minimum". The value is
// dropped by the serializer if it still is not a valid enum (e.g. it would start with a digit), so a
// surprising input yields no field rather than a bad one.
std::string to_code(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            out += c;
        } else {
            out += '_';
        }
    }
    return out;
}

} // namespace

tracer::tracer()
    : seq_(0), enabled_(false), next_corr_(0), pid_(0), level_(trace_level::off), started_(false),
      next_thread_label_(0) {}

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

bool tracer::write_runtime_json(const std::string& run_dir, const resolved_config& config) const {
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
    writer.field_string("locale", config.locale);
    writer.field_string("trace_level", level_name(config.level));
    writer.field_string("log_level", log_level_name(config.logging));
    writer.field_bool("enable_lan", config.enable_lan);
    writer.field_bool("enable_overlay", config.enable_overlay);
    writer.field_bool("unlock_dlcs", config.unlock_dlcs);
    writer.key("discovery_ports");
    writer.begin_array();
    writer.value_uint(config.discovery_ports.first);
    writer.value_uint(config.discovery_ports.last);
    writer.end_array();
    writer.end_object();
    writer.end_object();

    if (!writer.ok()) {
        return false; // never persist a malformed document
    }
    // Exclusively claim the file, then fill it: like trace.jsonl, the library owns runtime.json and
    // must not clobber a colliding one. An existing file means stale run metadata; the caller rejects
    // the whole run rather than pair fresh trace records with it.
    const std::string path = run_dir + "/runtime.json";
    if (!platform::create_new_file(path)) {
        return false;
    }
    if (!platform::append_file(path, writer.str())) {
        platform::remove_file(path); // do not leave a half-written runtime file behind
        return false;
    }
    return true;
}

void tracer::emit_diagnostics(const std::vector<config_diagnostic>& diagnostics) {
    for (std::size_t i = 0; i < diagnostics.size(); i++) {
        const config_diagnostic& d = diagnostics[i];
        if (sink_.active()) {
            // Stable codes go into the trace so it is self-contained even before the game installs a
            // log callback; the free-form message (parser detail) stays logger-only below.
            std::vector<trace_field> fields;
            fields.push_back(make_field(field_id::config_field, tv_enum(to_code(d.field))));
            fields.push_back(make_field(field_id::source, tv_enum(to_code(d.source))));
            fields.push_back(make_field(field_id::reason, tv_enum(to_code(d.reason))));
            fields.push_back(make_field(field_id::action, tv_enum(to_code(d.action))));
            sink_.write(trace_level::errors, serialize_meta(next_envelope(), "config", fields));
        }
        // Best-effort human line: the only record when the sink is off/failed, and the free-form
        // detail otherwise. Dropped silently if the game has not installed a log callback yet.
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
    next_corr_.store(0);
    enabled_.store(false); // an off run must not inherit the previous run's gate
    {
        std::lock_guard<std::mutex> lock(label_mutex_);
        thread_labels_.clear();
        call_frames_.clear();
        labels_.clear();
        next_thread_label_ = 0;
    }

    if (config.level == trace_level::off) {
        emit_diagnostics(config.diagnostics); // nothing on disk; diagnostics go to the logger
        return;
    }

    std::string run_dir;
    trace_sink::dir_mode mode;
    if (!config.run_dir.empty()) {
        // Runner mode: the launcher already created this directory; we only open it. Strip any
        // trailing separator so both the run id and the joined file paths stay clean.
        run_dir = trim_trailing_separators(config.run_dir);
        run_id_ = base_name(run_dir);
        mode = trace_sink::dir_mode::must_exist;
    } else {
        run_dir = resolve_manual_run_dir(config.trace_dir);
        mode = trace_sink::dir_mode::create;
    }

    run_dir_ = run_dir;
    sink_.open(run_dir, config.level, config.trace_max_bytes, config.trace_max_rotated_files, this,
               mode);
    if (sink_.active() && !write_runtime_json(run_dir, config)) {
        // A colliding or unwritable runtime.json would leave a fresh trace paired with stale run
        // metadata, so reject the whole run: close the sink and discard its trace file.
        sink_.close();
        platform::remove_file(run_dir + "/trace.jsonl");
        log_error("tracer: runtime.json could not be exclusively written; trace run rejected");
    }
    // The hot-path gate, set once the run's fate is settled: an EOS call checks this one atomic and
    // does nothing more when tracing is off or the run failed to open.
    enabled_.store(sink_.active());
    emit_diagnostics(config.diagnostics);
}

std::string tracer::next_corr() {
    return "c#" + std::to_string(next_corr_.fetch_add(1));
}

std::string tracer::label(label_kind kind, const std::string& token) {
    std::lock_guard<std::mutex> lock(label_mutex_);
    return labels_.label(kind, token);
}

std::string tracer::begin_async_call(const std::string& fn, i32 api,
                                     const std::vector<trace_field>& args) {
    if (!enabled()) {
        return std::string();
    }
    const std::string corr = next_corr();
    {
        std::lock_guard<std::mutex> lock(label_mutex_);
        call_frame frame;
        frame.fn = fn;
        frame.corr = corr;
        call_frames_[std::this_thread::get_id()].push_back(frame); // nest, never overwrite
    }
    record_call(fn, api, corr, args); // emitted with the lock released: the sink nests our lock
    return corr;
}

void tracer::end_async_call(const std::string& fn, const std::string& corr) {
    // An async EOS function hands its result to the callback, so its synchronous return carries the
    // correlation and nothing else.
    end_async_call(fn, corr, return_void());
}

void tracer::end_async_call(const std::string& fn, const std::string& corr,
                            const trace_return& value) {
    if (corr.empty()) {
        return; // tracing was off when the call began; it stays off for the whole call
    }
    {
        std::lock_guard<std::mutex> lock(label_mutex_);
        std::map<std::thread::id, std::vector<call_frame> >::iterator it =
            call_frames_.find(std::this_thread::get_id());
        if (it != call_frames_.end()) {
            // Pop this call's own frame, not merely the newest: a trampoline that nested another EOS
            // call must leave the stack exactly as it found it.
            std::vector<call_frame>& stack = it->second;
            for (std::size_t i = stack.size(); i > 0; i--) {
                if (stack[i - 1].corr == corr) {
                    stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(i - 1));
                    break;
                }
            }
            if (stack.empty()) {
                call_frames_.erase(it);
            }
        }
    }
    record_return(fn, corr, value);
}

std::string tracer::pending_fn() const {
    std::lock_guard<std::mutex> lock(label_mutex_);
    std::map<std::thread::id, std::vector<call_frame> >::const_iterator it =
        call_frames_.find(std::this_thread::get_id());
    if (it == call_frames_.end() || it->second.empty()) {
        return std::string();
    }
    return it->second.back().fn; // the innermost call in progress on this thread
}

std::string tracer::pending_corr() const {
    std::lock_guard<std::mutex> lock(label_mutex_);
    std::map<std::thread::id, std::vector<call_frame> >::const_iterator it =
        call_frames_.find(std::this_thread::get_id());
    if (it == call_frames_.end() || it->second.empty()) {
        return std::string();
    }
    return it->second.back().corr;
}

void tracer::record_call(const std::string& fn, i32 api, const std::string& corr,
                        const std::vector<trace_field>& args) {
    if (!enabled()) {
        return;
    }
    // An async call is half of a lifecycle pair; a plain call is only interesting at full.
    const trace_level at = corr.empty() ? trace_level::full : trace_level::lifecycle;
    sink_.write(at, serialize_call(next_envelope(), fn, api, corr, args));
}

void tracer::record_return(const std::string& fn, const std::string& corr,
                          const trace_return& value) {
    if (!enabled()) {
        return;
    }
    trace_level at = corr.empty() ? trace_level::full : trace_level::lifecycle;
    if (value.type == trace_return::r_result && value.result.code != 0) {
        at = trace_level::errors; // a failure is worth recording at every level above off
    }
    sink_.write(at, serialize_return(next_envelope(), fn, corr, value));
}

void tracer::record_callback(const std::string& fn, const std::string& corr,
                            const trace_result_code& result,
                            const std::vector<trace_field>& payload) {
    if (!enabled()) {
        return;
    }
    const trace_level at =
        (result.code != 0) ? trace_level::errors : trace_level::lifecycle;
    sink_.write(at, serialize_callback(next_envelope(), fn, corr, result, payload));
}

void tracer::record_notify(const std::string& event, const std::string& action,
                           const std::string& token, const std::vector<trace_field>& fields) {
    if (!enabled() || token.empty()) {
        return;
    }
    const std::string id = label(label_kind::notif, token);
    sink_.write(trace_level::lifecycle,
                serialize_notify(next_envelope(), event, action, id, fields));
}

void tracer::record_net(const std::string& event, const std::vector<trace_field>& fields,
                        bool failure) {
    if (!enabled()) {
        return;
    }
    const trace_level at = failure ? trace_level::errors : trace_level::lifecycle;
    sink_.write(at, serialize_net(next_envelope(), event, fields));
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
    // Drop the hot-path gate first: a call racing the shutdown must not mint a correlation, or build
    // a record, for a sink that is closing.
    enabled_.store(false);
    sink_.close();
    started_ = false;
    std::lock_guard<std::mutex> lock(label_mutex_);
    call_frames_.clear();
}

bool tracer::active() const {
    return sink_.active();
}

trace_scope::trace_scope(tracer& trace, const char* fn, i32 api,
                         const std::vector<trace_field>& args, call_mode mode)
    : tracer_(trace), fn_(fn), value_(return_void()), mode_(mode), active_(trace.enabled()) {
    if (!active_) {
        return; // tracing is off: mint nothing, record nothing, and stay that way for the whole call
    }
    // Opened before the trampoline validates anything, so a call rejected for a bad handle is still
    // a call in the trace.
    if (mode_ == call_mode::async) {
        corr_ = tracer_.begin_async_call(fn_, api, args);
        return;
    }
    // A synchronous call has no completion to correlate with, so it mints no corr and establishes no
    // ambient context -- which is also what keeps it a `full`-level record rather than a lifecycle one.
    tracer_.record_call(fn_, api, std::string(), args);
}

trace_scope::~trace_scope() {
    if (!active_) {
        return;
    }
    // Whatever path the trampoline took out -- an early return on a bad handle included -- the call
    // is closed by its return record here, carrying whatever the function actually returned.
    if (mode_ == call_mode::async) {
        tracer_.end_async_call(fn_, corr_, value_);
        return;
    }
    tracer_.record_return(fn_, std::string(), value_);
}

} // namespace eosr
