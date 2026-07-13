#include "doctest.h"

#include <cstring>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_p2p_types.h"

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/p2p.h"
#include "net/messages.h"
#include "net/message_router.h"
#include "net/wire.h"

using namespace eosr;

namespace {

const char* const peer_id = "0123456789abcdef0123456789abcdef";

int g_request_count;
std::string g_request_socket;
int g_established_count;
int g_closed_count;
EOS_EConnectionClosedReason g_closed_reason;
int g_queue_full_count;
u64 g_queue_full_max;
u64 g_queue_full_current;
u8 g_queue_full_channel;
u32 g_queue_full_packet_size;

void reset_captures() {
    g_request_count = 0;
    g_request_socket.clear();
    g_established_count = 0;
    g_closed_count = 0;
    g_closed_reason = EOS_EConnectionClosedReason::EOS_CCR_Unknown;
    g_queue_full_count = 0;
    g_queue_full_max = 0;
    g_queue_full_current = 0;
    g_queue_full_channel = 0;
    g_queue_full_packet_size = 0;
}

void EOS_CALL on_request(const EOS_P2P_OnIncomingConnectionRequestInfo* info) {
    g_request_count++;
    if (info->SocketId != 0) {
        g_request_socket = info->SocketId->SocketName;
    }
}
void EOS_CALL on_established(const EOS_P2P_OnPeerConnectionEstablishedInfo*) { g_established_count++; }
void EOS_CALL on_closed(const EOS_P2P_OnRemoteConnectionClosedInfo* info) {
    g_closed_count++;
    g_closed_reason = info->Reason;
}
void EOS_CALL on_queue_full(const EOS_P2P_OnIncomingPacketQueueFullInfo* info) {
    g_queue_full_count++;
    g_queue_full_max = info->PacketQueueMaxSizeBytes;
    g_queue_full_current = info->PacketQueueCurrentSizeBytes;
    g_queue_full_channel = info->OverflowPacketChannel;
    g_queue_full_packet_size = info->OverflowPacketSizeBytes;
}

EOS_P2P_SocketId make_socket(const char* name) {
    EOS_P2P_SocketId socket = {};
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::strncpy(socket.SocketName, name, EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
    return socket;
}

// Build an inbound p2p envelope from a peer, carrying a p2p_data payload.
net_envelope make_p2p_envelope_raw(message_type type, const std::string& source,
                                   const std::string& socket, i32 channel,
                                   const std::vector<u8>& data) {
    p2p_data payload;
    payload.socket_name = socket;
    payload.channel = static_cast<i32>(channel);
    payload.data = data;
    byte_writer writer;
    serialize(writer, payload);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(type);
    envelope.source_id = source;
    envelope.payload = writer.data();
    return envelope;
}

net_envelope make_p2p_envelope(message_type type, const std::string& source,
                               const std::string& socket, u8 channel,
                               const std::vector<u8>& data) {
    return make_p2p_envelope_raw(type, source, socket, static_cast<i32>(channel), data);
}

struct p2p_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    message_router network;
    sdk_p2p p2p;

    p2p_fixture() : p2p(settings, callbacks, network) {
        p2p.emu_init();
        reset_captures();
    }
    ~p2p_fixture() { p2p.emu_deinit(); }

    EOS_ProductUserId local() {
        return id_registry::instance().get_product_user_id(settings.product_user_id());
    }
    EOS_ProductUserId remote() { return id_registry::instance().get_product_user_id(peer_id); }

    // Bring a connection to the open state the way a game does: the peer asks, and we accept.
    // Data only reaches the game on a connection it agreed to, so tests that inject packets have
    // to get there first.
    void open_connection(const char* name) {
        p2p.on_network_message(make_p2p_envelope(message_type::p2p_connect_request, peer_id, name,
                                                 0, std::vector<u8>()));
        EOS_P2P_SocketId socket = make_socket(name);
        EOS_P2P_AcceptConnectionOptions accept = {};
        accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
        accept.LocalUserId = local();
        accept.RemoteUserId = remote();
        accept.SocketId = &socket;
        p2p.accept_connection(&accept);
    }
};

} // namespace

