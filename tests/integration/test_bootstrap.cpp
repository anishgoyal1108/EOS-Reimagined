#include "doctest.h"

#include <limits>
#include <string>
#include <vector>

#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_logging.h"
#include "eos_connect.h"
#include "eos_auth.h"
#include "eos_lobby.h"
#include "eos_ecom.h"
#include "eos_achievements.h"
#include "eos_stats.h"
#include "eos_p2p.h"
#include "eos_integratedplatform.h"
#include "eos_version.h"

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
bool g_p2p_nat_fired = false;
EOS_EResult g_p2p_nat_result = EOS_EResult::EOS_UnexpectedError;
EOS_ENATType g_p2p_nat_type = EOS_ENATType::EOS_NAT_Unknown;
void EOS_CALL on_p2p_nat(const EOS_P2P_OnQueryNATTypeCompleteInfo* info) {
    g_p2p_nat_fired = true;
    g_p2p_nat_result = info->ResultCode;
    g_p2p_nat_type = info->NATType;
}
void EOS_CALL on_p2p_queue_full(const EOS_P2P_OnIncomingPacketQueueFullInfo*) {
}

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

// Review regression: the public contract says both outputs are null on error. The core methods do
// that, but the flat trampolines used to return early for a bad parent handle and leave stale values.
TEST_CASE("Lobby flat ABI nulls handle outputs when the parent handle is invalid") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_get_lobby, EOS_Platform_GetLobbyInterface);
    RESOLVE(fn_create_search, EOS_Lobby_CreateLobbySearch);
    RESOLVE(fn_copy_details, EOS_Lobby_CopyLobbyDetailsHandle);

    EOS_InitializeOptions initialize = {};
    initialize.ApiVersion = EOS_INITIALIZE_API_LATEST;
    initialize.ProductName = "LobbyReview";
    initialize.ProductVersion = "1.0";
    REQUIRE(fn_initialize(&initialize) == EOS_EResult::EOS_Success);

    EOS_Platform_Options platform_options = {};
    platform_options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform_options.ProductId = "lobby-review";
    platform_options.SandboxId = "sandbox";
    platform_options.DeploymentId = "deployment";
    platform_options.ClientCredentials.ClientId = "client";
    platform_options.ClientCredentials.ClientSecret = "secret";
    EOS_HPlatform platform = fn_create(&platform_options);
    REQUIRE((platform != nullptr));
    REQUIRE((fn_get_lobby(platform) != nullptr));

    EOS_HLobby bad_lobby = reinterpret_cast<EOS_HLobby>(0xdeadbeef);
    EOS_Lobby_CreateLobbySearchOptions search_options = {};
    search_options.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    search_options.MaxResults = 1;
    EOS_HLobbySearch search = reinterpret_cast<EOS_HLobbySearch>(0x1);
    CHECK(fn_create_search(bad_lobby, &search_options, &search) ==
          EOS_EResult::EOS_InvalidParameters);
    CHECK((search == nullptr));

    EOS_Lobby_CopyLobbyDetailsHandleOptions copy_options = {};
    copy_options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    copy_options.LobbyId = "missing";
    EOS_HLobbyDetails details = reinterpret_cast<EOS_HLobbyDetails>(0x1);
    CHECK(fn_copy_details(bad_lobby, &copy_options, &details) ==
          EOS_EResult::EOS_InvalidParameters);
    CHECK((details == nullptr));

    fn_release(platform);
    CHECK(fn_shutdown() == EOS_EResult::EOS_Success);
}

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
        RESOLVE(fn_query_nat, EOS_P2P_QueryNATType);
        RESOLVE(fn_get_nat, EOS_P2P_GetNATType);
        RESOLVE(fn_set_relay, EOS_P2P_SetRelayControl);
        RESOLVE(fn_get_relay, EOS_P2P_GetRelayControl);
        RESOLVE(fn_set_port, EOS_P2P_SetPortRange);
        RESOLVE(fn_get_port, EOS_P2P_GetPortRange);
        RESOLVE(fn_set_queue, EOS_P2P_SetPacketQueueSize);
        RESOLVE(fn_get_queue, EOS_P2P_GetPacketQueueInfo);
        RESOLVE(fn_add_queue_full, EOS_P2P_AddNotifyIncomingPacketQueueFull);
        RESOLVE(fn_remove_queue_full, EOS_P2P_RemoveNotifyIncomingPacketQueueFull);
        RESOLVE(fn_clear_queue, EOS_P2P_ClearPacketQueue);
        RESOLVE(fn_puid_from_string, EOS_ProductUserId_FromString);

        EOS_HP2P p2p = fn_get_p2p(platform);
        REQUIRE((p2p != nullptr));

        EOS_ProductUserId local = g_conn_login_user;
        EOS_ProductUserId remote = fn_puid_from_string("fedcba9876543210fedcba9876543210");

        EOS_P2P_AddNotifyPeerConnectionRequestOptions notify_options = {};
        notify_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
        const EOS_NotificationId rejected_local =
            fn_add_req(p2p, &notify_options, nullptr, on_p2p_request);
        CHECK(rejected_local == EOS_INVALID_NOTIFICATIONID);
        if (rejected_local != EOS_INVALID_NOTIFICATIONID) {
            fn_remove_req(p2p, rejected_local);
        }

        EOS_P2P_SocketId invalid_socket = {};
        invalid_socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
        std::strncpy(invalid_socket.SocketName, "bad/socket",
                     EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
        notify_options.LocalUserId = local;
        notify_options.SocketId = &invalid_socket;
        const EOS_NotificationId rejected_socket =
            fn_add_req(p2p, &notify_options, nullptr, on_p2p_request);
        CHECK(rejected_socket == EOS_INVALID_NOTIFICATIONID);
        if (rejected_socket != EOS_INVALID_NOTIFICATIONID) {
            fn_remove_req(p2p, rejected_socket);
        }

        notify_options.SocketId = nullptr;
        const EOS_NotificationId req_id =
            fn_add_req(p2p, &notify_options, nullptr, on_p2p_request);
        CHECK(req_id != 0);

        EOS_P2P_SocketId socket = {};
        socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
        std::strncpy(socket.SocketName, "game", EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
        const unsigned char payload[] = {1, 2, 3, 4};

        EOS_P2P_SendPacketOptions send_options = {};
        send_options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
        send_options.LocalUserId = local;
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

        // GetNATType has no cached value until a successful query completes.
        EOS_P2P_GetNATTypeOptions nat_options = {};
        nat_options.ApiVersion = EOS_P2P_GETNATTYPE_API_LATEST;
        EOS_ENATType nat = EOS_ENATType::EOS_NAT_Unknown;
        CHECK(fn_get_nat(p2p, &nat_options, &nat) == EOS_EResult::EOS_NotFound);

        CHECK(fn_get_nat(p2p, nullptr, &nat) == EOS_EResult::EOS_InvalidParameters);
        g_p2p_nat_fired = false;
        fn_query_nat(p2p, nullptr, nullptr, on_p2p_nat);
        for (int i = 0; i < 8 && !g_p2p_nat_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_p2p_nat_fired);
        CHECK(g_p2p_nat_result == EOS_EResult::EOS_InvalidParameters);

        EOS_P2P_QueryNATTypeOptions query_options = {};
        query_options.ApiVersion = EOS_P2P_QUERYNATTYPE_API_LATEST;
        g_p2p_nat_fired = false;
        g_p2p_nat_result = EOS_EResult::EOS_UnexpectedError;
        fn_query_nat(p2p, &query_options, nullptr, on_p2p_nat);
        for (int i = 0; i < 8 && !g_p2p_nat_fired; i++) {
            fn_tick(platform);
        }
        CHECK(g_p2p_nat_fired);
        CHECK(g_p2p_nat_result == EOS_EResult::EOS_Success);
        CHECK(g_p2p_nat_type == EOS_ENATType::EOS_NAT_Open);
        CHECK(fn_get_nat(p2p, &nat_options, &nat) == EOS_EResult::EOS_Success);
        CHECK(nat == EOS_ENATType::EOS_NAT_Open);

        // Setters that report success must be observable through their paired getters.
        CHECK(fn_set_relay(p2p, nullptr) == EOS_EResult::EOS_InvalidParameters);
        EOS_P2P_SetRelayControlOptions relay_options = {};
        relay_options.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
        relay_options.RelayControl = EOS_ERelayControl::EOS_RC_ForceRelays;
        CHECK(fn_set_relay(p2p, &relay_options) == EOS_EResult::EOS_Success);
        EOS_P2P_GetRelayControlOptions get_relay_options = {};
        get_relay_options.ApiVersion = EOS_P2P_GETRELAYCONTROL_API_LATEST;
        EOS_ERelayControl relay = EOS_ERelayControl::EOS_RC_AllowRelays;
        CHECK(fn_get_relay(p2p, &get_relay_options, &relay) == EOS_EResult::EOS_Success);
        CHECK(relay == EOS_ERelayControl::EOS_RC_ForceRelays);

        CHECK(fn_set_port(p2p, nullptr) == EOS_EResult::EOS_InvalidParameters);
        EOS_P2P_SetPortRangeOptions port_options = {};
        port_options.ApiVersion = EOS_P2P_SETPORTRANGE_API_LATEST;
        port_options.Port = 9000;
        port_options.MaxAdditionalPortsToTry = 4;
        CHECK(fn_set_port(p2p, &port_options) == EOS_EResult::EOS_Success);
        EOS_P2P_GetPortRangeOptions get_port_options = {};
        get_port_options.ApiVersion = EOS_P2P_GETPORTRANGE_API_LATEST;
        uint16_t port = 0;
        uint16_t additional_ports = 0;
        CHECK(fn_get_port(p2p, &get_port_options, &port, &additional_ports) == EOS_EResult::EOS_Success);
        CHECK(port == 9000);
        CHECK(additional_ports == 4);

        CHECK(fn_set_queue(p2p, nullptr) == EOS_EResult::EOS_InvalidParameters);
        EOS_P2P_SetPacketQueueSizeOptions queue_options = {};
        queue_options.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
        queue_options.IncomingPacketQueueMaxSizeBytes = 4096;
        queue_options.OutgoingPacketQueueMaxSizeBytes = 8192;
        CHECK(fn_set_queue(p2p, &queue_options) == EOS_EResult::EOS_Success);
        EOS_P2P_GetPacketQueueInfoOptions get_queue_options = {};
        get_queue_options.ApiVersion = EOS_P2P_GETPACKETQUEUEINFO_API_LATEST;
        EOS_P2P_PacketQueueInfo queue_info = {};
        CHECK(fn_get_queue(p2p, &get_queue_options, &queue_info) == EOS_EResult::EOS_Success);
        CHECK(queue_info.IncomingPacketQueueMaxSizeBytes == 4096);
        CHECK(queue_info.OutgoingPacketQueueMaxSizeBytes == 8192);

        CHECK(fn_add_queue_full(p2p, nullptr, nullptr, on_p2p_queue_full) ==
              EOS_INVALID_NOTIFICATIONID);
        EOS_P2P_AddNotifyIncomingPacketQueueFullOptions queue_notify_options = {};
        queue_notify_options.ApiVersion = EOS_P2P_ADDNOTIFYINCOMINGPACKETQUEUEFULL_API_LATEST;
        const EOS_NotificationId queue_notification =
            fn_add_queue_full(p2p, &queue_notify_options, nullptr, on_p2p_queue_full);
        REQUIRE(queue_notification != EOS_INVALID_NOTIFICATIONID);

        CHECK(fn_clear_queue(p2p, nullptr) == EOS_EResult::EOS_InvalidParameters);
        EOS_P2P_ClearPacketQueueOptions clear_options = {};
        clear_options.ApiVersion = EOS_P2P_CLEARPACKETQUEUE_API_LATEST + 1;
        clear_options.LocalUserId = send_options.LocalUserId;
        clear_options.RemoteUserId = remote;
        clear_options.SocketId = &socket;
        CHECK(fn_clear_queue(p2p, &clear_options) == EOS_EResult::EOS_IncompatibleVersion);

        fn_remove_queue_full(p2p, queue_notification);
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

// A game resolves every EOS_* symbol it imports when the library loads. One it cannot find kills the
// process in the loader, before the SDK has run a single line -- so an export we simply do not have
// is not a feature we are missing, it is a game that never starts. These are the handle-free helpers
// a game reaches for outside any interface.
TEST_CASE("the built SDK library exposes the version, result and byte-array helpers") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_get_version, EOS_GetVersion);
    RESOLVE(fn_is_complete, EOS_EResult_IsOperationComplete);
    RESOLVE(fn_bytes_to_string, EOS_ByteArray_ToString);
    RESOLVE(fn_app_status_to_string, EOS_EApplicationStatus_ToString);
    RESOLVE(fn_net_status_to_string, EOS_ENetworkStatus_ToString);

    // We answer as the SDK whose headers we are built against, because that is what a game asking
    // the question actually wants to know: which API it may expect.
    CHECK(std::string(fn_get_version()) == std::string(EOS_VERSION_STRING));

    // A result is final unless the callback that carried it is going to be called again.
    CHECK(fn_is_complete(EOS_EResult::EOS_Success) == EOS_TRUE);
    CHECK(fn_is_complete(EOS_EResult::EOS_NotFound) == EOS_TRUE);
    CHECK(fn_is_complete(EOS_EResult::EOS_OperationWillRetry) == EOS_FALSE);
    CHECK(fn_is_complete(EOS_EResult::EOS_Auth_PinGrantCode) == EOS_FALSE);
    CHECK(fn_is_complete(EOS_EResult::EOS_Auth_MFARequired) == EOS_FALSE);

    // Hex, uppercase -- the encoding the header's own example spells out.
    const uint8_t bytes[] = {0xfa, 0x87, 0x09, 0x7a, 0x00};
    char buffer[32];
    uint32_t length = static_cast<uint32_t>(sizeof(buffer));
    CHECK(fn_bytes_to_string(bytes, 5, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "FA87097A00");
    CHECK(length == 11); // ten characters plus the null

    // A buffer too small is LimitExceeded with the length it would have needed, never a silent
    // truncation the caller cannot detect.
    uint32_t small = 4;
    CHECK(fn_bytes_to_string(bytes, 5, buffer, &small) == EOS_EResult::EOS_LimitExceeded);
    CHECK(small == 11);

    // A zero length is not an empty success. The header calls an invalid length InvalidParameters,
    // and the reference SDK refuses it outright -- there is nothing to encode.
    uint32_t empty = static_cast<uint32_t>(sizeof(buffer));
    CHECK(fn_bytes_to_string(bytes, 0, buffer, &empty) == EOS_EResult::EOS_InvalidParameters);

    CHECK(std::string(fn_app_status_to_string(EOS_EApplicationStatus::EOS_AS_Foreground)) ==
          "EOS_AS_Foreground");
    CHECK(std::string(fn_net_status_to_string(EOS_ENetworkStatus::EOS_NS_Offline)) ==
          "EOS_NS_Offline");
}

// Regression review: the encoded length is 2 * Length + 1, but both the input and output length
// use uint32_t.  A length above (UINT32_MAX - 1) / 2 therefore cannot be represented by this ABI
// and must be rejected before the arithmetic wraps.  With a one-byte advertised output buffer the
// old implementation treated the wrapped requirement as satisfied and entered a multi-gigabyte
// read/write loop; using a zero-capacity buffer here exposes the same wrap without invoking UB.
TEST_CASE("byte-array conversion rejects an encoded length that cannot fit its length type") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_bytes_to_string, EOS_ByteArray_ToString);

    const uint8_t byte = 0;
    char buffer[1] = {'x'};
    uint32_t capacity = 0;
    const uint32_t unrepresentable =
        (std::numeric_limits<uint32_t>::max() / 2u) + 1u;

    CHECK(fn_bytes_to_string(&byte, unrepresentable, buffer, &capacity) ==
          EOS_EResult::EOS_InvalidParameters);
    CHECK(capacity == 0);
    CHECK(buffer[0] == 'x');
}

