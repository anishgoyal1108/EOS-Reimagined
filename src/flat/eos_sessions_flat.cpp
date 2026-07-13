// Flat C ABI trampolines for the Sessions interface and its four sub-handles.
//
// The sub-handle calls (EOS_SessionModification_*, EOS_SessionSearch_*, EOS_SessionDetails_*,
// EOS_ActiveSession_*) take the sub-handle as their first argument, not the platform's Sessions
// handle, so there is no slot to compare them against. They resolve through the live platform and
// are validated against the store that minted them, which is what makes an unknown or stale handle
// a rejection instead of a dereference.
#include "eos_sessions.h"

#include "core/platform.h"
#include "core/runtime.h"
#include "interfaces/sessions.h"

namespace {

eosr::sdk_sessions* live_sessions() {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    return &platform->sessions();
}

// The Sessions handle itself is the platform's slot, so it can be checked exactly.
eosr::sdk_sessions* checked_sessions(EOS_HSessions handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HSessions>(platform->interface_handle(eosr::if_sessions))) {
        return 0;
    }
    return &platform->sessions();
}

template <class delegate_type>
void stub_async(void* client_data, delegate_type delegate, std::size_t info_size) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->queue_stub_result(client_data,
                                    reinterpret_cast<eosr::completion_delegate>(delegate), info_size);
    }
}

template <class delegate_type>
EOS_NotificationId stub_notify(void* client_data, delegate_type delegate, std::size_t info_size) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return sessions->add_stub_notification(
        client_data, reinterpret_cast<eosr::completion_delegate>(delegate), info_size);
}

} // namespace

// --- Sessions ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CreateSessionModification(EOS_HSessions Handle, const EOS_Sessions_CreateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    return (sessions != 0) ? sessions->create_session_modification(Options, OutSessionModificationHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_UpdateSessionModification(EOS_HSessions Handle, const EOS_Sessions_UpdateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    return (sessions != 0) ? sessions->update_session_modification(Options, OutSessionModificationHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CreateSessionSearch(EOS_HSessions Handle, const EOS_Sessions_CreateSessionSearchOptions* Options, EOS_HSessionSearch* OutSessionSearchHandle) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    return (sessions != 0) ? sessions->create_session_search(Options, OutSessionSearchHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopyActiveSessionHandle(EOS_HSessions Handle, const EOS_Sessions_CopyActiveSessionHandleOptions* Options, EOS_HActiveSession* OutSessionHandle) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    return (sessions != 0) ? sessions->copy_active_session_handle(Options, OutSessionHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_IsUserInSession(EOS_HSessions Handle, const EOS_Sessions_IsUserInSessionOptions* Options) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    return (sessions != 0) ? sessions->is_user_in_session(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_DumpSessionState(EOS_HSessions Handle, const EOS_Sessions_DumpSessionStateOptions* Options) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    return (sessions != 0) ? sessions->dump_session_state(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_Sessions_UpdateSession(EOS_HSessions Handle, const EOS_Sessions_UpdateSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnUpdateSessionCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->update_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_DestroySession(EOS_HSessions Handle, const EOS_Sessions_DestroySessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnDestroySessionCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->destroy_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_JoinSession(EOS_HSessions Handle, const EOS_Sessions_JoinSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnJoinSessionCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->join_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_StartSession(EOS_HSessions Handle, const EOS_Sessions_StartSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnStartSessionCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->start_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_EndSession(EOS_HSessions Handle, const EOS_Sessions_EndSessionOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnEndSessionCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->end_session(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RegisterPlayers(EOS_HSessions Handle, const EOS_Sessions_RegisterPlayersOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnRegisterPlayersCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->register_players(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Sessions_UnregisterPlayers(EOS_HSessions Handle, const EOS_Sessions_UnregisterPlayersOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnUnregisterPlayersCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = checked_sessions(Handle);
    if (sessions != 0) {
        sessions->unregister_players(Options, ClientData, CompletionDelegate);
    }
}

