#ifndef EOSR_CORE_TRACE_SINK_H
#define EOSR_CORE_TRACE_SINK_H

#include <mutex>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/config.h"       // trace_level
#include "core/trace_event.h"  // trace_envelope

namespace eosr {

// Supplies the envelope for the sink's own records (run_start / rotate / shutdown), so they share the
// run's sequence counter and clock with the game-driven records. The tracer implements it; a test
// supplies a deterministic fake.
class trace_meta_source {
public:
    virtual ~trace_meta_source() {}
    virtual trace_envelope next_envelope() = 0;
};

// The bounded file sink: it owns trace.jsonl in the run directory, buffers whole serialized lines and
// flushes them at the boundaries the tracer chooses, rotates when the file would exceed the byte cap,
// and never lets a failure or a runaway record take down the game.
//
// Off means it creates nothing. A write, flush, or rename failure disables the sink once -- it stops
// writing and every later call is a silent no-op, and a failure never generates more work. A record
// larger than a fresh file, or a serializer that returned the empty string, is skipped, not truncated.
// Spec: docs/alpha-tracing.md §4 (rotation, flushing), §6 (failure and lifecycle).
class trace_sink {
public:
    trace_sink();
    ~trace_sink();

    trace_sink(const trace_sink&) = delete;
    trace_sink& operator=(const trace_sink&) = delete;

    // Open for a run. `level` off creates nothing and leaves the sink inactive. `run_dir` must already
    // exist (the runner mode) or be creatable (the manual mode). `meta` -- which may be null -- supplies
    // envelopes for the sink's own run_start / rotate / shutdown records. Returns active().
    bool open(const std::string& run_dir, trace_level level, u64 max_bytes, u32 max_rotated_files,
              trace_meta_source* meta);

    // Write one serialized record line (no trailing newline) at its verbosity level. Dropped if the
    // sink is inactive or disabled, the record's level is above the configured threshold, or the line
    // is empty. Rotates first if appending it would exceed the byte cap.
    void write(trace_level record_level, const std::string& line);

    // Persist buffered lines to the file. Called at tick boundaries and shutdown.
    void flush();

    // Flush, write the shutdown record, and close. Safe to call more than once.
    void close();

    bool active() const;

private:
    void write_line_locked(const std::string& line);
    void flush_locked();
    void rotate_locked();
    void emit_meta_locked(const std::string& event, const std::vector<trace_field>& fields);
    void disable_locked();
    std::string numbered_path(u32 index) const;

    mutable std::mutex mutex_;
    std::string run_dir_;
    trace_level level_;
    u64 max_bytes_;
    u32 max_rotated_files_;
    trace_meta_source* meta_;
    std::string buffer_;         // whole lines not yet flushed
    u64 current_bytes_;          // bytes in trace.jsonl, flushed plus buffered
    std::vector<u64> rotated_sizes_;  // sizes of trace.1.jsonl .. trace.N.jsonl, newest first
    bool active_;
    bool disabled_;
    bool closed_;
};

} // namespace eosr

#endif
