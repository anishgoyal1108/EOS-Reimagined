#include "core/platform.h"

#include <chrono>

#include "common/log.h"
#include "platform/paths.h"
#include "platform/socket.h"

namespace eosr {

sdk_platform::sdk_platform()
    : connect_(settings_, cb_manager_, network_),
      auth_(settings_, cb_manager_),
      p2p_(settings_, cb_manager_, network_),
      sessions_(settings_, cb_manager_, network_, connect_),
      presence_(settings_, cb_manager_, network_),
      lobby_(settings_, cb_manager_, network_, connect_),
      // A game that never says otherwise is in the foreground with a working network, which is the
      // only state an emulator running beside it could be in.
      application_status_(EOS_EApplicationStatus::EOS_AS_Foreground),
      network_status_(EOS_ENetworkStatus::EOS_NS_Online),
      created_(false) {
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
    // Take the persistent profile before the options fold the title into it, so the identity we
    // advertise is the one whose key we can actually prove. Failing that, the ephemeral key the
    // settings started with stands: the mesh still works, the identity just does not outlive the run.
    if (!settings_.load_identity(platform::user_data_directory())) {
        log_warn("platform: no profile directory available; this identity lasts only for this run");
    }
    settings_.apply_platform_options(options);
    cb_manager_.set_max_tick_budget(std::chrono::milliseconds(settings_.tick_budget_ms()));

    // Discovery advertises who we are and which game we are running, so only peers running the
    // same product mesh with us -- and only after each has proved it holds the key its identity is
    // derived from. A platform still works with no network: peers simply never appear, and
    // everything local keeps functioning.
    network_.set_identity(settings_.profile(), settings_.product_id(), settings_.sandbox_id(),
                          settings_.deployment_id());
    if (!network_.start()) {
        log_warn("platform: peer discovery unavailable; running without peers");
    }

    connect_.emu_init();
    auth_.emu_init();
    p2p_.emu_init();
    sessions_.emu_init();
    presence_.emu_init();
    lobby_.emu_init();
    created_ = true;
    log_info("platform created for product '" + settings_.product_id() + "'");
    return true;
}

void sdk_platform::release() {
    if (!created_) {
        return;
    }
    // Unregister the interfaces before clearing the engine, then discard any queued callbacks
    // and registrations so a platform created after this one never inherits stale async state
    // or fires through a destroyed owner.
    connect_.emu_deinit();
    auth_.emu_deinit();
    p2p_.emu_deinit();
    sessions_.emu_deinit();
    presence_.emu_deinit();
    lobby_.emu_deinit();
    network_.stop();
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

EOS_EResult sdk_platform::set_application_status(EOS_EApplicationStatus status) {
    if (
        status != EOS_EApplicationStatus::EOS_AS_BackgroundConstrained &&
        status != EOS_EApplicationStatus::EOS_AS_BackgroundUnconstrained &&
        status != EOS_EApplicationStatus::EOS_AS_BackgroundSuspended &&
        status != EOS_EApplicationStatus::EOS_AS_Foreground
    ) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    application_status_ = status;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_platform::set_network_status(EOS_ENetworkStatus status) {
    if (
        status != EOS_ENetworkStatus::EOS_NS_Disabled &&
        status != EOS_ENetworkStatus::EOS_NS_Offline &&
        status != EOS_ENetworkStatus::EOS_NS_Online
    ) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    network_status_ = status;
    return EOS_EResult::EOS_Success;
}

void* sdk_platform::interface_handle(interface_id id) {
    if (!created_ || id < 0 || id >= if_count) {
        return 0;
    }
    // Implemented interfaces hand back their real object; the rest return their placeholder.
    if (id == if_connect) {
        return &connect_;
    }
    if (id == if_auth) {
        return &auth_;
    }
    if (id == if_p2p) {
        return &p2p_;
    }
    if (id == if_sessions) {
        return &sessions_;
    }
    if (id == if_presence) {
        return &presence_;
    }
    if (id == if_lobby) {
        return &lobby_;
    }
    return &interfaces_[id];
}

} // namespace eosr