// There is no valid continuance token in this emulator, but the ABI still distinguishes an invalid
// token from malformed output arguments.  The public contract (and the 2020 implementation) call
// the former InvalidUser; reporting InvalidParameters sends a game down a different error path.
TEST_CASE("continuance-token conversion reports a null token as an invalid user") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_token_to_string, EOS_ContinuanceToken_ToString);

    char buffer[8] = {};
    int32_t length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_token_to_string(nullptr, buffer, &length) == EOS_EResult::EOS_InvalidUser);
}

// A game tells the SDK when it is backgrounded or loses the network, and reads country and locale
// back. None of it reaches a service -- there is none -- but a game that sets a value and does not
// see it again has every reason to think the SDK is not listening to it.
TEST_CASE("the built SDK library carries application, network, country and locale state") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_set_app, EOS_Platform_SetApplicationStatus);
    RESOLVE(fn_get_app, EOS_Platform_GetApplicationStatus);
    RESOLVE(fn_set_net, EOS_Platform_SetNetworkStatus);
    RESOLVE(fn_get_net, EOS_Platform_GetNetworkStatus);
    RESOLVE(fn_set_country, EOS_Platform_SetOverrideCountryCode);
    RESOLVE(fn_get_country, EOS_Platform_GetOverrideCountryCode);
    RESOLVE(fn_get_active_country, EOS_Platform_GetActiveCountryCode);
    RESOLVE(fn_set_locale, EOS_Platform_SetOverrideLocaleCode);
    RESOLVE(fn_get_locale, EOS_Platform_GetOverrideLocaleCode);
    RESOLVE(fn_get_active_locale, EOS_Platform_GetActiveLocaleCode);
    RESOLVE(fn_crossplay, EOS_Platform_GetDesktopCrossplayStatus);

    EOS_InitializeOptions iopts = {};
    iopts.ApiVersion = EOS_INITIALIZE_API_LATEST;
    iopts.ProductName = "StatusTest";
    iopts.ProductVersion = "1.0.0";
    REQUIRE(fn_initialize(&iopts) == EOS_EResult::EOS_Success);

    EOS_Platform_Options popts = {};
    popts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    popts.ProductId = "prod-abc";
    popts.SandboxId = "sandbox-1";
    popts.DeploymentId = "deploy-2";
    popts.ClientCredentials.ClientId = "client";
    popts.ClientCredentials.ClientSecret = "secret";
    EOS_HPlatform platform = fn_create(&popts);
    REQUIRE((platform != nullptr));

    // A game that never says otherwise is in the foreground with a working network.
    CHECK(fn_get_app(platform) == EOS_EApplicationStatus::EOS_AS_Foreground);
    CHECK(fn_get_net(platform) == EOS_ENetworkStatus::EOS_NS_Online);

    CHECK(fn_set_app(platform, EOS_EApplicationStatus::EOS_AS_BackgroundSuspended) ==
          EOS_EResult::EOS_Success);
    CHECK(fn_get_app(platform) == EOS_EApplicationStatus::EOS_AS_BackgroundSuspended);
    CHECK(fn_set_net(platform, EOS_ENetworkStatus::EOS_NS_Offline) == EOS_EResult::EOS_Success);
    CHECK(fn_get_net(platform) == EOS_ENetworkStatus::EOS_NS_Offline);

    char buffer[32];
    int32_t length = static_cast<int32_t>(sizeof(buffer));

    // With no override there is nothing to be active: an account we could look one up from is
    // exactly the thing we do not have.
    CHECK(fn_get_active_country(platform, nullptr, buffer, &length) == EOS_EResult::EOS_NotFound);

    CHECK(fn_set_country(platform, "US") == EOS_EResult::EOS_Success);
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_get_country(platform, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "US");
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_get_active_country(platform, nullptr, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "US");

    CHECK(fn_set_locale(platform, "en-US") == EOS_EResult::EOS_Success);
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_get_locale(platform, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "en-US");
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_get_active_locale(platform, nullptr, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "en-US");

    // A code longer than the SDK allows is refused rather than stored and handed back truncated.
    // The setter has no output buffer to exceed: its documented error for an invalid/overlong code
    // is InvalidParameters.  LimitExceeded is reserved for the getter's caller-supplied buffer.
    CHECK(fn_set_country(platform, "TOOLONG") == EOS_EResult::EOS_InvalidParameters);
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_get_country(platform, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "US"); // and the one it had is still there

    CHECK(fn_set_locale(platform, "locale-is-too-long") == EOS_EResult::EOS_InvalidParameters);
    length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fn_get_locale(platform, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "en-US");

    // A buffer too small reports the length it would have needed.
    int32_t small = 1;
    CHECK(fn_get_country(platform, buffer, &small) == EOS_EResult::EOS_LimitExceeded);
    CHECK(small == 3); // "US" plus the null

    EOS_Platform_GetDesktopCrossplayStatusOptions crossplay = {};
    crossplay.ApiVersion = EOS_PLATFORM_GETDESKTOPCROSSPLAYSTATUS_API_LATEST;
    // Poisoned, not zeroed: every field of an out-struct is ours to write, and a zeroed one would
    // hide us failing to write one.
    EOS_Platform_DesktopCrossplayStatusInfo info;
    std::memset(&info, 0x5a, sizeof(info));
