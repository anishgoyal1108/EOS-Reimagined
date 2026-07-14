#ifndef EOSR_INTERFACES_P2P_H
#define EOSR_INTERFACES_P2P_H

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_p2p_types.h"

#include "common/types.h"
#include "core/i_run_callback.h"
#include "core/i_run_network.h"
#include "net/messages.h"

namespace eosr {

class sdk_settings;
class callback_manager;
class message_router;
class frame_result;

// The P2P interface: the packet data path co-op gameplay rides on. A local user sends packets to
// a remote peer on a named socket and channel, and receives packets queued from peers. Unlike
// Connect and Auth this interface is almost entirely synchronous; the connection notifications
// fire on the tick when connection events arrive over the network. Cross-peer delivery of a sent
// packet rides on the peer mesh, which lands with the networked-discovery milestone; the receive
// path, the packet queue, and the connection state machine are implemented and driven here.
// Spec: EOSSDK_P2P (wiki/internals/p2p.md), the P2P data path (wiki/internals/protocol.md)
class sdk_p2p : public i_run_callback, public i_run_network {
public:
    sdk_p2p(sdk_settings& settings, callback_manager& callbacks, message_router& network);
    ~sdk_p2p();

    sdk_p2p(const sdk_p2p&) = delete;
    sdk_p2p& operator=(const sdk_p2p&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Flat API surface (all synchronous except query_nat_type) ---

    EOS_EResult send_packet(const EOS_P2P_SendPacketOptions* options);
    EOS_EResult get_next_received_packet_size(const EOS_P2P_GetNextReceivedPacketSizeOptions* options,
                                              u32* out_packet_size);
    EOS_EResult receive_packet(const EOS_P2P_ReceivePacketOptions* options, EOS_ProductUserId* out_peer,
                               EOS_P2P_SocketId* out_socket, u8* out_channel, void* out_data,
                               u32* out_bytes_written);

    EOS_EResult accept_connection(const EOS_P2P_AcceptConnectionOptions* options);
    EOS_EResult close_connection(const EOS_P2P_CloseConnectionOptions* options);
    EOS_EResult close_connections(const EOS_P2P_CloseConnectionsOptions* options);

    EOS_NotificationId add_notify_connection_request(
        const EOS_P2P_SocketId* socket_filter, void* client_data,
        EOS_P2P_OnIncomingConnectionRequestCallback delegate);
    EOS_NotificationId add_notify_connection_established(
        void* client_data, EOS_P2P_OnPeerConnectionEstablishedCallback delegate,
        const EOS_P2P_SocketId* socket_filter = 0);
    EOS_NotificationId add_notify_connection_closed(
        void* client_data, EOS_P2P_OnRemoteConnectionClosedCallback delegate,
        const EOS_P2P_SocketId* socket_filter = 0);
    EOS_NotificationId add_notify_connection_interrupted(
        void* client_data, EOS_P2P_OnPeerConnectionInterruptedCallback delegate,
        const EOS_P2P_SocketId* socket_filter = 0);
    EOS_NotificationId add_notify_incoming_packet_queue_full(
        void* client_data, EOS_P2P_OnIncomingPacketQueueFullCallback delegate);
    void remove_notify(EOS_NotificationId id);

    // NAT type is unknown until a query completes; a LAN peer is always directly reachable.
    void query_nat_type(const EOS_P2P_QueryNATTypeOptions* options, void* client_data,
                        EOS_P2P_OnQueryNATTypeCompleteCallback delegate);
    EOS_EResult get_nat_type(EOS_ENATType* out_nat_type) const;

    EOS_EResult set_relay_control(const EOS_P2P_SetRelayControlOptions* options);
    EOS_EResult get_relay_control(EOS_ERelayControl* out_relay_control) const;
    EOS_EResult set_port_range(const EOS_P2P_SetPortRangeOptions* options);
    EOS_EResult get_port_range(u16* out_port, u16* out_additional_ports) const;
    EOS_EResult set_packet_queue_size(const EOS_P2P_SetPacketQueueSizeOptions* options);
    EOS_EResult get_packet_queue_info(EOS_P2P_PacketQueueInfo* out_info) const;
    EOS_EResult clear_packet_queue(const EOS_P2P_ClearPacketQueueOptions* options);
    void clear_packet_queue();

    // True when `user` is the configured local user. The flat layer validates the LocalUserId of
    // every notification registration through this.
    bool is_local_user(EOS_ProductUserId user) const;

    // --- i_run_callback ---
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // --- i_run_network ---
    bool on_network_message(const net_envelope& message);

private:
    // One received packet, tagged with where it came from so ReceivePacket can report it.
    struct received_packet {
        std::string peer;
        std::string socket;
        u8 channel;
        std::vector<u8> data;
    };

    // A connection is identified by the remote peer plus the socket name.
    struct connection_key {
        std::string peer;
        std::string socket;
        bool operator<(const connection_key& other) const {
            return peer < other.peer || (peer == other.peer && socket < other.socket);
        }
    };

    // A connection is only open once both sides have agreed. Reporting it established any earlier
    // would tell the game it can talk to a peer that never said yes.
    enum connection_state {
        connection_requested, // the peer asked us; the game has not accepted yet
        connection_pending,   // we asked the peer; waiting for it to agree
        connection_open       // both sides agreed
    };

    // A packet the game handed us before the peer had agreed to the connection. It keeps the
    // reliability it was sent with: a packet does not become one the game is willing to lose just
    // because it had to wait.
    struct delayed_packet {
        std::vector<u8> data;
        u8 channel;
        bool reliable;
    };

    // A connection we have opened from our side, plus anything the game asked us to send before the
    // peer agreed. Delayed delivery means holding those packets, not dropping them.
    struct connection {
        connection_state state;
        std::vector<delayed_packet> delayed;
    };

    // A connection notification to deliver on the next frame, so firing happens on the tick.
    struct pending_event {
        enum kind { request, established, interrupted, closed } type;
        std::string peer;
        std::string socket;
        EOS_EConnectionClosedReason reason;
    };

    // A packet the incoming queue had no room for. The game is told which packet it lost, so we keep
    // what it needs to know until the notification fires on the tick -- including the limit that
    // turned it away. The game may raise the limit before then, and reporting the new one would
    // describe a packet that would have fit.
    struct overflow_packet {
        u8 channel;
        u32 size_bytes;
        u64 queue_size_bytes; // what the queue held at the moment we turned this one away
        u64 queue_max_bytes;  // and the limit it was measured against
    };

    // A registered connection notification, with the socket it is limited to (empty = any).
    struct notify_filter {
        std::string socket;
    };

    // Frame one P2P message and get it to `peer`. A packet the game does not need to arrive goes by
    // datagram; everything else -- and anything the datagram path cannot carry yet -- goes over the
    // reliable mesh. Control messages are always reliable: a connection request that is allowed to
    // vanish is a connection that never opens.
    void send_p2p(message_type type, const std::string& peer, const std::string& socket, u8 channel,
                  const std::vector<u8>& data, bool reliable);
    // Push out everything we held back while the connection was being agreed.
    void flush_delayed(const connection_key& key, connection& entry);
    // Throw away what we are holding to send. An empty peer or socket means every one of them.
    void discard_delayed(const std::string& peer, const std::string& socket);
    void remember_filter(EOS_NotificationId id, const EOS_P2P_SocketId* socket_filter);
    void queue_event(pending_event::kind type, const std::string& peer, const std::string& socket,
                     EOS_EConnectionClosedReason reason);
    void fire_connection_notifications();
    void fire_queue_full_notifications();
    // Bytes the incoming queue is holding, and what the delayed-delivery queues are holding for
    // peers that have not agreed yet -- which is the only outgoing queue we own.
    u64 incoming_queued_bytes() const;
    u64 outgoing_queued_bytes() const;
    u64 outgoing_queued_packets() const;
    // Drop every queued packet from `peer` on `socket`; an empty socket means every socket.
    void flush_packets(const std::string& peer, const std::string& socket);

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;

    std::deque<received_packet> receive_queue_;
    std::map<connection_key, connection> connections_;
    std::vector<pending_event> pending_events_;
    std::vector<overflow_packet> overflows_;
    std::map<EOS_NotificationId, notify_filter> notify_filters_;

    // Configuration a game sets and reads back. The emulator does not act on the relay and port
    // settings (a LAN peer is reached directly), but it must report what the game configured.
    bool nat_queried_;
    EOS_ERelayControl relay_control_;
    u16 port_;
    u16 additional_ports_;
    u64 incoming_queue_max_bytes_;
    u64 outgoing_queue_max_bytes_;
    bool registered_;
};

} // namespace eosr

#endif
