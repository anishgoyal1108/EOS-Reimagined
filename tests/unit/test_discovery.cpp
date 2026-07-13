#include "doctest.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <cstring>

#include "eos_types.h"

#include "common/byte_buffer.h"
#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/i_run_network.h"
#include "core/settings.h"
#include "interfaces/connect.h"
#include "interfaces/p2p.h"
#include "interfaces/lobby.h"
#include "interfaces/presence.h"
#include "interfaces/sessions.h"
#include "net/message_router.h"
#include "net/messages.h"
#include "net/wire.h"
#include "platform/socket.h"

using namespace eosr;

namespace {

const char* const alice_id = "1111111111111111111111111111111a";
const char* const bob_id = "2222222222222222222222222222222b";

// Records the peer-lifecycle envelopes the router synthesizes, and any payload it carries.
struct event_listener : i_run_network {
    std::vector<std::string> connected;
    std::vector<std::string> disconnected;
    std::vector<std::string> messages;

    bool on_network_message(const net_envelope& msg) {
        if (msg.type_tag == static_cast<u16>(message_type::peer_connected)) {
            connected.push_back(msg.source_id);
        } else if (msg.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
            disconnected.push_back(msg.source_id);
        } else {
            byte_reader reader(msg.payload.data(), msg.payload.size());
            emu_infos info;
            if (deserialize(reader, info)) {
                messages.push_back(info.username);
            }
        }
        return true;
    }
};

// Counts frames and flags any that failed to decode, which is what a truncated stream looks like.
struct counting_listener : i_run_network {
    int count;
    int corrupt;

    counting_listener() : count(0), corrupt(0) {}

    bool on_network_message(const net_envelope& msg) {
        byte_reader reader(msg.payload.data(), msg.payload.size());
        emu_infos info;
        if (deserialize(reader, info)) {
            count++;
        } else {
            corrupt++;
        }
        return true;
    }
};

// A private discovery range on loopback, so the tests never advertise onto the real network.
net_config discovery_config(u16 first) {
    net_config config;
    config.discovery_port_first = first;
    config.discovery_port_last = static_cast<u16>(first + 3);
    config.broadcast_addresses.push_back(platform::ip_loopback);
    return config;
}

bool start_router(message_router& router, const std::string& id, const std::string& game, u16 first) {
    router.set_identity(id, game);
    router.set_config(discovery_config(first));
    return router.start();
}

net_envelope make_hello(const std::string& source, const std::string& dest,
                        const std::string& username) {
    emu_infos info;
    info.username = username;
    byte_writer writer;
    serialize(writer, info);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::emu_infos_response);
    envelope.source_id = source;
    envelope.dest_id = dest;
    envelope.payload = writer.data();
    return envelope;
}

// Tick both routers until `done` or we run out of patience. Discovery is timer-driven over real
// sockets, so the test drives real time rather than faking it.
template <class predicate>
void pump(message_router& a, message_router& b, predicate done, int max_ms = 4000) {
    for (int elapsed = 0; elapsed < max_ms && !done(); elapsed += 10) {
        a.cb_run_frame();
        b.cb_run_frame();
        if (!done()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

} // namespace

TEST_CASE("two instances discover each other and mesh over loopback") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45710));
    REQUIRE(start_router(bob, bob_id, "same-game", 45710));

    event_listener alice_events;
    event_listener bob_events;
    alice.register_listener(message_type::peer_connected, &alice_events);
    bob.register_listener(message_type::peer_connected, &bob_events);

    pump(alice, bob, [&]() {
        return !alice.peer_ids().empty() && !bob.peer_ids().empty();
    });

    REQUIRE(alice.peer_ids().size() == 1);
    REQUIRE(bob.peer_ids().size() == 1);
    CHECK(alice.peer_ids()[0] == bob_id);
    CHECK(bob.peer_ids()[0] == alice_id);

    // Each side was told its peer arrived.
    REQUIRE(alice_events.connected.size() == 1);
    CHECK(alice_events.connected[0] == bob_id);
    REQUIRE(bob_events.connected.size() == 1);
    CHECK(bob_events.connected[0] == alice_id);

    alice.stop();
    bob.stop();
    platform::net_shutdown();
}

TEST_CASE("a meshed peer receives a directly addressed message") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45720));
    REQUIRE(start_router(bob, bob_id, "same-game", 45720));

    event_listener bob_events;
    bob.register_listener(message_type::emu_infos_response, &bob_events);

    pump(alice, bob, [&]() { return !alice.peer_ids().empty(); });
    REQUIRE(alice.peer_ids().size() == 1);

    // The whole point of the milestone: a message crosses the wire to the other instance.
    CHECK(alice.send(make_hello(alice_id, bob_id, "InfernusHawk")));
    pump(alice, bob, [&]() { return !bob_events.messages.empty(); });

    REQUIRE(bob_events.messages.size() == 1);
    CHECK(bob_events.messages[0] == "InfernusHawk");

    alice.stop();
    bob.stop();
    platform::net_shutdown();
}

TEST_CASE("a broadcast with no destination reaches every peer") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45730));
    REQUIRE(start_router(bob, bob_id, "same-game", 45730));

    event_listener bob_events;
    bob.register_listener(message_type::emu_infos_response, &bob_events);

    pump(alice, bob, [&]() { return !alice.peer_ids().empty(); });
    REQUIRE(alice.peer_ids().size() == 1);

    CHECK(alice.send(make_hello(alice_id, std::string(), "Broadcaster")));
    pump(alice, bob, [&]() { return !bob_events.messages.empty(); });

    REQUIRE(bob_events.messages.size() == 1);
    CHECK(bob_events.messages[0] == "Broadcaster");

    alice.stop();
    bob.stop();
    platform::net_shutdown();
}

TEST_CASE("instances running a different game never mesh") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router stranger;
    REQUIRE(start_router(alice, alice_id, "game-one", 45740));
    REQUIRE(start_router(stranger, bob_id, "game-two", 45740));

    // Both advertise into the same range, but the game ids differ, so neither adopts the other.
    pump(alice, stranger, [&]() { return false; }, 1500);

    CHECK(alice.peer_ids().empty());
    CHECK(stranger.peer_ids().empty());

    alice.stop();
    stranger.stop();
    platform::net_shutdown();
}

TEST_CASE("sending to an unknown peer fails rather than going nowhere quietly") {
    REQUIRE(platform::net_init());
    message_router alice;
    REQUIRE(start_router(alice, alice_id, "same-game", 45750));

    CHECK_FALSE(alice.send(make_hello(alice_id, bob_id, "Nobody")));

    alice.stop();
    platform::net_shutdown();
}

