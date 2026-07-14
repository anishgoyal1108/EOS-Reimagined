#include "interfaces/friends.h"

#include <memory>

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "net/message_router.h"
#include "net/messages.h"

namespace eosr {

namespace {

// One-shot completions all run through add_callback, which does not discriminate on the type id;
// the notifications do, so those two get their own ids.
const callback_type_id cb_completion = 1;
const callback_type_id cb_friends_update = 2;
const callback_type_id cb_blocked_users_update = 3;

bool version_ok(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

EOS_EpicAccountId epic_from(const std::string& id_str) {
    return id_str.empty() ? 0 : id_registry::instance().get_epic_account_id(id_str);
}

} // namespace

sdk_friends::sdk_friends(sdk_settings& settings, callback_manager& callbacks, message_router& network)
    : settings_(settings), callbacks_(callbacks), network_(network), registered_(false) {
}

sdk_friends::~sdk_friends() {
    emu_deinit();
}

void sdk_friends::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::peer_connected, this);
    network_.register_listener(message_type::peer_disconnected, this);
    registered_ = true;
}

void sdk_friends::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::peer_connected, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    friends_.clear();
    friend_by_peer_.clear();
    pending_updates_.clear();
    registered_ = false;
}

bool sdk_friends::is_local_user(EOS_EpicAccountId id) const {
    return id != 0 && id->valid && id->id_str == settings_.epic_account_id();
}

bool sdk_friends::is_friend(const std::string& epic_id) const {
    for (std::size_t i = 0; i < friends_.size(); i++) {
        if (friends_[i] == epic_id) {
            return true;
        }
    }
    return false;
}

void sdk_friends::deliver_simple(void* client_data, completion_delegate delegate,
                                 std::size_t info_size, EOS_EResult code, const std::string& local,
                                 const std::string& target) {
    if (delegate == 0) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_completion, info_size, delegate);
    // Every friends completion begins with { ResultCode, ClientData, LocalUserId }; the invite ones
    // append TargetUserId. QueryFriendsCallbackInfo is that common prefix.
    EOS_Friends_QueryFriendsCallbackInfo* prefix =
        static_cast<EOS_Friends_QueryFriendsCallbackInfo*>(payload);
    prefix->ResultCode = code;
    prefix->ClientData = client_data;
    prefix->LocalUserId = epic_from(local);
    // Only the invite callbacks carry a target, and only their payload has room for it.
    if (!target.empty()) {
        EOS_Friends_SendInviteCallbackInfo* full =
            static_cast<EOS_Friends_SendInviteCallbackInfo*>(payload);
        full->TargetUserId = epic_from(target);
    }
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

// The friends list is the roster, and the roster is already built. So a query has nothing to fetch:
// it succeeds on the next tick, as the caller expects an async operation to.
void sdk_friends::query_friends(const EOS_Friends_QueryFriendsOptions* options, void* client_data,
                                EOS_Friends_OnQueryFriendsCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const std::size_t info_size = sizeof(EOS_Friends_QueryFriendsCallbackInfo);
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_QUERYFRIENDS_API_LATEST) ||
        options->LocalUserId == 0 || !options->LocalUserId->valid) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidParameters, std::string(), std::string());
        return;
    }
    if (!is_local_user(options->LocalUserId)) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidUser, options->LocalUserId->id_str, std::string());
        return;
    }
    deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                   EOS_EResult::EOS_Success, options->LocalUserId->id_str, std::string());
}

// A meshed peer is already a friend, so there is nothing to send, accept, or reject. We answer the
// well-formed request with success -- the friendship the game is asking for already exists -- rather
// than fail a call the game may be waiting on. There is no service here to do anything more.
void sdk_friends::send_invite(const EOS_Friends_SendInviteOptions* options, void* client_data,
                              EOS_Friends_OnSendInviteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const std::size_t info_size = sizeof(EOS_Friends_SendInviteCallbackInfo);
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_SENDINVITE_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0 || !options->LocalUserId->valid ||
        !options->TargetUserId->valid) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidParameters, std::string(), std::string());
        return;
    }
    if (!is_local_user(options->LocalUserId)) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidUser, options->LocalUserId->id_str,
                       options->TargetUserId->id_str);
        return;
    }
    deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                   EOS_EResult::EOS_Success, options->LocalUserId->id_str,
                   options->TargetUserId->id_str);
}

