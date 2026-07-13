#include "core/callback_manager.h"

namespace eosr {

// The first valid notification id; 0 is EOS_INVALID_NOTIFICATIONID.
static const EOS_NotificationId first_notification_id = 1;

callback_manager::callback_manager()
    : max_tick_budget_(0)
    , next_notification_id_(first_notification_id) {
}

void callback_manager::register_frame(i_run_callback* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    frames_to_run_.insert(owner);
}

void callback_manager::unregister_frame(i_run_callback* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    frames_to_run_.erase(owner);
}

void callback_manager::register_callbacks(i_run_callback* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    callbacks_to_run_[owner];
}

void callback_manager::unregister_callbacks(i_run_callback* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    callbacks_to_run_.erase(owner);
}

void callback_manager::add_callback(i_run_callback* owner, std::unique_ptr<frame_result> result) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    callbacks_to_run_[owner].push_back(std::move(result));
}

EOS_NotificationId callback_manager::add_notification(i_run_callback* owner,
                                                      std::unique_ptr<frame_result> result) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const EOS_NotificationId id = next_notification_id_;
    next_notification_id_++;
    notifications_[owner][id] = std::move(result);
    return id;
}

void callback_manager::remove_notification(i_run_callback* owner, EOS_NotificationId id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::map<i_run_callback*, std::map<EOS_NotificationId, std::unique_ptr<frame_result>>>::iterator it =
        notifications_.find(owner);
    if (it != notifications_.end()) {
        it->second.erase(id);
    }
}

void callback_manager::clear() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // A queued result may own heap fields inside its payload (e.g. duplicated strings). Give its
    // owner the chance to release them before we drop it, exactly as delivery would, so tearing a
    // platform down with callbacks still in flight does not leak them.
    std::map<i_run_callback*, std::deque<std::unique_ptr<frame_result>>>::iterator it =
        callbacks_to_run_.begin();
    for (; it != callbacks_to_run_.end(); ++it) {
        for (std::size_t i = 0; i < it->second.size(); i++) {
            if (it->second[i]) {
                it->first->free_callback(*it->second[i]);
            }
        }
    }
    frames_to_run_.clear();
    callbacks_to_run_.clear();
    notifications_.clear();
    next_notification_id_ = first_notification_id;
}

void callback_manager::remove_all_notifications(i_run_callback* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    notifications_.erase(owner);
}

std::vector<EOS_NotificationId> callback_manager::notification_ids(i_run_callback* owner,
                                                                   callback_type_id type_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<EOS_NotificationId> out;
    std::map<i_run_callback*, std::map<EOS_NotificationId, std::unique_ptr<frame_result>>>::iterator it =
        notifications_.find(owner);
    if (it == notifications_.end()) {
        return out;
    }
    std::map<EOS_NotificationId, std::unique_ptr<frame_result>>::iterator note = it->second.begin();
    for (; note != it->second.end(); ++note) {
        if (note->second->type_id() == type_id) {
            out.push_back(note->first);
        }
    }
    return out;
}

frame_result* callback_manager::find_notification(i_run_callback* owner, EOS_NotificationId id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::map<i_run_callback*, std::map<EOS_NotificationId, std::unique_ptr<frame_result>>>::iterator it =
        notifications_.find(owner);
    if (it == notifications_.end()) {
        return 0;
    }
    std::map<EOS_NotificationId, std::unique_ptr<frame_result>>::iterator note = it->second.find(id);
    if (note == it->second.end()) {
        return 0;
    }
    return note->second.get();
}

void callback_manager::set_max_tick_budget(std::chrono::milliseconds budget) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    max_tick_budget_ = budget;
}

void callback_manager::tick() {
    std::vector<ready_callback> ready;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        run_frames();
        ready = collect_ready_callbacks();
    }
    // Fire outside the lock: the delegate may re-enter the SDK, and the payload it reads is
    // owned locally here. free_callback releases heap fields inside the payload only.
    for (std::size_t i = 0; i < ready.size(); i++) {
        ready[i].result->fire();
        ready[i].owner->free_callback(*ready[i].result);
    }
}

void callback_manager::run_frames() {
    // Snapshot so a frame that (un)registers another does not invalidate the iteration.
    std::vector<i_run_callback*> owners(frames_to_run_.begin(), frames_to_run_.end());
    for (std::size_t i = 0; i < owners.size(); i++) {
        if (frames_to_run_.count(owners[i]) != 0) {
            owners[i]->cb_run_frame();
        }
    }
}

// Move every deliverable result out of the queues so it can be fired after the lock is
// released. A result delivers once it is past its timeout and either done or its owner
// reports it ready; the rest are kept for a later tick.
std::vector<callback_manager::ready_callback> callback_manager::collect_ready_callbacks() {
    std::vector<ready_callback> ready;

    std::vector<i_run_callback*> owners;
    std::map<i_run_callback*, std::deque<std::unique_ptr<frame_result>>>::iterator owner_it =
        callbacks_to_run_.begin();
    for (; owner_it != callbacks_to_run_.end(); ++owner_it) {
        owners.push_back(owner_it->first);
    }

    for (std::size_t i = 0; i < owners.size(); i++) {
        i_run_callback* owner = owners[i];
        std::map<i_run_callback*, std::deque<std::unique_ptr<frame_result>>>::iterator it =
            callbacks_to_run_.find(owner);
        if (it == callbacks_to_run_.end()) {
            continue;
        }

        std::deque<std::unique_ptr<frame_result>>& queue = it->second;
        std::deque<std::unique_ptr<frame_result>> keep;
        while (!queue.empty()) {
            std::unique_ptr<frame_result> res = std::move(queue.front());
            queue.pop_front();
            const bool deliver = res->callback_ok_timeout() && (res->done() || owner->run_callbacks(*res));
            if (deliver) {
                ready_callback item;
                item.owner = owner;
                item.result = std::move(res);
                ready.push_back(std::move(item));
            } else {
                keep.push_back(std::move(res));
            }
        }
        queue.swap(keep);
    }
    return ready;
}

} // namespace eosr
