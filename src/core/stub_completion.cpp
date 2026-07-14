#include "core/stub_completion.h"

#include <memory>

#include "core/callback_manager.h"
#include "core/i_run_callback.h"
#include "core/platform.h"
#include "core/runtime.h"

namespace eosr {

namespace {

// The prefix every completion's EOS_*CallbackInfo shares.
struct callback_info_head {
    EOS_EResult ResultCode;
    void* ClientData;
};

// One owner for every stubbed completion. It has no state to run and nothing in a payload to free:
// a stub's payload is plain zeroed bytes, never a heap field we duplicated.
class stub_owner : public i_run_callback {
public:
    bool cb_run_frame() { return false; }
    bool run_callbacks(frame_result&) { return false; }
    void free_callback(frame_result&) {}
};

stub_owner& owner() {
    static stub_owner the_owner;
    return the_owner;
}

// Stubs run through the live platform's async engine, so they deliver on its tick like anything else.
// Before EOS_Platform_Create there is nowhere to queue, and a game cannot have a handle to call us
// with anyway.
callback_manager* engine() {
    sdk_platform* platform = platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    return &platform->callbacks();
}

} // namespace

void stub_complete(void* client_data, completion_delegate delegate, std::size_t info_size) {
    callback_manager* callbacks = engine();
    if (callbacks == 0 || delegate == 0 || info_size < sizeof(callback_info_head)) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(0, info_size, delegate);
    callback_info_head* head = static_cast<callback_info_head*>(payload);
    head->ResultCode = EOS_EResult::EOS_NotImplemented;
    head->ClientData = client_data;
    result->set_done(true);
    callbacks->add_callback(&owner(), std::move(result));
}

EOS_NotificationId stub_add_notification(void* client_data, completion_delegate delegate,
                                         std::size_t info_size, const char* event) {
    callback_manager* callbacks = engine();
    if (callbacks == 0 || delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    // The payload is left zeroed. A notification info does *not* share the completion prefix -- it
    // begins with its own fields -- and this one never fires, so writing into it would be guessing at
    // a layout for no reason.
    (void)client_data;
    std::unique_ptr<frame_result> result(new frame_result());
    result->create_callback(0, info_size, delegate);
    return callbacks->add_notification(&owner(), std::move(result), event);
}

void stub_remove_notification(EOS_NotificationId id) {
    callback_manager* callbacks = engine();
    if (callbacks != 0) {
        callbacks->remove_notification(&owner(), id);
    }
}

} // namespace eosr
