#include "doctest.h"

#include <chrono>
#include <vector>

#include "common/byte_buffer.h"
#include "common/types.h"
#include "net/messages.h"
#include "net/wire.h"
#include "platform/socket.h"

using namespace eosr;
using namespace eosr::platform;

namespace {

bool wait_readable(socket& s, int timeout_ms) {
    native_socket handle = s.native();
    bool ready = false;
    poll_readable(&handle, 1, timeout_ms, &ready);
    return ready;
}

// Connect a TCP pair over loopback, returning the client and accepted server ends.
bool make_tcp_pair(socket& client, socket& server) {
    socket listener;
    if (!listener.open_tcp()) {
        return false;
    }
    listener.set_reuseaddr(true);
    if (!listener.bind(endpoint(ip_loopback, 0))) {
        return false;
    }
    endpoint local;
    if (!listener.local_endpoint(local)) {
        return false;
    }
    if (!listener.listen(1)) {
        return false;
    }
    if (!client.open_tcp()) {
        return false;
    }
    if (!client.connect(endpoint(ip_loopback, local.port))) {
        return false;
    }
    if (!wait_readable(listener, 1000)) {
        return false;
    }
    endpoint peer;
    return listener.accept(server, peer);
}

} // namespace

TEST_CASE("net_init succeeds") {
    CHECK(net_init());
    net_shutdown();
}

TEST_CASE("a UDP datagram round-trips over loopback") {
    REQUIRE(net_init());
    socket receiver;
    REQUIRE(receiver.open_udp());
    REQUIRE(receiver.bind(endpoint(ip_loopback, 0)));
    endpoint local;
    REQUIRE(receiver.local_endpoint(local));

    socket sender;
    REQUIRE(sender.open_udp());
    const u8 msg[] = {0xde, 0xad, 0xbe, 0xef};
    CHECK(sender.send_to(msg, sizeof(msg), endpoint(ip_loopback, local.port)) == 4);

    REQUIRE(wait_readable(receiver, 1000));
    u8 buf[16];
    endpoint from;
    const int n = receiver.recv_from(buf, sizeof(buf), from);
    REQUIRE(n == 4);
    CHECK(buf[0] == 0xde);
    CHECK(buf[3] == 0xef);
    net_shutdown();
}

TEST_CASE("poll times out on an idle socket") {
    REQUIRE(net_init());
    socket s;
    REQUIRE(s.open_udp());
    REQUIRE(s.bind(endpoint(ip_loopback, 0)));

    native_socket handle = s.native();
    bool ready = true;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const int rc = poll_readable(&handle, 1, 50, &ready);
    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;

    CHECK(rc == 0);
    CHECK_FALSE(ready);
    CHECK(elapsed >= std::chrono::milliseconds(40)); // roughly waited the timeout
    net_shutdown();
}

TEST_CASE("SO_REUSEADDR is settable") {
    REQUIRE(net_init());
    socket s;
    REQUIRE(s.open_tcp());
    CHECK(s.set_reuseaddr(true));
    net_shutdown();
}

// The interaction test the pipeline relies on: a message goes through serialize -> frame ->
// a real TCP socket -> deframe -> deserialize and comes back equal. Exercises wire, messages,
// framing, and the socket layer together.
TEST_CASE("a framed message round-trips through wire and the socket layer") {
    REQUIRE(net_init());
    socket client;
    socket server;
    REQUIRE(make_tcp_pair(client, server));

    emu_infos info;
    info.emulator = "1.0.0";
    info.appid = "CrabTest";
    info.username = "InfernusHawk";
    byte_writer body;
    serialize(body, info);
    std::vector<u8> framed = frame_message(body.data());
    CHECK(client.send(framed.data(), framed.size()) == static_cast<int>(framed.size()));

    REQUIRE(wait_readable(server, 1000));
    u8 buf[512];
    const int n = server.recv(buf, sizeof(buf));
    REQUIRE(n > 0);

    std::vector<u8> received_body;
    std::size_t consumed = 0;
    REQUIRE(try_deframe(buf, static_cast<std::size_t>(n), received_body, consumed));
    CHECK(consumed == framed.size());

    byte_reader reader(received_body.data(), received_body.size());
    emu_infos out;
    REQUIRE(deserialize(reader, out));
    CHECK(out.appid == "CrabTest");
    CHECK(out.username == "InfernusHawk");
    net_shutdown();
}

// The UDP path: an envelope is serialized, sent unframed as one datagram, and decoded.
TEST_CASE("an envelope round-trips through wire and a UDP datagram") {
    REQUIRE(net_init());
    socket receiver;
    REQUIRE(receiver.open_udp());
    REQUIRE(receiver.bind(endpoint(ip_loopback, 0)));
    endpoint local;
    REQUIRE(receiver.local_endpoint(local));

    net_envelope e;
    e.type_tag = static_cast<u16>(message_type::emu_infos_request);
    e.source_id = "0123456789abcdef0123456789abcdef";
    e.game_id = "CrabTest";
    e.timestamp = 42;
    byte_writer w;
    serialize(w, e);

    socket sender;
    REQUIRE(sender.open_udp());
    CHECK(sender.send_to(w.data().data(), w.size(), endpoint(ip_loopback, local.port)) ==
          static_cast<int>(w.size()));

    REQUIRE(wait_readable(receiver, 1000));
    u8 buf[1024];
    endpoint from;
    const int n = receiver.recv_from(buf, sizeof(buf), from);
    REQUIRE(n > 0);

    byte_reader reader(buf, static_cast<std::size_t>(n));
    net_envelope out;
    REQUIRE(deserialize(reader, out));
    CHECK(out.type_tag == static_cast<u16>(message_type::emu_infos_request));
    CHECK(out.source_id == e.source_id);
    CHECK(out.game_id == "CrabTest");
    net_shutdown();
}
