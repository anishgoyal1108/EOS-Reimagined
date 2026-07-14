// Flat C ABI trampolines for the Friends interface. Thin: resolve the platform, check the handle
// against its slot, forward. A game resolves every EOS_Friends_* symbol it imports when the library
// loads, so these have to exist even where the answer is "there is no friends service".
#include "eos_friends.h"

#include "common/eos_names.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
#include "interfaces/friends.h"

namespace {

eosr::sdk_friends* checked_friends(EOS_HFriends handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HFriends>(platform->interface_handle(eosr::if_friends))) {
        return 0;
    }
    return &platform->friends();
}

} // namespace

EOS_DECLARE_FUNC(void) EOS_Friends_QueryFriends(
    EOS_HFriends Handle, const EOS_Friends_QueryFriendsOptions* Options, void* ClientData,
    const EOS_Friends_OnQueryFriendsCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_QueryFriends",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_friends* friends = checked_friends(Handle);
    if (friends != 0) {
        friends->query_friends(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Friends_SendInvite(
    EOS_HFriends Handle, const EOS_Friends_SendInviteOptions* Options, void* ClientData,
    const EOS_Friends_OnSendInviteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_SendInvite",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_friends* friends = checked_friends(Handle);
    if (friends != 0) {
        friends->send_invite(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Friends_AcceptInvite(
    EOS_HFriends Handle, const EOS_Friends_AcceptInviteOptions* Options, void* ClientData,
    const EOS_Friends_OnAcceptInviteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_AcceptInvite",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_friends* friends = checked_friends(Handle);
    if (friends != 0) {
        friends->accept_invite(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Friends_RejectInvite(
    EOS_HFriends Handle, const EOS_Friends_RejectInviteOptions* Options, void* ClientData,
    const EOS_Friends_OnRejectInviteCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_RejectInvite",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_friends* friends = checked_friends(Handle);
    if (friends != 0) {
        friends->reject_invite(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(int32_t) EOS_Friends_GetFriendsCount(
    EOS_HFriends Handle, const EOS_Friends_GetFriendsCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_GetFriendsCount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    return eosr::traced_count(
        eosr_trace, (friends != 0) ? friends->get_friends_count(Options) : 0);
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Friends_GetFriendAtIndex(
    EOS_HFriends Handle, const EOS_Friends_GetFriendAtIndexOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_GetFriendAtIndex",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    const EOS_EpicAccountId result =
        (friends != 0) ? friends->get_friend_at_index(Options) : 0;
    return eosr::traced_handle(eosr_trace, result, eosr::label_kind::eaid);
}

EOS_DECLARE_FUNC(EOS_EFriendsStatus) EOS_Friends_GetStatus(
    EOS_HFriends Handle, const EOS_Friends_GetStatusOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_GetStatus",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    const EOS_EFriendsStatus result =
        (friends != 0) ? friends->get_status(Options)
                       : EOS_EFriendsStatus::EOS_FS_NotFriends;
    return eosr::traced_enum(eosr_trace, result, eosr::friends_status_name(result));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Friends_AddNotifyFriendsUpdate(
    EOS_HFriends Handle, const EOS_Friends_AddNotifyFriendsUpdateOptions* Options, void* ClientData,
    const EOS_Friends_OnFriendsUpdateCallback FriendsUpdateHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Friends_AddNotifyFriendsUpdate",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    const EOS_NotificationId result =
        (friends != 0)
            ? friends->add_notify_friends_update(Options, ClientData, FriendsUpdateHandler)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Friends_RemoveNotifyFriendsUpdate(EOS_HFriends Handle,
                                                             EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Friends_RemoveNotifyFriendsUpdate", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    if (friends != 0) {
        friends->remove_notify_friends_update(NotificationId);
    }
}

EOS_DECLARE_FUNC(int32_t) EOS_Friends_GetBlockedUsersCount(
    EOS_HFriends Handle, const EOS_Friends_GetBlockedUsersCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Friends_GetBlockedUsersCount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    return eosr::traced_count(
        eosr_trace, (friends != 0) ? friends->get_blocked_users_count(Options) : 0);
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Friends_GetBlockedUserAtIndex(
    EOS_HFriends Handle, const EOS_Friends_GetBlockedUserAtIndexOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Friends_GetBlockedUserAtIndex",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    const EOS_EpicAccountId result =
        (friends != 0) ? friends->get_blocked_user_at_index(Options) : 0;
    return eosr::traced_handle(eosr_trace, result, eosr::label_kind::eaid);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Friends_AddNotifyBlockedUsersUpdate(
    EOS_HFriends Handle, const EOS_Friends_AddNotifyBlockedUsersUpdateOptions* Options,
    void* ClientData, const EOS_Friends_OnBlockedUsersUpdateCallback BlockedUsersUpdateHandler) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Friends_AddNotifyBlockedUsersUpdate",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    const EOS_NotificationId result =
        (friends != 0)
            ? friends->add_notify_blocked_users_update(Options, ClientData,
                                                       BlockedUsersUpdateHandler)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Friends_RemoveNotifyBlockedUsersUpdate(
    EOS_HFriends Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Friends_RemoveNotifyBlockedUsersUpdate", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_friends* friends = checked_friends(Handle);
    if (friends != 0) {
        friends->remove_notify_blocked_users_update(NotificationId);
    }
}