#if defined(_WIN32)
    // The header says desktop crossplay "is required to use Epic accounts login with applications
    // that are distributed outside the Epic Games Store" -- and a game with our library dropped into
    // it is exactly that. Reporting a missing bootstrapper is the honest answer about infrastructure
    // we do not have, and a documented way for the game to gate the player out of the multiplayer we
    // exist to provide. So we say the prerequisites are met, because for us they are.
    CHECK(fn_crossplay(platform, &crossplay, &info) == EOS_EResult::EOS_Success);
    CHECK(info.Status == EOS_EDesktopCrossplayStatus::EOS_DCS_OK);
    // Only meaningful when the status is ServiceStartFailed, but a game is told to log it, so it
    // must not be whatever the game had in that memory before it called us.
    CHECK(info.ServiceInitResult == -1);
#else
    // This API is Windows-only; the header explicitly requires NotImplemented elsewhere.
    CHECK(fn_crossplay(platform, &crossplay, &info) == EOS_EResult::EOS_NotImplemented);
#endif

    fn_release(platform);
    CHECK(fn_shutdown() == EOS_EResult::EOS_Success);
}

// Directional compatibility test: on Windows the bootstrap probe now (correctly, and matching the
// reference emulator) says the social prerequisites are ready. A game may consequently enter its UI
// path, while a game that imports these functions resolves them before it reaches the probe at all.
// Missing UI exports are therefore a loader/runtime boundary, not merely a disabled visual feature.
// Keep this red until the headless UI compatibility shell lands; the companion can later replace the
// AcknowledgeEventId stub with its real event lifecycle without changing this surface contract.
TEST_CASE("the built SDK library exposes the complete social UI compatibility surface") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    const char* const ui_symbols[] = {
        "EOS_UI_ShowFriends",
        "EOS_UI_HideFriends",
        "EOS_UI_GetFriendsVisible",
        "EOS_UI_GetFriendsExclusiveInput",
        "EOS_UI_AddNotifyDisplaySettingsUpdated",
        "EOS_UI_RemoveNotifyDisplaySettingsUpdated",
        "EOS_UI_SetToggleFriendsKey",
        "EOS_UI_GetToggleFriendsKey",
        "EOS_UI_IsValidKeyCombination",
        "EOS_UI_SetToggleFriendsButton",
        "EOS_UI_GetToggleFriendsButton",
        "EOS_UI_IsValidButtonCombination",
        "EOS_UI_SetDisplayPreference",
        "EOS_UI_GetNotificationLocationPreference",
        "EOS_UI_AcknowledgeEventId",
        "EOS_UI_ReportInputState",
        "EOS_UI_PrePresent",
        "EOS_UI_ShowBlockPlayer",
        "EOS_UI_ShowReportPlayer",
        "EOS_UI_PauseSocialOverlay",
        "EOS_UI_IsSocialOverlayPaused",
        "EOS_UI_AddNotifyMemoryMonitor",
        "EOS_UI_RemoveNotifyMemoryMonitor",
        "EOS_UI_ShowNativeProfile",
        "EOS_UI_ConfigureOnScreenKeyboard",
        "EOS_UI_AddNotifyOnScreenKeyboardRequested",
        "EOS_UI_RemoveNotifyOnScreenKeyboardRequested"
    };
    const std::size_t ui_symbol_count = sizeof(ui_symbols) / sizeof(ui_symbols[0]);
    for (std::size_t i = 0; i < ui_symbol_count; i++) {
        CHECK_MESSAGE((lib.symbol(ui_symbols[i]) != nullptr),
                      (std::string("missing export: ") + ui_symbols[i]));
    }
}

