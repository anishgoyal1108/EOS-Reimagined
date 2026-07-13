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

// The milestone's real payoff: two complete P2P stacks, each with its own settings, callback
// manager, and router, discover each other over loopback and exchange a genuine packet.
TEST_CASE("two P2P instances exchange a real packet end to end") {
    REQUIRE(platform::net_init());

    sdk_settings alice_settings;
    sdk_settings bob_settings;
    alice_settings.set_username("Alice");
    bob_settings.set_username("Bob");
    // Both run the same product, which is what lets them mesh.
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

    pump(alice_net, bob_net, [&]() {
        return !alice_net.peer_ids().empty() && !bob_net.peer_ids().empty();
    });
    REQUIRE(alice_net.peer_ids().size() == 1);
    REQUIRE(bob_net.peer_ids().size() == 1);

    // Alice sends Bob a packet through the real EOS surface.
    EOS_P2P_SocketId socket = {};
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::strncpy(socket.SocketName, "game", EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
    const u8 payload[] = {0xc0, 0xff, 0xee};

    EOS_P2P_SendPacketOptions send = {};
    send.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    send.LocalUserId = id_registry::instance().get_product_user_id(alice_settings.product_user_id());
    send.RemoteUserId = id_registry::instance().get_product_user_id(bob_settings.product_user_id());
    send.SocketId = &socket;
    send.Channel = 7;
    send.DataLengthBytes = sizeof(payload);
    send.Data = payload;
    send.bDisableAutoAcceptConnection = EOS_FALSE;
    REQUIRE(alice.send_packet(&send) == EOS_EResult::EOS_Success);

    // Bob pumps his network until the packet lands in his receive queue.
    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId =
        id_registry::instance().get_product_user_id(bob_settings.product_user_id());
    u32 size = 0;
    pump(alice_net, bob_net, [&]() {
        return bob.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_Success;
    });
    REQUIRE(size == 3);

    EOS_P2P_ReceivePacketOptions receive = {};
    receive.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    receive.LocalUserId = size_options.LocalUserId;
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
