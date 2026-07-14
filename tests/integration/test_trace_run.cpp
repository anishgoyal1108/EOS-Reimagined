#include "doctest.h"

#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_connect.h"
#include "eos_auth.h"
#include "eos_p2p.h"
#include "eos_presence.h"
#include "eos_ecom.h"
#include "eos_achievements.h"
#include "eos_playerdatastorage.h"

#include "platform/dynlib.h"
#include "platform/paths.h"

using namespace eosr::platform;

// The path to the built .so/.dll under test, from the integration main.
extern std::string g_library_path;

namespace {

#define RESOLVE(var, api_name) \
    auto var = reinterpret_cast<decltype(&api_name)>(lib.symbol(#api_name)); \
    REQUIRE((var != nullptr))

bool g_login_fired = false;
EOS_EResult g_login_result = EOS_EResult::EOS_UnexpectedError;
EOS_ProductUserId g_login_user = nullptr;
int g_status_changed = 0;
bool g_ecom_fired = false;
EOS_EResult g_ecom_result = EOS_EResult::EOS_Success;
int g_storage_callbacks = 0;
bool g_auth_delete_fired = false;
void EOS_CALL on_login(const EOS_Connect_LoginCallbackInfo* info) {
    g_login_fired = true;
    g_login_result = info->ResultCode;
    g_login_user = info->LocalUserId;
}
void EOS_CALL on_status_changed(const EOS_Connect_LoginStatusChangedCallbackInfo*) {
    g_status_changed++;
}
void EOS_CALL on_ecom_ownership(const EOS_Ecom_QueryOwnershipCallbackInfo* info) {
    g_ecom_fired = true;
    g_ecom_result = info->ResultCode;
}
void EOS_CALL on_storage_delete(const EOS_PlayerDataStorage_DeleteCacheCallbackInfo* info) {
    CHECK(info->ResultCode == EOS_EResult::EOS_NotImplemented);
    g_storage_callbacks++;
}
void EOS_CALL on_storage_read(const EOS_PlayerDataStorage_ReadFileCallbackInfo* info) {
    CHECK(info->ResultCode == EOS_EResult::EOS_NotImplemented);
    g_storage_callbacks++;
}
void EOS_CALL on_achievement_unlocked(const EOS_Achievements_OnAchievementsUnlockedCallbackV2Info*) {}
void EOS_CALL on_auth_delete(const EOS_Auth_DeletePersistentAuthCallbackInfo* info) {
    CHECK(info->ResultCode == EOS_EResult::EOS_NotImplemented);
    g_auth_delete_fired = true;
}
void EOS_CALL on_join_game_accepted(const EOS_Presence_JoinGameAcceptedCallbackInfo*) {}

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string slurp(const std::string& path) {
    std::string out;
    read_file_capped(path, 16 * 1024 * 1024, out);
    return out;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

// Pull the integer value of the "seq" envelope field out of one record line, or -1 if absent.
long parse_seq(const std::string& line) {
    const std::string key = "\"seq\":";
    const std::size_t at = line.find(key);
    if (at == std::string::npos) {
        return -1;
    }
    std::size_t i = at + key.size();
    long value = 0;
    bool any = false;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        value = value * 10 + (line[i] - '0');
        any = true;
        i++;
    }
    return any ? value : -1;
}

// The first line whose text contains `needle`, or empty if there is none.
std::string find_line(const std::vector<std::string>& lines, const std::string& needle) {
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (lines[i].find(needle) != std::string::npos) {
            return lines[i];
        }
    }
    return std::string();
}

// The value of a string field ("corr":"c#0" -> c#0), or empty if the field is absent.
std::string field_of(const std::string& line, const std::string& name) {
    const std::string key = "\"" + name + "\":\"";
    const std::size_t at = line.find(key);
    if (at == std::string::npos) {
        return std::string();
    }
    const std::size_t start = at + key.size();
    const std::size_t end = line.find('"', start);
    return (end == std::string::npos) ? std::string() : line.substr(start, end - start);
}

std::size_t count_records(const std::vector<std::string>& lines, const std::string& kind,
                          const std::string& fn) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (field_of(lines[i], "kind") == kind && field_of(lines[i], "fn") == fn) {
            count++;
        }
    }
    return count;
}

} // namespace

