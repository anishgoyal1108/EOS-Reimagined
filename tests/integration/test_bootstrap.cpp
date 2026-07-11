#include "doctest.h"

#include <string>
#include <vector>

#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_logging.h"

#include "platform/dynlib.h"

using namespace eosr::platform;

// Provided by the integration test main: the path to the built .so/.dll under test.
extern std::string g_library_path;

namespace {

int g_log_count = 0;
void EOS_CALL on_log(const EOS_LogMessage*) { g_log_count++; }

// Every interface getter shares one ABI shape: (EOS_HPlatform) -> opaque pointer.
typedef void* (EOS_CALL* pfn_getter)(EOS_HPlatform);

const char* const getter_names[] = {
    "EOS_Platform_GetMetricsInterface",
    "EOS_Platform_GetAuthInterface",
    "EOS_Platform_GetConnectInterface",
    "EOS_Platform_GetEcomInterface",
    "EOS_Platform_GetUIInterface",
    "EOS_Platform_GetFriendsInterface",
    "EOS_Platform_GetPresenceInterface",
    "EOS_Platform_GetSessionsInterface",
    "EOS_Platform_GetLobbyInterface",
    "EOS_Platform_GetUserInfoInterface",
    "EOS_Platform_GetP2PInterface",
    "EOS_Platform_GetRTCInterface",
    "EOS_Platform_GetRTCAdminInterface",
    "EOS_Platform_GetPlayerDataStorageInterface",
    "EOS_Platform_GetTitleStorageInterface",
    "EOS_Platform_GetAchievementsInterface",
    "EOS_Platform_GetStatsInterface",
    "EOS_Platform_GetLeaderboardsInterface",
    "EOS_Platform_GetModsInterface",
    "EOS_Platform_GetAntiCheatClientInterface",
    "EOS_Platform_GetAntiCheatServerInterface",
    "EOS_Platform_GetProgressionSnapshotInterface",
    "EOS_Platform_GetReportsInterface",
    "EOS_Platform_GetSanctionsInterface",
    "EOS_Platform_GetKWSInterface",
    "EOS_Platform_GetCustomInvitesInterface",
    "EOS_Platform_GetIntegratedPlatformInterface"
};
const int getter_count = static_cast<int>(sizeof(getter_names) / sizeof(getter_names[0]));

} // namespace

// Resolve an export into a variable typed from its real declaration, and require it exists.
// A missing symbol means the library failed to export a required entry point.
#define RESOLVE(var, api_name) \
    auto var = reinterpret_cast<decltype(&api_name)>(lib.symbol(#api_name)); \
    REQUIRE((var != nullptr))

TEST_CASE("the built SDK library drives the whole bootstrap sequence") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_tick, EOS_Platform_Tick);
    RESOLVE(fn_check_launcher, EOS_Platform_CheckForLauncherAndRestart);
    RESOLVE(fn_set_callback, EOS_Logging_SetCallback);
    RESOLVE(fn_set_level, EOS_Logging_SetLogLevel);

    // Logging is rejected until the SDK is initialized.
    CHECK(fn_set_callback(on_log) == EOS_EResult::EOS_NotConfigured);

    // Initialize, then reject a second initialize.
    EOS_InitializeOptions iopts = {};
    iopts.ApiVersion = EOS_INITIALIZE_API_LATEST;
    iopts.ProductName = "BootstrapTest";
    iopts.ProductVersion = "1.0.0";
    CHECK(fn_initialize(&iopts) == EOS_EResult::EOS_Success);
    CHECK(fn_initialize(&iopts) == EOS_EResult::EOS_AlreadyConfigured);

    // Logging is now configurable; raise the level and start capturing.
    CHECK(fn_set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Verbose)
          == EOS_EResult::EOS_Success);
    CHECK(fn_set_callback(on_log) == EOS_EResult::EOS_Success);
    g_log_count = 0;

    // A null options pointer yields a null platform.
    CHECK((fn_create(0) == nullptr));

    // Create the platform.
    EOS_Platform_Options popts = {};
    popts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    popts.ProductId = "prod-abc";
    popts.SandboxId = "sandbox-1";
    popts.DeploymentId = "deploy-2";
    popts.ClientCredentials.ClientId = "client";
    popts.ClientCredentials.ClientSecret = "secret";
    EOS_HPlatform platform = fn_create(&popts);
    REQUIRE((platform != nullptr));
    CHECK(g_log_count > 0); // create emitted at least one log message

    // Every interface getter is exported, returns non-null, and is distinct.
    std::vector<void*> handles;
    for (int i = 0; i < getter_count; i++) {
        pfn_getter getter = reinterpret_cast<pfn_getter>(lib.symbol(getter_names[i]));
        REQUIRE((getter != nullptr));
        void* iface = getter(platform);
        CHECK((iface != nullptr));
        handles.push_back(iface);
    }
    for (std::size_t i = 0; i < handles.size(); i++) {
        for (std::size_t j = i + 1; j < handles.size(); j++) {
            CHECK((handles[i] != handles[j]));
        }
    }

    // We are never launched by the Epic launcher, so this must not tell the game to restart.
    CHECK(fn_check_launcher(platform) == EOS_EResult::EOS_NoChange);

    // Tick is the async pump; it must stay stable across repeated calls with no work queued.
    for (int i = 0; i < 8; i++) {
        fn_tick(platform);
    }

    // Release, then confirm stale-handle calls degrade to safe no-ops.
    fn_release(platform);
    fn_tick(platform);
    pfn_getter connect_getter =
        reinterpret_cast<pfn_getter>(lib.symbol("EOS_Platform_GetConnectInterface"));
    CHECK((connect_getter(platform) == nullptr));
    fn_release(platform); // a second release is harmless

    // Shut down, then reject a second shutdown.
    CHECK(fn_shutdown() == EOS_EResult::EOS_Success);
    CHECK(fn_shutdown() == EOS_EResult::EOS_NotConfigured);
}

TEST_CASE("the built SDK library exposes the common id and result helpers") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_result_to_string, EOS_EResult_ToString);
    RESOLVE(fn_puid_from_string, EOS_ProductUserId_FromString);
    RESOLVE(fn_puid_to_string, EOS_ProductUserId_ToString);
    RESOLVE(fn_puid_is_valid, EOS_ProductUserId_IsValid);

    CHECK(std::string(fn_result_to_string(EOS_EResult::EOS_Success)) == "EOS_Success");
    CHECK(std::string(fn_result_to_string(EOS_EResult::EOS_NotConfigured)) == "EOS_NotConfigured");

    // A well-formed id round-trips and validates.
    const char* id = "0123456789abcdef0123456789abcdef";
    EOS_ProductUserId puid = fn_puid_from_string(id);
    REQUIRE((puid != nullptr));
    CHECK(fn_puid_is_valid(puid) == EOS_TRUE);

    char buffer[64];
    int32_t length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_puid_to_string(puid, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == id);

    // Interning is stable: the same string resolves to the same handle.
    CHECK((fn_puid_from_string(id) == puid));

    // The all-zero id is the null sentinel and is not valid.
    EOS_ProductUserId null_id = fn_puid_from_string("00000000000000000000000000000000");
    CHECK(fn_puid_is_valid(null_id) == EOS_FALSE);

    // A too-small buffer is rejected and reports the required size.
    int32_t small = 4;
    CHECK(fn_puid_to_string(puid, buffer, &small) == EOS_EResult::EOS_LimitExceeded);
    CHECK(small == 33); // 32 characters plus the null terminator
}
