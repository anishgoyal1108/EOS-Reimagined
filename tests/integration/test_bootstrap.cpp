#include "doctest.h"

#include <string>
#include <vector>

#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_logging.h"
#include "eos_connect.h"
#include "eos_auth.h"
#include "eos_p2p.h"

#include <cstring>

#include "platform/dynlib.h"

using namespace eosr::platform;

// Provided by the integration test main: the path to the built .so/.dll under test.
extern std::string g_library_path;

namespace {

int g_log_count = 0;
void EOS_CALL on_log(const EOS_LogMessage*) { g_log_count++; }

// Connect login/status captures for the real-library exercise.
bool g_conn_login_fired = false;
EOS_EResult g_conn_login_result = EOS_EResult::EOS_UnexpectedError;
EOS_ProductUserId g_conn_login_user = 0;
int g_conn_status_count = 0;
EOS_ELoginStatus g_conn_status_current = EOS_ELoginStatus::EOS_LS_NotLoggedIn;

void EOS_CALL on_connect_login(const EOS_Connect_LoginCallbackInfo* info) {
    g_conn_login_fired = true;
    g_conn_login_result = info->ResultCode;
    g_conn_login_user = info->LocalUserId;
}

void EOS_CALL on_connect_status(const EOS_Connect_LoginStatusChangedCallbackInfo* info) {
    g_conn_status_count++;
    g_conn_status_current = info->CurrentStatus;
}

bool g_conn_logout_fired = false;
void EOS_CALL on_connect_logout(const EOS_Connect_LogoutCallbackInfo*) { g_conn_logout_fired = true; }

bool g_auth_login_fired = false;
EOS_EResult g_auth_login_result = EOS_EResult::EOS_UnexpectedError;
EOS_EpicAccountId g_auth_login_user = 0;
void EOS_CALL on_auth_login(const EOS_Auth_LoginCallbackInfo* info) {
    g_auth_login_fired = true;
    g_auth_login_result = info->ResultCode;
    g_auth_login_user = info->LocalUserId;
}
int g_p2p_request_count = 0;
void EOS_CALL on_p2p_request(const EOS_P2P_OnIncomingConnectionRequestInfo*) { g_p2p_request_count++; }

bool g_auth_logout_fired = false;
EOS_EResult g_auth_logout_result = EOS_EResult::EOS_UnexpectedError;
void EOS_CALL on_auth_logout(const EOS_Auth_LogoutCallbackInfo* info) {
    g_auth_logout_fired = true;
    g_auth_logout_result = info->ResultCode;
}
int g_auth_status_count = 0;
EOS_ELoginStatus g_auth_status_previous = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
EOS_ELoginStatus g_auth_status_current = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
void EOS_CALL on_auth_status(const EOS_Auth_LoginStatusChangedCallbackInfo* info) {
    g_auth_status_count++;
    g_auth_status_previous = info->PrevStatus;
    g_auth_status_current = info->CurrentStatus;
}

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

