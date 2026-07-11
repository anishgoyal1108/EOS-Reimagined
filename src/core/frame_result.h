#ifndef EOSR_CORE_FRAME_RESULT_H
#define EOSR_CORE_FRAME_RESULT_H

#include <chrono>
#include <cstddef>
#include <vector>

#include "eos_common.h"
#include "common/types.h"

namespace eosr {

// The game's completion delegate. Every EOS callback is a C function pointer taking a
// const pointer to its EOS_*CallbackInfo; we store it type-erased and call it with the
// payload below.
using completion_delegate = void (EOS_CALL *)(const void* callback_info);

// Identifies which EOS_*CallbackInfo type a payload holds. Values are assigned per
// callback-info type where the interfaces are implemented.
using callback_type_id = int;

// One pending async result. Holds a heap copy of the EOS_*CallbackInfo we hand back to
// the game, the delegate to call, and the delivery timing. Owned by callback_manager
// via unique_ptr; interfaces keep a non-owning frame_result* while a request is pending.
// The payload is a vector of max_align_t elements, so its storage is aligned for any
// callback-info type and copy/move/destruction are all automatic.
// Spec: FrameResult / CallbackMessage_t (docs/architecture.md §5)
class frame_result {
public:
    frame_result();

    // Allocate a fresh zeroed payload of `size` bytes tagged with `type_id`, record the
    // delegate and the minimum age before delivery, and reset the delivery state so the
    // result can be reused. Returns the payload so the caller fills the info in place.
    void* create_callback(callback_type_id type_id, size_t size, completion_delegate func,
                          std::chrono::milliseconds ok_timeout = std::chrono::milliseconds(0));

    template<class T>
    T* get_callback() {
        static_assert(alignof(T) <= alignof(std::max_align_t),
                      "callback payload storage cannot satisfy T's alignment");
        return reinterpret_cast<T*>(payload_.data());
    }

    template<class T>
    const T* get_callback() const {
        static_assert(alignof(T) <= alignof(std::max_align_t),
                      "callback payload storage cannot satisfy T's alignment");
        return reinterpret_cast<const T*>(payload_.data());
    }

    // True once at least ok_timeout has elapsed since the last create_callback.
    bool callback_ok_timeout() const;

    // Invoke the delegate with the payload, if a delegate was set.
    void fire() const;

    callback_type_id type_id() const { return type_id_; }
    bool done() const { return done_; }
    void set_done(bool value) { done_ = value; }
    bool remove_on_timeout() const { return remove_on_timeout_; }
    void set_remove_on_timeout(bool value) { remove_on_timeout_ = value; }

private:
    callback_type_id type_id_;
    std::vector<std::max_align_t> payload_;
    completion_delegate func_;
    std::chrono::milliseconds ok_timeout_;
    std::chrono::steady_clock::time_point created_time_;
    bool done_;
    bool remove_on_timeout_;
};

} // namespace eosr

#endif