TEST_CASE("send packet validates its options") {
    p2p_fixture fx;
    EOS_P2P_SocketId socket = make_socket("game");
    const u8 payload[] = {1, 2, 3};

    EOS_P2P_SendPacketOptions options = {};
    options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = &socket;
    options.Channel = 0;
    options.DataLengthBytes = sizeof(payload);
    options.Data = payload;
    options.bDisableAutoAcceptConnection = EOS_FALSE;

    CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_Success);

    SUBCASE("null options") {
        CHECK(fx.p2p.send_packet(0) == EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("oversized payload is LimitExceeded") {
        options.DataLengthBytes = EOS_P2P_MAX_PACKET_SIZE + 1;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_LimitExceeded);
    }
    SUBCASE("empty socket name is rejected") {
        EOS_P2P_SocketId empty = make_socket("");
        options.SocketId = &empty;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("unsupported socket-id version is rejected") {
        socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST + 1;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("socket name with a forbidden character is rejected") {
        EOS_P2P_SocketId invalid = make_socket("bad/socket");
        options.SocketId = &invalid;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("no auto-accept and no connection is NoConnection") {
        EOS_P2P_SocketId other = make_socket("unconnected");
        options.SocketId = &other;
        options.bDisableAutoAcceptConnection = EOS_TRUE;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_NoConnection);
    }
    SUBCASE("null data with a nonzero length is rejected") {
        options.Data = 0;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("a different local user is rejected") {
        options.LocalUserId = fx.remote();
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_InvalidUser);
    }
}

TEST_CASE("packet accessors validate versions and local users before touching the queue") {
    p2p_fixture fx;
    fx.open_connection("game");
    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_data, peer_id, "game", 4, {1, 2, 3}));

    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    u32 size = 99;

    SUBCASE("GetNext rejects an unsupported version") {
        size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST + 1;
        CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) ==
              EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("GetNext rejects a different local user") {
        size_options.LocalUserId = fx.remote();
        CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) ==
              EOS_EResult::EOS_InvalidUser);
    }
    SUBCASE("Receive rejects an unsupported version") {
        EOS_P2P_ReceivePacketOptions options = {};
        options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST + 1;
        options.LocalUserId = fx.local();
        options.MaxDataSizeBytes = 8;
        EOS_ProductUserId out_peer = 0;
        EOS_P2P_SocketId out_socket = {};
        u8 out_channel = 0;
        u8 buffer[8] = {0};
        u32 written = 0;
        CHECK(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel,
                                    buffer, &written) == EOS_EResult::EOS_InvalidParameters);
    }
    SUBCASE("Receive rejects a different local user") {
        EOS_P2P_ReceivePacketOptions options = {};
        options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
        options.LocalUserId = fx.remote();
        options.MaxDataSizeBytes = 8;
        EOS_ProductUserId out_peer = 0;
        EOS_P2P_SocketId out_socket = {};
        u8 out_channel = 0;
        u8 buffer[8] = {0};
        u32 written = 0;
        CHECK(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel,
                                    buffer, &written) == EOS_EResult::EOS_InvalidUser);
    }

    // Invalid calls cannot consume the packet they failed to read.
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_Success);
    CHECK(size == 3);
}

