#ifndef EOSR_CORE_TRACER_H
#define EOSR_CORE_TRACER_H

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "common/types.h"
#include "core/config.h"       // resolved_config, trace_level
#include "core/trace_event.h"  // trace_envelope
#include "core/trace_sink.h"   // trace_sink, trace_meta_source

namespace eosr {

// The per-run observability tracer: the thing EOS_Initialize builds and EOS_Shutdown tears down. It
// owns the trace_sink, supplies the shared envelope (seq / monotonic clock / pid / instance / logical
// thread label) for every record, resolves the run directory under the two ownership modes, and emits
// the lifecycle records. It never depends on the profile -- tracing opens at EOS_Initialize, before
// EOS_Platform_Create acquires an identity -- and off means nothing is written or created.
//
// The class is free of process-global machinery so a test can drive one directly against a temporary
// directory; the flat layer owns the single process instance (global_tracer()).
// Spec: docs/alpha-tracing.md §1 (run directory), §2 (config), §3 (runtime.json), §4/§6 (records,
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

    // The resolved run identity and directory for this run, or empty when off / not started. The run
    // directory has a random suffix in manual mode, so a caller (and a test) reads it back here rather
    // than reconstructing it.
    const std::string& run_id() const { return run_id_; }
    const std::string& run_directory() const { return run_dir_; }

    // trace_meta_source: the envelope for the sink's own run_start / rotate / shutdown records, sharing
    // this run's sequence counter and clock with everything else.
    trace_envelope next_envelope();

private:
    // The logical thread label for the calling thread (t#0, t#1, ...), assigned on first sight.
    std::string logical_thread_label();
    // Resolve a fresh, non-colliding <trace_dir>/<run_id> for manual mode.
    std::string resolve_manual_run_dir(const std::string& trace_dir);
    void write_runtime_json(const std::string& run_dir, const resolved_config& config) const;
    void route_diagnostics(const std::vector<config_diagnostic>& diagnostics) const;

    trace_sink sink_;
    std::atomic<u64> seq_;                          // per-process monotonic record counter
    std::chrono::steady_clock::time_point epoch_;   // t = now - epoch, nanoseconds
    u64 pid_;
    std::string instance_label_;
    std::string run_id_;
    std::string run_dir_;
    trace_level level_;
    bool started_;

    // Guards only the thread-label map -- a leaf lock the sink's mutex may nest, never the reverse.
    std::mutex label_mutex_;
    std::map<std::thread::id, std::string> thread_labels_;
    u32 next_thread_label_;
};

} // namespace eosr

#endif
