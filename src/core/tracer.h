#ifndef EOSR_CORE_TRACER_H
#define EOSR_CORE_TRACER_H

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "common/types.h"
#include "core/config.h"          // resolved_config, trace_level
#include "core/label_registry.h"  // label_kind, label_registry
#include "core/trace_event.h"     // trace_envelope
#include "core/trace_sink.h"      // trace_sink, trace_meta_source

namespace eosr {

// The per-run observability tracer: the thing EOS_Initialize builds and EOS_Shutdown tears down. It
// owns the trace_sink, supplies the shared envelope (seq / monotonic clock / pid / instance / logical
// thread label) for every record, resolves the run directory under the two ownership modes, and emits
// the lifecycle records. It never depends on the profile -- tracing opens at EOS_Initialize, before
// EOS_Platform_Create acquires an identity -- and off means nothing is written or created.
//
// The class is free of process-global machinery so a test can drive one directly against a temporary
// directory; the flat layer owns the single process instance (global_tracer()).
// Spec: wiki/internals/alpha-tracing.md §1 (run directory), §2 (config), §3 (runtime.json), §4/§6 (records,
// lifecycle).
class tracer : public trace_meta_source {
public:
    tracer();
    ~tracer();

    tracer(const tracer&) = delete;
    tracer& operator=(const tracer&) = delete;

    // Open the run for EOS_Initialize. Resolves the run directory (runner mode when run_dir is set,
    // else a fresh <trace_dir>/<run_id>), opens the sink, writes runtime.json, and routes any config
    // diagnostics. `off` creates nothing. Calling it again while already started is ignored, so a
    // second EOS_Initialize keeps the first run.
    void start(const resolved_config& config);

    // Close the run for EOS_Shutdown: the sink writes its shutdown record, flushes, and closes, and
    // the tracer resets so a later EOS_Initialize opens a wholly new run. Safe if never started.
    void stop();

    // Flush buffered records at a tick boundary.
    void flush();

    // Emit the local pseudonymous fingerprint as a meta/profile record, once EOS_Platform_Create has
    // acquired the profile. A no-op when the sink is inactive or the id yields no fingerprint.
    void on_profile(const std::string& product_user_id);

    bool active() const;

    // A cheap gate for the hot path: one atomic load, so a game with tracing off pays nothing per EOS
    // call. active() takes the sink's lock; this does not.
    bool enabled() const { return enabled_.load(); }

    // The resolved run identity and directory for this run, or empty when off / not started. The run
    // directory has a random suffix in manual mode, so a caller (and a test) reads it back here rather
    // than reconstructing it.
    const std::string& run_id() const { return run_id_; }
    const std::string& run_directory() const { return run_dir_; }

    // --- Correlation and labels ---

    // A fresh correlation id (c#0, c#1, ...) for one asynchronous operation.
    std::string next_corr();

    // The stable opaque label for a handle or id, so neither ever reaches the file raw.
    std::string label(label_kind kind, const std::string& token);

    // --- The ambient call context ---
    //
    // An exported EOS function establishes it for the duration of the call. Any asynchronous result
    // queued while it is live inherits the correlation id, so the callback that fires ticks later can
    // be stitched back to the call that made it -- without threading a corr through every interface.
    // It is per-thread, because the game may call EOS from more than one.
    // The frames nest: a game may re-enter the SDK from inside a synchronous log or completion
    // callback, and finishing that inner call must reveal the outer one again, so a result queued
    // afterwards still correlates to the operation that actually created it.
    std::string begin_async_call(const std::string& fn, i32 api,
                                 const std::vector<trace_field>& args);
    void end_async_call(const std::string& fn, const std::string& corr);
    void end_async_call(const std::string& fn, const std::string& corr, const trace_return& value);
    std::string pending_fn() const;
    std::string pending_corr() const;