TEST_CASE("an inbound packet is queued and received") {
    p2p_fixture fx;
    fx.open_connection("game");
    const std::vector<u8> data = {0xde, 0xad, 0xbe, 0xef};
    net_envelope envelope = make_p2p_envelope(message_type::p2p_data, peer_id, "game", 2, data);
    CHECK(fx.p2p.on_network_message(envelope));

    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    u32 size = 0;
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_Success);
    CHECK(size == 4);

    EOS_P2P_ReceivePacketOptions options = {};
    options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId = fx.local();
    options.MaxDataSizeBytes = 16;
    EOS_ProductUserId out_peer = 0;
    EOS_P2P_SocketId out_socket = {};
    u8 out_channel = 0;
    u8 buffer[16] = {0};
    u32 written = 0;
    REQUIRE(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel, buffer, &written) ==
            EOS_EResult::EOS_Success);
    CHECK(written == 4);
    CHECK(buffer[0] == 0xde);
    CHECK(buffer[3] == 0xef);
    CHECK(out_channel == 2);
    CHECK(std::string(out_socket.SocketName) == "game");
    CHECK((out_peer == fx.remote()));

    // The queue is now empty.
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("receive filters by requested channel") {
    p2p_fixture fx;
    fx.open_connection("s");
    fx.p2p.on_network_message(make_p2p_envelope(message_type::p2p_data, peer_id, "s", 1, {0xaa}));
    fx.p2p.on_network_message(make_p2p_envelope(message_type::p2p_data, peer_id, "s", 5, {0xbb}));

    const u8 wanted = 5;
    EOS_P2P_ReceivePacketOptions options = {};
    options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId = fx.local();
    options.MaxDataSizeBytes = 16;
    options.RequestedChannel = &wanted;
    EOS_ProductUserId out_peer = 0;
    EOS_P2P_SocketId out_socket = {};
    u8 out_channel = 0;
    u8 buffer[16] = {0};
    u32 written = 0;
    REQUIRE(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel, buffer, &written) ==
            EOS_EResult::EOS_Success);
    CHECK(out_channel == 5);
    CHECK(buffer[0] == 0xbb);
    // The channel-1 packet is still queued.
    const u8 other = 1;
    options.RequestedChannel = &other;
    CHECK(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel, buffer, &written) ==
          EOS_EResult::EOS_Success);
    CHECK(buffer[0] == 0xaa);
}

