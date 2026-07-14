// Flat C ABI trampolines for the P2P interface. Each validates the handle against the live
// platform's P2P object, then calls through. P2P's core is synchronous, so most of these return
// their result directly rather than queueing a callback.
#include "eos_p2p.h"

#include <string>
#include <vector>

#include "common/eos_names.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
#include "interfaces/p2p.h"

namespace {

eosr::sdk_p2p* checked_p2p(EOS_HP2P handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HP2P>(platform->interface_handle(eosr::if_p2p))) {
        return 0;
    }
    return reinterpret_cast<eosr::sdk_p2p*>(handle);
}

std::string socket_name(const EOS_P2P_SocketId* socket) {
    if (socket == 0 || socket->ApiVersion != EOS_P2P_SOCKETID_API_LATEST) {
        return std::string();
    }
    std::size_t length = 0;
    while (length < EOS_P2P_SOCKETID_SOCKETNAME_SIZE && socket->SocketName[length] != '\0') {
        length++;
    }
    return std::string(socket->SocketName, length);
}

std::vector<eosr::trace_field> send_packet_args(eosr::tracer& trace,
                                                const EOS_P2P_SendPacketOptions* options) {
    std::vector<eosr::trace_field> fields;
    if (!trace.enabled() || options == 0 || options->ApiVersion < 1 ||
        options->ApiVersion > EOS_P2P_SENDPACKET_API_LATEST) {
        return fields;
    }
    if (options->LocalUserId != 0) {
        fields.push_back(eosr::make_field(
            eosr::field_id::local,
            eosr::tv_label(trace.label_pointer(eosr::label_kind::puid, options->LocalUserId))));
    }
    if (options->RemoteUserId != 0) {
        fields.push_back(eosr::make_field(
            eosr::field_id::target,
            eosr::tv_label(trace.label_pointer(eosr::label_kind::puid, options->RemoteUserId))));
    }
    const std::string socket = socket_name(options->SocketId);
    if (!socket.empty()) {
        fields.push_back(eosr::make_field(
            eosr::field_id::socket,
            eosr::tv_label(trace.label(eosr::label_kind::socket, socket))));
    }
    fields.push_back(eosr::make_field(eosr::field_id::channel,
                                      eosr::tv_int(options->Channel)));
    const char* reliability = eosr::packet_reliability_name(options->Reliability);
    if (reliability != 0) {
        fields.push_back(eosr::make_field(eosr::field_id::reliability,
                                          eosr::tv_enum(reliability)));
    }
    fields.push_back(eosr::make_field(eosr::field_id::len,
                                      eosr::tv_uint(options->DataLengthBytes)));
    return fields;
}

} // namespace

