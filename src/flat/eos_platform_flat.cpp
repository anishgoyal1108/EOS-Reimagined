// Flat C ABI trampolines for the platform: lifecycle, the tick pump, and the interface
// getters. Each forwards to the one process-global sdk_platform after validating the handle.
#include <cstring>
#include <string>

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

// The EOS out-buffer contract for a plain string: a buffer too small is LimitExceeded with the
// length it would have needed, never a truncation the caller cannot detect.
EOS_EResult copy_out(const std::string& text, char* out_buffer, int32_t* in_out_length) {
    if (out_buffer == 0 || in_out_length == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const int32_t needed = static_cast<int32_t>(text.size()) + 1; // room for the null
    if (*in_out_length < needed) {
        *in_out_length = needed;
        return EOS_EResult::EOS_LimitExceeded;
    }
    std::memcpy(out_buffer, text.c_str(), static_cast<std::size_t>(needed));
    *in_out_length = needed;
    return EOS_EResult::EOS_Success;
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

// --- Application and network state ---
//
// A game tells the SDK when it is backgrounded or has lost the network, so the SDK can throttle what
// it sends to Epic. There is nothing to throttle here, but the game reads these values back, and one
// that suspends itself and sees no change has reason to think the SDK is not listening.

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetApplicationStatus(EOS_HPlatform Handle,
                                                                const EOS_EApplicationStatus NewStatus) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    return (platform != 0) ? platform->set_application_status(NewStatus)
                           : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EApplicationStatus) EOS_Platform_GetApplicationStatus(EOS_HPlatform Handle) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    return (platform != 0) ? platform->application_status()
                           : EOS_EApplicationStatus::EOS_AS_Foreground;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetNetworkStatus(EOS_HPlatform Handle,
                                                            const EOS_ENetworkStatus NewStatus) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    return (platform != 0) ? platform->set_network_status(NewStatus)
                           : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_ENetworkStatus) EOS_Platform_GetNetworkStatus(EOS_HPlatform Handle) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    return (platform != 0) ? platform->network_status() : EOS_ENetworkStatus::EOS_NS_Online;
}

// --- Country and locale ---
//
// These exist to be sent to services that localize a storefront. We have no storefront and no
// service, so they are only ever what the game itself put there. The header says so too: "This is
// not currently used for anything internally."

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetOverrideCountryCode(EOS_HPlatform Handle,
                                                                  const char* NewCountryCode) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0 || NewCountryCode == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // An overlong code is an invalid code, which is what the setter's contract calls it. LimitExceeded
    // is the getters' word, and it means something a caller can act on -- "your buffer was too small,
    // here is the size" -- which is not what happened here.
    if (std::strlen(NewCountryCode) >= EOS_COUNTRYCODE_MAX_LENGTH) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    platform->settings().set_override_country(NewCountryCode);
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetOverrideCountryCode(EOS_HPlatform Handle,
                                                                  char* OutBuffer,
                                                                  int32_t* InOutBufferLength) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return copy_out(platform->settings().override_country(), OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetOverrideLocaleCode(EOS_HPlatform Handle,
                                                                 const char* NewLocaleCode) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0 || NewLocaleCode == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (std::strlen(NewLocaleCode) >= EOS_LOCALECODE_MAX_LENGTH) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    platform->settings().set_override_locale(NewLocaleCode);
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetOverrideLocaleCode(EOS_HPlatform Handle,
                                                                 char* OutBuffer,
                                                                 int32_t* InOutBufferLength) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return copy_out(platform->settings().override_locale(), OutBuffer, InOutBufferLength);
}

// The active code is the override, and there is nothing else it could be: an account we could look
// one up from is exactly the thing an emulator does not have. The header already says NotFound is
// the answer when there is no override, so it is the honest one here.
EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetActiveCountryCode(EOS_HPlatform Handle,
                                                                EOS_EpicAccountId /*LocalUserId*/,
                                                                char* OutBuffer,
                                                                int32_t* InOutBufferLength) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0 || OutBuffer == 0 || InOutBufferLength == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string& country = platform->settings().override_country();
    if (country.empty()) {
        return EOS_EResult::EOS_NotFound;
    }
    return copy_out(country, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetActiveLocaleCode(EOS_HPlatform Handle,
                                                               EOS_EpicAccountId /*LocalUserId*/,
                                                               char* OutBuffer,
                                                               int32_t* InOutBufferLength) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0 || OutBuffer == 0 || InOutBufferLength == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string& locale = platform->settings().override_locale();
    if (locale.empty()) {
        return EOS_EResult::EOS_NotFound;
    }
    return copy_out(locale, OutBuffer, InOutBufferLength);
}

// The header says plainly that this is Windows only and answers EOS_NotImplemented anywhere else.
// Answering Success on Linux would hand a Linux game a Windows bootstrap state, which is an answer
// to a question it never asked, and one it may act on.
//
// On Windows we say the prerequisites are met, and that is a deliberate lie of the same kind as
// every other one this library tells. The header is explicit that desktop crossplay "is required to
// use Epic accounts login with applications that are distributed outside the Epic Games Store" --
// and a game that has had our library dropped into it is, by definition, distributed outside the
// Epic Games Store. Reporting ApplicationNotBootstrapped would be the descriptively honest answer
// about a bootstrapper we do not have, and it is a documented way for a game to gate the player out
// of Epic login and so out of multiplayer altogether: the exact thing this library exists to keep
// working. The prerequisites it is really asking after are "can I go online", and with us, it can.
//
// This is the same call CheckForLauncherAndRestart makes a few lines up: we are never launched by
// the Epic launcher either, and we say so in the way that lets the game carry on rather than the way
// that makes it quit.
EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetDesktopCrossplayStatus(
    EOS_HPlatform Handle, const EOS_Platform_GetDesktopCrossplayStatusOptions* Options,
    EOS_Platform_DesktopCrossplayStatusInfo* OutDesktopCrossplayStatusInfo) {
    eosr::sdk_platform* platform = checked_platform(Handle);
    if (platform == 0 || Options == 0 || OutDesktopCrossplayStatusInfo == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (Options->ApiVersion <= 0 ||
        Options->ApiVersion > EOS_PLATFORM_GETDESKTOPCROSSPLAYSTATUS_API_LATEST) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
#if defined(_WIN32)
    OutDesktopCrossplayStatusInfo->Status = EOS_EDesktopCrossplayStatus::EOS_DCS_OK;
    // Every field of an out-struct is ours to write. This one only carries meaning when the status
    // is ServiceStartFailed, which ours never is -- but a game is told to put it in its logs and its
    // error screens, so leaving it as whatever the game happened to have there is not an option. The
    // reference emulator writes -1, and so do we.
    OutDesktopCrossplayStatusInfo->ServiceInitResult = -1;
    return EOS_EResult::EOS_Success;
#else
    return EOS_EResult::EOS_NotImplemented;
#endif
}
