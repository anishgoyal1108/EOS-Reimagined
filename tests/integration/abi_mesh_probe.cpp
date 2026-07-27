#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "eos_auth.h"
#include "eos_connect.h"
#include "eos_friends.h"
#include "eos_init.h"
#include "eos_p2p.h"
#include "eos_sdk.h"
#include "eos_sessions.h"
#include "eos_userinfo.h"

#include "platform/dynlib.h"

namespace {

struct sdk_api {
    decltype(&EOS_Initialize) initialize;
    decltype(&EOS_Shutdown) shutdown;
    decltype(&EOS_Platform_Create) platform_create;
    decltype(&EOS_Platform_Release) platform_release;
    decltype(&EOS_Platform_Tick) platform_tick;
    decltype(&EOS_Platform_GetAuthInterface) get_auth;
    decltype(&EOS_Platform_GetConnectInterface) get_connect;
    decltype(&EOS_Platform_GetFriendsInterface) get_friends;
    decltype(&EOS_Platform_GetP2PInterface) get_p2p;
    decltype(&EOS_Platform_GetSessionsInterface) get_sessions;
    decltype(&EOS_Platform_GetUserInfoInterface) get_userinfo;
    decltype(&EOS_Auth_Login) auth_login;
    decltype(&EOS_Connect_Login) connect_login;
    decltype(&EOS_Friends_GetFriendsCount) friends_count;
    decltype(&EOS_Friends_GetFriendAtIndex) friend_at_index;
    decltype(&EOS_UserInfo_CopyUserInfo) copy_user_info;
    decltype(&EOS_UserInfo_Release) userinfo_release;
    decltype(&EOS_ProductUserId_FromString) puid_from_string;
    decltype(&EOS_ProductUserId_ToString) puid_to_string;
    decltype(&EOS_P2P_AddNotifyPeerConnectionRequest) add_connection_request;
    decltype(&EOS_P2P_RemoveNotifyPeerConnectionRequest) remove_connection_request;
    decltype(&EOS_P2P_AcceptConnection) accept_connection;
    decltype(&EOS_P2P_SendPacket) send_packet;
    decltype(&EOS_P2P_GetNextReceivedPacketSize) next_packet_size;
    decltype(&EOS_P2P_ReceivePacket) receive_packet;
    decltype(&EOS_Sessions_CreateSessionModification) create_session_modification;
    decltype(&EOS_Sessions_UpdateSession) update_session;
    decltype(&EOS_SessionModification_Release) release_session_modification;
    decltype(&EOS_Sessions_CreateSessionSearch) create_session_search;
    decltype(&EOS_SessionSearch_SetParameter) set_session_search_parameter;
    decltype(&EOS_SessionSearch_Find) find_sessions;
    decltype(&EOS_SessionSearch_GetSearchResultCount) search_result_count;
    decltype(&EOS_SessionSearch_CopySearchResultByIndex) copy_search_result;
    decltype(&EOS_SessionSearch_Release) release_session_search;
    decltype(&EOS_Sessions_JoinSession) join_session;
    decltype(&EOS_SessionDetails_Release) release_session_details;
};

#define LOAD_API(field, name) \
    api.field = reinterpret_cast<decltype(api.field)>(library.symbol(#name)); \
    if (api.field == 0) { return false; }

bool load_api(eosr::platform::dynamic_library& library, sdk_api& api) {
    LOAD_API(initialize, EOS_Initialize)
    LOAD_API(shutdown, EOS_Shutdown)
    LOAD_API(platform_create, EOS_Platform_Create)
    LOAD_API(platform_release, EOS_Platform_Release)
    LOAD_API(platform_tick, EOS_Platform_Tick)
    LOAD_API(get_auth, EOS_Platform_GetAuthInterface)
    LOAD_API(get_connect, EOS_Platform_GetConnectInterface)
    LOAD_API(get_friends, EOS_Platform_GetFriendsInterface)
    LOAD_API(get_p2p, EOS_Platform_GetP2PInterface)
    LOAD_API(get_sessions, EOS_Platform_GetSessionsInterface)
    LOAD_API(get_userinfo, EOS_Platform_GetUserInfoInterface)
    LOAD_API(auth_login, EOS_Auth_Login)
    LOAD_API(connect_login, EOS_Connect_Login)
    LOAD_API(friends_count, EOS_Friends_GetFriendsCount)
    LOAD_API(friend_at_index, EOS_Friends_GetFriendAtIndex)
    LOAD_API(copy_user_info, EOS_UserInfo_CopyUserInfo)
    LOAD_API(userinfo_release, EOS_UserInfo_Release)
    LOAD_API(puid_from_string, EOS_ProductUserId_FromString)
    LOAD_API(puid_to_string, EOS_ProductUserId_ToString)
    LOAD_API(add_connection_request, EOS_P2P_AddNotifyPeerConnectionRequest)
    LOAD_API(remove_connection_request, EOS_P2P_RemoveNotifyPeerConnectionRequest)
    LOAD_API(accept_connection, EOS_P2P_AcceptConnection)
    LOAD_API(send_packet, EOS_P2P_SendPacket)
    LOAD_API(next_packet_size, EOS_P2P_GetNextReceivedPacketSize)
    LOAD_API(receive_packet, EOS_P2P_ReceivePacket)
    LOAD_API(create_session_modification, EOS_Sessions_CreateSessionModification)
    LOAD_API(update_session, EOS_Sessions_UpdateSession)
    LOAD_API(release_session_modification, EOS_SessionModification_Release)
    LOAD_API(create_session_search, EOS_Sessions_CreateSessionSearch)
    LOAD_API(set_session_search_parameter, EOS_SessionSearch_SetParameter)
    LOAD_API(find_sessions, EOS_SessionSearch_Find)
    LOAD_API(search_result_count, EOS_SessionSearch_GetSearchResultCount)
    LOAD_API(copy_search_result, EOS_SessionSearch_CopySearchResultByIndex)
    LOAD_API(release_session_search, EOS_SessionSearch_Release)
    LOAD_API(join_session, EOS_Sessions_JoinSession)
    LOAD_API(release_session_details, EOS_SessionDetails_Release)
    return true;
}

#undef LOAD_API

bool g_auth_done = false;
bool g_connect_done = false;
bool g_session_update_done = false;
bool g_session_find_done = false;
bool g_session_join_done = false;
bool g_connection_requested = false;
EOS_EResult g_auth_result = EOS_EResult::EOS_UnexpectedError;
EOS_EResult g_connect_result = EOS_EResult::EOS_UnexpectedError;
EOS_EResult g_session_update_result = EOS_EResult::EOS_UnexpectedError;
EOS_EResult g_session_find_result = EOS_EResult::EOS_UnexpectedError;
EOS_EResult g_session_join_result = EOS_EResult::EOS_UnexpectedError;
EOS_EpicAccountId g_local_epic = 0;
EOS_ProductUserId g_local_product = 0;
EOS_ProductUserId g_request_peer = 0;
EOS_P2P_SocketId g_request_socket = {};

void EOS_CALL on_auth_login(const EOS_Auth_LoginCallbackInfo* info) {
    g_auth_done = true;
    g_auth_result = info->ResultCode;
    g_local_epic = info->LocalUserId;
}

void EOS_CALL on_connect_login(const EOS_Connect_LoginCallbackInfo* info) {
    g_connect_done = true;
    g_connect_result = info->ResultCode;
    g_local_product = info->LocalUserId;
}

void EOS_CALL on_session_update(const EOS_Sessions_UpdateSessionCallbackInfo* info) {
    g_session_update_done = true;
    g_session_update_result = info->ResultCode;
}

void EOS_CALL on_session_find(const EOS_SessionSearch_FindCallbackInfo* info) {
    g_session_find_done = true;
    g_session_find_result = info->ResultCode;
}

void EOS_CALL on_session_join(const EOS_Sessions_JoinSessionCallbackInfo* info) {
    g_session_join_done = true;
    g_session_join_result = info->ResultCode;
}

void EOS_CALL on_connection_request(const EOS_P2P_OnIncomingConnectionRequestInfo* info) {
    if (info->RemoteUserId == 0 || info->SocketId == 0) {
        return;
    }
    g_request_peer = info->RemoteUserId;
    g_request_socket = *info->SocketId;
    g_connection_requested = true;
}

void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

bool write_text(const std::string& path, const std::string& value) {
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
    return file.good();
}

bool read_text(const std::string& path, std::string& value) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }
    value.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !value.empty();
}

bool wait_for_file(const sdk_api& api, EOS_HPlatform platform, const std::string& path,
                   std::string& value, int ticks = 1500) {
    for (int i = 0; i < ticks; i++) {
        api.platform_tick(platform);
        if (read_text(path, value)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

template <class predicate_type>
bool pump_until(const sdk_api& api, EOS_HPlatform platform, predicate_type predicate,
                int ticks = 1500) {
    for (int i = 0; i < ticks; i++) {
        api.platform_tick(platform);
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool product_id_string(const sdk_api& api, EOS_ProductUserId id, std::string& value) {
    char buffer[128] = {};
    int32_t length = static_cast<int32_t>(sizeof(buffer));
    if (api.puid_to_string(id, buffer, &length) != EOS_EResult::EOS_Success) {
        return false;
    }
    value = buffer;
    return !value.empty();
}

int fail(const std::string& result_path, const std::string& reason) {
    write_text(result_path, "error:" + reason);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        return 2;
    }
    const std::string library_path = argv[1];
    const std::string role = argv[2];
    const std::string data_dir = argv[3];
    const std::string run_dir = argv[4];
    const std::string coord_dir = argv[5];
    const std::string display_name = argv[6];
    const bool alice = role == "alice";
    if (!alice && role != "bob") {
        return 2;
    }

    const std::string result_path = coord_dir + "/" + role + ".result";
    const std::string own_id_path = coord_dir + "/" + role + ".id";
    const std::string peer_role = alice ? "bob" : "alice";
    const std::string peer_id_path = coord_dir + "/" + peer_role + ".id";
    const std::string host_ready_path = coord_dir + "/host.ready";
    const std::string joined_path = coord_dir + "/session.joined";
    const std::string own_done_path = coord_dir + "/" + role + ".done";
    const std::string peer_done_path = coord_dir + "/" + peer_role + ".done";

    set_env("EOSR_DATA_DIR", data_dir);
    set_env("EOSR_RUN_DIR", run_dir);
    set_env("EOSR_TRACE", "full");
    set_env("EOSR_INSTANCE_LABEL", role);
    set_env("EOSR_DISPLAY_NAME", display_name);
    set_env("EOSR_DISCOVERY_PORTS", "45920-45929");
    set_env("EOSR_PEER_SEEDS", "127.0.0.1");

    eosr::platform::dynamic_library library;
    if (!library.open(library_path.c_str())) {
        return fail(result_path, "library_open");
    }
    sdk_api api = {};
    if (!load_api(library, api)) {
        return fail(result_path, "symbol_resolution");
    }

    EOS_InitializeOptions initialize = {};
    initialize.ApiVersion = EOS_INITIALIZE_API_LATEST;
    initialize.ProductName = "AbiMeshProbe";
    initialize.ProductVersion = "1.0";
    if (api.initialize(&initialize) != EOS_EResult::EOS_Success) {
        return fail(result_path, "initialize");
    }

    EOS_Platform_Options platform_options = {};
    platform_options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform_options.ProductId = "eosr-abi-mesh-probe";
    platform_options.SandboxId = "eosr-alpha";
    platform_options.DeploymentId = "eosr-lan";
    platform_options.ClientCredentials.ClientId = "probe";
    platform_options.ClientCredentials.ClientSecret = "probe";
    EOS_HPlatform platform = api.platform_create(&platform_options);
    if (platform == 0) {
        api.shutdown();
        return fail(result_path, "platform_create");
    }

    EOS_HAuth auth = api.get_auth(platform);
    EOS_HConnect connect = api.get_connect(platform);
    EOS_HFriends friends = api.get_friends(platform);
    EOS_HP2P p2p = api.get_p2p(platform);
    EOS_HSessions sessions = api.get_sessions(platform);
    EOS_HUserInfo userinfo = api.get_userinfo(platform);
    if (auth == 0 || connect == 0 || friends == 0 || p2p == 0 || sessions == 0 || userinfo == 0) {
        return fail(result_path, "interface_getter");
    }

    EOS_Auth_Credentials auth_credentials = {};
    auth_credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    auth_credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
    auth_credentials.Token = "probe";
    EOS_Auth_LoginOptions auth_options = {};
    auth_options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    auth_options.Credentials = &auth_credentials;
    api.auth_login(auth, &auth_options, 0, on_auth_login);

    EOS_Connect_Credentials connect_credentials = {};
    connect_credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    connect_credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    connect_credentials.Token = "probe";
    EOS_Connect_LoginOptions connect_options = {};
    connect_options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    connect_options.Credentials = &connect_credentials;
    api.connect_login(connect, &connect_options, 0, on_connect_login);

    if (!pump_until(api, platform, []() { return g_auth_done && g_connect_done; }) ||
        g_auth_result != EOS_EResult::EOS_Success ||
        g_connect_result != EOS_EResult::EOS_Success ||
        g_local_epic == 0 || g_local_product == 0) {
        return fail(result_path, "login");
    }

    std::string own_product_id;
    if (!product_id_string(api, g_local_product, own_product_id) ||
        !write_text(own_id_path, own_product_id)) {
        return fail(result_path, "local_identity");
    }
    std::string peer_product_id;
    if (!wait_for_file(api, platform, peer_id_path, peer_product_id)) {
        return fail(result_path, "peer_identity");
    }
    EOS_ProductUserId peer_product = api.puid_from_string(peer_product_id.c_str());
    if (peer_product == 0 || peer_product_id == own_product_id) {
        return fail(result_path, "distinct_identity");
    }

    EOS_Friends_GetFriendsCountOptions count_options = {};
    count_options.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
    count_options.LocalUserId = g_local_epic;
    if (!pump_until(api, platform, [&]() {
            return api.friends_count(friends, &count_options) == 1;
        })) {
        return fail(result_path, "friend_discovery");
    }

    EOS_Friends_GetFriendAtIndexOptions friend_options = {};
    friend_options.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
    friend_options.LocalUserId = g_local_epic;
    friend_options.Index = 0;
    EOS_EpicAccountId peer_epic = api.friend_at_index(friends, &friend_options);
    if (peer_epic == 0) {
        return fail(result_path, "friend_identity");
    }

    EOS_UserInfo_CopyUserInfoOptions user_options = {};
    user_options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    user_options.LocalUserId = g_local_epic;
    user_options.TargetUserId = peer_epic;
    const std::string expected_name = alice ? "Bob" : "Alice";
    bool name_seen = false;
    if (!pump_until(api, platform, [&]() {
            EOS_UserInfo* info = 0;
            const EOS_EResult result = api.copy_user_info(userinfo, &user_options, &info);
            if (result == EOS_EResult::EOS_Success && info != 0) {
                name_seen = info->DisplayName != 0 && info->DisplayName == expected_name;
                api.userinfo_release(info);
            }
            return name_seen;
        })) {
        return fail(result_path, "userinfo");
    }

    EOS_P2P_SocketId socket = {};
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::strncpy(socket.SocketName, "probe", EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
    EOS_NotificationId request_id = EOS_INVALID_NOTIFICATIONID;
    if (!alice) {
        EOS_P2P_AddNotifyPeerConnectionRequestOptions notify = {};
        notify.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
        notify.LocalUserId = g_local_product;
        notify.SocketId = &socket;
        request_id = api.add_connection_request(
            p2p, &notify, 0, on_connection_request);
        if (request_id == EOS_INVALID_NOTIFICATIONID) {
            return fail(result_path, "p2p_notify");
        }
    }

    EOS_HSessionSearch search = 0;
    EOS_HSessionDetails details = 0;
    if (alice) {
        EOS_Sessions_CreateSessionModificationOptions create = {};
        create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
        create.SessionName = "probe-host";
        create.BucketId = "eosr-alpha";
        create.MaxPlayers = 2;
        create.LocalUserId = g_local_product;
        EOS_HSessionModification modification = 0;
        if (api.create_session_modification(sessions, &create, &modification) !=
                EOS_EResult::EOS_Success || modification == 0) {
            return fail(result_path, "session_create");
        }
        EOS_Sessions_UpdateSessionOptions update = {};
        update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
        update.SessionModificationHandle = modification;
        api.update_session(sessions, &update, 0, on_session_update);
        const bool updated = pump_until(api, platform, []() { return g_session_update_done; });
        api.release_session_modification(modification);
        if (!updated || g_session_update_result != EOS_EResult::EOS_Success ||
            !write_text(host_ready_path, "ready")) {
            return fail(result_path, "session_host");
        }
        std::string joined;
        if (!wait_for_file(api, platform, joined_path, joined)) {
            return fail(result_path, "session_join_wait");
        }
    } else {
        std::string ready;
        if (!wait_for_file(api, platform, host_ready_path, ready)) {
            return fail(result_path, "session_host_wait");
        }
        EOS_Sessions_CreateSessionSearchOptions create_search = {};
        create_search.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
        create_search.MaxSearchResults = 4;
        if (api.create_session_search(sessions, &create_search, &search) !=
                EOS_EResult::EOS_Success || search == 0) {
            return fail(result_path, "session_search_create");
        }
        EOS_Sessions_AttributeData bucket = {};
        bucket.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
        bucket.Key = EOS_SESSIONS_SEARCH_BUCKET_ID;
        bucket.ValueType = EOS_EAttributeType::EOS_AT_STRING;
        bucket.Value.AsUtf8 = "eosr-alpha";
        EOS_SessionSearch_SetParameterOptions parameter = {};
        parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
        parameter.Parameter = &bucket;
        parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
        if (api.set_session_search_parameter(search, &parameter) != EOS_EResult::EOS_Success) {
            return fail(result_path, "session_search_parameter");
        }
        EOS_SessionSearch_FindOptions find = {};
        find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
        find.LocalUserId = g_local_product;
        api.find_sessions(search, &find, 0, on_session_find);
        if (!pump_until(api, platform, []() { return g_session_find_done; }) ||
            g_session_find_result != EOS_EResult::EOS_Success) {
            return fail(result_path, "session_search");
        }
        EOS_SessionSearch_GetSearchResultCountOptions result_count = {};
        result_count.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
        if (api.search_result_count(search, &result_count) != 1) {
            return fail(result_path, "session_result_count");
        }
        EOS_SessionSearch_CopySearchResultByIndexOptions copy = {};
        copy.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
        copy.SessionIndex = 0;
        if (api.copy_search_result(search, &copy, &details) != EOS_EResult::EOS_Success ||
            details == 0) {
            return fail(result_path, "session_result_copy");
        }
        EOS_Sessions_JoinSessionOptions join = {};
        join.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
        join.SessionName = "probe-guest";
        join.SessionHandle = details;
        join.LocalUserId = g_local_product;
        api.join_session(sessions, &join, 0, on_session_join);
        if (!pump_until(api, platform, []() { return g_session_join_done; }) ||
            g_session_join_result != EOS_EResult::EOS_Success ||
            !write_text(joined_path, "joined")) {
            return fail(result_path, "session_join");
        }
    }

    const char payload[] = "abi-private-payload";
    const uint32_t payload_size = static_cast<uint32_t>(sizeof(payload) - 1);
    if (alice) {
        EOS_P2P_SendPacketOptions send = {};
        send.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
        send.LocalUserId = g_local_product;
        send.RemoteUserId = peer_product;
        send.SocketId = &socket;
        send.Channel = 7;
        send.DataLengthBytes = payload_size;
        send.Data = payload;
        send.bAllowDelayedDelivery = EOS_TRUE;
        send.bDisableAutoAcceptConnection = EOS_FALSE;
        send.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
        if (api.send_packet(p2p, &send) != EOS_EResult::EOS_Success) {
            return fail(result_path, "p2p_send");
        }
        std::string bob_done;
        if (!wait_for_file(api, platform, peer_done_path, bob_done) ||
            !write_text(own_done_path, "done")) {
            return fail(result_path, "p2p_delivery_wait");
        }
    } else {
        bool accepted = false;
        uint32_t packet_size = 0;
        EOS_P2P_GetNextReceivedPacketSizeOptions next = {};
        next.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
        next.LocalUserId = g_local_product;
        if (!pump_until(api, platform, [&]() {
                if (g_connection_requested && !accepted) {
                    EOS_P2P_AcceptConnectionOptions accept = {};
                    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
                    accept.LocalUserId = g_local_product;
                    accept.RemoteUserId = g_request_peer;
                    accept.SocketId = &g_request_socket;
                    accepted = api.accept_connection(p2p, &accept) == EOS_EResult::EOS_Success;
                }
                return accepted &&
                    api.next_packet_size(p2p, &next, &packet_size) == EOS_EResult::EOS_Success;
            }) || packet_size != payload_size) {
            return fail(result_path, "p2p_receive_wait");
        }
        EOS_P2P_ReceivePacketOptions receive = {};
        receive.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
        receive.LocalUserId = g_local_product;
        receive.MaxDataSizeBytes = 32;
        EOS_ProductUserId from = 0;
        EOS_P2P_SocketId from_socket = {};
        uint8_t channel = 0;
        unsigned char received[32] = {};
        uint32_t written = 0;
        if (api.receive_packet(p2p, &receive, &from, &from_socket, &channel, received, &written) !=
                EOS_EResult::EOS_Success || written != payload_size || channel != 7 ||
            std::memcmp(received, payload, payload_size) != 0 ||
            std::string(from_socket.SocketName) != "probe") {
            return fail(result_path, "p2p_receive");
        }
        std::string from_id;
        if (!product_id_string(api, from, from_id) || from_id != peer_product_id ||
            !write_text(own_done_path, "done")) {
            return fail(result_path, "p2p_source");
        }
        std::string alice_done;
        if (!wait_for_file(api, platform, peer_done_path, alice_done)) {
            return fail(result_path, "peer_shutdown_barrier");
        }
    }

    if (request_id != EOS_INVALID_NOTIFICATIONID) {
        api.remove_connection_request(p2p, request_id);
    }
    if (details != 0) { api.release_session_details(details); }
    if (search != 0) { api.release_session_search(search); }
    api.platform_release(platform);
    const EOS_EResult shutdown_result = api.shutdown();
    library.close();
    if (shutdown_result != EOS_EResult::EOS_Success) {
        return fail(result_path, "shutdown");
    }
    return write_text(result_path, "ok") ? 0 : 1;
}
