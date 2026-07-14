#include "doctest.h"

#include <string>

#include "eos_common.h"
#include "eos_friends_types.h"

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/friends.h"
#include "net/message_router.h"
#include "net/messages.h"

using namespace eosr;

namespace {

int g_query_count;
EOS_EResult g_query_result;
void EOS_CALL on_query(const EOS_Friends_QueryFriendsCallbackInfo* info) {
    g_query_count++;
    g_query_result = info->ResultCode;
}

EOS_EResult g_invite_result;
void EOS_CALL on_invite(const EOS_Friends_SendInviteCallbackInfo* info) {
    g_invite_result = info->ResultCode;
}

int g_update_count;
EOS_EFriendsStatus g_update_prev;
EOS_EFriendsStatus g_update_cur;
std::string g_update_target;
void EOS_CALL on_friends_update(const EOS_Friends_OnFriendsUpdateInfo* info) {
    g_update_count++;
    g_update_prev = info->PreviousStatus;
    g_update_cur = info->CurrentStatus;
    g_update_target = (info->TargetUserId != 0) ? info->TargetUserId->id_str : std::string();
}

int g_blocked_count;
void EOS_CALL on_blocked_update(const EOS_Friends_OnBlockedUsersUpdateInfo* info) {
    (void)info;
    g_blocked_count++;
}

// A notification that unregisters itself the moment it fires. Delivery must survive freeing the
// registration it is walking.
sdk_friends* g_self_removing_friends;
EOS_NotificationId g_self_removing_note;
void EOS_CALL on_update_remove_self(const EOS_Friends_OnFriendsUpdateInfo* info) {
    on_friends_update(info);
    if (g_self_removing_friends != 0 && g_self_removing_note != EOS_INVALID_NOTIFICATIONID) {
        g_self_removing_friends->remove_notify_friends_update(g_self_removing_note);
    }
}

struct friends_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    message_router network;
    sdk_friends friends;

    friends_fixture() : friends(settings, callbacks, network) {
        friends.emu_init();
        g_query_count = 0;
        g_query_result = EOS_EResult::EOS_UnexpectedError;
        g_invite_result = EOS_EResult::EOS_UnexpectedError;
        g_update_count = 0;
        g_update_prev = EOS_EFriendsStatus::EOS_FS_NotFriends;
        g_update_cur = EOS_EFriendsStatus::EOS_FS_NotFriends;
        g_update_target.clear();
        g_blocked_count = 0;
        g_self_removing_friends = 0;
        g_self_removing_note = EOS_INVALID_NOTIFICATIONID;
    }
    ~friends_fixture() { friends.emu_deinit(); }

    EOS_EpicAccountId me() {
        return id_registry::instance().get_epic_account_id(settings.epic_account_id());
    }
    EOS_EpicAccountId id(const std::string& s) {
        return id_registry::instance().get_epic_account_id(s);
    }

    void meets(const std::string& peer_id, const std::string& epic_id) {
        net_envelope event;
        event.type_tag = static_cast<u16>(message_type::peer_connected);
        event.source_id = peer_id;
        event.game_id = settings.product_id();
        event.payload.assign(epic_id.begin(), epic_id.end());
        friends.on_network_message(event);
    }
    void leaves(const std::string& peer_id) {
        net_envelope event;
        event.type_tag = static_cast<u16>(message_type::peer_disconnected);
        event.source_id = peer_id;
        event.game_id = settings.product_id();
        friends.on_network_message(event);
    }

    EOS_NotificationId listen_updates(EOS_Friends_OnFriendsUpdateCallback fn) {
        EOS_Friends_AddNotifyFriendsUpdateOptions options = {};
        options.ApiVersion = EOS_FRIENDS_ADDNOTIFYFRIENDSUPDATE_API_LATEST;
        return friends.add_notify_friends_update(&options, 0, fn);
    }
    i32 count() {
        EOS_Friends_GetFriendsCountOptions options = {};
        options.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
        options.LocalUserId = me();
        return friends.get_friends_count(&options);
    }
    EOS_EFriendsStatus status(EOS_EpicAccountId target) {
        EOS_Friends_GetStatusOptions options = {};
        options.ApiVersion = EOS_FRIENDS_GETSTATUS_API_LATEST;
        options.LocalUserId = me();
        options.TargetUserId = target;
        return friends.get_status(&options);
    }
};

const char* peer_a_puid = "11111111111111111111111111111111";
const char* peer_a_epic = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* peer_b_puid = "22222222222222222222222222222222";
const char* peer_b_epic = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

} // namespace

TEST_CASE("a fresh friends list is empty") {
    friends_fixture fx;
    CHECK(fx.count() == 0);
    CHECK(fx.status(fx.id(peer_a_epic)) == EOS_EFriendsStatus::EOS_FS_NotFriends);
}