void sdk_friends::accept_invite(const EOS_Friends_AcceptInviteOptions* options, void* client_data,
                                EOS_Friends_OnAcceptInviteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const std::size_t info_size = sizeof(EOS_Friends_AcceptInviteCallbackInfo);
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_ACCEPTINVITE_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0 || !options->LocalUserId->valid ||
        !options->TargetUserId->valid) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidParameters, std::string(), std::string());
        return;
    }
    if (!is_local_user(options->LocalUserId)) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidUser, options->LocalUserId->id_str,
                       options->TargetUserId->id_str);
        return;
    }
    deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                   EOS_EResult::EOS_Success, options->LocalUserId->id_str,
                   options->TargetUserId->id_str);
}

void sdk_friends::reject_invite(const EOS_Friends_RejectInviteOptions* options, void* client_data,
                                EOS_Friends_OnRejectInviteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const std::size_t info_size = sizeof(EOS_Friends_RejectInviteCallbackInfo);
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_REJECTINVITE_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0 || !options->LocalUserId->valid ||
        !options->TargetUserId->valid) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidParameters, std::string(), std::string());
        return;
    }
    if (!is_local_user(options->LocalUserId)) {
        deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                       EOS_EResult::EOS_InvalidUser, options->LocalUserId->id_str,
                       options->TargetUserId->id_str);
        return;
    }
    deliver_simple(client_data, reinterpret_cast<completion_delegate>(delegate), info_size,
                   EOS_EResult::EOS_Success, options->LocalUserId->id_str,
                   options->TargetUserId->id_str);
}

i32 sdk_friends::get_friends_count(const EOS_Friends_GetFriendsCountOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST) ||
        !is_local_user(options->LocalUserId)) {
        return 0;
    }
    return static_cast<i32>(friends_.size());
}

EOS_EpicAccountId sdk_friends::get_friend_at_index(
    const EOS_Friends_GetFriendAtIndexOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST) ||
        !is_local_user(options->LocalUserId) || options->Index < 0 ||
        static_cast<std::size_t>(options->Index) >= friends_.size()) {
        return 0;
    }
    return epic_from(friends_[static_cast<std::size_t>(options->Index)]);
}

EOS_EFriendsStatus sdk_friends::get_status(const EOS_Friends_GetStatusOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_FRIENDS_GETSTATUS_API_LATEST) ||
        !is_local_user(options->LocalUserId) || options->TargetUserId == 0 ||
        !options->TargetUserId->valid) {
        return EOS_EFriendsStatus::EOS_FS_NotFriends;
    }
    return is_friend(options->TargetUserId->id_str) ? EOS_EFriendsStatus::EOS_FS_Friends
                                                     : EOS_EFriendsStatus::EOS_FS_NotFriends;
}