// A peer that hangs up must be noticed at once, not when its advertisement finally times out ten
// seconds later.
TEST_CASE("a peer that hangs up is dropped promptly") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45770));
    REQUIRE(start_router(bob, bob_id, "same-game", 45770));

    event_listener alice_events;
    alice.register_listener(message_type::peer_disconnected, &alice_events);

    pump(alice, bob, [&]() { return !alice.peer_ids().empty() && !bob.peer_ids().empty(); });
    REQUIRE(alice.peer_ids().size() == 1);

    bob.stop(); // the peer's process goes away, closing its side of the connection

    // Well inside the advertisement timeout: the hangup itself is what tells us.
    for (int i = 0; i < 20 && !alice.peer_ids().empty(); i++) {
        alice.cb_run_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(alice.peer_ids().empty());
    REQUIRE(alice_events.disconnected.size() == 1);
    CHECK(alice_events.disconnected[0] == bob_id);

    alice.stop();
    platform::net_shutdown();
}

// Writing faster than a peer reads fills the kernel buffer. That is backpressure, not a broken
// peer: the connection must survive it, and not one byte of the stream may be lost, or every frame
// after the truncated one would be misread.
TEST_CASE("a burst that fills the send buffer neither drops the peer nor corrupts the stream") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45780));
    REQUIRE(start_router(bob, bob_id, "same-game", 45780));

    counting_listener bob_counts;
    bob.register_listener(message_type::emu_infos_response, &bob_counts);

    pump(alice, bob, [&]() { return !alice.peer_ids().empty(); });
    REQUIRE(alice.peer_ids().size() == 1);

    // Bob is not reading, so keep writing until the kernel refuses more. Driving it by the backlog
    // rather than a fixed count means the test still bites on a machine with a bigger buffer.
    const int max_messages = 4000;
    const std::string big(4000, 'x');
    int sent = 0;
    while (sent < max_messages && alice.pending_output_bytes() == 0) {
        CHECK(alice.send(make_hello(alice_id, bob_id, big)));
        sent++;
    }
    // If this fails the test proved nothing: we never actually filled the buffer.
    REQUIRE(alice.pending_output_bytes() > 0);
    // The peer is alive. A full buffer said "not now", not "never".
    CHECK(alice.peer_ids().size() == 1);

    pump(alice, bob, [&]() { return bob_counts.count >= sent; }, 15000);

    CHECK(alice.peer_ids().size() == 1);          // still meshed
    CHECK(alice.pending_output_bytes() == 0);     // the backlog drained
    CHECK(bob_counts.count == sent);              // every frame arrived
    CHECK(bob_counts.corrupt == 0);               // and none was truncated
    CHECK(sent > 0);

    alice.stop();
    bob.stop();
    platform::net_shutdown();
}

// The milestone's real payoff, driven the way a game drives it. Two ordinary instances — nobody
// configured, exactly what two copies of a game start life as — discover each other, negotiate a
// P2P connection through the notification the game listens on, and exchange a packet.
namespace {

void EOS_CALL on_connect_login(const EOS_Connect_LoginCallbackInfo*) {}

int g_request_count = 0;
std::string g_request_peer;
std::string g_request_socket;
void EOS_CALL on_request(const EOS_P2P_OnIncomingConnectionRequestInfo* info) {
    g_request_count++;
    g_request_peer = (info->RemoteUserId != 0) ? info->RemoteUserId->id_str : std::string();
    g_request_socket = (info->SocketId != 0) ? info->SocketId->SocketName : std::string();
}

int g_alice_established = 0;
int g_bob_established = 0;
void EOS_CALL on_alice_established(const EOS_P2P_OnPeerConnectionEstablishedInfo*) {
    g_alice_established++;
}
void EOS_CALL on_bob_established(const EOS_P2P_OnPeerConnectionEstablishedInfo*) {
    g_bob_established++;
}

} // namespace

