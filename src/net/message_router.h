#ifndef EOSR_NET_MESSAGE_ROUTER_H
#define EOSR_NET_MESSAGE_ROUTER_H

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/i_run_network.h"
#include "core/identity.h"
#include "net/messages.h"
#include "net/peer_channel.h"
#include "platform/socket.h"

namespace eosr {

// Where discovery looks for peers. Every instance binds one port out of the range and advertises
// to all of them, so several instances on one machine each get a slot and still find each other:
// that is what makes two local copies of a game see one another. Tests inject a private range and
// a loopback-only address list so they never touch the real network.
// Spec: UDP discovery (wiki/internals/protocol.md)
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
// Discovery is a UDP advertisement broadcast on a timer. An advertisement is only ever a hint: it
// says where to look, and it is believed about nothing else. A peer that answers there runs a Noise
// handshake first, and only when that completes -- proving it holds the key its identity is derived
// from -- does it become a peer at all. Every frame afterwards is sealed under that handshake, so a
// frame cannot be forged, replayed, reordered, or read off the wire. Locally-originated envelopes
// go through a loopback self-pipe, in the clear, so they take the same decode-and-dispatch path
// without pretending to be a peer. A peer appearing or timing out is dispatched to the interfaces
// as a synthetic peer_connected / peer_disconnected envelope.
// Spec: Network (wiki/internals/protocol.md), authenticated mesh (wiki/internals/adr/0001)
class message_router {
public:
    message_router();
    ~message_router();

    message_router(const message_router&) = delete;
    message_router& operator=(const message_router&) = delete;

    // Who we are on the mesh. The profile's key is what proves this identity to a peer, and what a
    // peer's key proves to us: we recompute the id it answers to rather than believe the one it
    // claims. The title is bound into every handshake, so an instance of another game cannot finish
    // one. Our own id is derived here by the same formula a peer will use on us -- there is no other
    // id we could answer to. Set before start.
    void set_identity(const identity& profile, const std::string& product_id,
                      const std::string& sandbox_id, const std::string& deployment_id);
    void set_config(const net_config& config);

    // Bind a discovery slot, open the mesh listener, and establish the loopback self-pipe.
    // Returns false without a profile key, if no discovery port in the range is free, or if a
    // socket step fails.
    bool start();
    void stop();
    bool is_running() const { return running_; }

    // The id our key derives, and the one peers will recompute for us.
    const std::string& product_user_id() const { return product_user_id_; }

    void register_listener(message_type type, i_run_network* listener);
    void unregister_listener(message_type type, i_run_network* listener);

    // Route an envelope: an empty dest_id goes to every peer, otherwise to that one peer. Each peer
    // gets its own sealed copy -- they hold different keys -- so one set of bytes never goes to two.
    // The envelope is not looped back to us; use send_to_self for that.
    bool send(const net_envelope& msg);

    // Deliver an envelope to ourselves through the self-pipe, so locally-originated messages
    // flow through the same decode-and-dispatch path as messages from peers.
    bool send_to_self(const net_envelope& msg);

    // Send a P2P payload to `peer` as a datagram: sealed, but unreliable and unordered, and it may
    // simply not arrive. That is the point. A game that asks for an unreliable packet is also saying
    // it does not want the next one stuck behind this one, and a reliable stream cannot promise
    // that -- one lost segment there holds up every packet after it, including the ones that were
    // still perfectly good.
    //
    // False when we do not yet know where to aim a datagram at this peer, which is a moment that
    // exists between meeting a peer and its first sealed advertisement. The caller falls back to the
    // mesh, so the packet still arrives; it is only its unreliability that is briefly unavailable.
    bool send_datagram(const std::string& peer_id, const std::vector<u8>& payload);

    // The product user ids of every authenticated peer in the mesh.
    std::vector<std::string> peer_ids() const;

    // The epic account id a peer's key derives, or empty if we have no such peer. It is recomputed
    // from the static key the handshake proved, exactly as the product user id is, so it is not a
    // claim a peer makes -- it is the one identity that key can have. An interface that keys on an
    // epic account id asks here rather than believing a payload.
    std::string peer_epic_id(const std::string& peer_id) const;

    // Bytes we are holding for peers whose send buffer was full. Non-zero means we are under
    // backpressure right now.
    std::size_t pending_output_bytes() const;

    // How much P2P traffic actually took the datagram path. A game whose unreliable packets are all
    // arriving over the mesh is a game whose reliability setting is not doing anything, and that is
    // worth being able to see rather than infer.
    u64 datagrams_sent() const { return datagrams_sent_; }
    u64 datagrams_received() const { return datagrams_received_; }

    // Drain the ready sockets, advertise if it is time, drop timed-out peers, and dispatch
    // whatever decoded. Called once per tick.
    void cb_run_frame();

private:
    // One authenticated peer. `outbox` holds bytes the socket would not take yet: a send that fills
    // the kernel buffer is backpressure, not a dead peer, and abandoning a half-written frame
    // would desynchronize the peer's stream. We keep the remainder and push it out as the socket
    // drains. `channel` is the only thing that turns an envelope into bytes for this peer.
    struct peer {
        platform::socket connection;
        std::vector<u8> buffer;
        std::vector<u8> outbox;
        std::unique_ptr<peer_channel> channel;
        // The epic account id this peer's key derives. Computed once, when the key is proved.
        std::string epic_id;
        // Where this peer's datagrams go. The address comes from the connection whose handshake
        // authenticated it; the port comes from a sealed advertisement it sent us. Port zero means
        // it has not told us yet, and datagrams take the mesh until it does.
        platform::endpoint datagram_addr;
        std::chrono::steady_clock::time_point last_seen;
    };