TEST_CASE("receiving into a small buffer truncates and consumes the packet") {
    p2p_fixture fx;
    fx.open_connection("s");
    fx.p2p.on_network_message(make_p2p_envelope(message_type::p2p_data, peer_id, "s", 0, {1, 2, 3, 4, 5}));

    EOS_P2P_ReceivePacketOptions options = {};
    options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId = fx.local();
    options.MaxDataSizeBytes = 2; // smaller than the 5-byte packet
    EOS_ProductUserId out_peer = 0;
    EOS_P2P_SocketId out_socket = {};
    u8 out_channel = 0;
    u8 buffer[8] = {0};
    u32 written = 0;
    CHECK(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel, buffer, &written) ==
          EOS_EResult::EOS_Success);
    CHECK(written == 2);
    CHECK(buffer[0] == 1);
    CHECK(buffer[1] == 2);

    // The truncated packet was consumed.
    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    u32 size = 0;
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("malformed or misaddressed inbound packets are not queued") {
    p2p_fixture fx;
    fx.open_connection("game");
    net_envelope envelope;

    SUBCASE("negative wire channel") {
        envelope = make_p2p_envelope_raw(message_type::p2p_data, peer_id, "game", -1, {1});
    }
    SUBCASE("wire channel wider than the uint8 API") {
        envelope = make_p2p_envelope_raw(message_type::p2p_data, peer_id, "game", 256, {1});
    }
    SUBCASE("oversized packet") {
        envelope = make_p2p_envelope(
            message_type::p2p_data, peer_id, "game", 0,
            std::vector<u8>(EOS_P2P_MAX_PACKET_SIZE + 1, static_cast<u8>(1)));
    }
    SUBCASE("invalid socket name") {
        envelope = make_p2p_envelope(message_type::p2p_data, peer_id, "bad/socket", 0, {1});
    }
    SUBCASE("packet addressed to a different user") {
        envelope = make_p2p_envelope(message_type::p2p_data, peer_id, "game", 0, {1});
        envelope.dest_id = "ffffffffffffffffffffffffffffffff";
    }
    SUBCASE("packet without a source peer") {
        envelope = make_p2p_envelope(message_type::p2p_data, "", "game", 0, {1});
    }

    CHECK(fx.p2p.on_network_message(envelope));
    EOS_P2P_GetNextReceivedPacketSizeOptions options = {};
    options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    options.LocalUserId = fx.local();
    u32 size = 0;
    CHECK(fx.p2p.get_next_received_packet_size(&options, &size) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("receiving from an empty queue reports NotFound") {
    p2p_fixture fx;
    EOS_P2P_ReceivePacketOptions options = {};
    options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId = fx.local();
    options.MaxDataSizeBytes = 16;
    EOS_ProductUserId out_peer = 0;
    EOS_P2P_SocketId out_socket = {};
    u8 out_channel = 0;
    u8 buffer[16] = {0};
    u32 written = 0;
    CHECK(fx.p2p.receive_packet(&options, &out_peer, &out_socket, &out_channel, buffer, &written) ==
          EOS_EResult::EOS_NotFound);
}

TEST_CASE("our own looped-back packet is dropped") {
    p2p_fixture fx;
    fx.open_connection("game");
    net_envelope envelope =
        make_p2p_envelope(message_type::p2p_data, fx.settings.product_user_id(), "game", 0, {1, 2});
    CHECK(fx.p2p.on_network_message(envelope));

    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    u32 size = 0;
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("an inbound connection request fires the request notification on the tick") {
    p2p_fixture fx;
    EOS_NotificationId id = fx.p2p.add_notify_connection_request(0, 0, on_request);
    CHECK(id != 0);

    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_connect_request, peer_id, "lobby", 0, std::vector<u8>()));
    CHECK(g_request_count == 0); // deferred to the tick
    fx.callbacks.tick();
    CHECK(g_request_count == 1);
    CHECK(g_request_socket == "lobby");
}

TEST_CASE("connection APIs validate versions and the configured local user") {
    p2p_fixture fx;
    EOS_P2P_SocketId socket = make_socket("game");

    EOS_P2P_AcceptConnectionOptions accept = {};
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST + 1;
    accept.LocalUserId = fx.local();
    accept.RemoteUserId = fx.remote();
    accept.SocketId = &socket;
    CHECK(fx.p2p.accept_connection(&accept) == EOS_EResult::EOS_InvalidParameters);
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = fx.remote();
    CHECK(fx.p2p.accept_connection(&accept) == EOS_EResult::EOS_InvalidUser);
    accept.LocalUserId = fx.local();
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST + 1;
    CHECK(fx.p2p.accept_connection(&accept) == EOS_EResult::EOS_InvalidParameters);
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;

    EOS_P2P_CloseConnectionOptions close = {};
    close.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST + 1;
    close.LocalUserId = fx.local();
    close.RemoteUserId = fx.remote();
    close.SocketId = &socket;
    CHECK(fx.p2p.close_connection(&close) == EOS_EResult::EOS_InvalidParameters);
    close.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
    close.LocalUserId = fx.remote();
    CHECK(fx.p2p.close_connection(&close) == EOS_EResult::EOS_InvalidUser);
    close.LocalUserId = fx.local();
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST + 1;
    CHECK(fx.p2p.close_connection(&close) == EOS_EResult::EOS_InvalidParameters);
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;

    EOS_P2P_CloseConnectionsOptions close_all = {};
    close_all.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST + 1;
    close_all.LocalUserId = fx.local();
    close_all.SocketId = &socket;
    CHECK(fx.p2p.close_connections(&close_all) == EOS_EResult::EOS_InvalidParameters);
    close_all.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
    close_all.LocalUserId = fx.remote();
    CHECK(fx.p2p.close_connections(&close_all) == EOS_EResult::EOS_InvalidUser);
    close_all.LocalUserId = fx.local();
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST + 1;
    CHECK(fx.p2p.close_connections(&close_all) == EOS_EResult::EOS_InvalidParameters);
}

TEST_CASE("a socket filter limits which connection requests a listener sees") {
    p2p_fixture fx;
    EOS_P2P_SocketId filter = make_socket("wanted");
    fx.p2p.add_notify_connection_request(&filter, 0, on_request);

    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_connect_request, peer_id, "other", 0, std::vector<u8>()));
    fx.callbacks.tick();
    CHECK(g_request_count == 0); // filtered out

    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_connect_request, peer_id, "wanted", 0, std::vector<u8>()));
    fx.callbacks.tick();
    CHECK(g_request_count == 1);
}