// The whole integrated-platform lifecycle as a game actually performs it: build a container before
// any platform exists, add Steam to it, create the platform *from* it, release the container, and
// only then use the interface. The container is released while the platform is still running, which
// is exactly why platform creation has to copy what it needs rather than hold the handle.
TEST_CASE("the built SDK library takes an integrated-platform container and outlives it") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_get_integrated, EOS_Platform_GetIntegratedPlatformInterface);
    RESOLVE(fn_create_container, EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainer);
    RESOLVE(fn_container_add, EOS_IntegratedPlatformOptionsContainer_Add);
    RESOLVE(fn_container_release, EOS_IntegratedPlatformOptionsContainer_Release);
    RESOLVE(fn_set_login, EOS_IntegratedPlatform_SetUserLoginStatus);
    RESOLVE(fn_finalize, EOS_IntegratedPlatform_FinalizeDeferredUserLogout);

    EOS_InitializeOptions iopts = {};
    iopts.ApiVersion = EOS_INITIALIZE_API_LATEST;
    iopts.ProductName = "IntegratedTest";
    iopts.ProductVersion = "1.0.0";
    REQUIRE(fn_initialize(&iopts) == EOS_EResult::EOS_Success);

    // The container comes first: it is what the platform is created from.
    EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions create_container = {};
    create_container.ApiVersion =
        EOS_INTEGRATEDPLATFORM_CREATEINTEGRATEDPLATFORMOPTIONSCONTAINER_API_LATEST;
    EOS_HIntegratedPlatformOptionsContainer container = nullptr;
    REQUIRE(fn_create_container(&create_container, &container) == EOS_EResult::EOS_Success);
    REQUIRE((container != nullptr));

    EOS_IntegratedPlatform_Options steam = {};
    steam.ApiVersion = EOS_INTEGRATEDPLATFORM_OPTIONS_API_LATEST;
    steam.Type = EOS_IPT_Steam;
    steam.Flags =
        EOS_EIntegratedPlatformManagementFlags::EOS_IPMF_ApplicationManagedIdentityLogin;
    EOS_IntegratedPlatformOptionsContainer_AddOptions add = {};
    add.ApiVersion = EOS_INTEGRATEDPLATFORMOPTIONSCONTAINER_ADD_API_LATEST;
    add.Options = &steam;
    CHECK(fn_container_add(container, &add) == EOS_EResult::EOS_Success);
    CHECK(fn_container_add(container, &add) == EOS_EResult::EOS_DuplicateNotAllowed);

    EOS_Platform_Options popts = {};
    popts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    popts.ProductId = "prod-abc";
    popts.SandboxId = "sandbox-1";
    popts.DeploymentId = "deploy-2";
    popts.ClientCredentials.ClientId = "client";
    popts.ClientCredentials.ClientSecret = "secret";
    popts.IntegratedPlatformOptionsContainerHandle = container;
    EOS_HPlatform platform = fn_create(&popts);
    REQUIRE((platform != nullptr));

    // The game releases the container now, as the header instructs, and the platform keeps working.
    fn_container_release(container);

    EOS_HIntegratedPlatform integrated = fn_get_integrated(platform);
    REQUIRE((integrated != nullptr));

    // Steam was registered as application-managed, so the game may set its user's login status.
    EOS_IntegratedPlatform_SetUserLoginStatusOptions login = {};
    login.ApiVersion = EOS_INTEGRATEDPLATFORM_SETUSERLOGINSTATUS_API_LATEST;
    login.PlatformType = EOS_IPT_Steam;
    login.LocalPlatformUserId = "76561198000000000";
    login.CurrentLoginStatus = EOS_ELoginStatus::EOS_LS_LoggedIn;
    CHECK(fn_set_login(integrated, &login) == EOS_EResult::EOS_Success);

    // A platform the game never registered is NotConfigured, which is a different thing entirely.
    login.PlatformType = "PSN";
    CHECK(fn_set_login(integrated, &login) == EOS_EResult::EOS_NotConfigured);

    // Nothing ever tells us a user signed out, so there is never a deferred logout to finalize.
    EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions finalize = {};
    finalize.ApiVersion = EOS_INTEGRATEDPLATFORM_FINALIZEDEFERREDUSERLOGOUT_API_LATEST;
    finalize.PlatformType = EOS_IPT_Steam;
    finalize.LocalPlatformUserId = "76561198000000000";
    finalize.ExpectedLoginStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    CHECK(fn_finalize(integrated, &finalize) == EOS_EResult::EOS_InvalidUser);

    fn_release(platform);

    // The handle arrived in EOS_Platform_Options at version 12 -- a version-11 struct ends at
    // RTCOptions. So an older game's struct stops before the field, and reading it would read the
    // game's own memory. We hand ourselves a full-size struct with an old version and a *valid*
    // container in that slot: if we read it, Steam would be registered and the login below would
    // succeed. It must not be.
    EOS_HIntegratedPlatformOptionsContainer older_container = nullptr;
    REQUIRE(fn_create_container(&create_container, &older_container) == EOS_EResult::EOS_Success);
    add.Options = &steam;
    REQUIRE(fn_container_add(older_container, &add) == EOS_EResult::EOS_Success);

    EOS_Platform_Options older = popts;
    older.ApiVersion = 11;
    older.IntegratedPlatformOptionsContainerHandle = older_container;
    EOS_HPlatform older_platform = fn_create(&older);
    REQUIRE((older_platform != nullptr));
    EOS_HIntegratedPlatform older_integrated = fn_get_integrated(older_platform);
    REQUIRE((older_integrated != nullptr));

    login.PlatformType = EOS_IPT_Steam;
    CHECK(fn_set_login(older_integrated, &login) == EOS_EResult::EOS_NotConfigured);

    fn_container_release(older_container);
    fn_release(older_platform);
    CHECK(fn_shutdown() == EOS_EResult::EOS_Success);
}