TEST_CASE("two ordinary instances negotiate a connection and exchange a packet") {
    REQUIRE(platform::net_init());
    g_request_count = 0;
    g_alice_established = 0;
    g_bob_established = 0;

    // Nobody calls set_username: this is what a game gets out of the box. The two must still be
    // different players, or each would see the other's advertisement as its own.
    sdk_settings alice_settings;
    sdk_settings bob_settings;
    EOS_Platform_Options options = {};
    options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId = "co-op-game";
    alice_settings.apply_platform_options(&options);
    bob_settings.apply_platform_options(&options);
    REQUIRE(alice_settings.product_user_id() != bob_settings.product_user_id());

    callback_manager alice_callbacks;
    callback_manager bob_callbacks;
    message_router alice_net;
    message_router bob_net;
    sdk_p2p alice(alice_settings, alice_callbacks, alice_net);
    sdk_p2p bob(bob_settings, bob_callbacks, bob_net);
    alice.emu_init();
    bob.emu_init();

    REQUIRE(start_router(alice_net, alice_settings.product_user_id(),
                         alice_settings.product_id(), 45760));
    REQUIRE(start_router(bob_net, bob_settings.product_user_id(),
                         bob_settings.product_id(), 45760));

    EOS_ProductUserId alice_id_h =
        id_registry::instance().get_product_user_id(alice_settings.product_user_id());
    EOS_ProductUserId bob_id_h =
        id_registry::instance().get_product_user_id(bob_settings.product_user_id());

    // Bob listens for someone wanting to talk to him, and both watch for the connection opening.
    REQUIRE(bob.add_notify_connection_request(0, 0, on_request) != EOS_INVALID_NOTIFICATIONID);
    REQUIRE(bob.add_notify_connection_established(0, on_bob_established) !=
            EOS_INVALID_NOTIFICATIONID);
    REQUIRE(alice.add_notify_connection_established(0, on_alice_established) !=
            EOS_INVALID_NOTIFICATIONID);

    pump(alice_net, bob_net, [&]() {
        return !alice_net.peer_ids().empty() && !bob_net.peer_ids().empty();
    });
    REQUIRE(alice_net.peer_ids().size() == 1);
    REQUIRE(bob_net.peer_ids().size() == 1);

    // Alice sends before anyone has agreed to anything. Delayed delivery is what a game uses to
    // say "hold this until the connection comes up" — without it the packet would be dropped.
    EOS_P2P_SocketId socket = {};
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::strncpy(socket.SocketName, "game", EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
    const u8 payload[] = {0xc0, 0xff, 0xee};

    EOS_P2P_SendPacketOptions send = {};
    send.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    send.LocalUserId = alice_id_h;
    send.RemoteUserId = bob_id_h;
    send.SocketId = &socket;
    send.Channel = 7;
    send.DataLengthBytes = sizeof(payload);
    send.Data = payload;
    send.bAllowDelayedDelivery = EOS_TRUE;
    send.bDisableAutoAcceptConnection = EOS_FALSE;
    REQUIRE(alice.send_packet(&send) == EOS_EResult::EOS_Success);

    // Bob is asked to connect, exactly as a game would learn of it.
    pump(alice_net, bob_net, [&]() {
        bob_callbacks.tick();
        return g_request_count > 0;
    });
    REQUIRE(g_request_count == 1);
    CHECK(g_request_peer == alice_settings.product_user_id());
    CHECK(g_request_socket == "game");

    // Nothing has been delivered: Bob has not agreed yet.
    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = bob_id_h;
    u32 size = 0;
    CHECK(bob.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_NotFound);

    // Bob accepts. Now both sides are agreed, and Alice's held packet goes out.
    EOS_P2P_AcceptConnectionOptions accept = {};
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = bob_id_h;
    accept.RemoteUserId = alice_id_h;
    accept.SocketId = &socket;
    REQUIRE(bob.accept_connection(&accept) == EOS_EResult::EOS_Success);

    pump(alice_net, bob_net, [&]() {
        alice_callbacks.tick();
        bob_callbacks.tick();
        return bob.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_Success;
    });
    REQUIRE(size == 3);

    // Both sides were told the connection came up.
    CHECK(g_bob_established == 1);
    CHECK(g_alice_established == 1);

    EOS_P2P_ReceivePacketOptions receive = {};
    receive.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    receive.LocalUserId = bob_id_h;
    receive.MaxDataSizeBytes = 16;
    EOS_ProductUserId from = 0;
    EOS_P2P_SocketId from_socket = {};
    u8 channel = 0;
    u8 buffer[16] = {0};
    u32 written = 0;
    REQUIRE(bob.receive_packet(&receive, &from, &from_socket, &channel, buffer, &written) ==
            EOS_EResult::EOS_Success);

    // The bytes Alice sent are the bytes Bob got, tagged with her id, her socket and her channel.
    CHECK(written == 3);
    CHECK(buffer[0] == 0xc0);
    CHECK(buffer[1] == 0xff);
    CHECK(buffer[2] == 0xee);
    CHECK(channel == 7);
    CHECK(std::string(from_socket.SocketName) == "game");
    REQUIRE((from != 0));
    CHECK(from->id_str == alice_settings.product_user_id());

    alice.emu_deinit();
    bob.emu_deinit();
    alice_net.stop();
    bob_net.stop();
    platform::net_shutdown();
}

// The roster is what every other interface keys off, so a peer joining the mesh has to reach it.
// Before the mesh told Connect about peers, this stayed empty no matter who turned up.
TEST_CASE("connect learns about a peer that joins the mesh") {
    REQUIRE(platform::net_init());

    sdk_settings alice_settings;
    sdk_settings bob_settings;
    alice_settings.set_username("Alice");
    bob_settings.set_username("Bob");
    EOS_Platform_Options options = {};
    options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId = "co-op-game";
    alice_settings.apply_platform_options(&options);
    bob_settings.apply_platform_options(&options);

    callback_manager alice_callbacks;
    callback_manager bob_callbacks;
    message_router alice_net;
    message_router bob_net;
    sdk_connect alice(alice_settings, alice_callbacks, alice_net);
    sdk_connect bob(bob_settings, bob_callbacks, bob_net);
    alice.emu_init();
    bob.emu_init();

    REQUIRE(start_router(alice_net, alice_settings.product_user_id(),
                         alice_settings.product_id(), 45790));
    REQUIRE(start_router(bob_net, bob_settings.product_user_id(),
                         bob_settings.product_id(), 45790));

    // Both log in, which is what makes them willing to introduce themselves.
    EOS_Connect_Credentials credentials = {};
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Token = "device";
    credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_LoginOptions login = {};
    login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    login.Credentials = &credentials;
    alice.login(&login, 0, on_connect_login);
    bob.login(&login, 0, on_connect_login);
    alice_callbacks.tick();
    bob_callbacks.tick();

    pump(alice_net, bob_net, [&]() {
        return alice.known_peer_count() > 0 && bob.known_peer_count() > 0;
    });

    CHECK(alice.known_peer_count() == 1);
    CHECK(bob.known_peer_count() == 1);

    alice.emu_deinit();
    bob.emu_deinit();
    alice_net.stop();
    bob_net.stop();
    platform::net_shutdown();
}

// Matchmaking, for real: one instance hosts a game and the other finds it over the mesh and joins
// it. This is the loop a co-op player actually walks through, with nothing faked in between.
namespace {

bool g_host_updated = false;
std::string g_hosted_id;
void EOS_CALL on_host_update(const EOS_Sessions_UpdateSessionCallbackInfo* info) {
    g_host_updated = info->ResultCode == EOS_EResult::EOS_Success;
    g_hosted_id = (info->SessionId != 0) ? info->SessionId : "";
}

bool g_found = false;
void EOS_CALL on_found(const EOS_SessionSearch_FindCallbackInfo* info) {
    g_found = info->ResultCode == EOS_EResult::EOS_Success;
}

bool g_joined = false;
EOS_EResult g_join_result = EOS_EResult::EOS_UnexpectedError;
void EOS_CALL on_joined(const EOS_Sessions_JoinSessionCallbackInfo* info) {
    g_joined = true;
    g_join_result = info->ResultCode;
}

void EOS_CALL ignore_login(const EOS_Connect_LoginCallbackInfo*) {}
void EOS_CALL on_left(const EOS_Sessions_DestroySessionCallbackInfo*) {}

} // namespace

TEST_CASE("one instance hosts a game and another finds it and joins") {
    REQUIRE(platform::net_init());
    g_host_updated = false;
    g_found = false;
    g_joined = false;

    // Two ordinary instances of the same game.
    sdk_settings host_settings;
    sdk_settings guest_settings;
    EOS_Platform_Options options = {};
    options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId = "co-op-game";
    host_settings.apply_platform_options(&options);
    guest_settings.apply_platform_options(&options);
    REQUIRE(host_settings.product_user_id() != guest_settings.product_user_id());

    callback_manager host_cb;
    callback_manager guest_cb;
    message_router host_net;
    message_router guest_net;
    sdk_connect host_connect(host_settings, host_cb, host_net);
    sdk_connect guest_connect(guest_settings, guest_cb, guest_net);
    sdk_sessions host(host_settings, host_cb, host_net, host_connect);
    sdk_sessions guest(guest_settings, guest_cb, guest_net, guest_connect);
    host_connect.emu_init();
    guest_connect.emu_init();
    host.emu_init();
    guest.emu_init();

    REQUIRE(start_router(host_net, host_settings.product_user_id(), host_settings.product_id(), 45800));
    REQUIRE(start_router(guest_net, guest_settings.product_user_id(), guest_settings.product_id(), 45800));

    // Both log in, so each learns the other is a real player it has met.
    EOS_Connect_Credentials credentials = {};
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Token = "device";
    credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_LoginOptions login = {};
    login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    login.Credentials = &credentials;
    host_connect.login(&login, 0, ignore_login);
    guest_connect.login(&login, 0, ignore_login);
    host_cb.tick();
    guest_cb.tick();

    // The host has to know the guest before it will seat it.
    pump(host_net, guest_net, [&]() {
        return host_connect.known_peer_count() > 0 && guest_connect.known_peer_count() > 0;
    });
    REQUIRE(host_connect.known_peer_count() == 1);

    EOS_ProductUserId host_id =
        id_registry::instance().get_product_user_id(host_settings.product_user_id());
    EOS_ProductUserId guest_id =
        id_registry::instance().get_product_user_id(guest_settings.product_user_id());

    // The host puts a game up, with a map the guest can search for.
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.SessionName = "my-game";
    create.BucketId = "Region:Coop";
    create.MaxPlayers = 4;
    create.LocalUserId = host_id;
    EOS_HSessionModification modification = 0;
    REQUIRE(host.create_session_modification(&create, &modification) == EOS_EResult::EOS_Success);

    EOS_Sessions_AttributeData map_name = {};
    map_name.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    map_name.Key = "map";
    map_name.ValueType = EOS_EAttributeType::EOS_AT_STRING;
    map_name.Value.AsUtf8 = "crab-island";
    EOS_SessionModification_AddAttributeOptions add = {};
    add.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
    add.SessionAttribute = &map_name;
    add.AdvertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
    REQUIRE(host.modification_add_attribute(modification, &add) == EOS_EResult::EOS_Success);

    EOS_Sessions_UpdateSessionOptions update = {};
    update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    update.SessionModificationHandle = modification;
    host.update_session(&update, 0, on_host_update);
    host_cb.tick();
    host.modification_release(modification);
    REQUIRE(g_host_updated);
    REQUIRE(g_hosted_id.size() == 32);

    // The guest goes looking for a game on that map.
    EOS_Sessions_CreateSessionSearchOptions search_options = {};
    search_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    search_options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(guest.create_session_search(&search_options, &search) == EOS_EResult::EOS_Success);

    EOS_Sessions_AttributeData wanted = map_name;
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &wanted;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    REQUIRE(guest.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = guest_id;
    guest.search_find(search, &find, 0, on_found);

    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_found;
    });
    REQUIRE(g_found);
    REQUIRE(guest.search_result_count(search) == 1);

    // It is the host's game, described exactly as the host described it.
    EOS_SessionSearch_CopySearchResultByIndexOptions pick = {};
    pick.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    pick.SessionIndex = 0;
    EOS_HSessionDetails details = 0;
    REQUIRE(guest.search_copy_result(search, &pick, &details) == EOS_EResult::EOS_Success);

    EOS_SessionDetails_Info* info = 0;
    REQUIRE(guest.details_copy_info(details, &info) == EOS_EResult::EOS_Success);
    REQUIRE((info != 0));
    CHECK(std::string(info->SessionId) == g_hosted_id);
    CHECK(std::string(info->Settings->BucketId) == "Region:Coop");
    CHECK(info->Settings->NumPublicConnections == 4);
    CHECK(info->NumOpenPublicConnections == 3); // the host has a seat
    REQUIRE((info->OwnerUserId != 0));
    CHECK(info->OwnerUserId->id_str == host_settings.product_user_id());
    release_session_details_info(info);

    // And the guest joins it.
    EOS_Sessions_JoinSessionOptions join = {};
    join.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
    join.SessionName = "joined-game";
    join.SessionHandle = details;
    join.LocalUserId = guest_id;
    guest.join_session(&join, 0, on_joined);

    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_joined;
    });
    REQUIRE(g_joined);
    CHECK(g_join_result == EOS_EResult::EOS_Success);

    // The host seated the guest: two players, and a seat fewer to give.
    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    REQUIRE(host.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    CHECK(host.active_registered_count(active) == 2);

    EOS_ActiveSession_Info* active_info = 0;
    REQUIRE(host.active_copy_info(active, &active_info) == EOS_EResult::EOS_Success);
    CHECK(active_info->SessionDetails->NumOpenPublicConnections == 2);
    release_active_session_info(active_info);

    // And the guest holds the same game, under its own name for it.
    copy.SessionName = "joined-game";
    EOS_HActiveSession guest_active = 0;
    REQUIRE(guest.copy_active_session_handle(&copy, &guest_active) == EOS_EResult::EOS_Success);
    CHECK(guest.active_registered_count(guest_active) == 2);

    guest.active_release(guest_active);

    // The guest quits to the menu. Its seat has to come back, or a few rounds of joining and
    // leaving would fill the host's game with players who are not there.
    EOS_Sessions_DestroySessionOptions leave = {};
    leave.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
    leave.SessionName = "joined-game";
    guest.destroy_session(&leave, 0, on_left);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return host.active_registered_count(active) == 1;
    });
    CHECK(host.active_registered_count(active) == 1);

    EOS_ActiveSession_Info* after_leaving = 0;
    REQUIRE(host.active_copy_info(active, &after_leaving) == EOS_EResult::EOS_Success);
    CHECK(after_leaving->SessionDetails->NumOpenPublicConnections == 3); // the seat is back
    release_active_session_info(after_leaving);

    // It changes its mind and comes back.
    g_joined = false;
    guest.join_session(&join, 0, on_joined);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_joined;
    });
    REQUIRE(g_join_result == EOS_EResult::EOS_Success);
    CHECK(host.active_registered_count(active) == 2);

    // The guest has joined the host's session, so it holds a copy of it. When the guest searches,
    // it must find exactly the one game -- the host's -- and not a second phantom match from the
    // copy it merely joined. It asks the host (which answers, hosting) and seeds nothing of its own
    // (a joined session is not something to find); a result of two would mean a session showing up
    // once per member.
    EOS_Sessions_CreateSessionSearchOptions again_options = {};
    again_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    again_options.MaxSearchResults = 10;
    EOS_HSessionSearch again = 0;
    REQUIRE(guest.create_session_search(&again_options, &again) == EOS_EResult::EOS_Success);
    EOS_Sessions_AttributeData again_bucket = {};
    again_bucket.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    again_bucket.Key = EOS_SESSIONS_SEARCH_BUCKET_ID;
    again_bucket.ValueType = EOS_EAttributeType::EOS_AT_STRING;
    again_bucket.Value.AsUtf8 = "Region:Coop";
    EOS_SessionSearch_SetParameterOptions again_param = {};
    again_param.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    again_param.Parameter = &again_bucket;
    again_param.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    REQUIRE(guest.search_set_parameter(again, &again_param) == EOS_EResult::EOS_Success);
    EOS_SessionSearch_FindOptions again_find = {};
    again_find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    again_find.LocalUserId = guest_id;
    g_found = false;
    guest.search_find(again, &again_find, 0, on_found);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_found;
    });
    REQUIRE(g_found);
    CHECK(guest.search_result_count(again) == 1); // the host's game, once, not the joined copy too
    guest.search_release(again);

    // Now the host quits, and the game the guest was in is gone.
    EOS_Sessions_DestroySessionOptions shut_down = {};
    shut_down.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
    shut_down.SessionName = "my-game";
    host.destroy_session(&shut_down, 0, on_left);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return guest.copy_active_session_handle(&copy, &guest_active) == EOS_EResult::EOS_NotFound;
    });
    copy.SessionName = "joined-game";
    CHECK(guest.copy_active_session_handle(&copy, &guest_active) == EOS_EResult::EOS_NotFound);

    host.active_release(active);
    guest.details_release(details);
    guest.search_release(search);
    host.emu_deinit();
    guest.emu_deinit();
    host_connect.emu_deinit();
    guest_connect.emu_deinit();
    host_net.stop();
    guest_net.stop();
    platform::net_shutdown();
}

