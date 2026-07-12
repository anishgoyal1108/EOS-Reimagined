// Flat C ABI trampolines for the platform: lifecycle, the tick pump, and the interface
// getters. Each forwards to the one process-global sdk_platform after validating the handle.
#include "eos_sdk.h"

#include "core/platform.h"
#include "core/client.h"
#include "core/runtime.h"

namespace {

// A platform handle is only valid if it is the current live platform. Because each create
// allocates a fresh instance, this rejects null, a stray pointer, and a handle left over from
// a released platform, so every trampoline degrades to a safe no-op. We only compare the stale
// handle, never dereference it.
eosr::sdk_platform* checked_platform(EOS_HPlatform handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || handle != reinterpret_cast<EOS_HPlatform>(platform) || !platform->is_created()) {
        return 0;
    }
    return platform;
}

} // namespace

EOS_DECLARE_FUNC(EOS_HPlatform) EOS_Platform_Create(const EOS_Platform_Options* Options) {
    if (Options == 0 || !eosr::global_client().is_initialized()) {
        return 0;
    }
    eosr::sdk_platform* platform = eosr::platform_create();
    if (!platform->create(Options)) {
        eosr::platform_destroy();
        return 0;
    }
    return reinterpret_cast<EOS_HPlatform>(platform);
}

EOS_DECLARE_FUNC(void) EOS_Platform_Release(EOS_HPlatform Handle) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform != 0) {
        platform->release();
        // Free the instance so its address is never reused by a later create; the next create
        // hands out a distinct handle and this one becomes permanently unrecognized.
        eosr::platform_destroy();
    }
}

EOS_DECLARE_FUNC(void) EOS_Platform_Tick(EOS_HPlatform Handle) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform != 0) {
        platform->tick();
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_CheckForLauncherAndRestart(EOS_HPlatform Handle) {
    // We are never launched by the Epic launcher, so there is nothing to relaunch. Returning
    // EOS_Success would tell the game to quit and restart; EOS_NoChange tells it to carry on.
    if (checked_platform(Handle) == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_NoChange;
}

// Every EOS_Platform_Get<X>Interface follows the same shape: resolve the platform, hand back
// the stored interface object as the matching opaque handle. A getter on an invalid platform
// returns null, which the caller must be prepared for anyway.
#define EOSR_INTERFACE_GETTER(fn_name, handle_type, iface_id)                    \
    EOS_DECLARE_FUNC(handle_type) fn_name(EOS_HPlatform Handle) {                 \
        eosr::sdk_platform* platform = checked_platform(Handle);                 \
        void* iface = (platform != 0) ? platform->interface_handle(iface_id) : 0; \
        return reinterpret_cast<handle_type>(iface);                             \
    }

EOSR_INTERFACE_GETTER(EOS_Platform_GetMetricsInterface, EOS_HMetrics, eosr::if_metrics)
EOSR_INTERFACE_GETTER(EOS_Platform_GetAuthInterface, EOS_HAuth, eosr::if_auth)
EOSR_INTERFACE_GETTER(EOS_Platform_GetConnectInterface, EOS_HConnect, eosr::if_connect)
EOSR_INTERFACE_GETTER(EOS_Platform_GetEcomInterface, EOS_HEcom, eosr::if_ecom)
EOSR_INTERFACE_GETTER(EOS_Platform_GetUIInterface, EOS_HUI, eosr::if_ui)
EOSR_INTERFACE_GETTER(EOS_Platform_GetFriendsInterface, EOS_HFriends, eosr::if_friends)
EOSR_INTERFACE_GETTER(EOS_Platform_GetPresenceInterface, EOS_HPresence, eosr::if_presence)
EOSR_INTERFACE_GETTER(EOS_Platform_GetSessionsInterface, EOS_HSessions, eosr::if_sessions)
EOSR_INTERFACE_GETTER(EOS_Platform_GetLobbyInterface, EOS_HLobby, eosr::if_lobby)
EOSR_INTERFACE_GETTER(EOS_Platform_GetUserInfoInterface, EOS_HUserInfo, eosr::if_userinfo)
EOSR_INTERFACE_GETTER(EOS_Platform_GetP2PInterface, EOS_HP2P, eosr::if_p2p)
EOSR_INTERFACE_GETTER(EOS_Platform_GetRTCInterface, EOS_HRTC, eosr::if_rtc)
EOSR_INTERFACE_GETTER(EOS_Platform_GetRTCAdminInterface, EOS_HRTCAdmin, eosr::if_rtc_admin)
EOSR_INTERFACE_GETTER(EOS_Platform_GetPlayerDataStorageInterface, EOS_HPlayerDataStorage, eosr::if_playerdatastorage)
EOSR_INTERFACE_GETTER(EOS_Platform_GetTitleStorageInterface, EOS_HTitleStorage, eosr::if_titlestorage)
EOSR_INTERFACE_GETTER(EOS_Platform_GetAchievementsInterface, EOS_HAchievements, eosr::if_achievements)
EOSR_INTERFACE_GETTER(EOS_Platform_GetStatsInterface, EOS_HStats, eosr::if_stats)
EOSR_INTERFACE_GETTER(EOS_Platform_GetLeaderboardsInterface, EOS_HLeaderboards, eosr::if_leaderboards)
EOSR_INTERFACE_GETTER(EOS_Platform_GetModsInterface, EOS_HMods, eosr::if_mods)
EOSR_INTERFACE_GETTER(EOS_Platform_GetAntiCheatClientInterface, EOS_HAntiCheatClient, eosr::if_anticheatclient)
EOSR_INTERFACE_GETTER(EOS_Platform_GetAntiCheatServerInterface, EOS_HAntiCheatServer, eosr::if_anticheatserver)
EOSR_INTERFACE_GETTER(EOS_Platform_GetProgressionSnapshotInterface, EOS_HProgressionSnapshot, eosr::if_progressionsnapshot)
EOSR_INTERFACE_GETTER(EOS_Platform_GetReportsInterface, EOS_HReports, eosr::if_reports)
EOSR_INTERFACE_GETTER(EOS_Platform_GetSanctionsInterface, EOS_HSanctions, eosr::if_sanctions)
EOSR_INTERFACE_GETTER(EOS_Platform_GetKWSInterface, EOS_HKWS, eosr::if_kws)
EOSR_INTERFACE_GETTER(EOS_Platform_GetCustomInvitesInterface, EOS_HCustomInvites, eosr::if_custominvites)
EOSR_INTERFACE_GETTER(EOS_Platform_GetIntegratedPlatformInterface, EOS_HIntegratedPlatform, eosr::if_integratedplatform)

#undef EOSR_INTERFACE_GETTER