    // Drive the Connect interface through the real library exactly as a game would: log in,
    // pump ticks until the deferred callback lands, and confirm the roster and notification.
    {
        RESOLVE(fn_get_connect, EOS_Platform_GetConnectInterface);
        RESOLVE(fn_login, EOS_Connect_Login);
        RESOLVE(fn_logout, EOS_Connect_Logout);
        RESOLVE(fn_users_count, EOS_Connect_GetLoggedInUsersCount);
        RESOLVE(fn_user_by_index, EOS_Connect_GetLoggedInUserByIndex);
        RESOLVE(fn_login_status, EOS_Connect_GetLoginStatus);
        RESOLVE(fn_add_notify, EOS_Connect_AddNotifyLoginStatusChanged);
        RESOLVE(fn_remove_notify, EOS_Connect_RemoveNotifyLoginStatusChanged);
        RESOLVE(fn_puid_valid, EOS_ProductUserId_IsValid);

        EOS_HConnect connect = fn_get_connect(platform);
        REQUIRE((connect != nullptr));

        EOS_Connect_AddNotifyLoginStatusChangedOptions notify_options = {};
        notify_options.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
        const EOS_NotificationId notify_id =
            fn_add_notify(connect, &notify_options, nullptr, on_connect_status);
        CHECK(notify_id != 0);

        EOS_Connect_Credentials credentials = {};
        credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
        credentials.Token = "device";
        credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
        EOS_Connect_LoginOptions login_options = {};
        login_options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
        login_options.Credentials = &credentials;
        fn_login(connect, &login_options, nullptr, on_connect_login);

        for (int i = 0; i < 32 && !g_conn_login_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_conn_login_fired);
        CHECK(g_conn_login_result == EOS_EResult::EOS_Success);
        CHECK(fn_puid_valid(g_conn_login_user) == EOS_TRUE);

        CHECK(fn_users_count(connect) == 1);
        CHECK((fn_user_by_index(connect, 0) == g_conn_login_user));
        CHECK(fn_login_status(connect, g_conn_login_user) == EOS_ELoginStatus::EOS_LS_LoggedIn);
        CHECK(g_conn_status_count == 1);
        CHECK(g_conn_status_current == EOS_ELoginStatus::EOS_LS_LoggedIn);

        EOS_Connect_LogoutOptions logout_options = {};
        logout_options.ApiVersion = EOS_CONNECT_LOGOUT_API_LATEST;
        logout_options.LocalUserId = g_conn_login_user;
        fn_logout(connect, &logout_options, nullptr, on_connect_logout);
        for (int i = 0; i < 8 && !g_conn_logout_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_conn_logout_fired);
        CHECK(fn_users_count(connect) == 0);
        CHECK(g_conn_status_count == 2);
        CHECK(g_conn_status_current == EOS_ELoginStatus::EOS_LS_NotLoggedIn);

        fn_remove_notify(connect, notify_id);
    }