// Rich presence, for real: one instance goes into a game and the other sees its status, its rich
// text, and the join string a "join friend's game" button needs -- all over the mesh.
namespace {

bool g_presence_changed = false;
std::string g_changed_epic;
void EOS_CALL on_presence_changed(const EOS_Presence_PresenceChangedCallbackInfo* info) {
    g_presence_changed = true;
    if (info->PresenceUserId != 0) {
        g_changed_epic = info->PresenceUserId->id_str;
    }
}

bool g_e2e_set = false;
void EOS_CALL on_e2e_set(const EOS_Presence_SetPresenceCallbackInfo* info) {
    g_e2e_set = info->ResultCode == EOS_EResult::EOS_Success;
}

bool g_e2e_query = false;
EOS_EResult g_e2e_query_result = EOS_EResult::EOS_UnexpectedError;
void EOS_CALL on_e2e_query(const EOS_Presence_QueryPresenceCallbackInfo* info) {
    g_e2e_query = true;
    g_e2e_query_result = info->ResultCode;
}

} // namespace

TEST_CASE("one instance sets rich presence and another sees it over the mesh") {
    REQUIRE(platform::net_init());
    g_presence_changed = false;
    g_changed_epic.clear();
    g_e2e_set = false;
    g_e2e_query = false;

    sdk_settings host_settings;
    sdk_settings guest_settings;
    EOS_Platform_Options options = {};
    options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId = "presence-game";
    host_settings.apply_platform_options(&options);
    guest_settings.apply_platform_options(&options);
    REQUIRE(host_settings.epic_account_id() != guest_settings.epic_account_id());

    callback_manager host_cb;
    callback_manager guest_cb;
    message_router host_net;
    message_router guest_net;
    sdk_presence host(host_settings, host_cb, host_net);
    sdk_presence guest(guest_settings, guest_cb, guest_net);
    host.emu_init();
    guest.emu_init();

    REQUIRE(start_router(host_net, host_settings.product_user_id(), host_settings.product_id(), 45810));
    REQUIRE(start_router(guest_net, guest_settings.product_user_id(), guest_settings.product_id(), 45810));

    pump(host_net, guest_net, [&]() {
        return !host_net.peer_ids().empty() && !guest_net.peer_ids().empty();
    });
    REQUIRE(!guest_net.peer_ids().empty());

    EOS_EpicAccountId host_epic =
        id_registry::instance().get_epic_account_id(host_settings.epic_account_id());
    EOS_EpicAccountId guest_epic =
        id_registry::instance().get_epic_account_id(guest_settings.epic_account_id());

    // The guest wants to be told when a friend's presence changes.
    const EOS_NotificationId note =
        guest.add_notify_on_presence_changed(0, on_presence_changed);
    REQUIRE(note != EOS_INVALID_NOTIFICATIONID);

    // The host drops into a game, away from the menu, with a join string.
    EOS_Presence_CreatePresenceModificationOptions create = {};
    create.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
    create.LocalUserId = host_epic;
    EOS_HPresenceModification modification = 0;
    REQUIRE(host.create_presence_modification(&create, &modification) == EOS_EResult::EOS_Success);

    EOS_PresenceModification_SetStatusOptions status = {};
    status.ApiVersion = EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST;
    status.Status = EOS_Presence_EStatus::EOS_PS_Away;
    REQUIRE(host.modification_set_status(modification, &status) == EOS_EResult::EOS_Success);

    EOS_PresenceModification_SetRawRichTextOptions rich = {};
    rich.ApiVersion = EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;
    rich.RichText = "In the caves";
    REQUIRE(host.modification_set_raw_rich_text(modification, &rich) == EOS_EResult::EOS_Success);

    EOS_PresenceModification_SetJoinInfoOptions join = {};
    join.ApiVersion = EOS_PRESENCEMODIFICATION_SETJOININFO_API_LATEST;
    join.JoinInfo = "session:crab-island";
    REQUIRE(host.modification_set_join_info(modification, &join) == EOS_EResult::EOS_Success);

    EOS_Presence_SetPresenceOptions set = {};
    set.ApiVersion = EOS_PRESENCE_SETPRESENCE_API_LATEST;
    set.LocalUserId = host_epic;
    set.PresenceModificationHandle = modification;
    host.set_presence(&set, 0, on_e2e_set);
    host_cb.tick();
    host.modification_release(modification);
    REQUIRE(g_e2e_set);

    // Wait on the join string arriving: it is only ever set by SetPresence, never seeded, so it is
    // the unambiguous signal that the host's update -- not just its opening presence -- got here.
    EOS_Presence_GetJoinInfoOptions get = {};
    get.ApiVersion = EOS_PRESENCE_GETJOININFO_API_LATEST;
    get.LocalUserId = guest_epic;
    get.TargetUserId = host_epic;
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        char probe[128];
        i32 probe_length = sizeof(probe);
        return guest.get_join_info(&get, probe, &probe_length) == EOS_EResult::EOS_Success;
    });

    // The notification fired, naming the host as the one who changed.
    REQUIRE(g_presence_changed);
    CHECK(g_changed_epic == host_settings.epic_account_id());

    // The whole presence is there, exactly as the host set it.
    EOS_Presence_CopyPresenceOptions copy = {};
    copy.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
    copy.LocalUserId = guest_epic;
    copy.TargetUserId = host_epic;
    EOS_Presence_Info* info = 0;
    REQUIRE(guest.copy_presence(&copy, &info) == EOS_EResult::EOS_Success);
    REQUIRE((info != 0));
    CHECK(info->Status == EOS_Presence_EStatus::EOS_PS_Away);
    CHECK(std::string(info->RichText) == "In the caves");
    CHECK(info->UserId == host_epic);
    CHECK(std::string(info->ProductId) == "presence-game");
    release_presence_info(info);

    char buffer[128];
    i32 length = sizeof(buffer);
    REQUIRE(guest.get_join_info(&get, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "session:crab-island");

    // And a QueryPresence aimed at the host is answered over the wire.
    EOS_Presence_QueryPresenceOptions query = {};
    query.ApiVersion = EOS_PRESENCE_QUERYPRESENCE_API_LATEST;
    query.LocalUserId = guest_epic;
    query.TargetUserId = host_epic;
    guest.query_presence(&query, 0, on_e2e_query);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_e2e_query;
    });
    REQUIRE(g_e2e_query);
    CHECK(g_e2e_query_result == EOS_EResult::EOS_Success);

    guest.remove_notify_on_presence_changed(note);
    host.emu_deinit();
    guest.emu_deinit();
    host_net.stop();
    guest_net.stop();
    platform::net_shutdown();
}