// The real observability path: enable tracing through the loaded library and drive one whole EOS
// lifetime, then check the run it produced. Runner mode gives us a known directory to read back.
TEST_CASE("tracing through the loaded library produces a well-formed run") {
    REQUIRE_FALSE(g_library_path.empty());

    const std::string base = EOSR_PROBE_DIR;
    const std::string data = base + "/data";
    const std::string run = base + "/run-probe";
    REQUIRE(make_directories(data));
    REQUIRE(make_directories(run));
    // A run directory the runner "already created": clear any owned files a previous run left, so the
    // library exclusively creates a fresh trace.jsonl and runtime.json.
    remove_file(run + "/trace.jsonl");
    remove_file(run + "/runtime.json");

    set_env("EOSR_DATA_DIR", data.c_str());
    set_env("EOSR_RUN_DIR", run.c_str());
    set_env("EOSR_TRACE", "full");
    set_env("EOSR_INSTANCE_LABEL", "probe");

    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));
    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_tick, EOS_Platform_Tick);

    EOS_InitializeOptions iopts = {};
    iopts.ApiVersion = EOS_INITIALIZE_API_LATEST;
    iopts.ProductName = "TraceProbe";
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

    // Drive one real asynchronous operation, exactly as a game would: log in, then tick until the
    // completion fires. This is what the call/return/callback correlation is checked against below.
    RESOLVE(fn_get_connect, EOS_Platform_GetConnectInterface);
    RESOLVE(fn_login, EOS_Connect_Login);
    RESOLVE(fn_add_status, EOS_Connect_AddNotifyLoginStatusChanged);
    RESOLVE(fn_remove_status, EOS_Connect_RemoveNotifyLoginStatusChanged);
    RESOLVE(fn_puid_to_string, EOS_ProductUserId_ToString);
    RESOLVE(fn_get_ecom, EOS_Platform_GetEcomInterface);
    RESOLVE(fn_query_ownership, EOS_Ecom_QueryOwnership);
    RESOLVE(fn_get_storage, EOS_Platform_GetPlayerDataStorageInterface);
    RESOLVE(fn_delete_cache, EOS_PlayerDataStorage_DeleteCache);
    RESOLVE(fn_read_file, EOS_PlayerDataStorage_ReadFile);
    RESOLVE(fn_get_achievements, EOS_Platform_GetAchievementsInterface);
    RESOLVE(fn_add_achievement, EOS_Achievements_AddNotifyAchievementsUnlockedV2);
    RESOLVE(fn_remove_achievement, EOS_Achievements_RemoveNotifyAchievementsUnlocked);
    RESOLVE(fn_get_auth, EOS_Platform_GetAuthInterface);
    RESOLVE(fn_auth_delete, EOS_Auth_DeletePersistentAuth);
    RESOLVE(fn_connect_status, EOS_Connect_GetLoginStatus);
    RESOLVE(fn_get_p2p, EOS_Platform_GetP2PInterface);
    RESOLVE(fn_p2p_send, EOS_P2P_SendPacket);
    RESOLVE(fn_get_presence, EOS_Platform_GetPresenceInterface);
    RESOLVE(fn_add_join_game, EOS_Presence_AddNotifyJoinGameAccepted);
    RESOLVE(fn_remove_join_game, EOS_Presence_RemoveNotifyJoinGameAccepted);
    EOS_HConnect connect = fn_get_connect(platform);
    REQUIRE((connect != nullptr));

    EOS_Connect_AddNotifyLoginStatusChangedOptions notify_opts = {};
    notify_opts.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
    const EOS_NotificationId status_id =
        fn_add_status(connect, &notify_opts, nullptr, on_status_changed);
    REQUIRE(status_id != EOS_INVALID_NOTIFICATIONID);

    EOS_Connect_Credentials creds = {};
    creds.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    creds.Token = "unused";
    creds.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_LoginOptions login = {};
    login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    login.Credentials = &creds;
    fn_login(connect, &login, nullptr, on_login);

    for (int i = 0; i < 8 && !g_login_fired; i++) {
        fn_tick(platform);
    }
    CHECK(g_login_fired);
    CHECK(g_login_result == EOS_EResult::EOS_Success);
    CHECK(g_status_changed == 1);
    REQUIRE((g_login_user != nullptr));

    char raw_user_buffer[128] = {};
    int32_t raw_user_length = static_cast<int32_t>(sizeof(raw_user_buffer));
    REQUIRE(fn_puid_to_string(g_login_user, raw_user_buffer, &raw_user_length) ==
            EOS_EResult::EOS_Success);
    const std::string raw_user(raw_user_buffer);
    REQUIRE_FALSE(raw_user.empty());
    CHECK(fn_connect_status(connect, g_login_user) == EOS_ELoginStatus::EOS_LS_LoggedIn);

    EOS_P2P_SocketId socket = {};
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::memcpy(socket.SocketName, "trace", sizeof("trace"));
    const char packet_payload[] = "p2p-private-payload";
    EOS_P2P_SendPacketOptions send = {};
    send.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    send.LocalUserId = g_login_user;
    send.RemoteUserId = g_login_user;
    send.SocketId = &socket;
    send.Channel = 7;
    send.DataLengthBytes = static_cast<uint32_t>(sizeof(packet_payload) - 1);
    send.Data = packet_payload;
    send.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
    send.bDisableAutoAcceptConnection = EOS_TRUE;
    CHECK(fn_p2p_send(fn_get_p2p(platform), &send) == EOS_EResult::EOS_NoConnection);

    EOS_Presence_AddNotifyJoinGameAcceptedOptions join_game = {};
    join_game.ApiVersion = EOS_PRESENCE_ADDNOTIFYJOINGAMEACCEPTED_API_LATEST;
    const EOS_NotificationId join_game_id = fn_add_join_game(
        fn_get_presence(platform), &join_game, nullptr, on_join_game_accepted);
    REQUIRE(join_game_id != EOS_INVALID_NOTIFICATIONID);
    fn_remove_join_game(fn_get_presence(platform), join_game_id);

    // A diagnostic trace must retain calls that fail before dispatch too. Bad/stale handles are one
    // of the first things an in-game alpha probe needs to explain, not a reason for the probe itself
    // to go silent. The emulator already treats this as a safe no-op.
    fn_login(nullptr, &login, nullptr, on_login);

    EOS_Ecom_QueryOwnershipOptions ownership = {};
    ownership.ApiVersion = EOS_ECOM_QUERYOWNERSHIP_API_LATEST;
    fn_query_ownership(fn_get_ecom(platform), &ownership, nullptr, on_ecom_ownership);
    for (int i = 0; i < 8 && !g_ecom_fired; i++) {
        fn_tick(platform);
    }
    CHECK(g_ecom_fired);
    CHECK(g_ecom_result == EOS_EResult::EOS_NotImplemented);

    EOS_HPlayerDataStorage storage = fn_get_storage(platform);
    REQUIRE(storage != nullptr);
    EOS_PlayerDataStorage_DeleteCacheOptions delete_cache = {};
    delete_cache.ApiVersion = EOS_PLAYERDATASTORAGE_DELETECACHE_API_LATEST;
    CHECK(fn_delete_cache(storage, &delete_cache, nullptr, on_storage_delete) ==
          EOS_EResult::EOS_NotImplemented);
    EOS_PlayerDataStorage_ReadFileOptions read_file = {};
    read_file.ApiVersion = EOS_PLAYERDATASTORAGE_READFILE_API_LATEST;
    CHECK(fn_read_file(storage, &read_file, nullptr, on_storage_read) == nullptr);
    for (int i = 0; i < 8 && g_storage_callbacks < 2; i++) {
        fn_tick(platform);
    }
    CHECK(g_storage_callbacks == 2);

    EOS_Achievements_AddNotifyAchievementsUnlockedV2Options achievement_options = {};
    achievement_options.ApiVersion = EOS_ACHIEVEMENTS_ADDNOTIFYACHIEVEMENTSUNLOCKEDV2_API_LATEST;
    const EOS_NotificationId achievement_id = fn_add_achievement(
        fn_get_achievements(platform), &achievement_options, nullptr, on_achievement_unlocked);
    REQUIRE(achievement_id != EOS_INVALID_NOTIFICATIONID);
    fn_remove_achievement(fn_get_achievements(platform), achievement_id);

    EOS_Auth_DeletePersistentAuthOptions auth_delete = {};
    auth_delete.ApiVersion = EOS_AUTH_DELETEPERSISTENTAUTH_API_LATEST;
    fn_auth_delete(fn_get_auth(platform), &auth_delete, nullptr, on_auth_delete);
    for (int i = 0; i < 8 && !g_auth_delete_fired; i++) {
        fn_tick(platform);
    }
    CHECK(g_auth_delete_fired);

    fn_remove_status(connect, status_id);

    fn_release(platform);
    REQUIRE(fn_shutdown() == EOS_EResult::EOS_Success);
    lib.close();

    // runtime.json is present, well-formed, and identifies this run and this build.
    const std::string runtime = slurp(run + "/runtime.json");
    REQUIRE_FALSE(runtime.empty());
    CHECK(runtime[0] == '{');
    CHECK(runtime[runtime.size() - 1] == '}');
    CHECK(runtime.find("\"run_id\":\"run-probe\"") != std::string::npos);
    CHECK(runtime.find("\"schema_version\":1") != std::string::npos);
    CHECK(runtime.find("\"emulator_build\":\"eosr ") != std::string::npos);
    CHECK(runtime.find("\"trace_level\":\"full\"") != std::string::npos);
    CHECK(runtime.find("\"version\":null") == std::string::npos);
    std::string expected_os;
    std::string expected_wine;
    REQUIRE(system_versions(expected_os, expected_wine));
    CHECK(runtime.find("\"version\":\"" + expected_os + "\"") != std::string::npos);
    if (!expected_wine.empty()) {
        CHECK(runtime.find("\"wine\":\"" + expected_wine + "\"") != std::string::npos);
    }

    // The trace is parseable JSONL, in run_start -> profile -> shutdown order, with a strictly
    // increasing sequence from zero.
    const std::string trace = slurp(run + "/trace.jsonl");
    REQUIRE_FALSE(trace.empty());
    const std::size_t at_start = trace.find("\"event\":\"run_start\"");
    const std::size_t at_profile = trace.find("\"event\":\"profile\"");
    const std::size_t at_shutdown = trace.find("\"event\":\"shutdown\"");
    CHECK(at_start != std::string::npos);
    CHECK(at_profile != std::string::npos);
    CHECK(at_shutdown != std::string::npos);
    CHECK(at_start < at_profile);
    CHECK(at_profile < at_shutdown);

    const std::vector<std::string> lines = split_lines(trace);
    REQUIRE(lines.size() >= 3);
    long expected = 0;
    for (std::size_t i = 0; i < lines.size(); i++) {
        CHECK(lines[i].size() >= 2);
        CHECK(lines[i][0] == '{');
        CHECK(lines[i][lines[i].size() - 1] == '}');
        CHECK(lines[i].find("\"kind\":") != std::string::npos);
        CHECK(parse_seq(lines[i]) == expected); // monotonic, contiguous, from zero
        expected++;
    }

    const char* bootstrap_functions[] = {
        "EOS_Initialize",
        "EOS_Platform_Create",
        "EOS_Platform_GetConnectInterface",
        "EOS_ProductUserId_ToString",
        "EOS_Shutdown"
    };
    for (std::size_t i = 0; i < sizeof(bootstrap_functions) / sizeof(bootstrap_functions[0]); i++) {
        CHECK(count_records(lines, "call", bootstrap_functions[i]) == 1);
        CHECK(count_records(lines, "return", bootstrap_functions[i]) == 1);
    }
    const std::string platform_create_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_Platform_Create\"");
    CHECK(platform_create_return.find("\"value\":{\"type\":\"handle\",\"v\":\"handle#") !=
          std::string::npos);
    const std::string shutdown_return =
        find_line(lines, "\"kind\":\"return\",\"fn\":\"EOS_Shutdown\"");
    CHECK(shutdown_return.find("\"name\":\"EOS_Success\"") != std::string::npos);
    const std::string connect_status_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_Connect_GetLoginStatus\"");
    CHECK(connect_status_return.find(
              "\"value\":{\"type\":\"enum\",\"v\":\"EOS_LS_LoggedIn\"}") !=
          std::string::npos);
    const std::string p2p_send_call = find_line(
        lines, "\"kind\":\"call\",\"fn\":\"EOS_P2P_SendPacket\"");
    const std::string p2p_send_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_P2P_SendPacket\"");
    REQUIRE_FALSE(p2p_send_call.empty());
    REQUIRE_FALSE(p2p_send_return.empty());
    CHECK(p2p_send_call.find("\"local\":\"puid#") != std::string::npos);
    CHECK(p2p_send_call.find("\"target\":\"puid#") != std::string::npos);
    CHECK(p2p_send_call.find("\"socket\":\"socket#") != std::string::npos);
    CHECK(p2p_send_call.find("\"channel\":7") != std::string::npos);
    CHECK(p2p_send_call.find("\"reliability\":\"EOS_PR_ReliableOrdered\"") !=
          std::string::npos);
    CHECK(p2p_send_call.find("\"len\":19") != std::string::npos);
    CHECK(p2p_send_return.find("\"name\":\"EOS_NoConnection\"") != std::string::npos);
    const std::string join_game_return = find_line(
        lines,
        "\"kind\":\"return\",\"fn\":\"EOS_Presence_AddNotifyJoinGameAccepted\"");
    const std::string join_game_register = find_line(
        lines, "\"event\":\"JoinGameAccepted\",\"action\":\"register\"");
    const std::string join_game_remove = find_line(
        lines, "\"event\":\"JoinGameAccepted\",\"action\":\"remove\"");
    REQUIRE_FALSE(join_game_return.empty());
    REQUIRE_FALSE(join_game_register.empty());
    REQUIRE_FALSE(join_game_remove.empty());
    CHECK(field_of(join_game_return, "v") == field_of(join_game_register, "id"));
    CHECK(field_of(join_game_register, "id") == field_of(join_game_remove, "id"));

    // The asynchronous login: its call, its synchronous return, and the callback that completed it a
    // tick later all carry one correlation id, so a reader can stitch the operation back together.
    const std::string call =
        find_line(lines, "\"kind\":\"call\",\"fn\":\"EOS_Connect_Login\"");
    const std::string ret =
        find_line(lines, "\"kind\":\"return\",\"fn\":\"EOS_Connect_Login\"");
    const std::string callback =
        find_line(lines, "\"kind\":\"callback\",\"fn\":\"EOS_Connect_Login\"");
    REQUIRE_FALSE(call.empty());
    REQUIRE_FALSE(ret.empty());
    REQUIRE_FALSE(callback.empty());

    const std::string corr = field_of(call, "corr");
    CHECK_FALSE(corr.empty());
    CHECK(field_of(ret, "corr") == corr);
    CHECK(field_of(callback, "corr") == corr);
    CHECK(field_of(call, "fn") == "EOS_Connect_Login");
    CHECK(field_of(callback, "fn") == "EOS_Connect_Login");

    // Both the valid call and the call rejected for its null handle are visible. Only the valid one
    // has a completion callback, because the rejected call never reached the interface.
    CHECK(count_records(lines, "call", "EOS_Connect_Login") == 2);
    CHECK(count_records(lines, "return", "EOS_Connect_Login") == 2);
    CHECK(count_records(lines, "callback", "EOS_Connect_Login") == 1);

    const std::string ecom_call =
        find_line(lines, "\"fn\":\"EOS_Ecom_QueryOwnership\"");
    REQUIRE_FALSE(ecom_call.empty());
    CHECK(ecom_call.find("\"api\":" +
                         std::to_string(EOS_ECOM_QUERYOWNERSHIP_API_LATEST)) !=
          std::string::npos);
    const std::string ecom_corr = field_of(ecom_call, "corr");
    REQUIRE_FALSE(ecom_corr.empty());
    const std::string ecom_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_Ecom_QueryOwnership\"");
    const std::string ecom_callback = find_line(
        lines, "\"kind\":\"callback\",\"fn\":\"EOS_Ecom_QueryOwnership\"");
    REQUIRE_FALSE(ecom_return.empty());
    REQUIRE_FALSE(ecom_callback.empty());
    CHECK(field_of(ecom_return, "corr") == ecom_corr);
    CHECK(field_of(ecom_callback, "corr") == ecom_corr);
    CHECK(ecom_callback.find("\"name\":\"EOS_NotImplemented\"") != std::string::npos);

    const std::string auth_delete_call = find_line(
        lines, "\"kind\":\"call\",\"fn\":\"EOS_Auth_DeletePersistentAuth\"");
    const std::string auth_delete_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_Auth_DeletePersistentAuth\"");
    const std::string auth_delete_callback = find_line(
        lines, "\"kind\":\"callback\",\"fn\":\"EOS_Auth_DeletePersistentAuth\"");
    REQUIRE_FALSE(auth_delete_call.empty());
    REQUIRE_FALSE(auth_delete_return.empty());
    REQUIRE_FALSE(auth_delete_callback.empty());
    const std::string auth_delete_corr = field_of(auth_delete_call, "corr");
    CHECK(field_of(auth_delete_return, "corr") == auth_delete_corr);
    CHECK(field_of(auth_delete_callback, "corr") == auth_delete_corr);

    const std::string storage_delete_call = find_line(
        lines, "\"kind\":\"call\",\"fn\":\"EOS_PlayerDataStorage_DeleteCache\"");
    const std::string storage_delete_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_PlayerDataStorage_DeleteCache\"");
    const std::string storage_delete_callback = find_line(
        lines, "\"kind\":\"callback\",\"fn\":\"EOS_PlayerDataStorage_DeleteCache\"");
    REQUIRE_FALSE(storage_delete_call.empty());
    REQUIRE_FALSE(storage_delete_return.empty());
    REQUIRE_FALSE(storage_delete_callback.empty());
    const std::string storage_delete_corr = field_of(storage_delete_call, "corr");
    CHECK(field_of(storage_delete_return, "corr") == storage_delete_corr);
    CHECK(field_of(storage_delete_callback, "corr") == storage_delete_corr);
    CHECK(storage_delete_return.find("\"name\":\"EOS_NotImplemented\"") !=
          std::string::npos);

    const std::string storage_read_call = find_line(
        lines, "\"kind\":\"call\",\"fn\":\"EOS_PlayerDataStorage_ReadFile\"");
    const std::string storage_read_return = find_line(
        lines, "\"kind\":\"return\",\"fn\":\"EOS_PlayerDataStorage_ReadFile\"");
    const std::string storage_read_callback = find_line(
        lines, "\"kind\":\"callback\",\"fn\":\"EOS_PlayerDataStorage_ReadFile\"");
    REQUIRE_FALSE(storage_read_call.empty());
    REQUIRE_FALSE(storage_read_return.empty());
    REQUIRE_FALSE(storage_read_callback.empty());
    const std::string storage_read_corr = field_of(storage_read_call, "corr");
    CHECK(field_of(storage_read_return, "corr") == storage_read_corr);
    CHECK(field_of(storage_read_callback, "corr") == storage_read_corr);
    CHECK(storage_read_return.find("\"value\":{\"type\":\"handle\",\"v\":null}") !=
          std::string::npos);

    const std::string achievement_return = find_line(
        lines,
        "\"kind\":\"return\",\"fn\":\"EOS_Achievements_AddNotifyAchievementsUnlockedV2\"");
    const std::string achievement_register =
        find_line(lines, "\"event\":\"AchievementsUnlockedV2\",\"action\":\"register\"");
    const std::string achievement_remove =
        find_line(lines, "\"event\":\"AchievementsUnlockedV2\",\"action\":\"remove\"");
    REQUIRE_FALSE(achievement_return.empty());
    REQUIRE_FALSE(achievement_register.empty());
    REQUIRE_FALSE(achievement_remove.empty());
    CHECK(field_of(achievement_return, "v") == field_of(achievement_register, "id"));
    CHECK(field_of(achievement_register, "id") == field_of(achievement_remove, "id"));

    const std::string registered = find_line(
        lines, "\"event\":\"ConnectLoginStatusChanged\",\"action\":\"register\"");
    const std::string fired = find_line(
        lines, "\"event\":\"ConnectLoginStatusChanged\",\"action\":\"fire\"");
    const std::string removed = find_line(
        lines, "\"event\":\"ConnectLoginStatusChanged\",\"action\":\"remove\"");
    REQUIRE_FALSE(registered.empty());
    REQUIRE_FALSE(fired.empty());
    REQUIRE_FALSE(removed.empty());
    CHECK(field_of(registered, "event") == "ConnectLoginStatusChanged");
    CHECK(field_of(fired, "event") == "ConnectLoginStatusChanged");
    CHECK(field_of(removed, "event") == "ConnectLoginStatusChanged");
    CHECK(field_of(registered, "id") == field_of(fired, "id"));
    CHECK(field_of(fired, "id") == field_of(removed, "id"));
    CHECK(parse_seq(registered) < parse_seq(fired));
    CHECK(parse_seq(fired) < parse_seq(removed));
    const std::string status_notify_return = find_line(
        lines,
        "\"kind\":\"return\",\"fn\":\"EOS_Connect_AddNotifyLoginStatusChanged\"");
    REQUIRE_FALSE(status_notify_return.empty());
    CHECK(field_of(status_notify_return, "v") == field_of(registered, "id"));

    // The kind of credential is recorded; the token is not. The local user is a label, not an id.
    CHECK(call.find("\"cred_type\":\"EOS_ECT_DEVICEID_ACCESS_TOKEN\"") != std::string::npos);
    CHECK(callback.find("\"name\":\"EOS_Success\"") != std::string::npos);
    CHECK(callback.find("\"puid\":\"puid#") != std::string::npos);
    CHECK(trace.find("\"unused\"") == std::string::npos); // the credential token never appears
    CHECK(trace.find(packet_payload) == std::string::npos); // packet bytes never appear
    CHECK(trace.find(raw_user) == std::string::npos); // nor does the real id returned by the ABI
}