    // --- Record emitters ---
    //
    // Each picks its own verbosity: an asynchronous call/callback pair is `lifecycle`, a plain
    // call/return is `full`, and anything carrying a non-Success result is `errors`, so raising the
    // level only ever adds records.
    void record_call(const std::string& fn, i32 api, const std::string& corr,
                    const std::vector<trace_field>& args);
    void record_return(const std::string& fn, const std::string& corr, const trace_return& value);
    void record_callback(const std::string& fn, const std::string& corr,
                        const trace_result_code& result, const std::vector<trace_field>& payload);
    void record_notify(const std::string& event, const std::string& action,
                       const std::string& token, const std::vector<trace_field>& fields);
    void record_search(const char* reason, const std::string& peer, std::size_t count,
                       bool failure = false);
    void record_net(const std::string& event, const std::vector<trace_field>& fields,
                    bool failure = false);

    // trace_meta_source: the envelope for the sink's own run_start / rotate / shutdown records, sharing
    // this run's sequence counter and clock with everything else.
    trace_envelope next_envelope();

private:
    // The logical thread label for the calling thread (t#0, t#1, ...), assigned on first sight.
    std::string logical_thread_label();
    // Resolve a fresh, non-colliding <trace_dir>/<run_id> for manual mode.
    std::string resolve_manual_run_dir(const std::string& trace_dir);
    // Exclusively create and fill runtime.json. False if it already exists or cannot be written --
    // pairing a fresh trace with stale run metadata is a run failure, not something to ignore.
    bool write_runtime_json(const std::string& run_dir, const resolved_config& config) const;
    // Record each config diagnostic: a structured meta/config record when the sink is active (so the
    // trace is self-contained even before the game installs a log callback), otherwise a best-effort
    // logger line. The free-form message stays logger-only.
    void emit_diagnostics(const std::vector<config_diagnostic>& diagnostics);

    // The correlation id and the fn of the exported call in progress on one thread.
    struct call_frame {
        std::string fn;
        std::string corr;
    };

    trace_sink sink_;
    std::atomic<u64> seq_;                          // per-process monotonic record counter
    std::atomic<bool> enabled_;                     // the hot-path gate: no lock to read
    std::atomic<u64> next_corr_;                    // per-run correlation counter
    std::chrono::steady_clock::time_point epoch_;   // t = now - epoch, nanoseconds
    u64 pid_;
    std::string instance_label_;
    std::string run_id_;
    std::string run_dir_;
    trace_level level_;
    bool started_;

    // Guards the thread-label map, the per-thread call frames, and the label registry -- all leaf
    // state. The sink's mutex may nest this, never the reverse, so no emitter may hold it while
    // writing: next_envelope() takes it, and the sink calls next_envelope() under its own lock.
    mutable std::mutex label_mutex_;
    std::map<std::thread::id, std::string> thread_labels_;
    // A stack per thread, so nested EOS calls do not overwrite one another's correlation.
    std::map<std::thread::id, std::vector<call_frame> > call_frames_;
    label_registry labels_;
    u32 next_thread_label_;
};

// Whether an exported function completes through a callback or returns everything it has to say.
// Only an asynchronous call has a completion to correlate with, so only it mints a `corr` -- and that
// is what puts its call/return pair at `lifecycle`. A synchronous call is a `full`-level record.
enum class call_mode {
    sync,
    async
};

// One exported EOS function's trace, as a scope. Construct it at the very top of a C ABI entry point,
// *before* the handle and options are validated, so a call rejected for a null, stale, or foreign
// handle is still recorded -- that failure is exactly what an in-game probe exists to reveal, not a
// reason for the probe to go quiet. The `return` record is emitted when the scope closes, so every
// exit path -- including an early return -- is paired with its call.
//
// A function that returns something other than void tells the scope what it returned, via returns();
// otherwise the return is recorded as void.
class trace_scope {
public:
    trace_scope(tracer& trace, const char* fn, i32 api, const std::vector<trace_field>& args,
                call_mode mode);
    ~trace_scope();

    trace_scope(const trace_scope&) = delete;
    trace_scope& operator=(const trace_scope&) = delete;

    // The correlation id of this call. Empty when tracing is off, and empty for a synchronous call --
    // which has no completion to correlate with.
    const std::string& corr() const { return corr_; }

    // What this function returned. Defaults to void, which is what an async EOS function gives back.
    void returns(const trace_return& value) { value_ = value; }

private:
    tracer& tracer_;
    std::string fn_;
    std::string corr_;
    trace_return value_;
    call_mode mode_;
    bool active_;  // tracing was on when the call began; it stays that way for the whole call
};

} // namespace eosr

#endif
