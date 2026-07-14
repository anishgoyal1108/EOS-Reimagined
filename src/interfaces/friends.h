#ifndef EOSR_INTERFACES_FRIENDS_H
#define EOSR_INTERFACES_FRIENDS_H

#include <map>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_friends_types.h"

#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"
#include "core/i_run_network.h"

namespace eosr {

class sdk_settings;
class callback_manager;
class message_router;
struct net_envelope;

// The Friends interface: the friends list, blocked users, and invitations.
//
// There is no friends service here, and there is no need for one. On a LAN, the people you can play
// with are the people on the mesh, so the friends list *is* the authenticated peer roster: every
// peer we have met is a friend, learned from the same key-derived Epic account id the rest of the
// SDK trusts. A friend appears when a peer joins and disappears when it leaves, and the update
// notification fires on the tick with that transition -- which is exactly how a game learns a
// player is now reachable.
//
// The half of this interface that manages friendship with a *service* -- sending, accepting, and
// rejecting invitations -- has nothing to manage: a meshed peer is already a friend, so an invite
// is a no-op that succeeds. And there is no block list: nothing here can block a peer, so the
// blocked-users list is always empty and its notification never fires.
// Spec: EOSSDK_Friends (docs/friends.md)
class sdk_friends : public i_run_callback, public i_run_network {
public:
    sdk_friends(sdk_settings& settings, callback_manager& callbacks, message_router& network);
    ~sdk_friends();

    sdk_friends(const sdk_friends&) = delete;
    sdk_friends& operator=(const sdk_friends&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Flat API surface (called by the trampolines in flat/eos_friends_flat.cpp) ---

    void query_friends(const EOS_Friends_QueryFriendsOptions* options, void* client_data,
                       EOS_Friends_OnQueryFriendsCallback delegate);
    void send_invite(const EOS_Friends_SendInviteOptions* options, void* client_data,
                     EOS_Friends_OnSendInviteCallback delegate);
    void accept_invite(const EOS_Friends_AcceptInviteOptions* options, void* client_data,
                       EOS_Friends_OnAcceptInviteCallback delegate);
    void reject_invite(const EOS_Friends_RejectInviteOptions* options, void* client_data,
                       EOS_Friends_OnRejectInviteCallback delegate);

    i32 get_friends_count(const EOS_Friends_GetFriendsCountOptions* options) const;
    EOS_EpicAccountId get_friend_at_index(const EOS_Friends_GetFriendAtIndexOptions* options) const;
    EOS_EFriendsStatus get_status(const EOS_Friends_GetStatusOptions* options) const;

    EOS_NotificationId add_notify_friends_update(
        const EOS_Friends_AddNotifyFriendsUpdateOptions* options, void* client_data,
        EOS_Friends_OnFriendsUpdateCallback delegate);
    void remove_notify_friends_update(EOS_NotificationId id);

    i32 get_blocked_users_count(const EOS_Friends_GetBlockedUsersCountOptions* options) const;
    EOS_EpicAccountId get_blocked_user_at_index(
        const EOS_Friends_GetBlockedUserAtIndexOptions* options) const;

    EOS_NotificationId add_notify_blocked_users_update(
        const EOS_Friends_AddNotifyBlockedUsersUpdateOptions* options, void* client_data,
        EOS_Friends_OnBlockedUsersUpdateCallback delegate);
    void remove_notify_blocked_users_update(EOS_NotificationId id);

    // i_run_callback
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // i_run_network
    bool on_network_message(const net_envelope& message);

private:
    // A friend list change we owe the registered notifications, delivered on the next tick.
    struct friends_update {
        std::string target_epic;
        EOS_EFriendsStatus previous;
        EOS_EFriendsStatus current;
    };

    // Whether `id` names the local logged-in user -- the only account whose friends we hold.
    bool is_local_user(EOS_EpicAccountId id) const;
    bool is_friend(const std::string& epic_id) const;
    // Queue a one-shot completion carrying a result code and the local user id (the shape the
    // friends async callbacks share; SendInvite/Accept/Reject also carry the target).
    void deliver_simple(void* client_data, completion_delegate delegate, std::size_t info_size,
                        EOS_EResult code, const std::string& local, const std::string& target);

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;

    // Meshed peers, each a friend, as Epic account ids in the order they joined. Self is never here.
    std::vector<std::string> friends_;
    // Which peer (product user id) each friend came in on. A disconnect names only the product user
    // id -- the epic id is gone from the router by then -- so this is how we find the friend to drop.
    std::map<std::string, std::string> friend_by_peer_;
    std::vector<friends_update> pending_updates_;
    bool registered_;
};

} // namespace eosr

#endif
