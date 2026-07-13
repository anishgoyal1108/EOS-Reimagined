#include "doctest.h"

#include <cstring>
#include <string>

#include "eos_common.h"
#include "eos_sessions_types.h"

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "common/byte_buffer.h"
#include "interfaces/connect.h"
#include "interfaces/sessions.h"
#include "net/message_router.h"
#include "net/messages.h"
#include "net/wire.h"

using namespace eosr;

namespace {

EOS_EResult g_update_result;
std::string g_update_id;
bool g_update_fired;

void EOS_CALL on_update(const EOS_Sessions_UpdateSessionCallbackInfo* info) {
    g_update_fired = true;
    g_update_result = info->ResultCode;
    g_update_id = (info->SessionId != 0) ? info->SessionId : "";
}

EOS_EResult g_simple_result;
bool g_simple_fired;
void EOS_CALL on_simple(const EOS_Sessions_StartSessionCallbackInfo* info) {
    g_simple_fired = true;
    g_simple_result = info->ResultCode;
}

u32 g_registered_count;
EOS_ProductUserId g_registered_player;
void EOS_CALL on_registered(const EOS_Sessions_RegisterPlayersCallbackInfo* info) {
    g_simple_fired = true;
    g_simple_result = info->ResultCode;
    g_registered_count = info->RegisteredPlayersCount;
    g_registered_player =
        (info->RegisteredPlayersCount > 0 && info->RegisteredPlayers != 0)
            ? info->RegisteredPlayers[0]
            : 0;
}

EOS_EResult g_find_result;
bool g_find_fired;
void EOS_CALL on_find(const EOS_SessionSearch_FindCallbackInfo* info) {
    g_find_fired = true;
    g_find_result = info->ResultCode;
}

// Everything a session needs, wired together the way the platform wires it.
struct sessions_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    message_router network;
    sdk_connect connect;
    sdk_sessions sessions;

    sessions_fixture() : connect(settings, callbacks, network), sessions(settings, callbacks, network, connect) {
        sessions.emu_init();
        g_update_fired = false;
        g_simple_fired = false;
        g_find_fired = false;
        g_update_result = EOS_EResult::EOS_UnexpectedError;
        g_simple_result = EOS_EResult::EOS_UnexpectedError;
        g_find_result = EOS_EResult::EOS_UnexpectedError;
        g_registered_count = 0;
        g_registered_player = 0;
    }
    ~sessions_fixture() { sessions.emu_deinit(); }

    EOS_ProductUserId me() {
        return id_registry::instance().get_product_user_id(settings.product_user_id());
    }

    // Host a session, returning its id.
    std::string host(const char* name, const char* bucket, u32 max_players) {
        EOS_Sessions_CreateSessionModificationOptions create = {};
        create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
        create.SessionName = name;
        create.BucketId = bucket;
        create.MaxPlayers = max_players;
        create.LocalUserId = me();
        EOS_HSessionModification handle = 0;
        REQUIRE(sessions.create_session_modification(&create, &handle) == EOS_EResult::EOS_Success);

        EOS_Sessions_UpdateSessionOptions update = {};
        update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
        update.SessionModificationHandle = handle;
        sessions.update_session(&update, 0, on_update);
        callbacks.tick();
        sessions.modification_release(handle);
        return g_update_id;
    }
};

EOS_Sessions_AttributeData string_attribute(const char* key, const char* value) {
    EOS_Sessions_AttributeData data = {};
    data.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    data.Key = key;
    data.ValueType = EOS_EAttributeType::EOS_AT_STRING;
    data.Value.AsUtf8 = value;
    return data;
}

EOS_Sessions_AttributeData int_attribute(const char* key, i64 value) {
    EOS_Sessions_AttributeData data = {};
    data.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    data.Key = key;
    data.ValueType = EOS_EAttributeType::EOS_AT_INT64;
    data.Value.AsInt64 = value;
    return data;
}

} // namespace

