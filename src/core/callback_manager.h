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
// Spec: Callback_Manager (wiki/internals/architecture.md §5)
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

    // The ids of every live notification for `owner` whose payload matches `type_id`. Ids are
    // stable keys, so a caller can iterate them and fire the notifications one at a time even if
    // a fired callback removes another notification mid-iteration.
    std::vector<EOS_NotificationId> notification_ids(i_run_callback* owner, callback_type_id type_id);

    // The notification `owner` registered under `id`, or null if it was never registered or has
    // since been removed. The pointer is owned by the manager; fire it immediately, do not cache.
    frame_result* find_notification(i_run_callback* owner, EOS_NotificationId id);

    void set_max_tick_budget(std::chrono::milliseconds budget);

    // Drop every registration, queued callback, and notification. The platform calls this on
    // release so a later platform never inherits a prior one's async state or stale owners.
    void clear();

    // Run one frame: housekeeping, then deliver ready callbacks. Delegates are fired after
    // the lock is released so a re-entrant EOS call from inside a callback (which games do)
    // cannot stall the network thread or a slow delegate hold the lock.
    void tick();

private:
    // The prefix every EOS_*CallbackInfo shares, so the outcome of any completion can be read without
    // knowing which one it is.
    struct callback_info_head {
        EOS_EResult ResultCode;
        void* ClientData;
    };

    // Emit the `callback` record for a completion about to fire, correlated back to the call.
    void record_callback_for(const frame_result& result);

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