// A game binds the whole SDK surface. The families we have not built must still be *there*: a static
// import we lack refuses the process at load, and a Unity/Mono P/Invoke we lack throws
// EntryPointNotFoundException at the first call. Either way the emulator never gets control, so it
// cannot even report the problem. These exports exist so the failure is an honest NotImplemented the
// game can see -- and, crucially, so an asynchronous one still completes rather than hanging.
namespace {
bool g_ecom_fired = false;
EOS_EResult g_ecom_result = EOS_EResult::EOS_Success;
void* g_ecom_client_data = 0;
void EOS_CALL on_ecom_ownership(const EOS_Ecom_QueryOwnershipCallbackInfo* info) {
    g_ecom_fired = true;
    g_ecom_result = info->ResultCode;
    g_ecom_client_data = info->ClientData;
}
} // namespace

TEST_CASE("an unimplemented interface is exported, and its async call still completes") {
    REQUIRE_FALSE(g_library_path.empty());
    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));

    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_tick, EOS_Platform_Tick);
    // The whole point: these resolve at all. Before the compatibility shells they did not exist.
    RESOLVE(fn_get_ecom, EOS_Platform_GetEcomInterface);
    RESOLVE(fn_query_ownership, EOS_Ecom_QueryOwnership);
    RESOLVE(fn_ecom_count, EOS_Ecom_GetEntitlementsCount);
    RESOLVE(fn_achievements, EOS_Platform_GetAchievementsInterface);
    RESOLVE(fn_unlock, EOS_Achievements_UnlockAchievements);
    RESOLVE(fn_stats, EOS_Stats_IngestStat);
    RESOLVE(fn_definition_release, EOS_Achievements_Definition_Release);

    EOS_InitializeOptions iopts = {};
    iopts.ApiVersion = EOS_INITIALIZE_API_LATEST;
    iopts.ProductName = "StubTest";
    iopts.ProductVersion = "1.0.0";
    REQUIRE(fn_initialize(&iopts) == EOS_EResult::EOS_Success);

    EOS_Platform_Options popts = {};
    popts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    popts.ProductId = "prod-stub";
    popts.SandboxId = "sandbox-stub";
    popts.DeploymentId = "deploy-stub";
    EOS_HPlatform platform = fn_create(&popts);
    REQUIRE((platform != nullptr));

    // Every getter hands back a usable handle, even for an interface we have not built.
    EOS_HEcom ecom = fn_get_ecom(platform);
    CHECK((ecom != nullptr));
    CHECK((fn_achievements(platform) != nullptr));

    // A synchronous getter reports nothing rather than inventing a value.
    EOS_Ecom_GetEntitlementsCountOptions count_opts = {};
    count_opts.ApiVersion = EOS_ECOM_GETENTITLEMENTSCOUNT_API_LATEST;
    CHECK(fn_ecom_count(ecom, &count_opts) == 0);

    // An asynchronous one still fires its callback -- a game awaiting it must never hang.
    int client_data = 0;
    g_ecom_fired = false;
    EOS_Ecom_QueryOwnershipOptions own_opts = {};
    own_opts.ApiVersion = EOS_ECOM_QUERYOWNERSHIP_API_LATEST;
    fn_query_ownership(ecom, &own_opts, &client_data, on_ecom_ownership);
    for (int i = 0; i < 8 && !g_ecom_fired; i++) {
        fn_tick(platform);
    }
    CHECK(g_ecom_fired);
    CHECK(g_ecom_result == EOS_EResult::EOS_NotImplemented); // honest, not a fabricated success
    CHECK(g_ecom_client_data == &client_data);

    // A deprecated release the game's own SDK version still declares: present, and a safe no-op.
    fn_definition_release(nullptr);

    fn_release(platform);
    CHECK(fn_shutdown() == EOS_EResult::EOS_Success);
}
