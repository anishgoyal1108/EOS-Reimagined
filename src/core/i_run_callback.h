#ifndef EOSR_CORE_I_RUN_CALLBACK_H
#define EOSR_CORE_I_RUN_CALLBACK_H

namespace eosr {

class frame_result;

// Implemented by every interface that takes part in the per-tick callback loop.
// Spec: IRunCallback (wiki/internals/architecture.md §4)
class i_run_callback {
public:
    virtual ~i_run_callback() = default;

    // Per-tick housekeeping. Return value is advisory (most interfaces return false).
    virtual bool cb_run_frame() = 0;

    // Readiness predicate for a queued one-shot result: true when it may be delivered.
    // The result is borrowed (owned by callback_manager); do not retain or delete it.
    virtual bool run_callbacks(frame_result& result) = 0;

    // Release any heap fields this interface stashed inside the result's payload. This does
    // NOT delete the frame_result itself — callback_manager owns and destroys that.
    virtual void free_callback(frame_result& result) = 0;
};

} // namespace eosr

#endif