// --- Implemented: the packet path ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SendPacket(EOS_HP2P Handle, const EOS_P2P_SendPacketOptions* Options) {
    eosr::tracer& trace = eosr::global_tracer();
    const std::vector<eosr::trace_field> args = send_packet_args(trace, Options);
    eosr::trace_scope eosr_trace(trace, "EOS_P2P_SendPacket",
                                 Options != 0 ? Options->ApiVersion : 0, args,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->send_packet(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetNextReceivedPacketSize(
    EOS_HP2P Handle, const EOS_P2P_GetNextReceivedPacketSizeOptions* Options, uint32_t* OutPacketSizeBytes) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_GetNextReceivedPacketSize",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->get_next_received_packet_size(Options, OutPacketSizeBytes)
                   : EOS_EResult::EOS_InvalidParameters;
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if (result == EOS_EResult::EOS_Success && OutPacketSizeBytes != 0) {
        value.out.push_back(eosr::make_field(eosr::field_id::len,
                                              eosr::tv_uint(*OutPacketSizeBytes)));
    }
    eosr_trace.returns(value);
    return result;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_ReceivePacket(EOS_HP2P Handle, const EOS_P2P_ReceivePacketOptions* Options,
                                                    EOS_ProductUserId* OutPeerId, EOS_P2P_SocketId* OutSocketId,
                                                    uint8_t* OutChannel, void* OutData, uint32_t* OutBytesWritten) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_ReceivePacket",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0)
            ? p2p->receive_packet(Options, OutPeerId, OutSocketId, OutChannel, OutData,
                                  OutBytesWritten)
            : EOS_EResult::EOS_InvalidParameters;
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if (result == EOS_EResult::EOS_Success && OutBytesWritten != 0) {
        value.out.push_back(eosr::make_field(eosr::field_id::len,
                                              eosr::tv_uint(*OutBytesWritten)));
    }
    eosr_trace.returns(value);
    return result;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_AcceptConnection(EOS_HP2P Handle, const EOS_P2P_AcceptConnectionOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_AcceptConnection",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->accept_connection(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_CloseConnection(EOS_HP2P Handle, const EOS_P2P_CloseConnectionOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_CloseConnection",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->close_connection(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_CloseConnections(EOS_HP2P Handle, const EOS_P2P_CloseConnectionsOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_CloseConnections",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->close_connections(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

// --- Implemented: the connection notifications ---

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionRequest(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionRequestOptions* Options,
    void* ClientData, EOS_P2P_OnIncomingConnectionRequestCallback ConnectionRequestHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_AddNotifyPeerConnectionRequest",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return eosr::traced_notification(eosr_trace, EOS_INVALID_NOTIFICATIONID);
    }
    return eosr::traced_notification(
        eosr_trace,
        p2p->add_notify_connection_request(Options->SocketId, ClientData,
                                           ConnectionRequestHandler));
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionRequest(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_RemoveNotifyPeerConnectionRequest", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionEstablished(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionEstablishedOptions* Options,
    void* ClientData, EOS_P2P_OnPeerConnectionEstablishedCallback ConnectionEstablishedHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_AddNotifyPeerConnectionEstablished",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return eosr::traced_notification(eosr_trace, EOS_INVALID_NOTIFICATIONID);
    }
    return eosr::traced_notification(
        eosr_trace, p2p->add_notify_connection_established(
                        ClientData, ConnectionEstablishedHandler, Options->SocketId));
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionEstablished(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_RemoveNotifyPeerConnectionEstablished", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionInterrupted(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionInterruptedOptions* Options,
    void* ClientData, EOS_P2P_OnPeerConnectionInterruptedCallback ConnectionInterruptedHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_AddNotifyPeerConnectionInterrupted",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return eosr::traced_notification(eosr_trace, EOS_INVALID_NOTIFICATIONID);
    }
    return eosr::traced_notification(
        eosr_trace, p2p->add_notify_connection_interrupted(
                        ClientData, ConnectionInterruptedHandler, Options->SocketId));
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionInterrupted(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_RemoveNotifyPeerConnectionInterrupted", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionClosed(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionClosedOptions* Options,
    void* ClientData, EOS_P2P_OnRemoteConnectionClosedCallback ConnectionClosedHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_AddNotifyPeerConnectionClosed",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return eosr::traced_notification(eosr_trace, EOS_INVALID_NOTIFICATIONID);
    }
    return eosr::traced_notification(
        eosr_trace, p2p->add_notify_connection_closed(
                        ClientData, ConnectionClosedHandler, Options->SocketId));
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionClosed(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_RemoveNotifyPeerConnectionClosed", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

// --- NAT, relay, ports, and the packet queue ---

EOS_DECLARE_FUNC(void) EOS_P2P_QueryNATType(EOS_HP2P Handle, const EOS_P2P_QueryNATTypeOptions* Options,
                                            void* ClientData, const EOS_P2P_OnQueryNATTypeCompleteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_QueryNATType",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->query_nat_type(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetNATType(EOS_HP2P Handle, const EOS_P2P_GetNATTypeOptions* Options,
                                                 EOS_ENATType* OutNATType) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_GetNATType",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETNATTYPE_API_LATEST) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    return eosr::traced_result(eosr_trace, p2p->get_nat_type(OutNATType));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetRelayControl(EOS_HP2P Handle, const EOS_P2P_SetRelayControlOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_SetRelayControl",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->set_relay_control(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetRelayControl(EOS_HP2P Handle, const EOS_P2P_GetRelayControlOptions* Options,
                                                      EOS_ERelayControl* OutRelayControl) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_GetRelayControl",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETRELAYCONTROL_API_LATEST) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    return eosr::traced_result(eosr_trace, p2p->get_relay_control(OutRelayControl));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetPortRange(EOS_HP2P Handle, const EOS_P2P_SetPortRangeOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_SetPortRange",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->set_port_range(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetPortRange(EOS_HP2P Handle, const EOS_P2P_GetPortRangeOptions* Options,
                                                   uint16_t* OutPort, uint16_t* OutNumAdditionalPortsToTry) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_GetPortRange",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETPORTRANGE_API_LATEST) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    return eosr::traced_result(
        eosr_trace, p2p->get_port_range(OutPort, OutNumAdditionalPortsToTry));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetPacketQueueSize(EOS_HP2P Handle, const EOS_P2P_SetPacketQueueSizeOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_SetPacketQueueSize",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->set_packet_queue_size(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetPacketQueueInfo(EOS_HP2P Handle, const EOS_P2P_GetPacketQueueInfoOptions* Options,
                                                         EOS_P2P_PacketQueueInfo* OutPacketQueueInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_GetPacketQueueInfo",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETPACKETQUEUEINFO_API_LATEST) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    return eosr::traced_result(eosr_trace, p2p->get_packet_queue_info(OutPacketQueueInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyIncomingPacketQueueFull(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyIncomingPacketQueueFullOptions* Options,
    void* ClientData, EOS_P2P_OnIncomingPacketQueueFullCallback IncomingPacketQueueFullHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_AddNotifyIncomingPacketQueueFull",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYINCOMINGPACKETQUEUEFULL_API_LATEST) {
        return eosr::traced_notification(eosr_trace, EOS_INVALID_NOTIFICATIONID);
    }
    return eosr::traced_notification(
        eosr_trace,
        p2p->add_notify_incoming_packet_queue_full(ClientData, IncomingPacketQueueFullHandler));
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyIncomingPacketQueueFull(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_P2P_RemoveNotifyIncomingPacketQueueFull", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_ClearPacketQueue(EOS_HP2P Handle, const EOS_P2P_ClearPacketQueueOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_P2P_ClearPacketQueue",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    const EOS_EResult result =
        (p2p != 0) ? p2p->clear_packet_queue(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}
