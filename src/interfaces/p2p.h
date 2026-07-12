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

namespace eosr {

class sdk_settings;
class callback_manager;
class message_router;
class frame_result;
struct net_envelope;

// The P2P interface: the packet data path co-op gameplay rides on. A local user sends packets to
// a remote peer on a named socket and channel, and receives packets queued from peers. Unlike
// Connect and Auth this interface is almost entirely synchronous; the notifications fire on the
// tick when connection events arrive over the network. Cross-peer delivery of a sent packet
// rides on the peer mesh, which lands with the networked-discovery milestone; the receive path,
// the packet queue, and the connection state machine are implemented and driven here.
// Spec: EOSSDK_P2P (docs/p2p.md), the P2P data path (docs/protocol.md)
class sdk_p2p : public i_run_callback, public i_run_network {
public:
    sdk_p2p(sdk_settings& settings, callback_manager& callbacks, message_router& network);
    ~sdk_p2p();

    sdk_p2p(const sdk_p2p&) = delete;
    sdk_p2p& operator=(const sdk_p2p&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Flat API surface (all synchronous) ---

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
        void* client_data, EOS_P2P_OnPeerConnectionEstablishedCallback delegate);
    EOS_NotificationId add_notify_connection_closed(
        void* client_data, EOS_P2P_OnRemoteConnectionClosedCallback delegate);
    EOS_NotificationId add_notify_connection_interrupted(
        void* client_data, EOS_P2P_OnPeerConnectionInterruptedCallback delegate);
    EOS_NotificationId add_notify_incoming_packet_queue_full(
        void* client_data, EOS_P2P_OnIncomingPacketQueueFullCallback delegate);
    void remove_notify(EOS_NotificationId id);

    // The receive queue is unbounded, so the queue-full notification never fires; QueryNATType
    // completes immediately since a LAN peer is always directly reachable.
    void query_nat_type(void* client_data, EOS_P2P_OnQueryNATTypeCompleteCallback delegate);
    void clear_packet_queue();

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

    // A connection notification to deliver on the next frame, so firing happens on the tick.
    struct pending_event {
        enum kind { request, established, closed } type;
        std::string peer;
        std::string socket;
        EOS_EConnectionClosedReason reason;
    };

    bool is_local_user(EOS_ProductUserId user) const;
    void fire_connection_notifications();

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;

    std::deque<received_packet> receive_queue_;
    // A connection is present once accepted; the value is unused today but leaves room for state.
    std::map<connection_key, bool> connections_;
    std::vector<pending_event> pending_events_;
    // Socket filters per connection-request notification id (empty string means unfiltered).
    std::map<EOS_NotificationId, std::string> request_filters_;
    bool registered_;
};

} // namespace eosr

#endif
