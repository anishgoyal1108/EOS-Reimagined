#ifndef EOSR_CORE_CALLBACK_MANAGER_H
#define EOSR_CORE_CALLBACK_MANAGER_H

#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "eos_common.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"

namespace eosr {

// Drives the async model: per-tick frames, one-shot completion callbacks, and persistent
// notifications. Owns every frame_result via unique_ptr; interfaces hold non-owning
// pointers while a request is pending.
// Spec: Callback_Manager (docs/architecture.md §5)
class callback_manager {
public:
    callback_manager();

    void register_frame(i_run_callback* owner);
    void unregister_frame(i_run_callback* owner);

    void register_callbacks(i_run_callback* owner);
    void unregister_callbacks(i_run_callback* owner);

    // Take ownership of a one-shot result and queue it for delivery to `owner`.
    void add_callback(i_run_callback* owner, std::unique_ptr<frame_result> result);

    // Take ownership of a persistent notification; returns its id (never 0).
    EOS_NotificationId add_notification(i_run_callback* owner, std::unique_ptr<frame_result> result);
    void remove_notification(i_run_callback* owner, EOS_NotificationId id);
    void remove_all_notifications(i_run_callback* owner);

    // Every live notification for `owner` whose payload matches `type_id`. The returned
    // pointers are owned by the manager and remain valid until the next remove for this
    // owner; callers fire them immediately and must not cache them across ticks.
    std::vector<frame_result*> get_notifications(i_run_callback* owner, callback_type_id type_id);

    void set_max_tick_budget(std::chrono::milliseconds budget);

    // Run one frame: housekeeping, then deliver ready callbacks. Delegates are fired after
    // the lock is released so a re-entrant EOS call from inside a callback (which games do)
    // cannot stall the network thread or a slow delegate hold the lock.
    void tick();

private:
    // A result that is ready to deliver, kept with its owner so free_callback can run.
    struct ready_callback {
        i_run_callback* owner;
        std::unique_ptr<frame_result> result;
    };

    // Both run with mutex_ already held.
    void run_frames();
    std::vector<ready_callback> collect_ready_callbacks();

    std::set<i_run_callback*> frames_to_run_;
    std::map<i_run_callback*, std::deque<std::unique_ptr<frame_result>>> callbacks_to_run_;
    std::map<i_run_callback*, std::map<EOS_NotificationId, std::unique_ptr<frame_result>>> notifications_;
    std::chrono::milliseconds max_tick_budget_;
    EOS_NotificationId next_notification_id_;
    // Recursive because the EOS API allows a game to call back into the SDK from inside a
    // completion callback, so the same thread must be able to re-acquire the lock it holds.
    std::recursive_mutex mutex_;
};

} // namespace eosr

#endif