TEST_CASE("creating a session gives it an id and seats the host") {
    sessions_fixture fx;
    const std::string id = fx.host("my-game", "Region:Coop", 4);

    CHECK(g_update_fired);
    CHECK(g_update_result == EOS_EResult::EOS_Success);
    CHECK(id.size() == 32); // a session id nobody else will pick

    // The host is in its own session and registered, which is what lets it later admit others.
    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    REQUIRE(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    CHECK(fx.sessions.active_registered_count(active) == 1);

    EOS_ActiveSession_Info* info = 0;
    REQUIRE(fx.sessions.active_copy_info(active, &info) == EOS_EResult::EOS_Success);
    REQUIRE((info != 0));
    CHECK(std::string(info->SessionName) == "my-game");
    CHECK(info->State == EOS_EOnlineSessionState::EOS_OSS_Pending);
    REQUIRE((info->SessionDetails != 0));
    CHECK(std::string(info->SessionDetails->Settings->BucketId) == "Region:Coop");
    // Open connections are the capacity minus the players actually registered.
    CHECK(info->SessionDetails->NumOpenPublicConnections == 3);
    release_active_session_info(info);
    fx.sessions.active_release(active);
}

TEST_CASE("a session cannot be created twice under the same name") {
    sessions_fixture fx;
    fx.host("my-game", "b", 4);
    REQUIRE(g_update_result == EOS_EResult::EOS_Success);

    fx.host("my-game", "b", 4);
    CHECK(g_update_result == EOS_EResult::EOS_Sessions_SessionAlreadyExists);
}

TEST_CASE("a session runs through its states and refuses the illegal ones") {
    sessions_fixture fx;
    fx.host("my-game", "b", 4);

    EOS_Sessions_StartSessionOptions start = {};
    start.ApiVersion = EOS_SESSIONS_STARTSESSION_API_LATEST;
    start.SessionName = "my-game";
    fx.sessions.start_session(&start, 0, on_simple);
    fx.callbacks.tick();
    CHECK(g_simple_result == EOS_EResult::EOS_Success);

    // Starting one that is already running is not a state it can reach.
    g_simple_fired = false;
    fx.sessions.start_session(&start, 0, on_simple);
    fx.callbacks.tick();
    CHECK(g_simple_result == EOS_EResult::EOS_InvalidParameters);

    EOS_Sessions_EndSessionOptions end = {};
    end.ApiVersion = EOS_SESSIONS_ENDSESSION_API_LATEST;
    end.SessionName = "my-game";
    fx.sessions.end_session(&end, 0, reinterpret_cast<EOS_Sessions_OnEndSessionCallback>(on_simple));
    fx.callbacks.tick();
    CHECK(g_simple_result == EOS_EResult::EOS_Success);

    // And it can be started again once it has ended.
    fx.sessions.start_session(&start, 0, on_simple);
    fx.callbacks.tick();
    CHECK(g_simple_result == EOS_EResult::EOS_Success);

    // A session that does not exist cannot be started.
    start.SessionName = "no-such-game";
    fx.sessions.start_session(&start, 0, on_simple);
    fx.callbacks.tick();
    CHECK(g_simple_result == EOS_EResult::EOS_NotFound);
}

TEST_CASE("modifying a session keeps what it was not asked to change") {
    sessions_fixture fx;
    const std::string id = fx.host("my-game", "Region:Coop", 4);

    EOS_Sessions_UpdateSessionModificationOptions modify = {};
    modify.ApiVersion = EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST;
    modify.SessionName = "my-game";
    EOS_HSessionModification handle = 0;
    REQUIRE(fx.sessions.update_session_modification(&modify, &handle) == EOS_EResult::EOS_Success);

    // Change only the player cap.
    EOS_SessionModification_SetMaxPlayersOptions players = {};
    players.ApiVersion = EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST;
    players.MaxPlayers = 8;
    CHECK(fx.sessions.modification_set_max_players(handle, &players) == EOS_EResult::EOS_Success);

    EOS_Sessions_UpdateSessionOptions update = {};
    update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    update.SessionModificationHandle = handle;
    fx.sessions.update_session(&update, 0, on_update);
    fx.callbacks.tick();
    CHECK(g_update_result == EOS_EResult::EOS_Success);
    // The id survives a modification; a game cannot rewrite it by staging one.
    CHECK(g_update_id == id);
    fx.sessions.modification_release(handle);

    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    REQUIRE(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    EOS_ActiveSession_Info* info = 0;
    REQUIRE(fx.sessions.active_copy_info(active, &info) == EOS_EResult::EOS_Success);
    CHECK(info->SessionDetails->Settings->NumPublicConnections == 8);
    // The bucket was never touched, so it is still there.
    CHECK(std::string(info->SessionDetails->Settings->BucketId) == "Region:Coop");
    release_active_session_info(info);
    fx.sessions.active_release(active);
}

TEST_CASE("attributes round-trip through a session and can be read back by key") {
    sessions_fixture fx;
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.SessionName = "my-game";
    create.BucketId = "b";
    create.MaxPlayers = 4;
    create.LocalUserId = fx.me();
    EOS_HSessionModification handle = 0;
    REQUIRE(fx.sessions.create_session_modification(&create, &handle) == EOS_EResult::EOS_Success);

    EOS_Sessions_AttributeData map_name = string_attribute("map", "crab-island");
    EOS_SessionModification_AddAttributeOptions add = {};
    add.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
    add.SessionAttribute = &map_name;
    add.AdvertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
    CHECK(fx.sessions.modification_add_attribute(handle, &add) == EOS_EResult::EOS_Success);

    // A key with no value is not an attribute.
    EOS_Sessions_AttributeData broken = string_attribute("bad", 0);
    add.SessionAttribute = &broken;
    CHECK(fx.sessions.modification_add_attribute(handle, &add) == EOS_EResult::EOS_InvalidParameters);

    EOS_Sessions_UpdateSessionOptions update = {};
    update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    update.SessionModificationHandle = handle;
    fx.sessions.update_session(&update, 0, on_update);
    fx.callbacks.tick();
    fx.sessions.modification_release(handle);

    // Find our own session so we can read its details back.
    EOS_Sessions_CreateSessionSearchOptions search_options = {};
    search_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    search_options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(fx.sessions.create_session_search(&search_options, &search) == EOS_EResult::EOS_Success);

    EOS_Sessions_AttributeData wanted = string_attribute("map", "crab-island");
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &wanted;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    CHECK(fx.sessions.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = fx.me();
    fx.sessions.search_find(search, &find, 0, on_find);
    fx.callbacks.tick(); // no peers, so it settles at once
    CHECK(g_find_result == EOS_EResult::EOS_Success);
    REQUIRE(fx.sessions.search_result_count(search) == 1);

    EOS_SessionSearch_CopySearchResultByIndexOptions pick = {};
    pick.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    pick.SessionIndex = 0;
    EOS_HSessionDetails details = 0;
    REQUIRE(fx.sessions.search_copy_result(search, &pick, &details) == EOS_EResult::EOS_Success);

    CHECK(fx.sessions.details_attribute_count(details) == 1);
    EOS_SessionDetails_CopySessionAttributeByKeyOptions by_key = {};
    by_key.ApiVersion = EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYKEY_API_LATEST;
    by_key.AttrKey = "map";
    EOS_SessionDetails_Attribute* attribute = 0;
    REQUIRE(fx.sessions.details_copy_attribute_by_key(details, &by_key, &attribute) ==
            EOS_EResult::EOS_Success);
    REQUIRE((attribute != 0));
    CHECK(std::string(attribute->Data->Key) == "map");
    CHECK(std::string(attribute->Data->Value.AsUtf8) == "crab-island");
    CHECK(attribute->AdvertisementType ==
          EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise);
    release_session_details_attribute(attribute);

    by_key.AttrKey = "nope";
    CHECK(fx.sessions.details_copy_attribute_by_key(details, &by_key, &attribute) ==
          EOS_EResult::EOS_NotFound);

    fx.sessions.details_release(details);
    fx.sessions.search_release(search);
}

TEST_CASE("a search only returns the sessions that answer it") {
    sessions_fixture fx;
    // Two sessions, telling them apart by an attribute and by their bucket.
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.BucketId = "Coop";
    create.MaxPlayers = 4;
    create.LocalUserId = fx.me();

    const char* names[] = {"easy", "hard"};
    const i64 levels[] = {1, 9};
    for (int i = 0; i < 2; i++) {
        create.SessionName = names[i];
        EOS_HSessionModification handle = 0;
        REQUIRE(fx.sessions.create_session_modification(&create, &handle) == EOS_EResult::EOS_Success);
        EOS_Sessions_AttributeData level = int_attribute("difficulty", levels[i]);
        EOS_SessionModification_AddAttributeOptions add = {};
        add.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
        add.SessionAttribute = &level;
        add.AdvertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
        CHECK(fx.sessions.modification_add_attribute(handle, &add) == EOS_EResult::EOS_Success);
        EOS_Sessions_UpdateSessionOptions update = {};
        update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
        update.SessionModificationHandle = handle;
        fx.sessions.update_session(&update, 0, on_update);
        fx.callbacks.tick();
        fx.sessions.modification_release(handle);
    }

    EOS_Sessions_CreateSessionSearchOptions search_options = {};
    search_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    search_options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(fx.sessions.create_session_search(&search_options, &search) == EOS_EResult::EOS_Success);

    // Only the hard one is above difficulty 5.
    EOS_Sessions_AttributeData wanted = int_attribute("difficulty", 5);
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &wanted;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_GREATERTHAN;
    CHECK(fx.sessions.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = fx.me();
    fx.sessions.search_find(search, &find, 0, on_find);
    fx.callbacks.tick();
    CHECK(fx.sessions.search_result_count(search) == 1);
    fx.sessions.search_release(search);
}

TEST_CASE("a search must ask something, and cannot ask two things at once") {
    sessions_fixture fx;
    EOS_Sessions_CreateSessionSearchOptions search_options = {};
    search_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    search_options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(fx.sessions.create_session_search(&search_options, &search) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = fx.me();

    // Nothing set at all.
    fx.sessions.search_find(search, &find, 0, on_find);
    fx.callbacks.tick();
    CHECK(g_find_result == EOS_EResult::EOS_InvalidParameters);

    // A session id and a target user are two different questions.
    EOS_SessionSearch_SetSessionIdOptions by_id = {};
    by_id.ApiVersion = EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST;
    by_id.SessionId = "abc";
    CHECK(fx.sessions.search_set_session_id(search, &by_id) == EOS_EResult::EOS_Success);
    EOS_SessionSearch_SetTargetUserIdOptions by_user = {};
    by_user.ApiVersion = EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST;
    by_user.TargetUserId = fx.me();
    CHECK(fx.sessions.search_set_target_user(search, &by_user) == EOS_EResult::EOS_Success);

    fx.sessions.search_find(search, &find, 0, on_find);
    fx.callbacks.tick();
    CHECK(g_find_result == EOS_EResult::EOS_InvalidParameters);
    fx.sessions.search_release(search);
}

// The reference emulator inserted the key while removing it, which left the search demanding an
// attribute nothing carried, so it found nothing ever again.
TEST_CASE("removing a parameter that was never set does not poison the search") {
    sessions_fixture fx;
    fx.host("my-game", "Coop", 4);

    EOS_Sessions_CreateSessionSearchOptions search_options = {};
    search_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    search_options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(fx.sessions.create_session_search(&search_options, &search) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_RemoveParameterOptions remove = {};
    remove.ApiVersion = EOS_SESSIONSEARCH_REMOVEPARAMETER_API_LATEST;
    remove.Key = "never-set";
    remove.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    CHECK(fx.sessions.search_remove_parameter(search, &remove) == EOS_EResult::EOS_NotFound);

    // The search still works: asking for the bucket finds the session.
    EOS_Sessions_AttributeData bucket = string_attribute(EOS_SESSIONS_SEARCH_BUCKET_ID, "Coop");
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &bucket;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    CHECK(fx.sessions.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = fx.me();
    fx.sessions.search_find(search, &find, 0, on_find);
    fx.callbacks.tick();
    CHECK(fx.sessions.search_result_count(search) == 1);
    fx.sessions.search_release(search);
}

TEST_CASE("an invite-only session is not offered to a searcher") {
    sessions_fixture fx;
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.SessionName = "private";
    create.BucketId = "Coop";
    create.MaxPlayers = 4;
    create.LocalUserId = fx.me();
    EOS_HSessionModification handle = 0;
    REQUIRE(fx.sessions.create_session_modification(&create, &handle) == EOS_EResult::EOS_Success);

    EOS_SessionModification_SetPermissionLevelOptions permission = {};
    permission.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
    permission.PermissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly;
    CHECK(fx.sessions.modification_set_permission_level(handle, &permission) == EOS_EResult::EOS_Success);

    EOS_Sessions_UpdateSessionOptions update = {};
    update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    update.SessionModificationHandle = handle;
    fx.sessions.update_session(&update, 0, on_update);
    fx.callbacks.tick();
    fx.sessions.modification_release(handle);

    EOS_Sessions_CreateSessionSearchOptions search_options = {};
    search_options.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    search_options.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(fx.sessions.create_session_search(&search_options, &search) == EOS_EResult::EOS_Success);
    EOS_Sessions_AttributeData bucket = string_attribute(EOS_SESSIONS_SEARCH_BUCKET_ID, "Coop");
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &bucket;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    fx.sessions.search_set_parameter(search, &parameter);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = fx.me();
    fx.sessions.search_find(search, &find, 0, on_find);
    fx.callbacks.tick();
    CHECK(g_find_result == EOS_EResult::EOS_Success);
    CHECK(fx.sessions.search_result_count(search) == 0); // found, but not offered
    fx.sessions.search_release(search);
}

TEST_CASE("destroying a session takes it away") {
    sessions_fixture fx;
    fx.host("my-game", "b", 4);

    EOS_Sessions_DestroySessionOptions destroy = {};
    destroy.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
    destroy.SessionName = "my-game";
    fx.sessions.destroy_session(&destroy, 0,
                                reinterpret_cast<EOS_Sessions_OnDestroySessionCallback>(on_simple));
    fx.callbacks.tick();
    CHECK(g_simple_result == EOS_EResult::EOS_Success);

    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    CHECK(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("a released sub-handle stops working and can be released again safely") {
    sessions_fixture fx;
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.SessionName = "my-game";
    create.BucketId = "b";
    create.MaxPlayers = 4;
    create.LocalUserId = fx.me();
    EOS_HSessionModification handle = 0;
    REQUIRE(fx.sessions.create_session_modification(&create, &handle) == EOS_EResult::EOS_Success);

    EOS_SessionModification_SetMaxPlayersOptions players = {};
    players.ApiVersion = EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST;
    players.MaxPlayers = 8;
    CHECK(fx.sessions.modification_set_max_players(handle, &players) == EOS_EResult::EOS_Success);

    fx.sessions.modification_release(handle);

    // Using it afterwards is refused rather than followed into freed memory.
    CHECK(fx.sessions.modification_set_max_players(handle, &players) ==
          EOS_EResult::EOS_InvalidParameters);
    // And releasing it again, or releasing something we never handed out, does nothing.
    fx.sessions.modification_release(handle);
    fx.sessions.modification_release(reinterpret_cast<void*>(0xdeadbeef));
    fx.sessions.modification_release(0);
}

// A game copies this handle once and then polls it to draw its lobby list, so it has to keep
// telling the truth as people arrive and leave.
TEST_CASE("an active session handle is a live view, not a snapshot") {
    sessions_fixture fx;
    fx.host("my-game", "b", 4);

    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    REQUIRE(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    REQUIRE(fx.sessions.active_registered_count(active) == 1);

    // Someone joins, on the handle the game is already holding.
    EOS_ProductUserId newcomer = id_registry::instance().get_product_user_id(std::string(32, 'a'));
    EOS_Sessions_RegisterPlayersOptions add = {};
    add.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
    add.SessionName = "my-game";
    add.PlayersToRegister = &newcomer;
    add.PlayersToRegisterCount = 1;
    fx.sessions.register_players(&add, 0, reinterpret_cast<EOS_Sessions_OnRegisterPlayersCallback>(on_simple));
    fx.callbacks.tick();
    REQUIRE(g_simple_result == EOS_EResult::EOS_Success);

    CHECK(fx.sessions.active_registered_count(active) == 2);
    EOS_ActiveSession_Info* info = 0;
    REQUIRE(fx.sessions.active_copy_info(active, &info) == EOS_EResult::EOS_Success);
    CHECK(info->SessionDetails->NumOpenPublicConnections == 2); // a seat fewer, on the old handle
    release_active_session_info(info);

    // And they leave again, giving the seat back.
    EOS_Sessions_UnregisterPlayersOptions remove = {};
    remove.ApiVersion = EOS_SESSIONS_UNREGISTERPLAYERS_API_LATEST;
    remove.SessionName = "my-game";
    remove.PlayersToUnregister = &newcomer;
    remove.PlayersToUnregisterCount = 1;
    fx.sessions.unregister_players(&remove, 0,
                                   reinterpret_cast<EOS_Sessions_OnUnregisterPlayersCallback>(on_simple));
    fx.callbacks.tick();
    CHECK(fx.sessions.active_registered_count(active) == 1);

    // Once the session is gone the handle is no longer a usable one.
    EOS_Sessions_DestroySessionOptions destroy = {};
    destroy.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
    destroy.SessionName = "my-game";
    fx.sessions.destroy_session(&destroy, 0,
                                reinterpret_cast<EOS_Sessions_OnDestroySessionCallback>(on_simple));
    fx.callbacks.tick();
    CHECK(fx.sessions.active_registered_count(active) == 0);
    CHECK(fx.sessions.active_copy_info(active, &info) == EOS_EResult::EOS_InvalidParameters);
    fx.sessions.active_release(active);
}

// A peer that does not host a session cannot pronounce on who is in it. The reference emulator
// took any session_join_response, which let any peer erase a session or stuff its roster.
namespace {

net_envelope forged_join_response(const std::string& from, const std::string& session_id,
                                  const std::string& player_id, EOS_EResult reason) {
    session_join_response answer;
    answer.session_id = session_id;
    answer.player_id = player_id;
    answer.reason = static_cast<i32>(reason);
    byte_writer writer;
    serialize(writer, answer);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::session_join_response);
    envelope.source_id = from;
    envelope.game_id = "";
    envelope.payload = writer.data();
    return envelope;
}

} // namespace

TEST_CASE("a forged verdict from a non-host cannot erase a hosted session") {
    sessions_fixture fx;
    const std::string id = fx.host("my-game", "b", 4);
    const std::string foreign_peer(32, 'f');

    // The attacker claims our own join failed, hoping we drop the session we host.
    fx.sessions.on_network_message(
        forged_join_response(foreign_peer, id, fx.settings.product_user_id(),
                             EOS_EResult::EOS_Sessions_TooManyPlayers));

    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    CHECK(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    CHECK(fx.sessions.active_registered_count(active) == 1);
    fx.sessions.active_release(active);
}

TEST_CASE("a forged verdict from a non-host cannot stuff the roster with ghosts") {
    sessions_fixture fx;
    const std::string id = fx.host("my-game", "b", 4);
    const std::string foreign_peer(32, 'f');
    const std::string ghost(32, 'a');

    // The attacker says a player it invented has joined, hoping to fill the session.
    fx.sessions.on_network_message(
        forged_join_response(foreign_peer, id, ghost, EOS_EResult::EOS_Success));

    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    REQUIRE(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    CHECK(fx.sessions.active_registered_count(active) == 1); // still just the host; no ghost
    fx.sessions.active_release(active);
}

// Review regression: joined participants must not be able to remove the session owner.
TEST_CASE("a participant cannot remove the session owner from the roster") {
    sessions_fixture fx;
    const std::string id = fx.host("my-game", "b", 4);
    EOS_ProductUserId participant =
        id_registry::instance().get_product_user_id(std::string(32, 'a'));

    EOS_Sessions_RegisterPlayersOptions add = {};
    add.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
    add.SessionName = "my-game";
    add.PlayersToRegister = &participant;
    add.PlayersToRegisterCount = 1;
    fx.sessions.register_players(
        &add, 0, reinterpret_cast<EOS_Sessions_OnRegisterPlayersCallback>(on_simple));
    fx.callbacks.tick();
    REQUIRE(g_simple_result == EOS_EResult::EOS_Success);

    session_members update;
    update.session_id = id;
    update.player_ids.push_back(fx.settings.product_user_id());
    byte_writer writer;
    serialize(writer, update);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::session_unregister);
    envelope.source_id = participant->id_str;
    envelope.game_id = fx.settings.product_id();
    envelope.payload = writer.data();
    fx.sessions.on_network_message(envelope);

    EOS_Sessions_CopyActiveSessionHandleOptions copy = {};
    copy.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
    copy.SessionName = "my-game";
    EOS_HActiveSession active = 0;
    REQUIRE(fx.sessions.copy_active_session_handle(&copy, &active) == EOS_EResult::EOS_Success);
    CHECK(fx.sessions.active_registered_count(active) == 2);
    fx.sessions.active_release(active);
}

// Review regression: the completion ABI must identify the entries changed by the operation.
TEST_CASE("register players reports the successfully registered players") {
    sessions_fixture fx;
    fx.host("my-game", "b", 4);
    EOS_ProductUserId participant =
        id_registry::instance().get_product_user_id(std::string(32, 'a'));

    EOS_Sessions_RegisterPlayersOptions add = {};
    add.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
    add.SessionName = "my-game";
    add.PlayersToRegister = &participant;
    add.PlayersToRegisterCount = 1;
    fx.sessions.register_players(&add, 0, on_registered);
    fx.callbacks.tick();

    REQUIRE(g_simple_result == EOS_EResult::EOS_Success);
    CHECK(g_registered_count == 1);
    CHECK(g_registered_player == participant);
}

// Review regression: only requested peers may contribute results, and results must match the query.
TEST_CASE("a search ignores an unrequested non-matching response") {
    sessions_fixture fx;
    EOS_Sessions_CreateSessionSearchOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    create.MaxSearchResults = 10;
    EOS_HSessionSearch search = 0;
    REQUIRE(fx.sessions.create_session_search(&create, &search) == EOS_EResult::EOS_Success);

    EOS_Sessions_AttributeData bucket = string_attribute("bucket", "wanted");
    EOS_SessionSearch_SetParameterOptions parameter = {};
    parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &bucket;
    parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    REQUIRE(fx.sessions.search_set_parameter(search, &parameter) == EOS_EResult::EOS_Success);

    EOS_SessionSearch_FindOptions find = {};
    find.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    find.LocalUserId = fx.me();
    fx.sessions.search_find(search, &find, 0, on_find);

    session_infos non_matching;
    non_matching.session_id = std::string(32, '9');
    non_matching.owner_id = std::string(32, 'a');
    non_matching.bucket_id = "different";
    non_matching.max_players = 4;
    non_matching.state = static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_Pending);
    session_search_response response;
    response.search_id = "1";
    response.sessions.push_back(non_matching);
    byte_writer writer;
    serialize(writer, response);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::session_search_response);
    envelope.source_id = std::string(32, 'a');
    envelope.game_id = fx.settings.product_id();
    envelope.payload = writer.data();
    fx.sessions.on_network_message(envelope);
    fx.callbacks.tick();

    CHECK(g_find_fired);
    CHECK(fx.sessions.search_result_count(search) == 0);
    fx.sessions.search_release(search);
}

// An UpdateSession callback carries strings it allocated. Tearing the platform down before the
// callback is delivered must still free them; clear() runs free_callback for exactly this reason.
TEST_CASE("a queued update callback is not leaked when the callbacks are cleared") {
    sessions_fixture fx;
    EOS_Sessions_CreateSessionModificationOptions create = {};
    create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    create.SessionName = "my-game";
    create.BucketId = "b";
    create.MaxPlayers = 4;
    create.LocalUserId = fx.me();
    EOS_HSessionModification handle = 0;
    REQUIRE(fx.sessions.create_session_modification(&create, &handle) == EOS_EResult::EOS_Success);

    EOS_Sessions_UpdateSessionOptions update = {};
    update.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    update.SessionModificationHandle = handle;
    fx.sessions.update_session(&update, 0, on_update); // queued, deliberately never ticked
    fx.sessions.modification_release(handle);

    // Drop the queue the way platform release does. Under ASan this proves the strings were freed.
    fx.callbacks.clear();
    CHECK(true);
}