// --- SessionModification ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetBucketId(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetBucketIdOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_set_bucket_id(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetHostAddress(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetHostAddressOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_set_host_address(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetPermissionLevel(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetPermissionLevelOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_set_permission_level(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetJoinInProgressAllowed(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetJoinInProgressAllowedOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_set_join_in_progress(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetMaxPlayers(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetMaxPlayersOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_set_max_players(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetInvitesAllowed(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_SetInvitesAllowedOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_set_invites_allowed(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_AddAttribute(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_AddAttributeOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_add_attribute(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_RemoveAttribute(EOS_HSessionModification Handle,
                                                                 const EOS_SessionModification_RemoveAttributeOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->modification_remove_attribute(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

// --- SessionSearch ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetSessionId(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetSessionIdOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_set_session_id(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetTargetUserId(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetTargetUserIdOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_set_target_user(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetParameter(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetParameterOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_set_parameter(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_RemoveParameter(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_RemoveParameterOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_remove_parameter(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetMaxResults(EOS_HSessionSearch Handle,
                                                           const EOS_SessionSearch_SetMaxResultsOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_set_max_results(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_SessionSearch_Find(EOS_HSessionSearch Handle, const EOS_SessionSearch_FindOptions* Options,
                                             void* ClientData, const EOS_SessionSearch_OnFindCallback CompletionDelegate) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->search_find(Handle, Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(uint32_t) EOS_SessionSearch_GetSearchResultCount(EOS_HSessionSearch Handle,
                                                                  const EOS_SessionSearch_GetSearchResultCountOptions* Options) {
    (void)Options;
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_result_count(Handle) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_CopySearchResultByIndex(EOS_HSessionSearch Handle,
                                                                        const EOS_SessionSearch_CopySearchResultByIndexOptions* Options,
                                                                        EOS_HSessionDetails* OutSessionHandle) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->search_copy_result(Handle, Options, OutSessionHandle)
                           : EOS_EResult::EOS_InvalidParameters;
}

// --- SessionDetails ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopyInfo(EOS_HSessionDetails Handle,
                                                          const EOS_SessionDetails_CopyInfoOptions* Options,
                                                          EOS_SessionDetails_Info** OutSessionInfo) {
    (void)Options;
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->details_copy_info(Handle, OutSessionInfo)
                           : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(uint32_t) EOS_SessionDetails_GetSessionAttributeCount(EOS_HSessionDetails Handle,
                                                                       const EOS_SessionDetails_GetSessionAttributeCountOptions* Options) {
    (void)Options;
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->details_attribute_count(Handle) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopySessionAttributeByIndex(EOS_HSessionDetails Handle,
                                                                             const EOS_SessionDetails_CopySessionAttributeByIndexOptions* Options,
                                                                             EOS_SessionDetails_Attribute** OutSessionAttribute) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->details_copy_attribute_by_index(Handle, Options, OutSessionAttribute)
                           : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopySessionAttributeByKey(EOS_HSessionDetails Handle,
                                                                           const EOS_SessionDetails_CopySessionAttributeByKeyOptions* Options,
                                                                           EOS_SessionDetails_Attribute** OutSessionAttribute) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->details_copy_attribute_by_key(Handle, Options, OutSessionAttribute)
                           : EOS_EResult::EOS_InvalidParameters;
}

// --- ActiveSession ---

EOS_DECLARE_FUNC(EOS_EResult) EOS_ActiveSession_CopyInfo(EOS_HActiveSession Handle,
                                                         const EOS_ActiveSession_CopyInfoOptions* Options,
                                                         EOS_ActiveSession_Info** OutActiveSessionInfo) {
    (void)Options;
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->active_copy_info(Handle, OutActiveSessionInfo)
                           : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(uint32_t) EOS_ActiveSession_GetRegisteredPlayerCount(EOS_HActiveSession Handle,
                                                                      const EOS_ActiveSession_GetRegisteredPlayerCountOptions* Options) {
    (void)Options;
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->active_registered_count(Handle) : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_ActiveSession_GetRegisteredPlayerByIndex(EOS_HActiveSession Handle,
                                                                                 const EOS_ActiveSession_GetRegisteredPlayerByIndexOptions* Options) {
    eosr::sdk_sessions* sessions = live_sessions();
    return (sessions != 0) ? sessions->active_registered_by_index(Handle, Options) : 0;
}

// --- Release: the handles, and the structs CopyInfo hands out ---

EOS_DECLARE_FUNC(void) EOS_SessionModification_Release(EOS_HSessionModification SessionModificationHandle) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->modification_release(SessionModificationHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_SessionSearch_Release(EOS_HSessionSearch SessionSearchHandle) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->search_release(SessionSearchHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Release(EOS_HSessionDetails SessionHandle) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->details_release(SessionHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_ActiveSession_Release(EOS_HActiveSession ActiveSessionHandle) {
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->active_release(ActiveSessionHandle);
    }
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Info_Release(EOS_SessionDetails_Info* SessionInfo) {
    eosr::release_session_details_info(SessionInfo);
}

EOS_DECLARE_FUNC(void) EOS_ActiveSession_Info_Release(EOS_ActiveSession_Info* ActiveSessionInfo) {
    eosr::release_active_session_info(ActiveSessionInfo);
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Attribute_Release(EOS_SessionDetails_Attribute* SessionAttribute) {
    eosr::release_session_details_attribute(SessionAttribute);
}

// --- Deferred: the invite path and the overlay hooks it needs ---

EOS_DECLARE_FUNC(void) EOS_Sessions_SendInvite(EOS_HSessions Handle, const EOS_Sessions_SendInviteOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnSendInviteCallback CompletionDelegate) {
    (void)Handle;
    (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Sessions_SendInviteCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RejectInvite(EOS_HSessions Handle, const EOS_Sessions_RejectInviteOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnRejectInviteCallback CompletionDelegate) {
    (void)Handle;
    (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Sessions_RejectInviteCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_QueryInvites(EOS_HSessions Handle, const EOS_Sessions_QueryInvitesOptions* Options,
                                              void* ClientData, const EOS_Sessions_OnQueryInvitesCallback CompletionDelegate) {
    (void)Handle;
    (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Sessions_QueryInvitesCallbackInfo));
}

EOS_DECLARE_FUNC(uint32_t) EOS_Sessions_GetInviteCount(EOS_HSessions Handle, const EOS_Sessions_GetInviteCountOptions* Options) {
    (void)Handle;
    (void)Options;
    return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_GetInviteIdByIndex(EOS_HSessions Handle, const EOS_Sessions_GetInviteIdByIndexOptions* Options,
                                                              char* OutBuffer, int32_t* InOutBufferLength) {
    (void)Handle;
    (void)Options;
    (void)OutBuffer;
    (void)InOutBufferLength;
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleByInviteId(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleByInviteIdOptions* Options,
                                                                       EOS_HSessionDetails* OutSessionHandle) {
    (void)Handle;
    (void)Options;
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleByUiEventId(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleByUiEventIdOptions* Options,
                                                                        EOS_HSessionDetails* OutSessionHandle) {
    (void)Handle;
    (void)Options;
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleForPresence(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleForPresenceOptions* Options,
                                                                        EOS_HSessionDetails* OutSessionHandle) {
    (void)Handle;
    (void)Options;
    if (OutSessionHandle != 0) {
        *OutSessionHandle = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetAllowedPlatformIds(EOS_HSessionModification Handle,
                                                                            const EOS_SessionModification_SetAllowedPlatformIdsOptions* Options) {
    (void)Handle;
    (void)Options;
    // A LAN peer is whatever platform it is; we do not turn any away.
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteReceived(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteReceivedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSessionInviteReceivedCallback Handler) {
    (void)Handle;
    (void)Options;
    return stub_notify(ClientData, Handler, sizeof(EOS_Sessions_SessionInviteReceivedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteReceived(EOS_HSessions Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteAccepted(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteAcceptedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSessionInviteAcceptedCallback Handler) {
    (void)Handle;
    (void)Options;
    return stub_notify(ClientData, Handler, sizeof(EOS_Sessions_SessionInviteAcceptedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteAccepted(EOS_HSessions Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteRejected(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteRejectedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSessionInviteRejectedCallback Handler) {
    (void)Handle;
    (void)Options;
    return stub_notify(ClientData, Handler, sizeof(EOS_Sessions_SessionInviteRejectedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteRejected(EOS_HSessions Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifyJoinSessionAccepted(EOS_HSessions Handle, const EOS_Sessions_AddNotifyJoinSessionAcceptedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnJoinSessionAcceptedCallback Handler) {
    (void)Handle;
    (void)Options;
    return stub_notify(ClientData, Handler, sizeof(EOS_Sessions_JoinSessionAcceptedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifyJoinSessionAccepted(EOS_HSessions Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifyLeaveSessionRequested(EOS_HSessions Handle, const EOS_Sessions_AddNotifyLeaveSessionRequestedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnLeaveSessionRequestedCallback Handler) {
    (void)Handle;
    (void)Options;
    return stub_notify(ClientData, Handler, sizeof(EOS_Sessions_LeaveSessionRequestedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifyLeaveSessionRequested(EOS_HSessions Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySendSessionNativeInviteRequested(EOS_HSessions Handle, const EOS_Sessions_AddNotifySendSessionNativeInviteRequestedOptions* Options,
                                                                       void* ClientData, const EOS_Sessions_OnSendSessionNativeInviteRequestedCallback Handler) {
    (void)Handle;
    (void)Options;
    return stub_notify(ClientData, Handler, sizeof(EOS_Sessions_SendSessionNativeInviteRequestedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySendSessionNativeInviteRequested(EOS_HSessions Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_sessions* sessions = live_sessions();
    if (sessions != 0) {
        sessions->remove_notification(InId);
    }
}