// The reviewer's exact scenario: host A, participant B, searcher C. Once B has joined A's session,
// a search by C must turn up that session once -- from its host -- and never a second copy echoed
// by B, who only joined it.
namespace {

struct mesh_node {
    sdk_settings settings;
    callback_manager cb;
    message_router net;
    sdk_connect connect;
    sdk_sessions sessions;

    mesh_node() : connect(settings, cb, net), sessions(settings, cb, net, connect) {}

    void start(const char* product, u16 port) {
        EOS_Platform_Options options = {};
        options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
        options.ProductId = product;
        settings.apply_platform_options(&options);
        connect.emu_init();
        sessions.emu_init();
        REQUIRE(start_router(net, settings.product_user_id(), settings.product_id(), port));

        EOS_Connect_Credentials credentials = {};
        credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
        credentials.Token = "device";
        credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
        EOS_Connect_LoginOptions login = {};
        login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
        login.Credentials = &credentials;
        connect.login(&login, 0, ignore_login);
        cb.tick();
    }

    void stop() {
        sessions.emu_deinit();
        connect.emu_deinit();
        net.stop();
    }

    EOS_ProductUserId id() {
        return id_registry::instance().get_product_user_id(settings.product_user_id());
    }
};

template <class predicate>
void pump3(mesh_node& a, mesh_node& b, mesh_node& c, predicate done, int max_ms = 8000) {
    for (int elapsed = 0; elapsed < max_ms && !done(); elapsed += 10) {
        a.net.cb_run_frame();
        b.net.cb_run_frame();
        c.net.cb_run_frame();
        a.cb.tick();
        b.cb.tick();
        c.cb.tick();
        if (!done()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

EOS_HSessionSearch bucket_search(sdk_sessions& sessions, EOS_ProductUserId who, const char* bucket) {
    EOS_Sessions_CreateSessionSearchOptions options = {};
    options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(sessions.create_session_search(&options, &search) == EOS_EResult::EOS_Success);
    EOS_Sessions_AttributeData key = {};
    key.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    key.Key = EOS_SESSIONS_SEARCH_BUCKET_ID;
    key.ValueType = EOS_EAttributeType::EOS_AT_STRING;
    key.Value.AsUtf8 = bucket;
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &key;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    REQUIRE(sessions.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);
    return search;
}

} // namespace

TEST_CASE("a participant does not re-advertise the host's session to a third searcher") {
    REQUIRE(platform::net_init());
    mesh_node a;
    mesh_node b;
    mesh_node c;
    a.start("mesh-coop", 45820);
    b.start("mesh-coop", 45820);
    c.start("mesh-coop", 45820);

    // All three know one another.
    pump3(a, b, c, [&]() {
        return a.connect.known_peer_count() >= 2 && b.connect.known_peer_count() >= 2 &&
               c.connect.known_peer_count() >= 2;
    });
    REQUIRE(a.connect.known_peer_count() == 2);
    REQUIRE(c.connect.known_peer_count() == 2);

    // A hosts a game.
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.SessionName = "game";
    create.BucketId = "Coop";
    create.MaxPlayers = 4;
    create.LocalUserId = a.id();
    EOS_HSessionModification modification = 0;
    REQUIRE(a.sessions.create_session_modification(&create, &modification) == EOS_EResult::EOS_Success);
    EOS_Sessions_UpdateSessionOptions update = {};
    update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    update.SessionModificationHandle = modification;
    g_host_updated = false;
    a.sessions.update_session(&update, 0, on_host_update);
    a.cb.tick();
    a.sessions.modification_release(modification);
    REQUIRE(g_host_updated);

    // B finds it and joins it.
    EOS_HSessionSearch b_search = bucket_search(b.sessions, b.id(), "Coop");
    EOS_SessionSearch_FindOptions b_find = {};
    b_find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    b_find.LocalUserId = b.id();
    g_found = false;
    b.sessions.search_find(b_search, &b_find, 0, on_found);
    pump3(a, b, c, [&]() { return g_found; });
    REQUIRE(b.sessions.search_result_count(b_search) == 1);

    EOS_SessionSearch_CopySearchResultByIndexOptions pick = {};
    pick.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    pick.SessionIndex = 0;
    EOS_HSessionDetails b_details = 0;
    REQUIRE(b.sessions.search_copy_result(b_search, &pick, &b_details) == EOS_EResult::EOS_Success);
    EOS_Sessions_JoinSessionOptions join = {};
    join.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
    join.SessionName = "joined";
    join.SessionHandle = b_details;
    join.LocalUserId = b.id();
    g_joined = false;
    b.sessions.join_session(&join, 0, on_joined);
    pump3(a, b, c, [&]() { return g_joined; });
    REQUIRE(g_join_result == EOS_EResult::EOS_Success);

    // Now C searches. A answers (it hosts the game); B must stay silent (it only joined). One match.
    EOS_HSessionSearch c_search = bucket_search(c.sessions, c.id(), "Coop");
    EOS_SessionSearch_FindOptions c_find = {};
    c_find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    c_find.LocalUserId = c.id();
    g_found = false;
    c.sessions.search_find(c_search, &c_find, 0, on_found);
    pump3(a, b, c, [&]() { return g_found; });
    REQUIRE(g_found);
    CHECK(c.sessions.search_result_count(c_search) == 1);

    c.sessions.search_release(c_search);
    b.sessions.details_release(b_details);
    b.sessions.search_release(b_search);
    a.stop();
    b.stop();
    c.stop();
    platform::net_shutdown();
}

// The connection-bound-identity hardening: once a peer is in the mesh, the socket a frame arrives
// on decides who it is from, and a frame meant for someone else is not delivered.
namespace {

struct source_listener : i_run_network {
    std::vector<std::string> sources;

    bool on_network_message(const net_envelope& msg) {
        sources.push_back(msg.source_id);
        return true;
    }
};

} // namespace

TEST_CASE("a peer cannot present a frame under another peer's id") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45760));
    REQUIRE(start_router(bob, bob_id, "same-game", 45760));

