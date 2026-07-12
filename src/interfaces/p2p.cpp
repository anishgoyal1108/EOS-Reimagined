#include "interfaces/p2p.h"

#include <cstring>
#include <memory>

#include "common/ids.h"
#include "common/types.h"
#include "core/callback_manager.h"
#include "core/frame_result.h"
#include "core/settings.h"
#include "net/messages.h"
#include "net/message_router.h"
#include "net/wire.h"

namespace eosr {

namespace {

const callback_type_id cb_connection_request = 1;
const callback_type_id cb_connection_established = 2;
const callback_type_id cb_connection_closed = 3;
const callback_type_id cb_connection_interrupted = 4;
const callback_type_id cb_packet_queue_full = 5;
const callback_type_id cb_query_nat = 6;

// A socket name must be a non-empty string of at most 32 characters (the buffer holds 33 with the
// null terminator).
bool socket_name_is_valid(const EOS_P2P_SocketId* socket) {
    if (socket == 0) {
        return false;
    }
    const std::size_t length = ::strnlen(socket->SocketName, EOS_P2P_SOCKETID_SOCKETNAME_SIZE);
    return length >= 1 && length <= EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1;
}

std::string socket_name_of(const EOS_P2P_SocketId* socket) {
    return std::string(socket->SocketName,
                       ::strnlen(socket->SocketName, EOS_P2P_SOCKETID_SOCKETNAME_SIZE));
}

// Fill an out socket id from a socket name, truncating defensively to the buffer.
void write_socket_id(EOS_P2P_SocketId* out, const std::string& name) {
    if (out == 0) {
        return;
    }
    out->ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::memset(out->SocketName, 0, EOS_P2P_SOCKETID_SOCKETNAME_SIZE);
    const std::size_t copy = (name.size() < EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1)
                                 ? name.size()
                                 : EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1;
    std::memcpy(out->SocketName, name.data(), copy);
}

} // namespace

sdk_p2p::sdk_p2p(sdk_settings& settings, callback_manager& callbacks, message_router& network)
    : settings_(settings), callbacks_(callbacks), network_(network), registered_(false) {
}

sdk_p2p::~sdk_p2p() {
    emu_deinit();
}

void sdk_p2p::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::p2p_connect_request, this);
    network_.register_listener(message_type::p2p_connect_response, this);
    network_.register_listener(message_type::p2p_data, this);
    network_.register_listener(message_type::p2p_connection_close, this);
    registered_ = true;
}

void sdk_p2p::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::p2p_connect_request, this);
    network_.unregister_listener(message_type::p2p_connect_response, this);
    network_.unregister_listener(message_type::p2p_data, this);
    network_.unregister_listener(message_type::p2p_connection_close, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    receive_queue_.clear();
    connections_.clear();
    pending_events_.clear();
    request_filters_.clear();
    registered_ = false;
}

bool sdk_p2p::is_local_user(EOS_ProductUserId user) const {
    return user != 0 && user->id_str == settings_.product_user_id();
}

