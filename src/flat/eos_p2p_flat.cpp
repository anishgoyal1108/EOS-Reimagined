// Flat C ABI trampolines for the P2P interface. Each validates the handle against the live
// platform's P2P object, then calls through. P2P's core is synchronous, so most of these return
// their result directly rather than queueing a callback.
#include "eos_p2p.h"

#include "core/platform.h"
#include "core/runtime.h"
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

} // namespace

// --- Implemented: the packet path ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SendPacket(EOS_HP2P Handle, const EOS_P2P_SendPacketOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->send_packet(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetNextReceivedPacketSize(
    EOS_HP2P Handle, const EOS_P2P_GetNextReceivedPacketSizeOptions* Options, uint32_t* OutPacketSizeBytes) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->get_next_received_packet_size(Options, OutPacketSizeBytes)
                      : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_ReceivePacket(EOS_HP2P Handle, const EOS_P2P_ReceivePacketOptions* Options,
                                                    EOS_ProductUserId* OutPeerId, EOS_P2P_SocketId* OutSocketId,
                                                    uint8_t* OutChannel, void* OutData, uint32_t* OutBytesWritten) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->receive_packet(Options, OutPeerId, OutSocketId, OutChannel, OutData, OutBytesWritten)
                      : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_AcceptConnection(EOS_HP2P Handle, const EOS_P2P_AcceptConnectionOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->accept_connection(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_CloseConnection(EOS_HP2P Handle, const EOS_P2P_CloseConnectionOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->close_connection(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_CloseConnections(EOS_HP2P Handle, const EOS_P2P_CloseConnectionsOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->close_connections(Options) : EOS_EResult::EOS_InvalidParameters;
}

// --- Implemented: the connection notifications ---

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionRequest(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionRequestOptions* Options,
    void* ClientData, EOS_P2P_OnIncomingConnectionRequestCallback ConnectionRequestHandler) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return p2p->add_notify_connection_request(Options->SocketId, ClientData, ConnectionRequestHandler);
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionRequest(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionEstablished(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionEstablishedOptions* Options,
    void* ClientData, EOS_P2P_OnPeerConnectionEstablishedCallback ConnectionEstablishedHandler) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return p2p->add_notify_connection_established(ClientData, ConnectionEstablishedHandler,
                                                 Options->SocketId);
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionEstablished(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionInterrupted(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionInterruptedOptions* Options,
    void* ClientData, EOS_P2P_OnPeerConnectionInterruptedCallback ConnectionInterruptedHandler) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return p2p->add_notify_connection_interrupted(ClientData, ConnectionInterruptedHandler,
                                                 Options->SocketId);
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionInterrupted(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionClosed(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionClosedOptions* Options,
    void* ClientData, EOS_P2P_OnRemoteConnectionClosedCallback ConnectionClosedHandler) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST ||
        !p2p->is_local_user(Options->LocalUserId)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return p2p->add_notify_connection_closed(ClientData, ConnectionClosedHandler, Options->SocketId);
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionClosed(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

// --- NAT, relay, ports, and the packet queue ---

EOS_DECLARE_FUNC(void) EOS_P2P_QueryNATType(EOS_HP2P Handle, const EOS_P2P_QueryNATTypeOptions* Options,
                                            void* ClientData, const EOS_P2P_OnQueryNATTypeCompleteCallback CompletionDelegate) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->query_nat_type(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetNATType(EOS_HP2P Handle, const EOS_P2P_GetNATTypeOptions* Options,
                                                 EOS_ENATType* OutNATType) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETNATTYPE_API_LATEST) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return p2p->get_nat_type(OutNATType);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetRelayControl(EOS_HP2P Handle, const EOS_P2P_SetRelayControlOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->set_relay_control(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetRelayControl(EOS_HP2P Handle, const EOS_P2P_GetRelayControlOptions* Options,
                                                      EOS_ERelayControl* OutRelayControl) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETRELAYCONTROL_API_LATEST) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return p2p->get_relay_control(OutRelayControl);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetPortRange(EOS_HP2P Handle, const EOS_P2P_SetPortRangeOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->set_port_range(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetPortRange(EOS_HP2P Handle, const EOS_P2P_GetPortRangeOptions* Options,
                                                   uint16_t* OutPort, uint16_t* OutNumAdditionalPortsToTry) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETPORTRANGE_API_LATEST) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return p2p->get_port_range(OutPort, OutNumAdditionalPortsToTry);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetPacketQueueSize(EOS_HP2P Handle, const EOS_P2P_SetPacketQueueSizeOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->set_packet_queue_size(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetPacketQueueInfo(EOS_HP2P Handle, const EOS_P2P_GetPacketQueueInfoOptions* Options,
                                                         EOS_P2P_PacketQueueInfo* OutPacketQueueInfo) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 || Options->ApiVersion != EOS_P2P_GETPACKETQUEUEINFO_API_LATEST) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return p2p->get_packet_queue_info(OutPacketQueueInfo);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyIncomingPacketQueueFull(
    EOS_HP2P Handle, const EOS_P2P_AddNotifyIncomingPacketQueueFullOptions* Options,
    void* ClientData, EOS_P2P_OnIncomingPacketQueueFullCallback IncomingPacketQueueFullHandler) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p == 0 || Options == 0 ||
        Options->ApiVersion != EOS_P2P_ADDNOTIFYINCOMINGPACKETQUEUEFULL_API_LATEST) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return p2p->add_notify_incoming_packet_queue_full(ClientData, IncomingPacketQueueFullHandler);
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyIncomingPacketQueueFull(EOS_HP2P Handle, EOS_NotificationId NotificationId) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    if (p2p != 0) {
        p2p->remove_notify(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_ClearPacketQueue(EOS_HP2P Handle, const EOS_P2P_ClearPacketQueueOptions* Options) {
    eosr::sdk_p2p* p2p = checked_p2p(Handle);
    return (p2p != 0) ? p2p->clear_packet_queue(Options) : EOS_EResult::EOS_InvalidParameters;
}