TEST_CASE("accepting locally waits for the remote response before reporting established") {
    p2p_fixture fx;
    fx.p2p.add_notify_connection_established(0, on_established);

    EOS_P2P_SocketId socket = make_socket("game");
    EOS_P2P_AcceptConnectionOptions options = {};
    options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = &socket;
    CHECK(fx.p2p.accept_connection(&options) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();
    CHECK(g_established_count == 0);

    fx.p2p.on_network_message(make_p2p_envelope(
        message_type::p2p_connect_response, peer_id, "game", 0, std::vector<u8>()));
    fx.callbacks.tick();
    CHECK(g_established_count == 1);
}

TEST_CASE("an unsolicited connection response does not open a connection") {
    p2p_fixture fx;
    fx.p2p.on_network_message(make_p2p_envelope(
        message_type::p2p_connect_response, peer_id, "game", 0, std::vector<u8>()));
    fx.callbacks.tick();

    EOS_P2P_SocketId socket = make_socket("game");
    const u8 payload[] = {1};
    EOS_P2P_SendPacketOptions options = {};
    options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = &socket;
    options.DataLengthBytes = sizeof(payload);
    options.Data = payload;
    options.bDisableAutoAcceptConnection = EOS_TRUE;
    CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_NoConnection);
}

TEST_CASE("an accepted connection suppresses later request notifications") {
    p2p_fixture fx;
    fx.p2p.add_notify_connection_request(0, 0, on_request);

    EOS_P2P_SocketId socket = make_socket("game");
    EOS_P2P_AcceptConnectionOptions options = {};
    options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = &socket;
    REQUIRE(fx.p2p.accept_connection(&options) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();

    fx.p2p.on_network_message(make_p2p_envelope(
        message_type::p2p_connect_request, peer_id, "game", 0, std::vector<u8>()));
    fx.callbacks.tick();
    CHECK(g_request_count == 0);
}

TEST_CASE("a remote close fires the closed notification with a reason") {
    p2p_fixture fx;
    fx.p2p.add_notify_connection_closed(0, on_closed);

    EOS_P2P_SocketId socket = make_socket("game");
    EOS_P2P_AcceptConnectionOptions accept = {};
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = fx.local();
    accept.RemoteUserId = fx.remote();
    accept.SocketId = &socket;
    REQUIRE(fx.p2p.accept_connection(&accept) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();

    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_connection_close, peer_id, "game", 0, std::vector<u8>()));
    fx.callbacks.tick();
    CHECK(g_closed_count == 1);
    CHECK(g_closed_reason == EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer);
}

TEST_CASE("an unsolicited remote close does not fire a closed notification") {
    p2p_fixture fx;
    fx.p2p.add_notify_connection_closed(0, on_closed);
    fx.p2p.on_network_message(make_p2p_envelope(
        message_type::p2p_connection_close, peer_id, "unknown", 0, std::vector<u8>()));
    fx.callbacks.tick();
    CHECK(g_closed_count == 0);
}

TEST_CASE("closing a connection flushes queued packets and reports a local close") {
    p2p_fixture fx;
    fx.p2p.add_notify_connection_closed(0, on_closed);

    EOS_P2P_SocketId socket = make_socket("game");
    EOS_P2P_AcceptConnectionOptions accept = {};
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = fx.local();
    accept.RemoteUserId = fx.remote();
    accept.SocketId = &socket;
    REQUIRE(fx.p2p.accept_connection(&accept) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();
    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_data, peer_id, "game", 0, {9}));

    EOS_P2P_CloseConnectionOptions close = {};
    close.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
    close.LocalUserId = fx.local();
    close.RemoteUserId = fx.remote();
    close.SocketId = &socket;
    CHECK(fx.p2p.close_connection(&close) == EOS_EResult::EOS_Success);
    fx.callbacks.tick();

    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    u32 size = 0;
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_NotFound);
    CHECK(g_closed_count == 1);
    CHECK(g_closed_reason == EOS_EConnectionClosedReason::EOS_CCR_ClosedByLocalUser);
}