    // Drive the Auth interface through the real library: log in, then mint and free the auth and
    // id tokens (the release helpers must actually free, unlike Connect's no-op releases).
    {
        RESOLVE(fn_get_auth, EOS_Platform_GetAuthInterface);
        RESOLVE(fn_auth_login, EOS_Auth_Login);
        RESOLVE(fn_auth_logout, EOS_Auth_Logout);
        RESOLVE(fn_auth_count, EOS_Auth_GetLoggedInAccountsCount);
        RESOLVE(fn_auth_by_index, EOS_Auth_GetLoggedInAccountByIndex);
        RESOLVE(fn_auth_status, EOS_Auth_GetLoginStatus);
        RESOLVE(fn_auth_selected, EOS_Auth_GetSelectedAccountId);
        RESOLVE(fn_copy_token, EOS_Auth_CopyUserAuthToken);
        RESOLVE(fn_token_release, EOS_Auth_Token_Release);
        RESOLVE(fn_copy_id, EOS_Auth_CopyIdToken);
        RESOLVE(fn_id_release, EOS_Auth_IdToken_Release);
        RESOLVE(fn_auth_add_notify, EOS_Auth_AddNotifyLoginStatusChanged);
        RESOLVE(fn_auth_remove_notify, EOS_Auth_RemoveNotifyLoginStatusChanged);
        RESOLVE(fn_eaid_valid, EOS_EpicAccountId_IsValid);

        EOS_HAuth auth = fn_get_auth(platform);
        REQUIRE((auth != nullptr));

        EOS_Auth_Credentials credentials = {};
        credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
        credentials.Token = "code";
        credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
        EOS_Auth_LoginOptions login_options = {};
        login_options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
        login_options.Credentials = &credentials;
        fn_auth_login(auth, &login_options, nullptr, on_auth_login);

        for (int i = 0; i < 32 && !g_auth_login_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_auth_login_fired);
        CHECK(g_auth_login_result == EOS_EResult::EOS_Success);
        CHECK(fn_eaid_valid(g_auth_login_user) == EOS_TRUE);
        CHECK(fn_auth_count(auth) == 1);
        CHECK((fn_auth_by_index(auth, 0) == g_auth_login_user));
        CHECK(fn_auth_status(auth, g_auth_login_user) == EOS_ELoginStatus::EOS_LS_LoggedIn);

        EOS_EpicAccountId selected = 0;
        CHECK(fn_auth_selected(auth, g_auth_login_user, &selected) == EOS_EResult::EOS_Success);
        CHECK((selected == g_auth_login_user));

        // The flat ABI must validate the options object before minting a token.
        EOS_Auth_CopyUserAuthTokenOptions token_options = {};
        token_options.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;
        EOS_Auth_Token* rejected_token = nullptr;
        CHECK(fn_copy_token(auth, nullptr, g_auth_login_user, &rejected_token) ==
              EOS_EResult::EOS_InvalidParameters);
        if (rejected_token != nullptr) {
            fn_token_release(rejected_token);
        }
        token_options.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST + 1;
        rejected_token = nullptr;
        CHECK(fn_copy_token(auth, &token_options, g_auth_login_user, &rejected_token) ==
              EOS_EResult::EOS_VersionMismatch);
        if (rejected_token != nullptr) {
            fn_token_release(rejected_token);
        }
        token_options.ApiVersion = 0;
        rejected_token = nullptr;
        CHECK(fn_copy_token(auth, &token_options, g_auth_login_user, &rejected_token) !=
              EOS_EResult::EOS_Success);
        if (rejected_token != nullptr) {
            fn_token_release(rejected_token);
        }

        token_options.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;
        EOS_Auth_Token* token = nullptr;
        CHECK(fn_copy_token(auth, &token_options, g_auth_login_user, &token) == EOS_EResult::EOS_Success);
        REQUIRE((token != nullptr));
        CHECK((token->AccountId == g_auth_login_user));
        fn_token_release(token);

        EOS_Auth_CopyIdTokenOptions id_options = {};
        id_options.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST;
        id_options.AccountId = g_auth_login_user;
        EOS_Auth_IdToken* rejected_id_token = nullptr;
        CHECK(fn_copy_id(auth, nullptr, &rejected_id_token) == EOS_EResult::EOS_InvalidParameters);
        CHECK((rejected_id_token == nullptr));
        id_options.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST + 1;
        CHECK(fn_copy_id(auth, &id_options, &rejected_id_token) == EOS_EResult::EOS_VersionMismatch);
        if (rejected_id_token != nullptr) {
            fn_id_release(rejected_id_token);
        }
        id_options.ApiVersion = 0;
        rejected_id_token = nullptr;
        CHECK(fn_copy_id(auth, &id_options, &rejected_id_token) != EOS_EResult::EOS_Success);
        if (rejected_id_token != nullptr) {
            fn_id_release(rejected_id_token);
        }

        id_options.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST;
        EOS_Auth_IdToken* id_token = nullptr;
        CHECK(fn_copy_id(auth, &id_options, &id_token) == EOS_EResult::EOS_Success);
        REQUIRE((id_token != nullptr));
        fn_id_release(id_token);

        // Notification registration also has a required, versioned options object.
        const EOS_NotificationId rejected_notification =
            fn_auth_add_notify(auth, nullptr, nullptr, on_auth_status);
        CHECK(rejected_notification == EOS_INVALID_NOTIFICATIONID);
        if (rejected_notification != EOS_INVALID_NOTIFICATIONID) {
            fn_auth_remove_notify(auth, rejected_notification);
        }
        EOS_Auth_AddNotifyLoginStatusChangedOptions notify_options = {};
        notify_options.ApiVersion = EOS_AUTH_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST + 1;
        const EOS_NotificationId rejected_notification_version =
            fn_auth_add_notify(auth, &notify_options, nullptr, on_auth_status);
        CHECK(rejected_notification_version == EOS_INVALID_NOTIFICATIONID);
        if (rejected_notification_version != EOS_INVALID_NOTIFICATIONID) {
            fn_auth_remove_notify(auth, rejected_notification_version);
        }

        notify_options.ApiVersion = EOS_AUTH_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
        const EOS_NotificationId notification =
            fn_auth_add_notify(auth, &notify_options, nullptr, on_auth_status);
        REQUIRE(notification != EOS_INVALID_NOTIFICATIONID);

        EOS_Auth_LogoutOptions logout_options = {};
        logout_options.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST + 1;
        logout_options.LocalUserId = g_auth_login_user;
        g_auth_logout_fired = false;
        g_auth_logout_result = EOS_EResult::EOS_UnexpectedError;
        fn_auth_logout(auth, &logout_options, nullptr, on_auth_logout);
        for (int i = 0; i < 8 && !g_auth_logout_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_auth_logout_fired);
        CHECK(g_auth_logout_result != EOS_EResult::EOS_Success);
        CHECK(fn_auth_count(auth) == 1);

        // Keep teardown valid even when the implementation under review accepted the bad call.
        if (fn_auth_count(auth) == 0) {
            g_auth_login_fired = false;
            fn_auth_login(auth, &login_options, nullptr, on_auth_login);
            for (int i = 0; i < 8 && !g_auth_login_fired; i++) {
                fn_tick(platform);
            }
        }

        logout_options.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
        g_auth_logout_fired = false;
        fn_auth_logout(auth, &logout_options, nullptr, on_auth_logout);
        for (int i = 0; i < 8 && !g_auth_logout_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_auth_logout_fired);
        CHECK(g_auth_logout_result == EOS_EResult::EOS_Success);
        CHECK(fn_auth_count(auth) == 0);
        CHECK(g_auth_status_count == 1);
        CHECK(g_auth_status_previous == EOS_ELoginStatus::EOS_LS_LoggedIn);
        CHECK(g_auth_status_current == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
        fn_auth_remove_notify(auth, notification);
    }

    // Drive the P2P interface through the real library: register a connection-request listener,
    // then send a packet and confirm the synchronous validation contract.
    {
        RESOLVE(fn_get_p2p, EOS_Platform_GetP2PInterface);
        RESOLVE(fn_send, EOS_P2P_SendPacket);
        RESOLVE(fn_next_size, EOS_P2P_GetNextReceivedPacketSize);
        RESOLVE(fn_add_req, EOS_P2P_AddNotifyPeerConnectionRequest);
        RESOLVE(fn_remove_req, EOS_P2P_RemoveNotifyPeerConnectionRequest);
        RESOLVE(fn_get_nat, EOS_P2P_GetNATType);
        RESOLVE(fn_puid_from_string, EOS_ProductUserId_FromString);

        EOS_HP2P p2p = fn_get_p2p(platform);
        REQUIRE((p2p != nullptr));

        EOS_P2P_AddNotifyPeerConnectionRequestOptions notify_options = {};
        notify_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
        const EOS_NotificationId req_id =
            fn_add_req(p2p, &notify_options, nullptr, on_p2p_request);
        CHECK(req_id != 0);

        EOS_ProductUserId remote = fn_puid_from_string("fedcba9876543210fedcba9876543210");
        EOS_P2P_SocketId socket = {};
        socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
        std::strncpy(socket.SocketName, "game", EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
        const unsigned char payload[] = {1, 2, 3, 4};

        EOS_P2P_SendPacketOptions send_options = {};
        send_options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
        send_options.LocalUserId = fn_puid_from_string("00112233445566778899aabbccddeeff");
        send_options.RemoteUserId = remote;
        send_options.SocketId = &socket;
        send_options.Channel = 0;
        send_options.DataLengthBytes = sizeof(payload);
        send_options.Data = payload;
        send_options.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
        send_options.bDisableAutoAcceptConnection = EOS_FALSE;
        CHECK(fn_send(p2p, &send_options) == EOS_EResult::EOS_Success);

        send_options.DataLengthBytes = EOS_P2P_MAX_PACKET_SIZE + 1;
        CHECK(fn_send(p2p, &send_options) == EOS_EResult::EOS_LimitExceeded);

        // No packets have arrived over the (deferred) mesh, so the receive queue is empty.
        EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
        size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
        size_options.LocalUserId = send_options.LocalUserId;
        uint32_t size = 0;
        CHECK(fn_next_size(p2p, &size_options, &size) == EOS_EResult::EOS_NotFound);

        EOS_P2P_GetNATTypeOptions nat_options = {};
        nat_options.ApiVersion = EOS_P2P_GETNATTYPE_API_LATEST;
        EOS_ENATType nat = EOS_ENATType::EOS_NAT_Unknown;
        CHECK(fn_get_nat(p2p, &nat_options, &nat) == EOS_EResult::EOS_Success);
        CHECK(nat == EOS_ENATType::EOS_NAT_Open);

        fn_remove_req(p2p, req_id);
    }

    // Release, then confirm stale-handle calls degrade to safe no-ops.
    fn_release(platform);
    fn_tick(platform);
    pfn_getter connect_getter =
        reinterpret_cast<pfn_getter>(lib.symbol("EOS_Platform_GetConnectInterface"));
    CHECK((connect_getter(platform) == nullptr));
    fn_release(platform); // a second release is harmless

    // Recreate: the new handle differs from the released one, and the stale handle stays
    // rejected even though the allocator might otherwise have reused the address.
    EOS_HPlatform platform2 = fn_create(&popts);
    REQUIRE((platform2 != nullptr));
    CHECK((platform2 != platform));
    CHECK((connect_getter(platform) == nullptr));
    CHECK((connect_getter(platform2) != nullptr));
    fn_release(platform2);

    // Shut down, then distinguish a repeated shutdown from a never-configured client.
    CHECK(fn_shutdown() == EOS_EResult::EOS_Success);
    CHECK(fn_shutdown() == EOS_EResult::EOS_UnexpectedError);
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

    // FromString performs no format validation, as required by the EOS contract.
    EOS_ProductUserId null_id = fn_puid_from_string("00000000000000000000000000000000");
    CHECK(fn_puid_is_valid(null_id) == EOS_TRUE);

    const char* serialized_id = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
    EOS_ProductUserId serialized = fn_puid_from_string(serialized_id);
    REQUIRE((serialized != nullptr));
    CHECK(fn_puid_is_valid(serialized) == EOS_TRUE);
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_puid_to_string(serialized, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == serialized_id);

    // A too-small buffer is rejected and reports the required size.
    int32_t small = 4;
    CHECK(fn_puid_to_string(puid, buffer, &small) == EOS_EResult::EOS_LimitExceeded);
    CHECK(small == 33); // 32 characters plus the null terminator
}

TEST_CASE("the dynamic library wrapper handles failures and repeated cleanup") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    CHECK_FALSE(lib.is_open());
    CHECK_FALSE(lib.open(0));
    CHECK_FALSE(lib.is_open());
    CHECK((lib.symbol("EOS_Initialize") == nullptr));

    CHECK_FALSE(lib.open("this-library-does-not-exist"));
    CHECK_FALSE(lib.is_open());

    REQUIRE(lib.open(g_library_path.c_str()));
    CHECK(lib.is_open());
    CHECK((lib.symbol("EOS_Initialize") != nullptr));
    CHECK((lib.symbol("EOS_SymbolThatDoesNotExist") == nullptr));

    lib.close();
    lib.close();
    CHECK_FALSE(lib.is_open());
    CHECK((lib.symbol("EOS_Initialize") == nullptr));
}
