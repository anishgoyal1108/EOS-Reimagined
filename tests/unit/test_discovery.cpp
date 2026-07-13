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