    source_listener seen;
    alice.register_listener(message_type::emu_infos_response, &seen);
    pump(alice, bob, [&]() { return !alice.peer_ids().empty() && !bob.peer_ids().empty(); });
    REQUIRE(!bob.peer_ids().empty());

    // Bob sends a frame that claims to come from a third identity.
    const std::string spoofed = "9999999999999999999999999999999c";
    CHECK(bob.send(make_hello(spoofed, alice_id, "Impostor")));
    pump(alice, bob, [&]() { return !seen.sources.empty(); });

    REQUIRE(seen.sources.size() == 1);
    // Alice sees it as from bob -- the socket it came on -- not the id bob wrote into it.
    CHECK(seen.sources[0] == bob_id);
    CHECK(seen.sources[0] != spoofed);

    alice.stop();
    bob.stop();
    platform::net_shutdown();
}

TEST_CASE("a frame tagged for a different game is not delivered") {
    REQUIRE(platform::net_init());
    message_router alice;
    message_router bob;
    REQUIRE(start_router(alice, alice_id, "same-game", 45770));
    REQUIRE(start_router(bob, bob_id, "same-game", 45770));

    source_listener seen;
    alice.register_listener(message_type::emu_infos_response, &seen);
    pump(alice, bob, [&]() { return !alice.peer_ids().empty() && !bob.peer_ids().empty(); });
    REQUIRE(!bob.peer_ids().empty());

    // Bob sends alice a frame stamped as a different game, then one for the game they share.
    net_envelope wrong_game = make_hello(bob_id, alice_id, "WrongGame");
    wrong_game.game_id = "some-other-game";
    CHECK(bob.send(wrong_game));
    net_envelope right_game = make_hello(bob_id, alice_id, "RightGame");
    right_game.game_id = "same-game";
    CHECK(bob.send(right_game));
    pump(alice, bob, [&]() { return !seen.sources.empty(); });

    // Only the frame for the shared game arrived; the other was dropped before dispatch.
    REQUIRE(seen.sources.size() == 1);
    CHECK(seen.sources[0] == bob_id);

    alice.stop();
    bob.stop();
    platform::net_shutdown();
}

