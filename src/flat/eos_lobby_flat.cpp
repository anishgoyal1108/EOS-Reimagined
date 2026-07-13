// Flat C ABI trampolines for the Lobby interface and its three sub-handles.
//
// Sub-handle calls take the sub-handle as their first argument, so they resolve through the
// live platform and are validated against the store that minted them. Invites, RTC rooms, and
// the overlay are deferred: their async calls report NotImplemented and their notifications
// register but never fire, so a game that uses them still runs.
#include "eos_lobby.h"

#include "core/platform.h"
#include "core/runtime.h"
#include "interfaces/lobby.h"

namespace {

eosr::sdk_lobby* live_lobby() {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    return &platform->lobby();
}

eosr::sdk_lobby* checked_lobby(EOS_HLobby handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HLobby>(platform->interface_handle(eosr::if_lobby))) {
        return 0;
    }
    return &platform->lobby();
}

template <class delegate_type>
void stub_async(void* client_data, delegate_type delegate, std::size_t info_size) {
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) {
        lobby->queue_stub_result(client_data,
                                 reinterpret_cast<eosr::completion_delegate>(delegate), info_size);
    }
}

template <class delegate_type>
EOS_NotificationId stub_notify(void* client_data, delegate_type delegate, std::size_t info_size) {
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    return lobby->add_stub_notification(
        client_data, reinterpret_cast<eosr::completion_delegate>(delegate), info_size);
}

} // namespace

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyAttributeByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyAttributeByIndexOptions* Options, EOS_Lobby_Attribute ** OutAttribute) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_copy_attribute_by_index(Handle, Options, OutAttribute) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyAttributeByKey(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyAttributeByKeyOptions* Options, EOS_Lobby_Attribute ** OutAttribute) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_copy_attribute_by_key(Handle, Options, OutAttribute) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyInfo(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyInfoOptions* Options, EOS_LobbyDetails_Info ** OutLobbyDetailsInfo) {
    eosr::sdk_lobby* lobby = live_lobby();
    (void)Options;
    return (lobby != 0) ? lobby->details_copy_info(Handle, OutLobbyDetailsInfo) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberAttributeByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberAttributeByIndexOptions* Options, EOS_Lobby_Attribute ** OutAttribute) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_copy_member_attribute_by_index(Handle, Options, OutAttribute) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberAttributeByKey(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberAttributeByKeyOptions* Options, EOS_Lobby_Attribute ** OutAttribute) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_copy_member_attribute_by_key(Handle, Options, OutAttribute) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberInfo(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberInfoOptions* Options, EOS_LobbyDetails_MemberInfo ** OutLobbyDetailsMemberInfo) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_copy_member_info(Handle, Options, OutLobbyDetailsMemberInfo) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetAttributeCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetAttributeCountOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    (void)Options;
    return (lobby != 0) ? lobby->details_attribute_count(Handle) : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_LobbyDetails_GetLobbyOwner(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetLobbyOwnerOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_get_lobby_owner(Handle, Options) : 0;
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetMemberAttributeCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberAttributeCountOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_member_attribute_count(Handle, Options) : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_LobbyDetails_GetMemberByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberByIndexOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_member_by_index(Handle, Options) : 0;
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetMemberCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberCountOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->details_member_count(Handle, Options) : 0;
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_Info_Release(EOS_LobbyDetails_Info* LobbyDetailsInfo) {
    eosr::release_lobby_details_info(LobbyDetailsInfo);
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_MemberInfo_Release(EOS_LobbyDetails_MemberInfo* LobbyDetailsMemberInfo) {
    eosr::release_lobby_details_member_info(LobbyDetailsMemberInfo);
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_Release(EOS_HLobbyDetails LobbyHandle) {
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->details_release(LobbyHandle); }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_AddAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_AddAttributeOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_add_attribute(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_AddMemberAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_AddMemberAttributeOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_add_member_attribute(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_LobbyModification_Release(EOS_HLobbyModification LobbyModificationHandle) {
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->modification_release(LobbyModificationHandle); }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_RemoveAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_RemoveAttributeOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_remove_attribute(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_RemoveMemberAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_RemoveMemberAttributeOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_remove_member_attribute(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetAllowedPlatformIds(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetAllowedPlatformIdsOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_set_allowed_platform_ids(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetBucketId(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetBucketIdOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_set_bucket_id(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetInvitesAllowed(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetInvitesAllowedOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_set_invites_allowed(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetMaxMembers(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetMaxMembersOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_set_max_members(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetPermissionLevel(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetPermissionLevelOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->modification_set_permission_level(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_CopySearchResultByIndex(EOS_HLobbySearch Handle, const EOS_LobbySearch_CopySearchResultByIndexOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->search_copy_result(Handle, Options, OutLobbyDetailsHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_LobbySearch_Find(EOS_HLobbySearch Handle, const EOS_LobbySearch_FindOptions* Options, void* ClientData, const EOS_LobbySearch_OnFindCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->search_find(Handle, Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbySearch_GetSearchResultCount(EOS_HLobbySearch Handle, const EOS_LobbySearch_GetSearchResultCountOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    (void)Options;
    return (lobby != 0) ? lobby->search_result_count(Handle) : 0;
}

EOS_DECLARE_FUNC(void) EOS_LobbySearch_Release(EOS_HLobbySearch LobbySearchHandle) {
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->search_release(LobbySearchHandle); }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_RemoveParameter(EOS_HLobbySearch Handle, const EOS_LobbySearch_RemoveParameterOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->search_remove_parameter(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetLobbyId(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetLobbyIdOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->search_set_lobby_id(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetMaxResults(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetMaxResultsOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->search_set_max_results(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetParameter(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetParameterOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->search_set_parameter(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetTargetUserId(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetTargetUserIdOptions* Options) {
    eosr::sdk_lobby* lobby = live_lobby();
    return (lobby != 0) ? lobby->search_set_target_user(Handle, Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyJoinLobbyAccepted(EOS_HLobby Handle, const EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyAcceptedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_JoinLobbyAcceptedCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLeaveLobbyRequested(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLeaveLobbyRequestedOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveLobbyRequestedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_LeaveLobbyRequestedCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteAccepted(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteAcceptedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_LobbyInviteAcceptedCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteReceivedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_LobbyInviteReceivedCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteRejected(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteRejectedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteRejectedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_LobbyInviteRejectedCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyMemberStatusReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberStatusReceivedCallback NotificationFn) {
    (void)Options;
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    return (lobby != 0) ? lobby->add_notify_lobby_member_status_received(ClientData, NotificationFn) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberUpdateReceivedCallback NotificationFn) {
    (void)Options;
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    return (lobby != 0) ? lobby->add_notify_lobby_member_update_received(ClientData, NotificationFn) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyUpdateReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyUpdateReceivedCallback NotificationFn) {
    (void)Options;
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    return (lobby != 0) ? lobby->add_notify_lobby_update_received(ClientData, NotificationFn) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyRTCRoomConnectionChanged(EOS_HLobby Handle, const EOS_Lobby_AddNotifyRTCRoomConnectionChangedOptions* Options, void* ClientData, const EOS_Lobby_OnRTCRoomConnectionChangedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_RTCRoomConnectionChangedCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifySendLobbyNativeInviteRequested(EOS_HLobby Handle, const EOS_Lobby_AddNotifySendLobbyNativeInviteRequestedOptions* Options, void* ClientData, const EOS_Lobby_OnSendLobbyNativeInviteRequestedCallback NotificationFn) {
    (void)Handle; (void)Options;
    return stub_notify(ClientData, NotificationFn, sizeof(EOS_Lobby_SendLobbyNativeInviteRequestedCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Lobby_Attribute_Release(EOS_Lobby_Attribute* LobbyAttribute) {
    eosr::release_lobby_attribute(LobbyAttribute);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandle(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby == 0) {
        // The header promises the out handle is null on any non-success return.
        if (OutLobbyDetailsHandle != 0) { *OutLobbyDetailsHandle = 0; }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return lobby->copy_lobby_details_handle(Options, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandleByInviteId(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle) {
    (void)Handle; (void)Options;
    if (OutLobbyDetailsHandle != 0) { *OutLobbyDetailsHandle = 0; }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle) {
    (void)Handle; (void)Options;
    if (OutLobbyDetailsHandle != 0) { *OutLobbyDetailsHandle = 0; }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_CreateLobby(EOS_HLobby Handle, const EOS_Lobby_CreateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnCreateLobbyCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->create_lobby(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CreateLobbySearch(EOS_HLobby Handle, const EOS_Lobby_CreateLobbySearchOptions* Options, EOS_HLobbySearch* OutLobbySearchHandle) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby == 0) {
        // The header promises the out handle is null on any non-success return.
        if (OutLobbySearchHandle != 0) { *OutLobbySearchHandle = 0; }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return lobby->create_lobby_search(Options, OutLobbySearchHandle);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_DestroyLobby(EOS_HLobby Handle, const EOS_Lobby_DestroyLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnDestroyLobbyCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->destroy_lobby(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetConnectString(EOS_HLobby Handle, const EOS_Lobby_GetConnectStringOptions* Options, char* OutBuffer, uint32_t* InOutBufferLength) {
    (void)Handle; (void)Options; (void)OutBuffer; (void)InOutBufferLength;
    return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Lobby_GetInviteCount(EOS_HLobby Handle, const EOS_Lobby_GetInviteCountOptions* Options) {
    (void)Handle; (void)Options;
    return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetInviteIdByIndex(EOS_HLobby Handle, const EOS_Lobby_GetInviteIdByIndexOptions* Options, char* OutBuffer, int32_t* InOutBufferLength) {
    (void)Handle; (void)Options; (void)OutBuffer; (void)InOutBufferLength;
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetRTCRoomName(EOS_HLobby Handle, const EOS_Lobby_GetRTCRoomNameOptions* Options, char* OutBuffer, uint32_t* InOutBufferLength) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    return (lobby != 0) ? lobby->get_rtc_room_name(Options, OutBuffer, InOutBufferLength) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_HardMuteMember(EOS_HLobby Handle, const EOS_Lobby_HardMuteMemberOptions* Options, void* ClientData, const EOS_Lobby_OnHardMuteMemberCallback CompletionDelegate) {
    (void)Handle; (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Lobby_HardMuteMemberCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_IsRTCRoomConnected(EOS_HLobby Handle, const EOS_Lobby_IsRTCRoomConnectedOptions* Options, EOS_Bool* bOutIsConnected) {
    (void)Handle; (void)Options;
    if (bOutIsConnected != 0) { *bOutIsConnected = EOS_FALSE; }
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinLobby(EOS_HLobby Handle, const EOS_Lobby_JoinLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->join_lobby(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinLobbyById(EOS_HLobby Handle, const EOS_Lobby_JoinLobbyByIdOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyByIdCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->join_lobby_by_id(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinRTCRoom(EOS_HLobby Handle, const EOS_Lobby_JoinRTCRoomOptions* Options, void* ClientData, const EOS_Lobby_OnJoinRTCRoomCallback CompletionDelegate) {
    (void)Handle; (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Lobby_JoinRTCRoomCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Lobby_KickMember(EOS_HLobby Handle, const EOS_Lobby_KickMemberOptions* Options, void* ClientData, const EOS_Lobby_OnKickMemberCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->kick_member(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_LeaveLobby(EOS_HLobby Handle, const EOS_Lobby_LeaveLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveLobbyCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->leave_lobby(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_LeaveRTCRoom(EOS_HLobby Handle, const EOS_Lobby_LeaveRTCRoomOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveRTCRoomCallback CompletionDelegate) {
    (void)Handle; (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Lobby_LeaveRTCRoomCallbackInfo));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_ParseConnectString(EOS_HLobby Handle, const EOS_Lobby_ParseConnectStringOptions* Options, char* OutBuffer, uint32_t* InOutBufferLength) {
    (void)Handle; (void)Options; (void)OutBuffer; (void)InOutBufferLength;
    return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_PromoteMember(EOS_HLobby Handle, const EOS_Lobby_PromoteMemberOptions* Options, void* ClientData, const EOS_Lobby_OnPromoteMemberCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->promote_member(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_QueryInvites(EOS_HLobby Handle, const EOS_Lobby_QueryInvitesOptions* Options, void* ClientData, const EOS_Lobby_OnQueryInvitesCallback CompletionDelegate) {
    (void)Handle; (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Lobby_QueryInvitesCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RejectInvite(EOS_HLobby Handle, const EOS_Lobby_RejectInviteOptions* Options, void* ClientData, const EOS_Lobby_OnRejectInviteCallback CompletionDelegate) {
    (void)Handle; (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Lobby_RejectInviteCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyJoinLobbyAccepted(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLeaveLobbyRequested(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteAccepted(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteReceived(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteRejected(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyUpdateReceived(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyRTCRoomConnectionChanged(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifySendLobbyNativeInviteRequested(EOS_HLobby Handle, EOS_NotificationId InId) {
    (void)Handle;
    eosr::sdk_lobby* lobby = live_lobby();
    if (lobby != 0) { lobby->remove_notify(InId); }
}

EOS_DECLARE_FUNC(void) EOS_Lobby_SendInvite(EOS_HLobby Handle, const EOS_Lobby_SendInviteOptions* Options, void* ClientData, const EOS_Lobby_OnSendInviteCallback CompletionDelegate) {
    (void)Handle; (void)Options;
    stub_async(ClientData, CompletionDelegate, sizeof(EOS_Lobby_SendInviteCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Lobby_UpdateLobby(EOS_HLobby Handle, const EOS_Lobby_UpdateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnUpdateLobbyCallback CompletionDelegate) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby != 0) { lobby->update_lobby(Options, ClientData, CompletionDelegate); }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_UpdateLobbyModification(EOS_HLobby Handle, const EOS_Lobby_UpdateLobbyModificationOptions* Options, EOS_HLobbyModification* OutLobbyModificationHandle) {
    eosr::sdk_lobby* lobby = checked_lobby(Handle);
    if (lobby == 0) {
        // The header promises the out handle is null on any non-success return.
        if (OutLobbyModificationHandle != 0) { *OutLobbyModificationHandle = 0; }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return lobby->update_lobby_modification(Options, OutLobbyModificationHandle);
}

