#ifndef EOSR_CORE_PLATFORM_H
#define EOSR_CORE_PLATFORM_H

#include "eos_types.h"

#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/auth.h"
#include "interfaces/connect.h"
#include "interfaces/lobby.h"
#include "interfaces/p2p.h"
#include "interfaces/presence.h"
#include "interfaces/sessions.h"
#include "net/message_router.h"

namespace eosr {

// The interfaces the platform exposes, in the order EOS_Platform_Create instantiates them.
// Each EOS_Platform_Get<X>Interface maps to one of these.
enum interface_id {
    if_metrics = 0,
    if_auth,
    if_connect,
    if_ecom,
    if_ui,
    if_friends,
    if_presence,
    if_sessions,
    if_lobby,
    if_userinfo,
    if_p2p,
    if_rtc,
    if_rtc_admin,
    if_playerdatastorage,
    if_titlestorage,
    if_achievements,
    if_stats,
    if_leaderboards,
    if_mods,
    if_anticheatclient,
    if_anticheatserver,
    if_progressionsnapshot,
    if_reports,
    if_sanctions,
    if_kws,
    if_custominvites,
    if_integratedplatform,
    if_count
};

// A placeholder interface object. Every getter must hand the game a non-null handle, so until
// an interface is implemented for real its slot holds one of these. Distinct array elements
// give distinct handle addresses, which is what a game expects from two different getters.
struct stub_interface {
    interface_id id;
};

// One SDK platform instance: the object EOS_HPlatform points at. It owns the async engine
// (callback_manager), the peer network (message_router), the resolved settings, and one object
// per interface. Tick is the sole driver of async delivery and network dispatch. Kept a plain
// class — the flat layer holds the single process instance — so tests can create it in isolation.
// Spec: EOSSDK_Platform (docs/client.md), EOS_Platform_Create/Tick/Release (docs/architecture.md)
class sdk_platform {
public:
    sdk_platform();
    ~sdk_platform();

    sdk_platform(const sdk_platform&) = delete;
    sdk_platform& operator=(const sdk_platform&) = delete;

    // Bring the platform up from EOS_Platform_Options. Returns false only if the socket
    // subsystem cannot be initialized; a null options pointer is rejected by the caller.
    bool create(const EOS_Platform_Options* options);
    void release();
    bool is_created() const { return created_; }

    // Run one frame: drain the network, run per-interface housekeeping, deliver matured
    // game callbacks. A no-op until the platform is created.
    void tick();

    // The stored interface object for `id`, or null before creation. The handle a game holds
    // is this pointer reinterpret_cast to the matching EOS_H<X> type.
    void* interface_handle(interface_id id);

    // The application and network state a game tells us it is in. Nothing here reaches a service --
    // there is none -- but a game sets these and reads them back, and one that suspends itself and
    // sees no change has reason to think the SDK is broken. So we remember what it told us.
    EOS_EApplicationStatus application_status() const { return application_status_; }
    EOS_EResult set_application_status(EOS_EApplicationStatus status);
    EOS_ENetworkStatus network_status() const { return network_status_; }
    EOS_EResult set_network_status(EOS_ENetworkStatus status);

    sdk_settings& settings() { return settings_; }
    callback_manager& callbacks() { return cb_manager_; }
    message_router& network() { return network_; }
    sdk_connect& connect() { return connect_; }
    sdk_auth& auth() { return auth_; }
    sdk_p2p& p2p() { return p2p_; }
    sdk_sessions& sessions() { return sessions_; }
    sdk_presence& presence() { return presence_; }
    sdk_lobby& lobby() { return lobby_; }

private:
    sdk_settings settings_;
    callback_manager cb_manager_;
    message_router network_;
    // Implemented interfaces are real objects; the rest are placeholders until promoted. These
    // are declared after their dependencies so they construct with valid references to them.
    sdk_connect connect_;
    sdk_auth auth_;
    sdk_p2p p2p_;
    sdk_sessions sessions_;
    sdk_presence presence_;
    sdk_lobby lobby_;
    stub_interface interfaces_[if_count];
    EOS_EApplicationStatus application_status_;
    EOS_ENetworkStatus network_status_;
    bool created_;
};

} // namespace eosr

#endif