EOS_EResult sdk_p2p::send_packet(const EOS_P2P_SendPacketOptions* options) {
    if (options == 0 || options->ApiVersion <= 0 || options->ApiVersion > EOS_P2P_SENDPACKET_API_LATEST) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->LocalUserId == 0 || options->RemoteUserId == 0 ||
        !socket_name_is_valid(options->SocketId) ||
        (options->DataLengthBytes > 0 && options->Data == 0)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->DataLengthBytes > EOS_P2P_MAX_PACKET_SIZE) {
        return EOS_EResult::EOS_LimitExceeded;
    }

    connection_key key;
    key.peer = options->RemoteUserId->id_str;
    key.socket = socket_name_of(options->SocketId);
    const bool connected = connections_.find(key) != connections_.end();
    if (options->bDisableAutoAcceptConnection == EOS_TRUE && !connected) {
        // The caller declined auto-accept and there is no open connection, so the data is dropped.
        return EOS_EResult::EOS_NoConnection;
    }
    if (!connected) {
        connections_[key] = true; // auto-accept opens the connection
    }

    // The packet is accepted for delivery. The actual datagram to the peer rides on the peer mesh
    // that lands with the networked-discovery milestone; until then a sent packet is not delivered.
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::get_next_received_packet_size(
    const EOS_P2P_GetNextReceivedPacketSizeOptions* options, u32* out_packet_size) {
    if (options == 0 || out_packet_size == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    for (std::size_t i = 0; i < receive_queue_.size(); i++) {
        if (options->RequestedChannel == 0 || receive_queue_[i].channel == *options->RequestedChannel) {
            *out_packet_size = static_cast<u32>(receive_queue_[i].data.size());
            return EOS_EResult::EOS_Success;
        }
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_p2p::receive_packet(const EOS_P2P_ReceivePacketOptions* options,
                                    EOS_ProductUserId* out_peer, EOS_P2P_SocketId* out_socket,
                                    u8* out_channel, void* out_data, u32* out_bytes_written) {
    if (options == 0 || out_peer == 0 || out_socket == 0 || out_channel == 0 || out_data == 0 ||
        out_bytes_written == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    for (std::deque<received_packet>::iterator it = receive_queue_.begin(); it != receive_queue_.end();
         ++it) {
        if (options->RequestedChannel != 0 && it->channel != *options->RequestedChannel) {
            continue;
        }
        if (it->data.size() > options->MaxDataSizeBytes) {
            // The caller's buffer is too small; GetNextReceivedPacketSize reports the size to use.
            return EOS_EResult::EOS_LimitExceeded;
        }
        std::memcpy(out_data, it->data.data(), it->data.size());
        *out_bytes_written = static_cast<u32>(it->data.size());
        *out_peer = id_registry::instance().get_product_user_id(it->peer);
        write_socket_id(out_socket, it->socket);
        *out_channel = it->channel;
        receive_queue_.erase(it);
        return EOS_EResult::EOS_Success;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_p2p::accept_connection(const EOS_P2P_AcceptConnectionOptions* options) {
    if (options == 0 || options->LocalUserId == 0 || options->RemoteUserId == 0 ||
        !socket_name_is_valid(options->SocketId)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    connection_key key;
    key.peer = options->RemoteUserId->id_str;
    key.socket = socket_name_of(options->SocketId);
    const bool was_open = connections_.find(key) != connections_.end();
    connections_[key] = true;
    if (!was_open) {
        pending_event event;
        event.type = pending_event::established;
        event.peer = key.peer;
        event.socket = key.socket;
        event.reason = EOS_EConnectionClosedReason::EOS_CCR_Unknown;
        pending_events_.push_back(event);
    }
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::close_connection(const EOS_P2P_CloseConnectionOptions* options) {
    if (options == 0 || options->LocalUserId == 0 || options->RemoteUserId == 0 ||
        !socket_name_is_valid(options->SocketId)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    connection_key key;
    key.peer = options->RemoteUserId->id_str;
    key.socket = socket_name_of(options->SocketId);
    connections_.erase(key);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::close_connections(const EOS_P2P_CloseConnectionsOptions* options) {
    if (options == 0 || options->LocalUserId == 0 || !socket_name_is_valid(options->SocketId)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string socket = socket_name_of(options->SocketId);
    std::map<connection_key, bool>::iterator it = connections_.begin();
    while (it != connections_.end()) {
        if (it->first.socket == socket) {
            connections_.erase(it++);
        } else {
            ++it;
        }
    }
    return EOS_EResult::EOS_Success;
}

EOS_NotificationId sdk_p2p::add_notify_connection_request(
    const EOS_P2P_SocketId* socket_filter, void* client_data,
    EOS_P2P_OnIncomingConnectionRequestCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnIncomingConnectionRequestInfo* info =
        static_cast<EOS_P2P_OnIncomingConnectionRequestInfo*>(result->create_callback(
            cb_connection_request, sizeof(EOS_P2P_OnIncomingConnectionRequestInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    const EOS_NotificationId id = callbacks_.add_notification(this, std::move(result));
    if (id != 0 && socket_filter != 0 && socket_name_is_valid(socket_filter)) {
        request_filters_[id] = socket_name_of(socket_filter);
    }
    return id;
}

EOS_NotificationId sdk_p2p::add_notify_connection_established(
    void* client_data, EOS_P2P_OnPeerConnectionEstablishedCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnPeerConnectionEstablishedInfo* info =
        static_cast<EOS_P2P_OnPeerConnectionEstablishedInfo*>(result->create_callback(
            cb_connection_established, sizeof(EOS_P2P_OnPeerConnectionEstablishedInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

EOS_NotificationId sdk_p2p::add_notify_connection_closed(
    void* client_data, EOS_P2P_OnRemoteConnectionClosedCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnRemoteConnectionClosedInfo* info =
        static_cast<EOS_P2P_OnRemoteConnectionClosedInfo*>(result->create_callback(
            cb_connection_closed, sizeof(EOS_P2P_OnRemoteConnectionClosedInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

EOS_NotificationId sdk_p2p::add_notify_connection_interrupted(
    void* client_data, EOS_P2P_OnPeerConnectionInterruptedCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnPeerConnectionInterruptedInfo* info =
        static_cast<EOS_P2P_OnPeerConnectionInterruptedInfo*>(result->create_callback(
            cb_connection_interrupted, sizeof(EOS_P2P_OnPeerConnectionInterruptedInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

EOS_NotificationId sdk_p2p::add_notify_incoming_packet_queue_full(
    void* client_data, EOS_P2P_OnIncomingPacketQueueFullCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    // Our receive queue is unbounded, so this notification is registered but never fires.
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnIncomingPacketQueueFullInfo* info =
        static_cast<EOS_P2P_OnIncomingPacketQueueFullInfo*>(result->create_callback(
            cb_packet_queue_full, sizeof(EOS_P2P_OnIncomingPacketQueueFullInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_p2p::remove_notify(EOS_NotificationId id) {
    request_filters_.erase(id);
    callbacks_.remove_notification(this, id);
}

void sdk_p2p::query_nat_type(void* client_data, EOS_P2P_OnQueryNATTypeCompleteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    // A LAN peer is always directly reachable, so we complete immediately reporting an open NAT.
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnQueryNATTypeCompleteInfo* info = static_cast<EOS_P2P_OnQueryNATTypeCompleteInfo*>(
        result->create_callback(cb_query_nat, sizeof(EOS_P2P_OnQueryNATTypeCompleteInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = EOS_EResult::EOS_Success;
    info->ClientData = client_data;
    info->NATType = EOS_ENATType::EOS_NAT_Open;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

void sdk_p2p::clear_packet_queue() {
    receive_queue_.clear();
}

void sdk_p2p::fire_connection_notifications() {
    std::vector<pending_event> events;
    events.swap(pending_events_);
    for (std::size_t i = 0; i < events.size(); i++) {
        const pending_event& event = events[i];
        EOS_ProductUserId local = id_registry::instance().get_product_user_id(settings_.product_user_id());
        EOS_ProductUserId remote = id_registry::instance().get_product_user_id(event.peer);
        EOS_P2P_SocketId socket;
        write_socket_id(&socket, event.socket);

        const callback_type_id type = (event.type == pending_event::request)
                                           ? cb_connection_request
                                           : (event.type == pending_event::established)
                                                 ? cb_connection_established
                                                 : cb_connection_closed;
        // Re-look-up each notification by id before firing: a fired callback may remove another.
        std::vector<EOS_NotificationId> ids = callbacks_.notification_ids(this, type);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            if (event.type == pending_event::request) {
                std::map<EOS_NotificationId, std::string>::iterator filter = request_filters_.find(ids[n]);
                if (filter != request_filters_.end() && filter->second != event.socket) {
                    continue; // this listener only wants a different socket
                }
                EOS_P2P_OnIncomingConnectionRequestInfo* info =
                    note->get_callback<EOS_P2P_OnIncomingConnectionRequestInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
                note->fire();
            } else if (event.type == pending_event::established) {
                EOS_P2P_OnPeerConnectionEstablishedInfo* info =
                    note->get_callback<EOS_P2P_OnPeerConnectionEstablishedInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
                info->ConnectionType = EOS_EConnectionEstablishedType::EOS_CET_NewConnection;
                info->NetworkType = EOS_ENetworkConnectionType::EOS_NCT_DirectConnection;
                note->fire();
            } else {
                EOS_P2P_OnRemoteConnectionClosedInfo* info =
                    note->get_callback<EOS_P2P_OnRemoteConnectionClosedInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
                info->Reason = event.reason;
                note->fire();
            }
        }
    }
}

bool sdk_p2p::cb_run_frame() {
    if (!pending_events_.empty()) {
        fire_connection_notifications();
    }
    return false;
}

bool sdk_p2p::run_callbacks(frame_result&) {
    return false;
}

void sdk_p2p::free_callback(frame_result&) {
}

bool sdk_p2p::on_network_message(const net_envelope& message) {
    // Drop our own looped-back messages before they reach the queue or fire a notification.
    if (message.source_id == settings_.product_user_id()) {
        return true;
    }

    p2p_data payload;
    byte_reader reader(message.payload.data(), message.payload.size());
    if (!deserialize(reader, payload)) {
        return true;
    }
    const std::string socket = payload.socket_name;

    if (message.type_tag == static_cast<u16>(message_type::p2p_data)) {
        received_packet packet;
        packet.peer = message.source_id;
        packet.socket = socket;
        packet.channel = static_cast<u8>(payload.channel);
        packet.data = payload.data;
        receive_queue_.push_back(packet);
        return true;
    }

    pending_event event;
    event.peer = message.source_id;
    event.socket = socket;
    event.reason = EOS_EConnectionClosedReason::EOS_CCR_Unknown;
    if (message.type_tag == static_cast<u16>(message_type::p2p_connect_request)) {
        event.type = pending_event::request;
        pending_events_.push_back(event);
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::p2p_connect_response)) {
        connection_key key;
        key.peer = event.peer;
        key.socket = socket;
        connections_[key] = true;
        event.type = pending_event::established;
        pending_events_.push_back(event);
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::p2p_connection_close)) {
        connection_key key;
        key.peer = event.peer;
        key.socket = socket;
        connections_.erase(key);
        event.type = pending_event::closed;
        event.reason = EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer;
        pending_events_.push_back(event);
        return true;
    }
    return false;
}

} // namespace eosr
