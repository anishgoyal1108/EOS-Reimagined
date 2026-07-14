#include "interfaces/p2p.h"

#include "common/eos_names.h"
#include "core/label_registry.h"
#include "core/runtime.h"
#include "core/trace_event.h"
#include "core/tracer.h"

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

// Defaults the SDK reports until a game configures them.
const u16 default_port = 7777;
const u16 default_additional_ports = 99;

// The widest channel the API can express: EOS_P2P_SendPacketOptions::Channel is a uint8_t, so a
// wire channel outside this range is malformed and must not be narrowed into it.
const i32 max_channel = 255;

// Bounded string length, since the socket name is a fixed-size buffer that a hostile peer or a
// careless caller may leave unterminated. We avoid strnlen, which is not standard C++11.
std::size_t bounded_length(const char* text, std::size_t max_length) {
    std::size_t length = 0;
    while (length < max_length && text[length] != '\0') {
        length++;
    }
    return length;
}

// A socket name is 1-32 characters drawn from a restricted alphabet. Anything else is malformed,
// whether it came from the game or off the wire.
bool socket_name_is_valid(const std::string& name) {
    if (name.empty() || name.size() > EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); i++) {
        const char c = name[i];
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ' ||
                             c == '+' || c == '=' || c == '.';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

bool socket_id_is_valid(const EOS_P2P_SocketId* socket) {
    if (socket == 0 || socket->ApiVersion != EOS_P2P_SOCKETID_API_LATEST) {
        return false;
    }
    return socket_name_is_valid(
        std::string(socket->SocketName,
                    bounded_length(socket->SocketName, EOS_P2P_SOCKETID_SOCKETNAME_SIZE)));
}

std::string socket_name_of(const EOS_P2P_SocketId* socket) {
    return std::string(socket->SocketName,
                       bounded_length(socket->SocketName, EOS_P2P_SOCKETID_SOCKETNAME_SIZE));
}

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

bool version_is_supported(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

// An EOS option struct only ever gains fields, so a field is present exactly when the caller's
// ApiVersion is at least the version that introduced it. A game built against an older SDK passes a
// *shorter* struct, so reading a newer field reads whatever happens to sit after it in the game's
// memory -- and for a pointer field we would then dereference that. These are the cutoffs; the
// struct histories are in the SDK's versioned headers.
const i32 sendpacket_with_reliability = 2;
const i32 sendpacket_with_disable_auto_accept = 3;
const i32 receivepacket_with_requested_channel = 2;
const i32 packet_size_with_requested_channel = 2;

// The channel a caller is asking for, or none when its struct is too old to have said. Never read
// from a struct that does not have the field.
const u8* requested_channel_of(i32 version, i32 introduced, const u8* field) {
    return (version >= introduced) ? field : 0;
}

// Whether a packet has to arrive. A struct older than the reliability field is from a game that
// never had the option, so it never asked us to drop anything: we treat it as reliable, which can
// only ever deliver more than was promised.
bool packet_must_arrive(const EOS_P2P_SendPacketOptions* options) {
    if (options->ApiVersion < sendpacket_with_reliability) {
        return true;
    }
    return options->Reliability != EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
}

} // namespace

sdk_p2p::sdk_p2p(sdk_settings& settings, callback_manager& callbacks, message_router& network)
    : settings_(settings),
      callbacks_(callbacks),
      network_(network),
      nat_queried_(false),
      relay_control_(EOS_ERelayControl::EOS_RC_AllowRelays),
      port_(default_port),
      additional_ports_(default_additional_ports),
      incoming_queue_max_bytes_(EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED),
      outgoing_queue_max_bytes_(EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED),
      registered_(false) {
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
    // A peer leaving the mesh takes its connections with it, so we have to hear about that too.
    network_.register_listener(message_type::peer_connected, this);
    network_.register_listener(message_type::peer_disconnected, this);
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
    network_.unregister_listener(message_type::peer_connected, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    receive_queue_.clear();
    connections_.clear();
    pending_events_.clear();
    overflows_.clear();
    notify_filters_.clear();
    registered_ = false;
}

bool sdk_p2p::is_local_user(EOS_ProductUserId user) const {
    return user != 0 && user->id_str == settings_.product_user_id();
}

void sdk_p2p::remember_filter(EOS_NotificationId id, const EOS_P2P_SocketId* socket_filter) {
    if (id == EOS_INVALID_NOTIFICATIONID) {
        return;
    }
    notify_filter filter;
    filter.socket = (socket_filter != 0) ? socket_name_of(socket_filter) : std::string();
    notify_filters_[id] = filter;
}

void sdk_p2p::queue_event(pending_event::kind type, const std::string& peer,
                          const std::string& socket, EOS_EConnectionClosedReason reason) {
    pending_event event;
    event.type = type;
    event.peer = peer;
    event.socket = socket;
    event.reason = reason;
    pending_events_.push_back(event);

    tracer& trace = global_tracer();
    if (!trace.enabled()) {
        return;
    }
    const char* net_event = 0;
    bool is_close = false;
    if (type == pending_event::established) {
        net_event = "p2p_open";
    } else if (type == pending_event::closed || type == pending_event::interrupted) {
        net_event = "p2p_close";
        is_close = true;
    }
    if (net_event == 0) {
        return;
    }
    std::vector<trace_field> fields;
    fields.push_back(make_field(field_id::peer, tv_label(trace.label(label_kind::puid, peer))));
    if (!socket.empty()) {
        fields.push_back(
            make_field(field_id::socket, tv_label(trace.label(label_kind::socket, socket))));
    }
    if (is_close) {
        fields.push_back(
            make_field(field_id::reason, tv_enum(connection_closed_reason_name(reason))));
    }
    trace.record_net(net_event, fields, type == pending_event::interrupted);
}

// An empty peer or socket means every one of them, so this is both "drop what this peer sent" and
// "drop everything the game asked us to clear".
void sdk_p2p::flush_packets(const std::string& peer, const std::string& socket) {
    std::deque<received_packet>::iterator it = receive_queue_.begin();
    while (it != receive_queue_.end()) {
        const bool same_peer = peer.empty() || it->peer == peer;
        const bool same_socket = socket.empty() || it->socket == socket;
        if (same_peer && same_socket) {
            it = receive_queue_.erase(it);
        } else {
            ++it;
        }
    }
}

void sdk_p2p::discard_delayed(const std::string& peer, const std::string& socket) {
    std::map<connection_key, connection>::iterator it = connections_.begin();
    for (; it != connections_.end(); ++it) {
        const bool same_peer = peer.empty() || it->first.peer == peer;
        const bool same_socket = socket.empty() || it->first.socket == socket;
        if (same_peer && same_socket) {
            it->second.delayed.clear();
        }
    }
}

EOS_EResult sdk_p2p::send_packet(const EOS_P2P_SendPacketOptions* options) {
    if (options == 0 || !version_is_supported(options->ApiVersion, EOS_P2P_SENDPACKET_API_LATEST) ||
        !socket_id_is_valid(options->SocketId) ||
        (options->DataLengthBytes > 0 && options->Data == 0)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!is_local_user(options->LocalUserId) || options->RemoteUserId == 0) {
        return EOS_EResult::EOS_InvalidUser;
    }
    if (options->DataLengthBytes > EOS_P2P_MAX_PACKET_SIZE) {
        return EOS_EResult::EOS_LimitExceeded;
    }

    connection_key key;
    key.peer = options->RemoteUserId->id_str;
    key.socket = socket_name_of(options->SocketId);
    std::map<connection_key, connection>::iterator it = connections_.find(key);

    const bool disable_auto_accept =
        (options->ApiVersion >= sendpacket_with_disable_auto_accept) &&
        (options->bDisableAutoAcceptConnection == EOS_TRUE);

    if (it == connections_.end()) {
        if (disable_auto_accept) {
            // The caller declined auto-accept and there is no connection, so the data is dropped.
            return EOS_EResult::EOS_NoConnection;
        }
        // Auto-accept opens the connection from our side and asks the peer to agree.
        connection entry;
        entry.state = connection_pending;
        it = connections_.insert(std::make_pair(key, entry)).first;
        send_p2p(message_type::p2p_connect_request, key.peer, key.socket, 0, std::vector<u8>(),
                 true);
    }

    std::vector<u8> data(static_cast<const u8*>(options->Data),
                         static_cast<const u8*>(options->Data) + options->DataLengthBytes);
    const bool reliable = packet_must_arrive(options);

    if (it->second.state != connection_open) {
        // The peer has not agreed yet. Delayed delivery means holding the packet until it does;
        // without it the packet is dropped, which is what the API promises.
        if (options->bAllowDelayedDelivery != EOS_TRUE) {
            return EOS_EResult::EOS_Success;
        }
        // What we hold for a peer that has not agreed is the outgoing queue, and the game's limit
        // applies to it. A packet we have no room for is not sent -- and saying otherwise would be
        // worse than dropping it, because EOS_Success means we accepted the packet for sending, so
        // a game may throw away its own copy of one we never had room for.
        const u64 held = outgoing_queued_bytes();
        if (
            outgoing_queue_max_bytes_ != EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED &&
            held + static_cast<u64>(data.size()) > outgoing_queue_max_bytes_
        ) {
            return EOS_EResult::EOS_LimitExceeded;
        }
        delayed_packet waiting;
        waiting.data = data;
        waiting.channel = options->Channel;
        waiting.reliable = reliable;
        it->second.delayed.push_back(waiting);
        return EOS_EResult::EOS_Success;
    }

    send_p2p(message_type::p2p_data, key.peer, key.socket, options->Channel, data, reliable);
    return EOS_EResult::EOS_Success;
}

void sdk_p2p::flush_delayed(const connection_key& key, connection& entry) {
    for (std::size_t i = 0; i < entry.delayed.size(); i++) {
        const delayed_packet& held = entry.delayed[i];
        send_p2p(message_type::p2p_data, key.peer, key.socket, held.channel, held.data,
                 held.reliable);
    }
    entry.delayed.clear();
}

void sdk_p2p::send_p2p(message_type type, const std::string& peer, const std::string& socket,
                       u8 channel, const std::vector<u8>& data, bool reliable) {
    p2p_data payload;
    payload.socket_name = socket;
    payload.channel = static_cast<i32>(channel);
    payload.data = data;
    byte_writer writer;
    serialize(writer, payload);

    // A game that asked for an unreliable packet is not only saying it can live without this one --
    // it is saying it does not want the *next* one stuck behind it. A reliable stream cannot promise
    // that: one lost segment there holds up every packet after it, including the ones that arrived
    // perfectly well. So this goes as a datagram, and if it is lost it is lost.
    //
    // The mesh still carries it when the datagram path cannot yet -- we have met the peer but it has
    // not told us where its datagrams go. Arriving reliably when unreliable was asked for is a
    // promise kept too well, never one broken, so the fallback is always safe.
    if (!reliable && type == message_type::p2p_data &&
        network_.send_datagram(peer, writer.data())) {
        return;
    }

    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(type);
    envelope.source_id = settings_.product_user_id();
    envelope.dest_id = peer;
    envelope.game_id = settings_.product_id();
    envelope.payload = writer.data();
    network_.send(envelope);
}

EOS_EResult sdk_p2p::get_next_received_packet_size(
    const EOS_P2P_GetNextReceivedPacketSizeOptions* options, u32* out_packet_size) {
    if (options == 0 || out_packet_size == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!is_local_user(options->LocalUserId)) {
        return EOS_EResult::EOS_InvalidUser;
    }
    const u8* requested = requested_channel_of(
        options->ApiVersion, packet_size_with_requested_channel, options->RequestedChannel);
    for (std::size_t i = 0; i < receive_queue_.size(); i++) {
        if (requested == 0 || receive_queue_[i].channel == *requested) {
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
        out_bytes_written == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_RECEIVEPACKET_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!is_local_user(options->LocalUserId)) {
        return EOS_EResult::EOS_InvalidUser;
    }
    const u8* requested = requested_channel_of(
        options->ApiVersion, receivepacket_with_requested_channel, options->RequestedChannel);
    for (std::deque<received_packet>::iterator it = receive_queue_.begin(); it != receive_queue_.end();
         ++it) {
        if (requested != 0 && it->channel != *requested) {
            continue;
        }
        // A buffer smaller than the packet truncates it rather than failing; the caller sizes the
        // buffer with GetNextReceivedPacketSize to avoid losing data. Either way it is consumed.
        const std::size_t copied = (it->data.size() < options->MaxDataSizeBytes)
                                       ? it->data.size()
                                       : static_cast<std::size_t>(options->MaxDataSizeBytes);
        std::memcpy(out_data, it->data.data(), copied);
        *out_bytes_written = static_cast<u32>(copied);
        *out_peer = id_registry::instance().get_product_user_id(it->peer);
        write_socket_id(out_socket, it->socket);
        *out_channel = it->channel;
        receive_queue_.erase(it);
        return EOS_EResult::EOS_Success;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_p2p::accept_connection(const EOS_P2P_AcceptConnectionOptions* options) {
    if (options == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_ACCEPTCONNECTION_API_LATEST) ||
        !socket_id_is_valid(options->SocketId)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!is_local_user(options->LocalUserId) || options->RemoteUserId == 0) {
        return EOS_EResult::EOS_InvalidUser;
    }

    connection_key key;
    key.peer = options->RemoteUserId->id_str;
    key.socket = socket_name_of(options->SocketId);
    std::map<connection_key, connection>::iterator it = connections_.find(key);

    if (it != connections_.end() && it->second.state == connection_open) {
        return EOS_EResult::EOS_Success; // already open; accepting again changes nothing
    }

    if (it != connections_.end() && it->second.state == connection_requested) {
        // The peer asked and the game has now said yes, so both sides have agreed: the connection
        // is open, and telling the peer completes it on its side too.
        it->second.state = connection_open;
        send_p2p(message_type::p2p_connect_response, key.peer, key.socket, 0, std::vector<u8>(),
                 true);
        queue_event(pending_event::established, key.peer, key.socket,
                    EOS_EConnectionClosedReason::EOS_CCR_Unknown);
        flush_delayed(key, it->second);
        return EOS_EResult::EOS_Success;
    }

    // Nobody has asked us, so this is us opening the connection. We wait for the peer to agree.
    if (it == connections_.end()) {
        connection entry;
        entry.state = connection_pending;
        connections_.insert(std::make_pair(key, entry));
    }
    send_p2p(message_type::p2p_connect_request, key.peer, key.socket, 0, std::vector<u8>(),
             true);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::close_connection(const EOS_P2P_CloseConnectionOptions* options) {
    if (options == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_CLOSECONNECTION_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // A null socket id closes every socket we share with the peer.
    if (options->SocketId != 0 && !socket_id_is_valid(options->SocketId)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!is_local_user(options->LocalUserId) || options->RemoteUserId == 0) {
        return EOS_EResult::EOS_InvalidUser;
    }

    const std::string peer = options->RemoteUserId->id_str;
    const std::string socket =
        (options->SocketId != 0) ? socket_name_of(options->SocketId) : std::string();

    std::map<connection_key, connection>::iterator it = connections_.begin();
    while (it != connections_.end()) {
        if (it->first.peer == peer && (socket.empty() || it->first.socket == socket)) {
            send_p2p(message_type::p2p_connection_close, peer, it->first.socket, 0,
                     std::vector<u8>(), true);
            queue_event(pending_event::closed, peer, it->first.socket,
                        EOS_EConnectionClosedReason::EOS_CCR_ClosedByLocalUser);
            connections_.erase(it++);
        } else {
            ++it;
        }
    }
    // Closing stops receiving, so anything already queued from the peer on that socket is dropped.
    flush_packets(peer, socket);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::close_connections(const EOS_P2P_CloseConnectionsOptions* options) {
    if (options == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_CLOSECONNECTIONS_API_LATEST) ||
        !socket_id_is_valid(options->SocketId)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!is_local_user(options->LocalUserId)) {
        return EOS_EResult::EOS_InvalidUser;
    }

    const std::string socket = socket_name_of(options->SocketId);
    std::map<connection_key, connection>::iterator it = connections_.begin();
    while (it != connections_.end()) {
        if (it->first.socket == socket) {
            const std::string peer = it->first.peer;
            send_p2p(message_type::p2p_connection_close, peer, socket, 0, std::vector<u8>(), true);
            queue_event(pending_event::closed, peer, socket,
                        EOS_EConnectionClosedReason::EOS_CCR_ClosedByLocalUser);
            connections_.erase(it++);
            flush_packets(peer, socket);
        } else {
            ++it;
        }
    }
    return EOS_EResult::EOS_Success;
}

namespace {

// Every connection notification is registered the same way: validate the local user and the
// optional socket filter, then stash the payload the event loop fills in.
template <class info_type, class delegate_type>
EOS_NotificationId register_connection_notify(callback_manager& callbacks, i_run_callback* owner,
                                              callback_type_id type, void* client_data,
                                              delegate_type delegate, const char* event) {
    std::unique_ptr<frame_result> result(new frame_result());
    info_type* info = static_cast<info_type*>(result->create_callback(
        type, sizeof(info_type), reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    return callbacks.add_notification(owner, std::move(result), event);
}

} // namespace

EOS_NotificationId sdk_p2p::add_notify_connection_request(
    const EOS_P2P_SocketId* socket_filter, void* client_data,
    EOS_P2P_OnIncomingConnectionRequestCallback delegate) {
    if (delegate == 0 || (socket_filter != 0 && !socket_id_is_valid(socket_filter))) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    const EOS_NotificationId id =
        register_connection_notify<EOS_P2P_OnIncomingConnectionRequestInfo>(
            callbacks_, this, cb_connection_request, client_data, delegate,
            "P2PConnectionRequest");
    remember_filter(id, socket_filter);
    return id;
}

EOS_NotificationId sdk_p2p::add_notify_connection_established(
    void* client_data, EOS_P2P_OnPeerConnectionEstablishedCallback delegate,
    const EOS_P2P_SocketId* socket_filter) {
    if (delegate == 0 || (socket_filter != 0 && !socket_id_is_valid(socket_filter))) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    const EOS_NotificationId id =
        register_connection_notify<EOS_P2P_OnPeerConnectionEstablishedInfo>(
            callbacks_, this, cb_connection_established, client_data, delegate,
            "P2PConnectionEstablished");
    remember_filter(id, socket_filter);
    return id;
}

EOS_NotificationId sdk_p2p::add_notify_connection_closed(
    void* client_data, EOS_P2P_OnRemoteConnectionClosedCallback delegate,
    const EOS_P2P_SocketId* socket_filter) {
    if (delegate == 0 || (socket_filter != 0 && !socket_id_is_valid(socket_filter))) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    const EOS_NotificationId id = register_connection_notify<EOS_P2P_OnRemoteConnectionClosedInfo>(
        callbacks_, this, cb_connection_closed, client_data, delegate, "P2PConnectionClosed");
    remember_filter(id, socket_filter);
    return id;
}

EOS_NotificationId sdk_p2p::add_notify_connection_interrupted(
    void* client_data, EOS_P2P_OnPeerConnectionInterruptedCallback delegate,
    const EOS_P2P_SocketId* socket_filter) {
    if (delegate == 0 || (socket_filter != 0 && !socket_id_is_valid(socket_filter))) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    const EOS_NotificationId id =
        register_connection_notify<EOS_P2P_OnPeerConnectionInterruptedInfo>(
            callbacks_, this, cb_connection_interrupted, client_data, delegate,
            "P2PConnectionInterrupted");
    remember_filter(id, socket_filter);
    return id;
}

EOS_NotificationId sdk_p2p::add_notify_incoming_packet_queue_full(
    void* client_data, EOS_P2P_OnIncomingPacketQueueFullCallback delegate) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    // The incoming queue is only bounded once a game sets a limit; until a packet would overflow
    // that limit this notification stays registered and silent.
    return register_connection_notify<EOS_P2P_OnIncomingPacketQueueFullInfo>(
        callbacks_, this, cb_packet_queue_full, client_data, delegate,
        "P2PIncomingPacketQueueFull");
}

void sdk_p2p::remove_notify(EOS_NotificationId id) {
    notify_filters_.erase(id);
    callbacks_.remove_notification(this, id);
}

void sdk_p2p::query_nat_type(const EOS_P2P_QueryNATTypeOptions* options, void* client_data,
                             EOS_P2P_OnQueryNATTypeCompleteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid = options != 0 &&
                       version_is_supported(options->ApiVersion, EOS_P2P_QUERYNATTYPE_API_LATEST);
    if (valid) {
        nat_queried_ = true;
    }

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_P2P_OnQueryNATTypeCompleteInfo* info = static_cast<EOS_P2P_OnQueryNATTypeCompleteInfo*>(
        result->create_callback(cb_query_nat, sizeof(EOS_P2P_OnQueryNATTypeCompleteInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = valid ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters;
    info->ClientData = client_data;
    // A LAN peer is always directly reachable, so the query always resolves to an open NAT.
    info->NATType = valid ? EOS_ENATType::EOS_NAT_Open : EOS_ENATType::EOS_NAT_Unknown;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

EOS_EResult sdk_p2p::get_nat_type(EOS_ENATType* out_nat_type) const {
    if (out_nat_type == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!nat_queried_) {
        // Nothing is cached until a query completes.
        return EOS_EResult::EOS_NotFound;
    }
    *out_nat_type = EOS_ENATType::EOS_NAT_Open;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::set_relay_control(const EOS_P2P_SetRelayControlOptions* options) {
    if (options == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_SETRELAYCONTROL_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // We always reach a LAN peer directly, so this setting only has to be reported back faithfully.
    relay_control_ = options->RelayControl;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::get_relay_control(EOS_ERelayControl* out_relay_control) const {
    if (out_relay_control == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out_relay_control = relay_control_;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::set_port_range(const EOS_P2P_SetPortRangeOptions* options) {
    if (options == 0 || !version_is_supported(options->ApiVersion, EOS_P2P_SETPORTRANGE_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    port_ = options->Port;
    additional_ports_ = options->MaxAdditionalPortsToTry;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::get_port_range(u16* out_port, u16* out_additional_ports) const {
    if (out_port == 0 || out_additional_ports == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out_port = port_;
    *out_additional_ports = additional_ports_;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::set_packet_queue_size(const EOS_P2P_SetPacketQueueSizeOptions* options) {
    if (options == 0 ||
        !version_is_supported(options->ApiVersion, EOS_P2P_SETPACKETQUEUESIZE_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    incoming_queue_max_bytes_ = options->IncomingPacketQueueMaxSizeBytes;
    outgoing_queue_max_bytes_ = options->OutgoingPacketQueueMaxSizeBytes;
    return EOS_EResult::EOS_Success;
}

u64 sdk_p2p::incoming_queued_bytes() const {
    u64 total = 0;
    for (std::size_t i = 0; i < receive_queue_.size(); i++) {
        total += receive_queue_[i].data.size();
    }
    return total;
}

// A packet we are holding for a peer that has not agreed to the connection yet is a packet we have
// not sent. That is the only outgoing queue we own -- once a connection is open, a packet goes
// straight out and the mesh's own backpressure takes over.
u64 sdk_p2p::outgoing_queued_bytes() const {
    u64 total = 0;
    std::map<connection_key, connection>::const_iterator it = connections_.begin();
    for (; it != connections_.end(); ++it) {
        for (std::size_t i = 0; i < it->second.delayed.size(); i++) {
            total += it->second.delayed[i].data.size();
        }
    }
    return total;
}

u64 sdk_p2p::outgoing_queued_packets() const {
    u64 total = 0;
    std::map<connection_key, connection>::const_iterator it = connections_.begin();
    for (; it != connections_.end(); ++it) {
        total += it->second.delayed.size();
    }
    return total;
}

EOS_EResult sdk_p2p::get_packet_queue_info(EOS_P2P_PacketQueueInfo* out_info) const {
    if (out_info == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    out_info->IncomingPacketQueueMaxSizeBytes = incoming_queue_max_bytes_;
    out_info->IncomingPacketQueueCurrentSizeBytes = incoming_queued_bytes();
    out_info->IncomingPacketQueueCurrentPacketCount = receive_queue_.size();
    out_info->OutgoingPacketQueueMaxSizeBytes = outgoing_queue_max_bytes_;
    out_info->OutgoingPacketQueueCurrentSizeBytes = outgoing_queued_bytes();
    out_info->OutgoingPacketQueueCurrentPacketCount = outgoing_queued_packets();
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_p2p::clear_packet_queue(const EOS_P2P_ClearPacketQueueOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_is_supported(options->ApiVersion, EOS_P2P_CLEARPACKETQUEUE_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    if (!is_local_user(options->LocalUserId)) {
        return EOS_EResult::EOS_InvalidUser;
    }
    // Both queues go. The packets we are holding for the game to receive, and the ones we are
    // holding to send once a peer agrees -- a game that clears its queues and then watches an
    // obsolete packet go out anyway has not cleared the thing it was worried about.
    //
    // No peer named means every peer, and no socket named means every socket.
    const std::string peer =
        (options->RemoteUserId != 0) ? options->RemoteUserId->id_str : std::string();
    const std::string socket =
        (options->SocketId != 0) ? socket_name_of(options->SocketId) : std::string();
    flush_packets(peer, socket);
    discard_delayed(peer, socket);
    return EOS_EResult::EOS_Success;
}

void sdk_p2p::clear_packet_queue() {
    receive_queue_.clear();
}

void sdk_p2p::fire_connection_notifications() {
    std::vector<pending_event> events;
    events.swap(pending_events_);
    for (std::size_t i = 0; i < events.size(); i++) {
        const pending_event& event = events[i];
        EOS_ProductUserId local =
            id_registry::instance().get_product_user_id(settings_.product_user_id());
        EOS_ProductUserId remote = id_registry::instance().get_product_user_id(event.peer);
        EOS_P2P_SocketId socket;
        write_socket_id(&socket, event.socket);

        callback_type_id type = cb_connection_request;
        if (event.type == pending_event::established) {
            type = cb_connection_established;
        } else if (event.type == pending_event::interrupted) {
            type = cb_connection_interrupted;
        } else if (event.type == pending_event::closed) {
            type = cb_connection_closed;
        }

        // Re-look-up each notification by id before firing: a fired callback may remove another.
        std::vector<EOS_NotificationId> ids = callbacks_.notification_ids(this, type);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            std::map<EOS_NotificationId, notify_filter>::iterator filter = notify_filters_.find(ids[n]);
            if (filter != notify_filters_.end() && !filter->second.socket.empty() &&
                filter->second.socket != event.socket) {
                continue; // this listener only wants a different socket
            }

            if (event.type == pending_event::request) {
                EOS_P2P_OnIncomingConnectionRequestInfo* info =
                    note->get_callback<EOS_P2P_OnIncomingConnectionRequestInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
            } else if (event.type == pending_event::established) {
                EOS_P2P_OnPeerConnectionEstablishedInfo* info =
                    note->get_callback<EOS_P2P_OnPeerConnectionEstablishedInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
                info->ConnectionType = EOS_EConnectionEstablishedType::EOS_CET_NewConnection;
                info->NetworkType = EOS_ENetworkConnectionType::EOS_NCT_DirectConnection;
            } else if (event.type == pending_event::interrupted) {
                EOS_P2P_OnPeerConnectionInterruptedInfo* info =
                    note->get_callback<EOS_P2P_OnPeerConnectionInterruptedInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
            } else {
                EOS_P2P_OnRemoteConnectionClosedInfo* info =
                    note->get_callback<EOS_P2P_OnRemoteConnectionClosedInfo>();
                info->LocalUserId = local;
                info->RemoteUserId = remote;
                info->SocketId = &socket;
                info->Reason = event.reason;
            }
            note->fire();
        }
    }
}

// The queue-full notification is not filtered by socket -- it is about the queue, not a connection --
// so it does not go through fire_connection_notifications.
void sdk_p2p::fire_queue_full_notifications() {
    std::vector<overflow_packet> dropped;
    dropped.swap(overflows_);
    const EOS_ProductUserId local =
        id_registry::instance().get_product_user_id(settings_.product_user_id());

    for (std::size_t i = 0; i < dropped.size(); i++) {
        // Re-look-up each notification by id before firing: a fired callback may remove another.
        const std::vector<EOS_NotificationId> ids =
            callbacks_.notification_ids(this, cb_packet_queue_full);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            EOS_P2P_OnIncomingPacketQueueFullInfo* info =
                note->get_callback<EOS_P2P_OnIncomingPacketQueueFullInfo>();
            // The limit that turned this packet away, not whatever the limit is by the time the
            // game hears about it -- a game that raised it in between would otherwise be handed an
            // event describing a packet that would have fit.
            info->PacketQueueMaxSizeBytes = dropped[i].queue_max_bytes;
            info->PacketQueueCurrentSizeBytes = dropped[i].queue_size_bytes;
            info->OverflowPacketLocalUserId = local;
            info->OverflowPacketChannel = dropped[i].channel;
            info->OverflowPacketSizeBytes = dropped[i].size_bytes;
            note->fire();
        }
    }
}

bool sdk_p2p::cb_run_frame() {
    if (!pending_events_.empty()) {
        fire_connection_notifications();
    }
    if (!overflows_.empty()) {
        fire_queue_full_notifications();
    }
    return false;
}

bool sdk_p2p::run_callbacks(frame_result&) {
    return false;
}

void sdk_p2p::free_callback(frame_result&) {
}

bool sdk_p2p::on_network_message(const net_envelope& message) {
    // A peer whose connection to us went away takes its connections with it. The mesh tells us
    // through these synthetic events, which is the only warning the game will get.
    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        std::map<connection_key, connection>::iterator it = connections_.begin();
        while (it != connections_.end()) {
            if (it->first.peer == message.source_id) {
                // The peer may come back, so the connection is interrupted rather than closed,
                // which is exactly what the interrupted notification is for.
                queue_event(pending_event::interrupted, it->first.peer, it->first.socket,
                            EOS_EConnectionClosedReason::EOS_CCR_Unknown);
                connections_.erase(it++);
            } else {
                ++it;
            }
        }
        flush_packets(message.source_id, std::string());
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::peer_connected)) {
        return true; // nothing to do until the game opens a connection with it
    }

    // A peer controls every byte of this envelope, so nothing here is trusted. We drop our own
    // looped-back messages, anything addressed to someone else, and anything malformed, before it
    // can reach the packet queue or fire a notification.
    if (message.source_id.empty() || message.source_id == settings_.product_user_id()) {
        return true;
    }
    if (!message.dest_id.empty() && message.dest_id != settings_.product_user_id()) {
        return true;
    }

    p2p_data payload;
    byte_reader reader(message.payload.data(), message.payload.size());
    if (!deserialize(reader, payload)) {
        return true;
    }
    if (!socket_name_is_valid(payload.socket_name)) {
        return true;
    }

    connection_key key;
    key.peer = message.source_id;
    key.socket = payload.socket_name;
    std::map<connection_key, connection>::iterator it = connections_.find(key);

    if (message.type_tag == static_cast<u16>(message_type::p2p_data)) {
        // The wire channel is wider than the API's uint8_t, so an out-of-range channel is
        // malformed rather than something to narrow into a valid one.
        if (payload.channel < 0 || payload.channel > max_channel) {
            return true;
        }
        if (payload.data.size() > EOS_P2P_MAX_PACKET_SIZE) {
            return true;
        }
        // Data only reaches the game on a connection it agreed to. A peer that sends before we
        // accept is asking to connect, so we surface that instead of handing over its bytes.
        if (it == connections_.end()) {
            connection entry;
            entry.state = connection_requested;
            connections_.insert(std::make_pair(key, entry));
            queue_event(pending_event::request, key.peer, key.socket,
                        EOS_EConnectionClosedReason::EOS_CCR_Unknown);
            return true;
        }
        if (it->second.state != connection_open) {
            return true;
        }

        // The game asked us to hold no more than this much. Taking the packet anyway would let a
        // peer grow the queue without bound, and the game would never learn it was happening -- so
        // we turn the packet away and tell it which one it lost.
        const u64 held = incoming_queued_bytes();
        const u64 arriving = static_cast<u64>(payload.data.size());
        if (
            incoming_queue_max_bytes_ != EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED &&
            held + arriving > incoming_queue_max_bytes_
        ) {
            overflow_packet dropped;
            dropped.channel = static_cast<u8>(payload.channel);
            dropped.size_bytes = static_cast<u32>(payload.data.size());
            dropped.queue_size_bytes = held;
            dropped.queue_max_bytes = incoming_queue_max_bytes_;
            overflows_.push_back(dropped);
            return true;
        }

        received_packet packet;
        packet.peer = message.source_id;
        packet.socket = payload.socket_name;
        packet.channel = static_cast<u8>(payload.channel);
        packet.data = payload.data;
        receive_queue_.push_back(packet);
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::p2p_connect_request)) {
        if (it == connections_.end()) {
            connection entry;
            entry.state = connection_requested;
            connections_.insert(std::make_pair(key, entry));
            queue_event(pending_event::request, key.peer, key.socket,
                        EOS_EConnectionClosedReason::EOS_CCR_Unknown);
            return true;
        }
        if (it->second.state == connection_pending) {
            // We both reached for the connection at once. Each side already wants it, so agreeing
            // is all that is left.
            it->second.state = connection_open;
            send_p2p(message_type::p2p_connect_response, key.peer, key.socket, 0, std::vector<u8>(),
                 true);
            queue_event(pending_event::established, key.peer, key.socket,
                        EOS_EConnectionClosedReason::EOS_CCR_Unknown);
            flush_delayed(key, it->second);
            return true;
        }
        if (it->second.state == connection_open) {
            // It asked again for one we have already opened; confirm so its side settles too.
            send_p2p(message_type::p2p_connect_response, key.peer, key.socket, 0, std::vector<u8>(),
                 true);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::p2p_connect_response)) {
        // Only a connection we asked for can be established by a response; an unsolicited one is a
        // peer trying to open a connection we never agreed to.
        if (it != connections_.end() && it->second.state == connection_pending) {
            it->second.state = connection_open;
            queue_event(pending_event::established, key.peer, key.socket,
                        EOS_EConnectionClosedReason::EOS_CCR_Unknown);
            flush_delayed(key, it->second);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::p2p_connection_close)) {
        // A close for a connection we do not have tells us nothing and must not reach the game.
        if (it != connections_.end()) {
            connections_.erase(it);
            flush_packets(key.peer, key.socket);
            queue_event(pending_event::closed, key.peer, key.socket,
                        EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer);
        }
        return true;
    }

    return false;
}

} // namespace eosr
