#include "core/frame_result.h"

#include <cstring>

#include "core/runtime.h"
#include "core/tracer.h"

namespace eosr {

frame_result::frame_result()
    : type_id_(0)
    , func_(nullptr)
    , ok_timeout_(0)
    , created_time_(std::chrono::steady_clock::now())
    , done_(false)
    , remove_on_timeout_(false) {
}

void* frame_result::create_callback(callback_type_id type_id, size_t size, completion_delegate func,
                                    std::chrono::milliseconds ok_timeout) {
    type_id_ = type_id;
    func_ = func;
    ok_timeout_ = ok_timeout;

    // Round the byte size up to whole max_align_t units, then zero the storage so unset
    // fields of the callback-info are the zero the SDK expects.
    const size_t unit = sizeof(std::max_align_t);
    payload_.assign((size + unit - 1) / unit, std::max_align_t());
    if (!payload_.empty()) {
        std::memset(payload_.data(), 0, payload_.size() * unit);
    }

    created_time_ = std::chrono::steady_clock::now();
    notification_event_.clear();
    notification_token_.clear();
    done_ = false;
    remove_on_timeout_ = false;
    return payload_.data();
}

bool frame_result::callback_ok_timeout() const {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    return (now - created_time_) >= ok_timeout_;
}

void frame_result::fire() const {
    if (!notification_event_.empty() && !notification_token_.empty()) {
        global_tracer().record_notify(notification_event_, "fire", notification_token_,
                                      trace_payload_);
    }
    if (func_ != nullptr) {
        func_(payload_.data());
    }
}

} // namespace eosr