TEST_CASE("close connection accepts a null socket to close every socket for a peer") {
    p2p_fixture fx;
    EOS_P2P_CloseConnectionOptions options = {};
    options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = 0;
    CHECK(fx.p2p.close_connection(&options) == EOS_EResult::EOS_Success);
}

TEST_CASE("close connection drops the connection and clear empties the queue") {
    p2p_fixture fx;
    fx.open_connection("game");
    EOS_P2P_SocketId socket = make_socket("game");
    EOS_P2P_CloseConnectionOptions options = {};
    options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = &socket;
    CHECK(fx.p2p.close_connection(&options) == EOS_EResult::EOS_Success);

    fx.p2p.on_network_message(make_p2p_envelope(message_type::p2p_data, peer_id, "game", 0, {9}));
    fx.p2p.clear_packet_queue();
    EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
    size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId = fx.local();
    u32 size = 0;
    CHECK(fx.p2p.get_next_received_packet_size(&size_options, &size) == EOS_EResult::EOS_NotFound);
}

// SetPacketQueueSize is observable behavior, not bookkeeping: once the next packet would exceed
// the configured incoming limit, EOS reports that packet through the queue-full notification and
// does not let the queue grow without bound.
TEST_CASE("the incoming packet limit rejects overflow and fires the queue-full notification") {
    p2p_fixture fx;
    fx.open_connection("game");
    REQUIRE(fx.p2p.add_notify_incoming_packet_queue_full(0, on_queue_full) !=
            EOS_INVALID_NOTIFICATIONID);

    EOS_P2P_SetPacketQueueSizeOptions limit = {};
    limit.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
    limit.IncomingPacketQueueMaxSizeBytes = 5;
    limit.OutgoingPacketQueueMaxSizeBytes = EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED;
    REQUIRE(fx.p2p.set_packet_queue_size(&limit) == EOS_EResult::EOS_Success);

    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_data, peer_id, "game", 4, {1, 2, 3}));
    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_data, peer_id, "game", 7, {4, 5, 6}));
    fx.callbacks.tick();

    EOS_P2P_PacketQueueInfo queue = {};
    REQUIRE(fx.p2p.get_packet_queue_info(&queue) == EOS_EResult::EOS_Success);
    CHECK(queue.IncomingPacketQueueCurrentSizeBytes == 3);
    CHECK(queue.IncomingPacketQueueCurrentPacketCount == 1);
    REQUIRE(g_queue_full_count == 1);
    CHECK(g_queue_full_max == 5);
    CHECK(g_queue_full_current == 3);
    CHECK(g_queue_full_channel == 7);
    CHECK(g_queue_full_packet_size >= 3);
}

TEST_CASE("a p2p notification that removes another while firing is memory-safe") {
    static sdk_p2p* remover = 0;
    static EOS_NotificationId target = 0;
    static int remover_fired = 0;
    static int victim_fired = 0;
    struct handlers {
        static void EOS_CALL remover_cb(const EOS_P2P_OnIncomingConnectionRequestInfo*) {
            remover_fired++;
            if (remover != 0) {
                remover->remove_notify(target);
            }
        }
        static void EOS_CALL victim_cb(const EOS_P2P_OnIncomingConnectionRequestInfo*) { victim_fired++; }
    };

    p2p_fixture fx;
    remover = &fx.p2p;
    remover_fired = 0;
    victim_fired = 0;
    fx.p2p.add_notify_connection_request(0, 0, handlers::remover_cb);
    target = fx.p2p.add_notify_connection_request(0, 0, handlers::victim_cb);

    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_connect_request, peer_id, "s", 0, std::vector<u8>()));
    fx.callbacks.tick();

    CHECK(remover_fired == 1);
    CHECK(victim_fired == 0);
    remover = 0;
}