// Lobby matchmaking over the real mesh: one instance opens a lobby, another finds it and joins,
// the member list propagates to both, and the owner can kick.
namespace {

bool g_l_created = false;
std::string g_l_id;
void EOS_CALL on_l_create(const EOS_Lobby_CreateLobbyCallbackInfo* info) {
    g_l_created = info->ResultCode == EOS_EResult::EOS_Success;
    g_l_id = (info->LobbyId != 0) ? info->LobbyId : "";
}
bool g_l_found = false;
void EOS_CALL on_l_find(const EOS_LobbySearch_FindCallbackInfo* info) {
    g_l_found = info->ResultCode == EOS_EResult::EOS_Success;
}
bool g_l_joined = false;
EOS_EResult g_l_join_result = EOS_EResult::EOS_UnexpectedError;
void EOS_CALL on_l_join(const EOS_Lobby_JoinLobbyCallbackInfo* info) {
    g_l_joined = true;
    g_l_join_result = info->ResultCode;
}
void EOS_CALL on_l_kick(const EOS_Lobby_KickMemberCallbackInfo*) {}

u32 lobby_member_count(sdk_lobby& lobby, const std::string& id, EOS_ProductUserId who) {
    EOS_Lobby_CopyLobbyDetailsHandleOptions copy = {};
    copy.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    copy.LobbyId = id.c_str();
    copy.LocalUserId = who;
    EOS_HLobbyDetails details = 0;
    if (lobby.copy_lobby_details_handle(&copy, &details) != EOS_EResult::EOS_Success) {
        return 0;
    }
    EOS_LobbyDetails_GetMemberCountOptions count = {};
    count.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    const u32 n = lobby.details_member_count(details, &count);
    lobby.details_release(details);
    return n;
}

EOS_ProductUserId lobby_owner(sdk_lobby& lobby, const std::string& id, EOS_ProductUserId who) {
    EOS_Lobby_CopyLobbyDetailsHandleOptions copy = {};
    copy.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    copy.LobbyId = id.c_str();
    copy.LocalUserId = who;
    EOS_HLobbyDetails details = 0;
    if (lobby.copy_lobby_details_handle(&copy, &details) != EOS_EResult::EOS_Success) {
        return 0;
    }
    EOS_LobbyDetails_GetLobbyOwnerOptions owner = {};
    owner.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
    EOS_ProductUserId result = lobby.details_get_lobby_owner(details, &owner);
    lobby.details_release(details);
    return result;
}

} // namespace