TEST_CASE("every meshed peer is a friend, learned from its key-derived id") {
    friends_fixture fx;
    fx.meets(peer_a_puid, peer_a_epic);
    CHECK(fx.count() == 1);

    EOS_Friends_GetFriendAtIndexOptions at = {};
    at.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
    at.LocalUserId = fx.me();
    at.Index = 0;
    EOS_EpicAccountId friend0 = fx.friends.get_friend_at_index(&at);
    REQUIRE((friend0 != 0));
    CHECK(friend0->id_str == peer_a_epic);
    CHECK(fx.status(fx.id(peer_a_epic)) == EOS_EFriendsStatus::EOS_FS_Friends);

    // Out of range is a null id, not an out-of-bounds read.
    at.Index = 1;
    CHECK((fx.friends.get_friend_at_index(&at) == 0));
}

TEST_CASE("a peer join and leave fire the friends-update notification on the tick") {
    friends_fixture fx;
    const EOS_NotificationId note = fx.listen_updates(on_friends_update);
    REQUIRE(note != EOS_INVALID_NOTIFICATIONID);

    fx.meets(peer_a_puid, peer_a_epic);
    CHECK(g_update_count == 0); // never synchronously
    fx.callbacks.tick();
    CHECK(g_update_count == 1);
    CHECK(g_update_prev == EOS_EFriendsStatus::EOS_FS_NotFriends);
    CHECK(g_update_cur == EOS_EFriendsStatus::EOS_FS_Friends);
    CHECK(g_update_target == peer_a_epic);

    fx.leaves(peer_a_puid);
    CHECK(fx.count() == 0);
    fx.callbacks.tick();
    CHECK(g_update_count == 2);
    CHECK(g_update_prev == EOS_EFriendsStatus::EOS_FS_Friends);
    CHECK(g_update_cur == EOS_EFriendsStatus::EOS_FS_NotFriends);

    fx.friends.remove_notify_friends_update(note);
}

TEST_CASE("our own looped-back arrival does not make us our own friend") {
    friends_fixture fx;
    // A peer id we have not seen, but carrying our own epic id: we must not befriend ourselves.
    fx.meets(peer_a_puid, fx.settings.epic_account_id());
    CHECK(fx.count() == 0);
}

TEST_CASE("a duplicate peer announcement does not double-count a friend") {
    friends_fixture fx;
    fx.meets(peer_a_puid, peer_a_epic);
    fx.meets(peer_a_puid, peer_a_epic);
    CHECK(fx.count() == 1);
}

TEST_CASE("query friends succeeds for the local user and rejects another") {
    friends_fixture fx;
    EOS_Friends_QueryFriendsOptions options = {};
    options.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;
    options.LocalUserId = fx.me();
    fx.friends.query_friends(&options, 0, on_query);
    CHECK(g_query_count == 0);
    fx.callbacks.tick();
    CHECK(g_query_count == 1);
    CHECK(g_query_result == EOS_EResult::EOS_Success);

    options.LocalUserId = fx.id(peer_a_epic); // not the local user
    fx.friends.query_friends(&options, 0, on_query);
    fx.callbacks.tick();
    CHECK(g_query_count == 2);
    CHECK(g_query_result == EOS_EResult::EOS_InvalidUser);
}

TEST_CASE("an invite to a meshed peer succeeds, there being nothing to send") {
    friends_fixture fx;
    fx.meets(peer_a_puid, peer_a_epic);
    EOS_Friends_SendInviteOptions options = {};
    options.ApiVersion = EOS_FRIENDS_SENDINVITE_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.id(peer_a_epic);
    fx.friends.send_invite(&options, 0, on_invite);
    fx.callbacks.tick();
    CHECK(g_invite_result == EOS_EResult::EOS_Success);
}

TEST_CASE("the blocked-users list is always empty and its notification never fires") {
    friends_fixture fx;
    EOS_Friends_GetBlockedUsersCountOptions count = {};
    count.ApiVersion = EOS_FRIENDS_GETBLOCKEDUSERSCOUNT_API_LATEST;
    count.LocalUserId = fx.me();
    CHECK(fx.friends.get_blocked_users_count(&count) == 0);

    EOS_Friends_AddNotifyBlockedUsersUpdateOptions options = {};
    options.ApiVersion = EOS_FRIENDS_ADDNOTIFYBLOCKEDUSERSUPDATE_API_LATEST;
    const EOS_NotificationId note =
        fx.friends.add_notify_blocked_users_update(&options, 0, on_blocked_update);
    REQUIRE(note != EOS_INVALID_NOTIFICATIONID);
    fx.meets(peer_a_puid, peer_a_epic);
    fx.callbacks.tick();
    CHECK(g_blocked_count == 0);
    fx.friends.remove_notify_blocked_users_update(note);
}

TEST_CASE("a friends-update callback may unregister itself during delivery") {
    friends_fixture fx;
    g_self_removing_friends = &fx.friends;
    g_self_removing_note = fx.listen_updates(on_update_remove_self);
    REQUIRE(g_self_removing_note != EOS_INVALID_NOTIFICATIONID);

    fx.meets(peer_a_puid, peer_a_epic);
    fx.callbacks.tick();
    CHECK(g_update_count == 1);
    // A second event delivers to nobody, because the sole registration removed itself.
    fx.meets(peer_b_puid, peer_b_epic);
    fx.callbacks.tick();
    CHECK(g_update_count == 1);
}