EOS_NotificationId sdk_friends::add_notify_friends_update(
    const EOS_Friends_AddNotifyFriendsUpdateOptions* options, void* client_data,
    EOS_Friends_OnFriendsUpdateCallback delegate) {
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_FRIENDS_ADDNOTIFYFRIENDSUPDATE_API_LATEST)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Friends_OnFriendsUpdateInfo* info = static_cast<EOS_Friends_OnFriendsUpdateInfo*>(
        result->create_callback(cb_friends_update, sizeof(EOS_Friends_OnFriendsUpdateInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->LocalUserId = 0;
    info->TargetUserId = 0;
    info->PreviousStatus = EOS_EFriendsStatus::EOS_FS_NotFriends;
    info->CurrentStatus = EOS_EFriendsStatus::EOS_FS_NotFriends;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_friends::remove_notify_friends_update(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

// There is no block list -- nothing here can block a peer -- so it is always empty.
i32 sdk_friends::get_blocked_users_count(
    const EOS_Friends_GetBlockedUsersCountOptions* options) const {
    (void)options;
    return 0;
}

EOS_EpicAccountId sdk_friends::get_blocked_user_at_index(
    const EOS_Friends_GetBlockedUserAtIndexOptions* options) const {
    (void)options;
    return 0;
}

// The blocked-users list never changes because it is always empty, so this notification binds and
// never fires -- but the binding is real, so a game gets a valid id and can pair a Remove with it.
EOS_NotificationId sdk_friends::add_notify_blocked_users_update(
    const EOS_Friends_AddNotifyBlockedUsersUpdateOptions* options, void* client_data,
    EOS_Friends_OnBlockedUsersUpdateCallback delegate) {
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_FRIENDS_ADDNOTIFYBLOCKEDUSERSUPDATE_API_LATEST)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Friends_OnBlockedUsersUpdateInfo* info = static_cast<EOS_Friends_OnBlockedUsersUpdateInfo*>(
        result->create_callback(cb_blocked_users_update, sizeof(EOS_Friends_OnBlockedUsersUpdateInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->LocalUserId = 0;
    info->TargetUserId = 0;
    info->bBlocked = EOS_FALSE;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_friends::remove_notify_blocked_users_update(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

bool sdk_friends::cb_run_frame() {
    if (pending_updates_.empty()) {
        return false;
    }
    std::vector<friends_update> updates;
    updates.swap(pending_updates_);
    const EOS_EpicAccountId self = epic_from(settings_.epic_account_id());
    for (std::size_t i = 0; i < updates.size(); i++) {
        const friends_update& change = updates[i];
        // Re-look-up each notification by id before firing: a fired callback may remove another, or
        // remove itself. We never touch the payload after fire() -- self-removal frees it.
        const std::vector<EOS_NotificationId> ids =
            callbacks_.notification_ids(this, cb_friends_update);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            EOS_Friends_OnFriendsUpdateInfo* info =
                note->get_callback<EOS_Friends_OnFriendsUpdateInfo>();
            info->LocalUserId = self;
            info->TargetUserId = epic_from(change.target_epic);
            info->PreviousStatus = change.previous;
            info->CurrentStatus = change.current;
            note->fire();
        }
    }
    return false;
}

bool sdk_friends::run_callbacks(frame_result&) {
    return false;
}

void sdk_friends::free_callback(frame_result&) {
}

bool sdk_friends::on_network_message(const net_envelope& message) {
    if (message.source_id.empty() || message.source_id == settings_.product_user_id()) {
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::peer_connected)) {
        // A peer arrives carrying the Epic account id its key derives -- the one Presence and the
        // rest of the SDK trust -- so a joining peer becomes a friend under an id it cannot forge.
        const std::string epic(message.payload.begin(), message.payload.end());
        if (epic.empty() || epic == settings_.epic_account_id() || is_friend(epic)) {
            return true;
        }
        friends_.push_back(epic);
        friend_by_peer_[message.source_id] = epic;
        friends_update change;
        change.target_epic = epic;
        change.previous = EOS_EFriendsStatus::EOS_FS_NotFriends;
        change.current = EOS_EFriendsStatus::EOS_FS_Friends;
        pending_updates_.push_back(change);
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        // The disconnect carries no epic id -- the peer is gone from the router by the time it fires
        // -- so we drop the friend we recorded under this product user id.
        std::map<std::string, std::string>::iterator it = friend_by_peer_.find(message.source_id);
        if (it == friend_by_peer_.end()) {
            return true;
        }
        const std::string epic = it->second;
        friend_by_peer_.erase(it);
        for (std::size_t i = 0; i < friends_.size(); i++) {
            if (friends_[i] == epic) {
                friends_.erase(friends_.begin() + i);
                break;
            }
        }
        friends_update change;
        change.target_epic = epic;
        change.previous = EOS_EFriendsStatus::EOS_FS_Friends;
        change.current = EOS_EFriendsStatus::EOS_FS_NotFriends;
        pending_updates_.push_back(change);
        return true;
    }
    return false;
}

} // namespace eosr
