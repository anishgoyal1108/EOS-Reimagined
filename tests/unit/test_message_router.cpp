#include "doctest.h"

#include <chrono>
#include <string>
#include <thread>

#include "common/byte_buffer.h"
#include "core/i_run_network.h"
#include "net/message_router.h"
#include "net/messages.h"
#include "net/wire.h"
#include "platform/socket.h"

using namespace eosr;

namespace {

// Records how many envelopes it received and the last decoded username.
struct capture_listener : i_run_network {
    int count;
    std::string last_username;

    capture_listener() : count(0) {}

    bool on_network_message(const net_envelope& msg) {
        count++;
        byte_reader reader(msg.payload.data(), msg.payload.size());
        emu_infos info;
        if (deserialize(reader, info)) {
            last_username = info.username;
        }
        return true;
    }
};

struct unregistering_listener : capture_listener {
    message_router* router;

    explicit unregistering_listener(message_router& value) : router(&value) {}

    bool on_network_message(const net_envelope& msg) {
        capture_listener::on_network_message(msg);
        router->unregister_listener(message_type::emu_infos_response, this);
        return true;
    }
};

struct registering_listener : capture_listener {
    message_router* router;
    i_run_network* listener_to_add;

    registering_listener(message_router& value, i_run_network& added)
        : router(&value), listener_to_add(&added) {}

    bool on_network_message(const net_envelope& msg) {
        capture_listener::on_network_message(msg);
        router->register_listener(message_type::emu_infos_response, listener_to_add);
        return true;
    }
};

net_envelope make_envelope(message_type type, const std::string& username) {
    emu_infos info;
    info.appid = "CrabTest";
    info.username = username;
    byte_writer payload;
    serialize(payload, info);

    net_envelope e;
    e.type_tag = static_cast<u16>(type);
    e.source_id = "0123456789abcdef0123456789abcdef";
    e.game_id = "CrabTest";
    e.payload = payload.data();
    return e;
}

// Pump the router until the predicate holds or a bounded number of ticks elapse.
template<class Predicate>
void pump_until(message_router& router, Predicate done) {
    for (int i = 0; i < 200 && !done(); i++) {
        router.cb_run_frame();
        if (!done()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// A private discovery range on loopback, so the tests never advertise onto the real network.
net_config test_config(u16 first) {
    net_config config;
    config.discovery_port_first = first;
    config.discovery_port_last = static_cast<u16>(first + 3);
    config.broadcast_addresses.push_back(platform::ip_loopback);
    return config;
}

// Bring a router up on its own discovery range with a distinct identity.
bool start_router(message_router& router, const std::string& id, u16 first) {
    router.set_identity(id, "test-game");
    router.set_config(test_config(first));
    return router.start();
}

} // namespace

TEST_CASE("the router delivers a self-sent message to its registered listener") {
    REQUIRE(platform::net_init());
    message_router router;
    REQUIRE(start_router(router, "self", 45700));

    capture_listener listener;
    router.register_listener(message_type::emu_infos_response, &listener);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "InfernusHawk")));
    pump_until(router, [&]() { return listener.count == 1; });

    REQUIRE(listener.count == 1);
    CHECK(listener.last_username == "InfernusHawk");

    router.stop();
    platform::net_shutdown();
}

TEST_CASE("the router does not deliver a message of an unregistered type") {
    REQUIRE(platform::net_init());
    message_router router;
    REQUIRE(start_router(router, "self", 45700));

    capture_listener listener;
    router.register_listener(message_type::emu_infos_response, &listener);

    // Send a different type than the one the listener registered for.
    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_request, "nobody")));
    for (int i = 0; i < 30; i++) {
        router.cb_run_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(listener.count == 0);

    router.stop();
    platform::net_shutdown();
}

TEST_CASE("an unregistered listener stops receiving") {
    REQUIRE(platform::net_init());
    message_router router;
    REQUIRE(start_router(router, "self", 45700));

    capture_listener listener;
    router.register_listener(message_type::emu_infos_response, &listener);
    router.unregister_listener(message_type::emu_infos_response, &listener);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "gone")));
    for (int i = 0; i < 30; i++) {
        router.cb_run_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(listener.count == 0);

    router.stop();
    platform::net_shutdown();
}

TEST_CASE("listener mutation during dispatch takes effect on the next message") {
    REQUIRE(platform::net_init());
    message_router router;
    REQUIRE(start_router(router, "self", 45700));

    unregistering_listener removes_itself(router);
    capture_listener stable;
    router.register_listener(message_type::emu_infos_response, &removes_itself);
    router.register_listener(message_type::emu_infos_response, &stable);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "first")));
    pump_until(router, [&]() { return stable.count == 1; });
    REQUIRE(removes_itself.count == 1);
    REQUIRE(stable.count == 1);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "second")));
    pump_until(router, [&]() { return stable.count == 2; });
    CHECK(removes_itself.count == 1);
    CHECK(stable.count == 2);

    router.stop();
    platform::net_shutdown();
}

TEST_CASE("a listener registered during dispatch does not receive the current message") {
    REQUIRE(platform::net_init());
    message_router router;
    REQUIRE(start_router(router, "self", 45700));

    capture_listener added;
    registering_listener registrar(router, added);
    router.register_listener(message_type::emu_infos_response, &registrar);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "first")));
    pump_until(router, [&]() { return registrar.count == 1; });
    REQUIRE(registrar.count == 1);
    CHECK(added.count == 0);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "second")));
    pump_until(router, [&]() { return added.count == 1; });
    CHECK(registrar.count == 2);
    CHECK(added.count == 1);

    router.stop();
    platform::net_shutdown();
}

TEST_CASE("duplicate and null listener registrations are ignored") {
    REQUIRE(platform::net_init());
    message_router router;
    REQUIRE(start_router(router, "self", 45700));

    capture_listener listener;
    router.register_listener(message_type::emu_infos_response, &listener);
    router.register_listener(message_type::emu_infos_response, &listener);
    router.register_listener(message_type::emu_infos_response, 0);

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "once")));
    pump_until(router, [&]() { return listener.count == 1; });
    CHECK(listener.count == 1);

    router.stop();
    platform::net_shutdown();
}

TEST_CASE("the router can stop and restart while retaining registrations") {
    REQUIRE(platform::net_init());
    message_router router;
    capture_listener listener;
    router.register_listener(message_type::emu_infos_response, &listener);

    CHECK_FALSE(router.send_to_self(make_envelope(message_type::emu_infos_response, "stopped")));
    REQUIRE(start_router(router, "self", 45700));
    router.stop();
    router.stop();
    REQUIRE(start_router(router, "self", 45700));

    REQUIRE(router.send_to_self(make_envelope(message_type::emu_infos_response, "restarted")));
    pump_until(router, [&]() { return listener.count == 1; });
    CHECK(listener.count == 1);
    CHECK(listener.last_username == "restarted");

    router.stop();
    platform::net_shutdown();
}
