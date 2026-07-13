#ifndef EOSR_NET_MESSAGE_ROUTER_H
#define EOSR_NET_MESSAGE_ROUTER_H

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/i_run_network.h"
#include "net/messages.h"
#include "platform/socket.h"

namespace eosr {

// Where discovery looks for peers. Every instance binds one port out of the range and advertises
// to all of them, so several instances on one machine each get a slot and still find each other:
// that is what makes two local copies of a game see one another. Tests inject a private range and
// a loopback-only address list so they never touch the real network.
// Spec: UDP discovery (docs/protocol.md)
struct net_config {
    u16 discovery_port_first;
    u16 discovery_port_last;
    // Where advertisements are sent. Empty means "ask the platform for every broadcast address".
    std::vector<u32> broadcast_addresses;

    net_config();
};

// Moves envelopes between this instance and its peers, and hands each decoded envelope to the
// interface that registered for its type.
//
// Discovery is a UDP advertisement broadcast on a timer; a peer that answers our game id gets a
// TCP connection in the mesh, and every envelope after that travels the mesh with a length
// prefix. Locally-originated envelopes go through a loopback self-pipe so they take the same
// decode-and-dispatch path as a peer's. A peer appearing or timing out is dispatched to the
// interfaces as a synthetic peer_connected / peer_disconnected envelope.
// Spec: Network (docs/protocol.md)
class message_router {
public:
    message_router();
    ~message_router();

    message_router(const message_router&) = delete;
    message_router& operator=(const message_router&) = delete;

    // Who we advertise as. Set before start; peers only mesh with us when the game id matches.
    void set_identity(const std::string& product_user_id, const std::string& game_id);
    void set_config(const net_config& config);

    // Bind a discovery slot, open the mesh listener, and establish the loopback self-pipe.
    // Returns false if no discovery port in the range is free or a socket step fails.
    bool start();
    void stop();
    bool is_running() const { return running_; }

    void register_listener(message_type type, i_run_network* listener);
    void unregister_listener(message_type type, i_run_network* listener);

    // Route an envelope: an empty dest_id goes to every peer, otherwise to that one peer. The
    // envelope is not looped back to us; use send_to_self for that.
    bool send(const net_envelope& msg);

    // Deliver an envelope to ourselves through the self-pipe, so locally-originated messages
    // flow through the same decode-and-dispatch path as messages from peers.
    bool send_to_self(const net_envelope& msg);

    // The product user ids of every peer currently in the mesh.
    std::vector<std::string> peer_ids() const;

    // Drain the ready sockets, advertise if it is time, drop timed-out peers, and dispatch
    // whatever decoded. Called once per tick.
    void cb_run_frame();

private:
    // One peer in the mesh: the connection we exchange envelopes over and when we last heard it.
    struct peer {
        platform::socket connection;
        std::vector<u8> buffer;
        std::chrono::steady_clock::time_point last_seen;
    };

    // A connection we accepted but cannot name yet: the peer identifies itself in the first
    // envelope it sends, and only then does it join the peer table.
    struct pending_peer {
        platform::socket connection;
        std::vector<u8> buffer;
        std::chrono::steady_clock::time_point accepted_at;
    };

    bool open_discovery();
    bool open_mesh();
    bool open_self_pipe();

    void advertise();
    void accept_peers();
    void drain_pending();
    void handle_advertise(const net_envelope& msg, const platform::endpoint& from);
    void adopt_peer(const std::string& id, platform::socket connection);
    void drop_peer(const std::string& id, bool notify);
    void expire_peers();

    // Decode every whole frame in `buffer`, handing each to `sink`. Returns false when the peer
    // sent a frame we refuse to buffer.
    bool decode_frames(std::vector<u8>& buffer, std::vector<net_envelope>& out);
    void drain_stream(platform::socket& sock, std::vector<u8>& buffer,
                      std::vector<net_envelope>& out);
    void drain_datagrams();
    void dispatch(const net_envelope& msg);
    void dispatch_peer_event(message_type type, const std::string& peer_id);
    bool send_framed(platform::socket& sock, const std::vector<u8>& framed);

    net_config config_;
    std::string product_user_id_;
    std::string game_id_;

    platform::socket udp_;
    platform::socket mesh_;
    u16 mesh_port_;
    platform::socket self_send_;
    platform::socket self_recv_;
    std::vector<u8> self_buffer_;

    // Peers are keyed by product user id. A peer is only ever in the mesh once.
    std::map<std::string, peer> peers_;
    std::vector<pending_peer> pending_;
    std::chrono::steady_clock::time_point last_advertise_;

    // Listeners are non-owning and must unregister before they are destroyed.
    std::map<message_type, std::vector<i_run_network*>> listeners_;
    bool running_;
};

} // namespace eosr

#endif