    // A connection whose far end has not proved who it is yet. It is deliberately not a peer: an
    // unauthenticated socket has no identity to key it by, so it waits here -- out of the peer
    // table, invisible to every interface -- until the handshake says who it is.
    struct handshaking_peer {
        platform::socket connection;
        std::vector<u8> buffer;
        std::vector<u8> outbox;
        std::unique_ptr<peer_channel> channel;
        // For a connection we dialed: the id the advertisement promised would answer. The key has
        // the last word. If it derives a different id, the advertisement was not telling the truth
        // about who lives at that address, and we want no part of the connection.
        std::string expected_id;
        // The far end of this connection. Once the handshake authenticates it, this is the address
        // its datagrams may be sent to -- nobody without the key could have been on the other end.
        platform::endpoint remote;
        std::chrono::steady_clock::time_point started_at;
    };

    // A peer we are dialing. The socket is non-blocking, so the connect is still in flight and we
    // finish it on a later tick rather than stalling the game inside connect().
    struct dialing_peer {
        platform::socket connection;
        platform::endpoint address;
        std::chrono::steady_clock::time_point started_at;
    };

    // What a read told us about the far end.
    enum stream_health {
        stream_alive,  // nothing more to read for now
        stream_closed  // the peer hung up or the connection failed
    };

    // What pulling one frame off a stream found.
    enum frame_state {
        frame_none,    // not a whole frame yet
        frame_ready,   // one frame taken
        frame_refused  // the peer declared a frame we will not buffer
    };

    bool open_discovery();
    bool open_mesh();
    bool open_self_pipe();

    void advertise();
    // Who we are, where our mesh listens, and where our datagrams should be sent.
    net_envelope self_advertisement() const;
    void accept_peers();
    void finish_dialing();
    void drain_handshaking();
    void drain_peers();
    void drain_datagrams();
    void handle_advertise(const net_envelope& msg, const platform::endpoint& from);

    // The id a static public key certifies, by the same formula every peer applies to ours.
    std::string id_of_key(const u8* public_key) const;
    // Are we already on our way to this peer, dialing it or shaking hands with it?
    bool is_connecting_to(const std::string& id) const;

    // Bring an authenticated connection into the mesh under the id its key derived. Refuses -- and
    // drops the connection -- when that id is already connected, so a second socket cannot take over
    // an established peer's identity even if it does hold the key.
    bool adopt_peer(const std::string& id, platform::socket connection,
                    std::unique_ptr<peer_channel> channel, const platform::endpoint& remote,
                    const std::vector<u8>& leftover, const std::vector<u8>& unsent);
    // Tell a peer who we are and where to aim its datagrams, over the sealed mesh.
    void announce_to(const std::string& id);
    // A peer's sealed advertisement is where we learn its datagram port.
    void learn_datagram_port(const std::string& peer_id, const net_envelope& msg);
    // Take one sealed datagram apart: find whose key it is tagged for, open it, and deliver it as
    // the same p2p_data envelope the mesh would have carried.
    void handle_datagram(const u8* data, std::size_t len);
    // Whether an inbound frame from a meshed peer is one we should deliver: not for a different
    // game, and not addressed to a peer other than us.
    bool accept_inbound(const net_envelope& msg) const;
    // `reason` is the stable trace code for why the mesh lost this peer.
    void drop_peer(const std::string& id, bool notify, const char* reason);
    void expire_peers();
    void expire_handshaking();

    // Pull one whole [u32 BE length][body] frame off the front of `buffer`.
    frame_state take_frame(std::vector<u8>& buffer, std::vector<u8>& body) const;
    // Read whatever the socket has into `buffer`. What the bytes mean is the caller's business: a
    // handshaking connection reads them as handshake messages, an established one as sealed frames.
    stream_health read_stream(platform::socket& sock, std::vector<u8>& buffer);

    bool seal_frame(peer_channel& channel, const std::vector<u8>& plain,
                    std::vector<u8>& framed) const;
    void dispatch(const net_envelope& msg);
    void dispatch_peer_event(message_type type, const std::string& peer_id);

    // Queue `framed` for `entry` and push out as much as the socket will take. Returns false only
    // when the connection is genuinely broken, never for mere backpressure.
    bool queue_and_flush(peer& entry, const std::vector<u8>& framed);
    bool flush_outbox(platform::socket& connection, std::vector<u8>& outbox);

    net_config config_;
    std::string product_user_id_;
    std::string game_id_;
    std::string sandbox_id_;
    std::string deployment_id_;

    // Our profile key: the secret half proves this identity to a peer, and never leaves the process.
    u8 static_priv_[profile_key_len];
    u8 static_pub_[profile_key_len];
    bool have_profile_;
    std::vector<u8> prologue_;

    platform::socket udp_;
    u16 discovery_port_;
    platform::socket mesh_;
    u16 mesh_port_;
    platform::socket self_send_;
    platform::socket self_recv_;
    std::vector<u8> self_buffer_;

    // Peers are keyed by product user id. A peer is only ever in the mesh once.
    std::map<std::string, peer> peers_;
    std::vector<handshaking_peer> handshaking_;
    std::map<std::string, dialing_peer> dialing_;
    std::chrono::steady_clock::time_point last_advertise_;

    // Listeners are non-owning and must unregister before they are destroyed.
    std::map<message_type, std::vector<i_run_network*>> listeners_;
    u64 datagrams_sent_;
    u64 datagrams_received_;
    bool running_;
};

} // namespace eosr

#endif
