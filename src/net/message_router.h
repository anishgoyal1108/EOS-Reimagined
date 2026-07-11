#ifndef EOSR_NET_MESSAGE_ROUTER_H
#define EOSR_NET_MESSAGE_ROUTER_H

#include <map>
#include <vector>

#include "common/types.h"
#include "core/i_run_network.h"
#include "net/messages.h"
#include "platform/socket.h"

namespace eosr {

// Moves envelopes between this instance and its peers and hands each decoded envelope to the
// interface that registered for its type. This is the skeleton: it sets up the discovery
// socket and the loopback self-pipe, and dispatches on tick. The peer TCP mesh and the RX
// thread are layered on in a later step.
// Spec: Network (docs/protocol.md)
class message_router {
public:
    message_router();
    ~message_router();

    message_router(const message_router&) = delete;
    message_router& operator=(const message_router&) = delete;

    // Bind the discovery UDP socket (port 0 = ephemeral, for tests) and establish the
    // loopback self-pipe. Returns false if any socket step fails.
    bool start(u16 discovery_port);
    void stop();

    void register_listener(message_type type, i_run_network* listener);
    void unregister_listener(message_type type, i_run_network* listener);

    // Deliver an envelope to ourselves through the self-pipe, so locally-originated messages
    // flow through the same decode-and-dispatch path as messages from peers.
    bool send_to_self(const net_envelope& msg);

    // Drain the ready sockets and dispatch whatever decoded. Called once per tick.
    void cb_run_frame();

private:
    void drain_stream(platform::socket& sock, std::vector<u8>& buffer);
    void drain_datagrams(platform::socket& sock);
    void dispatch(const net_envelope& msg);

    platform::socket udp_;
    platform::socket self_send_;
    platform::socket self_recv_;
    std::vector<u8> self_buffer_;
    std::map<u16, std::vector<i_run_network*>> listeners_;
    bool running_;
};

} // namespace eosr

#endif
