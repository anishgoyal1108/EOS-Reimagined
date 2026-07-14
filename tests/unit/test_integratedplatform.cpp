#include "doctest.h"

#include <string>

#include "eos_integratedplatform_types.h"

#include "core/callback_manager.h"
#include "interfaces/integratedplatform.h"

using namespace eosr;

namespace {

int g_status_count;
std::string g_status_platform;
std::string g_status_user;
EOS_ELoginStatus g_status_previous;
EOS_ELoginStatus g_status_current;

void reset_captures() {
    g_status_count = 0;
    g_status_platform.clear();
    g_status_user.clear();
    g_status_previous = EOS_ELoginStatus::EOS_LS_LoggedIn;
    g_status_current = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

void EOS_CALL on_status(const EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo* info) {
    g_status_count++;
    g_status_platform = (info->PlatformType != 0) ? info->PlatformType : "";
    g_status_user = (info->LocalPlatformUserId != 0) ? info->LocalPlatformUserId : "";
    g_status_previous = info->PreviousLoginStatus;
    g_status_current = info->CurrentLoginStatus;
}

// The handler returns the game's verdict on the logout: process it now, or defer it and finalize
// later. Ours is never asked, because no platform ever tells us a user signed out.
EOS_EIntegratedPlatformPreLogoutAction EOS_CALL
on_pre_logout(const EOS_IntegratedPlatform_UserPreLogoutCallbackInfo*) {
    return EOS_EIntegratedPlatformPreLogoutAction::EOS_IPLA_ProcessLogoutImmediately;
}

struct integrated_fixture {
    callback_manager callbacks;
    sdk_integrated_platform platform;

    integrated_fixture() : platform(callbacks) {
        platform.emu_init();
        reset_captures();
    }
    ~integrated_fixture() { platform.emu_deinit(); }

    // Register a platform the way a game does, by building a container and handing over its entries.
    void configure(const char* type, EOS_EIntegratedPlatformManagementFlags flags) {
        std::vector<integrated_platform_entry> entries;
        integrated_platform_entry entry;
        entry.type = type;
        entry.flags = flags;
        entries.push_back(entry);
        platform.configure(entries);
    }

    EOS_NotificationId listen() {
        EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions options = {};
        options.ApiVersion = EOS_INTEGRATEDPLATFORM_ADDNOTIFYUSERLOGINSTATUSCHANGED_API_LATEST;
        return platform.add_notify_user_login_status_changed(&options, 0, on_status);
    }

    EOS_EResult set_status(const char* type, const char* user, EOS_ELoginStatus status) {
        EOS_IntegratedPlatform_SetUserLoginStatusOptions options = {};
        options.ApiVersion = EOS_INTEGRATEDPLATFORM_SETUSERLOGINSTATUS_API_LATEST;
        options.PlatformType = type;
        options.LocalPlatformUserId = user;
        options.CurrentLoginStatus = status;
        return platform.set_user_login_status(&options);
    }
};

EOS_IntegratedPlatform_Options steam_options(EOS_EIntegratedPlatformManagementFlags flags) {
    EOS_IntegratedPlatform_Options options = {};
    options.ApiVersion = EOS_INTEGRATEDPLATFORM_OPTIONS_API_LATEST;
    options.Type = EOS_IPT_Steam;
    options.Flags = flags;
    return options;
}

} // namespace

// The container is created before any platform exists -- it is what a platform is created *from* --
// and the game releases it afterwards. So it has to be a real object with real ownership: a handle
// we minted, that we can find again, and that a stale or double release does not turn into a free of
// somebody else's memory.
TEST_CASE("an options container is owned, found, and released exactly once") {
    EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions create = {};
    create.ApiVersion =
        EOS_INTEGRATEDPLATFORM_CREATEINTEGRATEDPLATFORMOPTIONSCONTAINER_API_LATEST;

    EOS_HIntegratedPlatformOptionsContainer handle = 0;
    REQUIRE(create_integrated_platform_container(&create, &handle) == EOS_EResult::EOS_Success);
    REQUIRE(handle != 0);
    REQUIRE((find_integrated_platform_container(handle) != 0));

    // A handle we never minted is not ours to hand back or to free.
    EOS_HIntegratedPlatformOptionsContainer foreign =
        reinterpret_cast<EOS_HIntegratedPlatformOptionsContainer>(0xdeadbeef);
    CHECK((find_integrated_platform_container(foreign) == 0));
    release_integrated_platform_container(foreign); // must not free anything
    CHECK((find_integrated_platform_container(handle) != 0));

    release_integrated_platform_container(handle);
    CHECK((find_integrated_platform_container(handle) == 0));
    // Releasing it again changes nothing, rather than freeing memory that is no longer ours.
    release_integrated_platform_container(handle);
    CHECK((find_integrated_platform_container(handle) == 0));

    // The out-handle is nulled even when the call fails, so a caller cannot use a stale one.
    EOS_HIntegratedPlatformOptionsContainer stale =
        reinterpret_cast<EOS_HIntegratedPlatformOptionsContainer>(0x1);
    create.ApiVersion = 0;
    CHECK(create_integrated_platform_container(&create, &stale) ==
          EOS_EResult::EOS_InvalidParameters);
    CHECK(stale == 0);
}

// One entry per platform. A second entry for the same platform is not a refinement of the first, it
// is two answers to one question.
TEST_CASE("a container takes one entry per platform and refuses a duplicate") {
    integrated_platform_container container;

    EOS_IntegratedPlatform_Options steam =
        steam_options(EOS_EIntegratedPlatformManagementFlags::EOS_IPMF_LibraryManagedBySDK);
    EOS_IntegratedPlatformOptionsContainer_AddOptions add = {};
    add.ApiVersion = EOS_INTEGRATEDPLATFORMOPTIONSCONTAINER_ADD_API_LATEST;
    add.Options = &steam;

    CHECK(container.add(&add) == EOS_EResult::EOS_Success);
    REQUIRE(container.entries().size() == 1);
    CHECK(container.entries()[0].type == std::string(EOS_IPT_Steam));

    CHECK(container.add(&add) == EOS_EResult::EOS_DuplicateNotAllowed);
    CHECK(container.entries().size() == 1);

    // A malformed entry leaves no trace.
    EOS_IntegratedPlatform_Options nameless = steam;
    nameless.Type = 0;
    add.Options = &nameless;
    CHECK(container.add(&add) == EOS_EResult::EOS_InvalidParameters);
    add.Options = 0;
    CHECK(container.add(&add) == EOS_EResult::EOS_InvalidParameters);
    CHECK(container.entries().size() == 1);
}

// SetUserLoginStatus is the half of this interface that runs *from* the game, and it needs no Steam
// at all -- only somewhere to keep the answer and a notification to fire. So we honour it properly.
TEST_CASE("the application can set an integrated user's login status, and is told when it changes") {
    integrated_fixture fx;
    fx.configure(EOS_IPT_Steam, EOS_EIntegratedPlatformManagementFlags::
                                    EOS_IPMF_ApplicationManagedIdentityLogin);
    REQUIRE(fx.listen() != EOS_INVALID_NOTIFICATIONID);

    CHECK(fx.set_status(EOS_IPT_Steam, "76561198000000000",
                        EOS_ELoginStatus::EOS_LS_LoggedIn) == EOS_EResult::EOS_Success);
    CHECK(g_status_count == 0); // never synchronously
    fx.callbacks.tick();

    REQUIRE(g_status_count == 1);
    CHECK(g_status_platform == std::string(EOS_IPT_Steam));
    CHECK(g_status_user == "76561198000000000");
    // A user we had never heard of was not logged in as far as we knew.
    CHECK(g_status_previous == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
    CHECK(g_status_current == EOS_ELoginStatus::EOS_LS_LoggedIn);

    // The header is explicit: an unchanged status does nothing, succeeds, and does not notify.
    CHECK(fx.set_status(EOS_IPT_Steam, "76561198000000000",
                        EOS_ELoginStatus::EOS_LS_LoggedIn) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();
    CHECK(g_status_count == 1);

    // A real change does notify, and carries the status it came from.
    CHECK(fx.set_status(EOS_IPT_Steam, "76561198000000000",
                        EOS_ELoginStatus::EOS_LS_NotLoggedIn) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();
    REQUIRE(g_status_count == 2);
    CHECK(g_status_previous == EOS_ELoginStatus::EOS_LS_LoggedIn);
    CHECK(g_status_current == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
}

// The three ways this call is not the game's to make, each with its own word. Collapsing them would
// leave a game unable to tell "you never asked for Steam" from "you asked, but you told us the SDK
// would own the identity".
TEST_CASE("setting a login status says exactly why it cannot") {
    integrated_fixture fx;

    // Nothing was registered at platform creation at all.
    CHECK(fx.set_status(EOS_IPT_Steam, "user", EOS_ELoginStatus::EOS_LS_LoggedIn) ==
          EOS_EResult::EOS_NotConfigured);

    // Registered, but the game said the SDK would manage the identity -- so this is not its to set.
    fx.configure(EOS_IPT_Steam,
                 EOS_EIntegratedPlatformManagementFlags::EOS_IPMF_LibraryManagedBySDK);
    CHECK(fx.set_status(EOS_IPT_Steam, "user", EOS_ELoginStatus::EOS_LS_LoggedIn) ==
          EOS_EResult::EOS_InvalidState);

    // Registered and application-managed, but no user named.
    fx.configure(EOS_IPT_Steam, EOS_EIntegratedPlatformManagementFlags::
                                    EOS_IPMF_ApplicationManagedIdentityLogin);
    CHECK(fx.set_status(EOS_IPT_Steam, 0, EOS_ELoginStatus::EOS_LS_LoggedIn) ==
          EOS_EResult::EOS_InvalidUser);

    // A platform that was never registered stays NotConfigured even once another one is.
    CHECK(fx.set_status("PSN", "user", EOS_ELoginStatus::EOS_LS_LoggedIn) ==
          EOS_EResult::EOS_NotConfigured);

    CHECK(fx.platform.set_user_login_status(0) == EOS_EResult::EOS_InvalidParameters);
}

// A pre-logout handler exists so a game can veto a logout an integrated platform told us about. No
// platform ever tells us anything, so it binds, unbinds, and never fires -- but the binding itself
// is real, and the header says there can only ever be one.
TEST_CASE("only one pre-logout handler may be bound at a time") {
    integrated_fixture fx;
    EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions set = {};
    set.ApiVersion = EOS_INTEGRATEDPLATFORM_SETUSERPRELOGOUTCALLBACK_API_LATEST;

    CHECK(fx.platform.set_user_pre_logout_callback(&set, 0, on_pre_logout) ==
          EOS_EResult::EOS_Success);
    CHECK(fx.platform.set_user_pre_logout_callback(&set, 0, on_pre_logout) ==
          EOS_EResult::EOS_AlreadyConfigured);

    EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions clear = {};
    clear.ApiVersion = EOS_INTEGRATEDPLATFORM_CLEARUSERPRELOGOUTCALLBACK_API_LATEST;
    fx.platform.clear_user_pre_logout_callback(&clear);

    // Cleared, so it can be bound again.
    CHECK(fx.platform.set_user_pre_logout_callback(&set, 0, on_pre_logout) ==
          EOS_EResult::EOS_Success);
}

// A deferred logout is one we started because an integrated platform told us a user signed out.
// Nothing ever tells us, so there is never one waiting -- and the header already has the word for a
// finalize with nothing to finalize.
TEST_CASE("there is never a deferred logout waiting to be finalized") {
    integrated_fixture fx;
    EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions options = {};
    options.ApiVersion = EOS_INTEGRATEDPLATFORM_FINALIZEDEFERREDUSERLOGOUT_API_LATEST;
    options.PlatformType = EOS_IPT_Steam;
    options.LocalPlatformUserId = "user";
    options.ExpectedLoginStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;

    // Not registered at all is a different answer from registered-but-nothing-pending.
    CHECK(fx.platform.finalize_deferred_user_logout(&options) == EOS_EResult::EOS_NotConfigured);

    fx.configure(EOS_IPT_Steam,
                 EOS_EIntegratedPlatformManagementFlags::EOS_IPMF_LibraryManagedByApplication);
    CHECK(fx.platform.finalize_deferred_user_logout(&options) == EOS_EResult::EOS_InvalidUser);

    CHECK(fx.platform.finalize_deferred_user_logout(0) == EOS_EResult::EOS_InvalidParameters);
}