// An EOS option struct only ever gains fields, so a game built against an older SDK passes a
// *shorter* one. Reading a field it does not have reads whatever sits after it in the game's own
// memory. These three fields were each added after version 1, and we used to read all of them from
// every caller. The tests below hand us a full-size struct with an old ApiVersion and the newer
// fields set to values that would visibly change our answer if we read them -- so if we ever read
// one again, the test says so instead of a game crashing.
// Spec: version cascade (EOS versioned option structs)

// bDisableAutoAcceptConnection arrived at version 3.
TEST_CASE("an older SendPacket struct has no auto-accept flag, so we must not read one") {
    p2p_fixture fx;
    EOS_P2P_SocketId socket = make_socket("game");
    const u8 payload[] = {1, 2, 3};

    EOS_P2P_SendPacketOptions options = {};
    options.LocalUserId = fx.local();
    options.RemoteUserId = fx.remote();
    options.SocketId = &socket;
    options.Channel = 0;
    options.DataLengthBytes = sizeof(payload);
    options.Data = payload;
    // A version-1 or -2 caller's struct ends before this field. Set it to the value that would make
    // us refuse the send, so reading it is not something we can get away with.
    options.bDisableAutoAcceptConnection = EOS_TRUE;

    SUBCASE("version 1") {
        options.ApiVersion = 1;
        // Auto-accept is all a v1 caller has, so the send opens the connection and succeeds.
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_Success);
    }
    SUBCASE("version 2") {
        options.ApiVersion = 2;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_Success);
    }
    SUBCASE("version 3 does have the field, and it is honoured") {
        options.ApiVersion = 3;
        CHECK(fx.p2p.send_packet(&options) == EOS_EResult::EOS_NoConnection);
    }
}

// RequestedChannel arrived at version 2 -- and it is a *pointer*, so reading it from a version-1
// struct does not merely read a stray value, it dereferences one.
TEST_CASE("an older GetNextReceivedPacketSize struct has no channel filter, so we must not read one") {
    p2p_fixture fx;
    fx.open_connection("game");
    const std::vector<u8> data(5, 0xab);
    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_data, peer_id, "game", 0, data));

    const u8 another_channel = 7;
    EOS_P2P_GetNextReceivedPacketSizeOptions options = {};
    options.LocalUserId = fx.local();
    options.RequestedChannel = &another_channel;

    u32 size = 0;
    SUBCASE("version 1 asks for no channel at all, so the packet we have is the answer") {
        options.ApiVersion = 1;
        CHECK(fx.p2p.get_next_received_packet_size(&options, &size) == EOS_EResult::EOS_Success);
        CHECK(size == data.size());
    }
    SUBCASE("version 2 does have the field, and a packet on another channel is not a match") {
        options.ApiVersion = 2;
        CHECK(fx.p2p.get_next_received_packet_size(&options, &size) == EOS_EResult::EOS_NotFound);
    }
}

TEST_CASE("an older ReceivePacket struct has no channel filter, so we must not read one") {
    p2p_fixture fx;
    fx.open_connection("game");
    const std::vector<u8> data(5, 0xcd);
    fx.p2p.on_network_message(
        make_p2p_envelope(message_type::p2p_data, peer_id, "game", 0, data));

    const u8 another_channel = 7;
    u8 buffer[16] = {0};
    EOS_ProductUserId from = 0;
    EOS_P2P_SocketId socket = {};
    u8 channel = 0xff;
    u32 written = 0;

    EOS_P2P_ReceivePacketOptions options = {};
    options.LocalUserId = fx.local();
    options.MaxDataSizeBytes = sizeof(buffer);
    options.RequestedChannel = &another_channel;

    SUBCASE("version 1 asks for no channel at all, so it gets the packet") {
        options.ApiVersion = 1;
        CHECK(fx.p2p.receive_packet(&options, &from, &socket, &channel, buffer, &written) ==
              EOS_EResult::EOS_Success);
        CHECK(written == data.size());
        CHECK(channel == 0);
    }
    SUBCASE("version 2 does have the field, and a packet on another channel is not a match") {
        options.ApiVersion = 2;
        CHECK(fx.p2p.receive_packet(&options, &from, &socket, &channel, buffer, &written) ==
              EOS_EResult::EOS_NotFound);
    }
}
