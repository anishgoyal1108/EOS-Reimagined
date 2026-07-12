#include "core/platform.h"

#include <chrono>

#include "common/log.h"
#include "platform/socket.h"

namespace eosr {

sdk_platform::sdk_platform() : created_(false) {
    for (int i = 0; i < if_count; i++) {
        interfaces_[i].id = static_cast<interface_id>(i);
    }
}

sdk_platform::~sdk_platform() {
    release();
}

bool sdk_platform::create(const EOS_Platform_Options* options) {
    if (created_) {
        return true;
    }
    if (!platform::net_init()) {
        log_error("platform: socket subsystem failed to initialize");
        return false;
    }
    settings_.apply_platform_options(options);
    cb_manager_.set_max_tick_budget(std::chrono::milliseconds(settings_.tick_budget_ms()));
    created_ = true;
    log_info("platform created for product '" + settings_.product_id() + "'");
    return true;
}

void sdk_platform::release() {
    if (!created_) {
        return;
    }
    network_.stop();
    // Discard any queued callbacks and registrations so a platform created after this one never
    // inherits stale async state or fires through a destroyed owner.
    cb_manager_.clear();
    platform::net_shutdown();
    created_ = false;
    log_info("platform released");
}

void sdk_platform::tick() {
    if (!created_) {
        return;
    }
    // Drain inbound peer traffic first so a message that arrived this frame can enqueue its
    // callback, then run housekeeping frames and deliver every matured game callback. The
    // precise frame/network interleave is revisited when the RX path lands.
    network_.cb_run_frame();
    cb_manager_.tick();
}

void* sdk_platform::interface_handle(interface_id id) {
    if (!created_ || id < 0 || id >= if_count) {
        return 0;
    }
    return &interfaces_[id];
}

} // namespace eosr