TEST_CASE("one instance opens a lobby and another finds it and joins") {
    REQUIRE(platform::net_init());
    g_l_created = false;
    g_l_found = false;
    g_l_joined = false;

    sdk_settings host_settings;
    sdk_settings guest_settings;
    EOS_Platform_Options options = {};
    options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId = "lobby-coop";
    host_settings.apply_platform_options(&options);
    guest_settings.apply_platform_options(&options);

    callback_manager host_cb;
    callback_manager guest_cb;
    message_router host_net;
    message_router guest_net;
    sdk_connect host_connect(host_settings, host_cb, host_net);
    sdk_connect guest_connect(guest_settings, guest_cb, guest_net);
    sdk_lobby host(host_settings, host_cb, host_net, host_connect);
    sdk_lobby guest(guest_settings, guest_cb, guest_net, guest_connect);
    host_connect.emu_init();
    guest_connect.emu_init();
    host.emu_init();
    guest.emu_init();

    REQUIRE(start_router(host_net, host_settings.product_user_id(), host_settings.product_id(), 45830));
    REQUIRE(start_router(guest_net, guest_settings.product_user_id(), guest_settings.product_id(), 45830));

    EOS_Connect_Credentials credentials = {};
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Token = "device";
    credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_LoginOptions login = {};
    login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    login.Credentials = &credentials;
    host_connect.login(&login, 0, ignore_login);
    guest_connect.login(&login, 0, ignore_login);
    host_cb.tick();
    guest_cb.tick();
    pump(host_net, guest_net, [&]() {
        return host_connect.known_peer_count() > 0 && guest_connect.known_peer_count() > 0;
    });
    REQUIRE(host_connect.known_peer_count() == 1);

    EOS_ProductUserId host_id =
        id_registry::instance().get_product_user_id(host_settings.product_user_id());
    EOS_ProductUserId guest_id =
        id_registry::instance().get_product_user_id(guest_settings.product_user_id());

    // The host opens a lobby.
    EOS_Lobby_CreateLobbyOptions create = {};
    create.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    create.LocalUserId = host_id;
    create.MaxLobbyMembers = 4;
    create.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    create.BucketId = "Coop";
    create.bAllowInvites = EOS_TRUE;
    host.create_lobby(&create, 0, on_l_create);
    host_cb.tick();
    REQUIRE(g_l_created);
    const std::string lobby_id = g_l_id;

    // The guest searches for it and joins.
    EOS_Lobby_CreateLobbySearchOptions search_options = {};
    search_options.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    search_options.MaxResults = 10;
    EOS_HLobbySearch search = 0;
    REQUIRE(guest.create_lobby_search(&search_options, &search) == EOS_EResult::EOS_Success);
    EOS_Lobby_AttributeData bucket = {};
    bucket.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    bucket.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
    bucket.ValueType = EOS_EAttributeType::EOS_AT_STRING;
    bucket.Value.AsUtf8 = "Coop";
    EOS_LobbySearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &bucket;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    REQUIRE(guest.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);
    EOS_LobbySearch_FindOptions find = {};
    find.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
    find.LocalUserId = guest_id;
    guest.search_find(search, &find, 0, on_l_find);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_l_found;
    });
    REQUIRE(guest.search_result_count(search) == 1);

    EOS_LobbySearch_CopySearchResultByIndexOptions pick = {};
    pick.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    pick.LobbyIndex = 0;
    EOS_HLobbyDetails result_details = 0;
    REQUIRE(guest.search_copy_result(search, &pick, &result_details) == EOS_EResult::EOS_Success);

    // Join by id: we do not know who hosts the id until the host answers, so the join must learn the
    // owner from the reply and settle on it -- not sit until the 5s deadline and report TimedOut.
    EOS_Lobby_JoinLobbyByIdOptions join = {};
    join.ApiVersion = EOS_LOBBY_JOINLOBBYBYID_API_LATEST;
    join.LobbyId = lobby_id.c_str();
    join.LocalUserId = guest_id;
    guest.join_lobby_by_id(&join, 0, reinterpret_cast<EOS_Lobby_OnJoinLobbyByIdCallback>(on_l_join));
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return g_l_joined;
    });
    REQUIRE(g_l_join_result == EOS_EResult::EOS_Success);

    // Both sides now see two members.
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return lobby_member_count(host, lobby_id, host_id) == 2 &&
               lobby_member_count(guest, lobby_id, guest_id) == 2;
    });
    CHECK(lobby_member_count(host, lobby_id, host_id) == 2);
    CHECK(lobby_member_count(guest, lobby_id, guest_id) == 2);

    // The host promotes the guest. Ownership moves on both sides, and the guest becomes the one who
    // runs the lobby from now on.
    EOS_Lobby_PromoteMemberOptions promote = {};
    promote.ApiVersion = EOS_LOBBY_PROMOTEMEMBER_API_LATEST;
    promote.LobbyId = lobby_id.c_str();
    promote.LocalUserId = host_id;
    promote.TargetUserId = guest_id;
    host.promote_member(&promote, 0, reinterpret_cast<EOS_Lobby_OnPromoteMemberCallback>(on_l_kick));
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return lobby_owner(guest, lobby_id, guest_id) == guest_id &&
               lobby_owner(host, lobby_id, host_id) == guest_id;
    });
    CHECK(lobby_owner(guest, lobby_id, guest_id) == guest_id);
    CHECK(lobby_owner(host, lobby_id, host_id) == guest_id);

    // The guest, now the owner, kicks the former host, whose copy of the lobby goes away.
    EOS_Lobby_KickMemberOptions kick = {};
    kick.ApiVersion = EOS_LOBBY_KICKMEMBER_API_LATEST;
    kick.LobbyId = lobby_id.c_str();
    kick.LocalUserId = guest_id;
    kick.TargetUserId = host_id;
    guest.kick_member(&kick, 0, on_l_kick);
    pump(host_net, guest_net, [&]() {
        host_cb.tick();
        guest_cb.tick();
        return lobby_member_count(host, lobby_id, host_id) == 0;
    });
    CHECK(lobby_member_count(guest, lobby_id, guest_id) == 1); // just the new owner
    CHECK(lobby_member_count(host, lobby_id, host_id) == 0);   // kicked out

    guest.details_release(result_details);
    guest.search_release(search);
    host.emu_deinit();
    guest.emu_deinit();
    host_connect.emu_deinit();
    guest_connect.emu_deinit();
    host_net.stop();
    guest_net.stop();
    platform::net_shutdown();
}
