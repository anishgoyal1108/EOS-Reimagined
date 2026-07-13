#include "doctest.h"

#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_presence_types.h"

#include "common/byte_buffer.h"
#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/presence.h"
#include "net/message_router.h"
#include "net/messages.h"
#include "net/wire.h"

using namespace eosr;

namespace {

EOS_EResult g_set_result;
bool g_set_fired;
void EOS_CALL on_set(const EOS_Presence_SetPresenceCallbackInfo* info) {
    g_set_fired = true;
    g_set_result = info->ResultCode;
}

EOS_EResult g_query_result;
bool g_query_fired;
void EOS_CALL on_query(const EOS_Presence_QueryPresenceCallbackInfo* info) {
    g_query_fired = true;
    g_query_result = info->ResultCode;
}

struct presence_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    message_router network;
    sdk_presence presence;

    presence_fixture() : presence(settings, callbacks, network) {
        presence.emu_init();
        g_set_fired = false;
        g_query_fired = false;
        g_set_result = EOS_EResult::EOS_UnexpectedError;
        g_query_result = EOS_EResult::EOS_UnexpectedError;
    }
    ~presence_fixture() { presence.emu_deinit(); }

    EOS_EpicAccountId me() {
        return id_registry::instance().get_epic_account_id(settings.epic_account_id());
    }

    // A peer joins the mesh carrying the epic account id its key derives -- the only account it may
    // ever speak for. In production the router computes that from the key the handshake proved; here
    // we say outright what that key would have derived.
    void meets(const std::string& peer_id, const std::string& epic_id) {
        net_envelope event;
        event.type_tag = static_cast<u16>(message_type::peer_connected);
        event.source_id = peer_id;
        event.game_id = settings.product_id();
        event.payload.assign(epic_id.begin(), epic_id.end());
        presence.on_network_message(event);
    }

    // Stage a modification and apply it to our own presence, returning the result code.
    EOS_HPresenceModification begin() {
        EOS_Presence_CreatePresenceModificationOptions options = {};
        options.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
        options.LocalUserId = me();
        EOS_HPresenceModification handle = 0;
        REQUIRE(presence.create_presence_modification(&options, &handle) == EOS_EResult::EOS_Success);
        return handle;
    }

    void apply(EOS_HPresenceModification handle) {
        EOS_Presence_SetPresenceOptions options = {};
        options.ApiVersion = EOS_PRESENCE_SETPRESENCE_API_LATEST;
        options.LocalUserId = me();
        options.PresenceModificationHandle = handle;
        presence.set_presence(&options, 0, on_set);
        callbacks.tick();
        presence.modification_release(handle);
    }
};

EOS_Presence_DataRecord record(const char* key, const char* value) {
    EOS_Presence_DataRecord data = {};
    data.ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
    data.Key = key;
    data.Value = value;
    return data;
}

} // namespace

TEST_CASE("a fresh instance already has its own presence") {
    presence_fixture fx;
    EOS_Presence_HasPresenceOptions options = {};
    options.ApiVersion = EOS_PRESENCE_HASPRESENCE_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.me();
    CHECK(fx.presence.has_presence(&options) == EOS_TRUE);

    // Someone we have never heard of, we have no presence for.
    options.TargetUserId = id_registry::instance().get_epic_account_id(std::string(32, 'c'));
    CHECK(fx.presence.has_presence(&options) == EOS_FALSE);
}

TEST_CASE("a modification changes what it is asked to and leaves the rest") {
    presence_fixture fx;
    EOS_HPresenceModification handle = fx.begin();

    EOS_PresenceModification_SetStatusOptions status = {};
    status.ApiVersion = EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST;
    status.Status = EOS_Presence_EStatus::EOS_PS_Away;
    CHECK(fx.presence.modification_set_status(handle, &status) == EOS_EResult::EOS_Success);

    EOS_PresenceModification_SetRawRichTextOptions rich = {};
    rich.ApiVersion = EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;
    rich.RichText = "In the caves";
    CHECK(fx.presence.modification_set_raw_rich_text(handle, &rich) == EOS_EResult::EOS_Success);

    EOS_Presence_DataRecord hp = record("hp", "42");
    EOS_PresenceModification_SetDataOptions data = {};
    data.ApiVersion = EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST;
    data.RecordsCount = 1;
    data.Records = &hp;
    CHECK(fx.presence.modification_set_data(handle, &data) == EOS_EResult::EOS_Success);

    fx.apply(handle);
    CHECK(g_set_result == EOS_EResult::EOS_Success);

    EOS_Presence_CopyPresenceOptions copy = {};
    copy.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
    copy.LocalUserId = fx.me();
    copy.TargetUserId = fx.me();
    EOS_Presence_Info* info = 0;
    REQUIRE(fx.presence.copy_presence(&copy, &info) == EOS_EResult::EOS_Success);
    REQUIRE((info != 0));
    CHECK(info->ApiVersion == EOS_PRESENCE_INFO_API_LATEST);
    CHECK(info->Status == EOS_Presence_EStatus::EOS_PS_Away);
    CHECK(std::string(info->RichText) == "In the caves");
    CHECK(info->UserId == fx.me());
    // The product it was seeded with is still there; we only changed status and rich text.
    CHECK(std::string(info->ProductId) == fx.settings.product_id());
    REQUIRE(info->RecordsCount == 1);
    CHECK(std::string(info->Records[0].Key) == "hp");
    CHECK(std::string(info->Records[0].Value) == "42");
    release_presence_info(info);
}

TEST_CASE("presence data records upsert and delete") {
    presence_fixture fx;

    EOS_HPresenceModification first = fx.begin();
    EOS_Presence_DataRecord initial[] = {record("hp", "42"), record("zone", "caves")};
    EOS_PresenceModification_SetDataOptions data = {};
    data.ApiVersion = EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST;
    data.RecordsCount = 2;
    data.Records = initial;
    CHECK(fx.presence.modification_set_data(first, &data) == EOS_EResult::EOS_Success);
    fx.apply(first);

    // A second modification overwrites one key, adds another, and deletes the third.
    EOS_HPresenceModification second = fx.begin();
    EOS_Presence_DataRecord changed[] = {record("hp", "7"), record("boss", "crab")};
    data.RecordsCount = 2;
    data.Records = changed;
    CHECK(fx.presence.modification_set_data(second, &data) == EOS_EResult::EOS_Success);

    EOS_PresenceModification_DataRecordId drop = {};
    drop.ApiVersion = EOS_PRESENCEMODIFICATION_DATARECORDID_API_LATEST;
    drop.Key = "zone";
    EOS_PresenceModification_DeleteDataOptions remove = {};
    remove.ApiVersion = EOS_PRESENCEMODIFICATION_DELETEDATA_API_LATEST;
    remove.RecordsCount = 1;
    remove.Records = &drop;
    CHECK(fx.presence.modification_delete_data(second, &remove) == EOS_EResult::EOS_Success);
    fx.apply(second);

    EOS_Presence_CopyPresenceOptions copy = {};
    copy.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
    copy.LocalUserId = fx.me();
    copy.TargetUserId = fx.me();
    EOS_Presence_Info* info = 0;
    REQUIRE(fx.presence.copy_presence(&copy, &info) == EOS_EResult::EOS_Success);
    REQUIRE(info->RecordsCount == 2); // hp (overwritten) and boss; zone is gone
    std::string hp_value, boss_value;
    bool saw_zone = false;
    for (i32 i = 0; i < info->RecordsCount; i++) {
        const std::string key = info->Records[i].Key;
        if (key == "hp") { hp_value = info->Records[i].Value; }
        if (key == "boss") { boss_value = info->Records[i].Value; }
        if (key == "zone") { saw_zone = true; }
    }
    CHECK(hp_value == "7");
    CHECK(boss_value == "crab");
    CHECK_FALSE(saw_zone);
    release_presence_info(info);
}

TEST_CASE("join info comes back through GetJoinInfo, honouring the buffer") {
    presence_fixture fx;

    // With nothing set, there is no join info to get.
    EOS_Presence_GetJoinInfoOptions get = {};
    get.ApiVersion = EOS_PRESENCE_GETJOININFO_API_LATEST;
    get.LocalUserId = fx.me();
    get.TargetUserId = fx.me();
    char buffer[128];
    i32 length = sizeof(buffer);
    CHECK(fx.presence.get_join_info(&get, buffer, &length) == EOS_EResult::EOS_NotFound);

    EOS_HPresenceModification handle = fx.begin();
    EOS_PresenceModification_SetJoinInfoOptions join = {};
    join.ApiVersion = EOS_PRESENCEMODIFICATION_SETJOININFO_API_LATEST;
    join.JoinInfo = "session:crab-island";
    CHECK(fx.presence.modification_set_join_info(handle, &join) == EOS_EResult::EOS_Success);
    fx.apply(handle);

    length = sizeof(buffer);
    REQUIRE(fx.presence.get_join_info(&get, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "session:crab-island");
    CHECK(length == static_cast<i32>(std::string("session:crab-island").size()) + 1);

    // A buffer that cannot hold the string is refused, with the needed size reported back.
    char small[4];
    i32 small_length = sizeof(small);
    CHECK(fx.presence.get_join_info(&get, small, &small_length) == EOS_EResult::EOS_LimitExceeded);
    CHECK(small_length == static_cast<i32>(std::string("session:crab-island").size()) + 1);
}

TEST_CASE("the modification setters refuse what the header forbids") {
    presence_fixture fx;
    EOS_HPresenceModification handle = fx.begin();

    // Rich text longer than the cap.
    EOS_PresenceModification_SetRawRichTextOptions rich = {};
    rich.ApiVersion = EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;
    const std::string too_long(EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH + 1, 'x');
    rich.RichText = too_long.c_str();
    CHECK(fx.presence.modification_set_raw_rich_text(handle, &rich) == EOS_EResult::EOS_LimitExceeded);

    // A data value longer than the cap.
    const std::string long_value(EOS_PRESENCE_DATA_MAX_VALUE_LENGTH + 1, 'y');
    EOS_Presence_DataRecord big = record("k", long_value.c_str());
    EOS_PresenceModification_SetDataOptions data = {};
    data.ApiVersion = EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST;
    data.RecordsCount = 1;
    data.Records = &big;
    CHECK(fx.presence.modification_set_data(handle, &data) == EOS_EResult::EOS_LimitExceeded);

    // A record with a null value is not a record.
    EOS_Presence_DataRecord broken = record("k", 0);
    data.Records = &broken;
    CHECK(fx.presence.modification_set_data(handle, &data) == EOS_EResult::EOS_InvalidParameters);

    // A wrong API version.
    rich.ApiVersion = 999;
    rich.RichText = "ok";
    CHECK(fx.presence.modification_set_raw_rich_text(handle, &rich) == EOS_EResult::EOS_InvalidParameters);

    fx.presence.modification_release(handle);
}

TEST_CASE("only the local user may stage or set their own presence") {
    presence_fixture fx;
    EOS_Presence_CreatePresenceModificationOptions options = {};
    options.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
    options.LocalUserId = id_registry::instance().get_epic_account_id(std::string(32, 'd'));
    EOS_HPresenceModification handle = 0;
    CHECK(fx.presence.create_presence_modification(&options, &handle) == EOS_EResult::EOS_InvalidUser);
    CHECK(handle == 0);
}

TEST_CASE("querying our own presence settles at once") {
    presence_fixture fx;
    EOS_Presence_QueryPresenceOptions options = {};
    options.ApiVersion = EOS_PRESENCE_QUERYPRESENCE_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.me();
    fx.presence.query_presence(&options, 0, on_query);
    fx.callbacks.tick();
    CHECK(g_query_fired);
    CHECK(g_query_result == EOS_EResult::EOS_Success);
}

TEST_CASE("a released modification stops working and can be released again safely") {
    presence_fixture fx;
    EOS_HPresenceModification handle = fx.begin();

    EOS_PresenceModification_SetStatusOptions status = {};
    status.ApiVersion = EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST;
    status.Status = EOS_Presence_EStatus::EOS_PS_Online;
    CHECK(fx.presence.modification_set_status(handle, &status) == EOS_EResult::EOS_Success);

    fx.presence.modification_release(handle);

    // Using it after release is refused rather than followed into freed memory.
    CHECK(fx.presence.modification_set_status(handle, &status) == EOS_EResult::EOS_InvalidParameters);
    // Releasing it again, or something we never issued, does nothing.
    fx.presence.modification_release(handle);
    fx.presence.modification_release(reinterpret_cast<void*>(0xdeadbeef));
    fx.presence.modification_release(0);
}

// --- Inbound presence over the wire, hostile and benign ---
namespace {

i32 g_change_count = 0;
void EOS_CALL count_presence_changed(const EOS_Presence_PresenceChangedCallbackInfo*) {
    g_change_count++;
}

net_envelope presence_envelope(sdk_settings& settings, const std::string& source,
                               const presence_info& info) {
    byte_writer writer;
    serialize(writer, info);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::presence_info);
    envelope.source_id = source;
    envelope.game_id = settings.product_id();
    envelope.payload = writer.data();
    return envelope;
}

presence_info remote_presence(const std::string& epic_id, i32 status) {
    presence_info info;
    info.epic_id = epic_id;
    info.status = status;
    info.product_id = "co-op-game";
    return info;
}

} // namespace

// A peer speaks only for the account its own key derives, so it cannot overwrite a friend's presence
// -- or their join string, which is what a "join friend's game" click follows.
//
// This used to be first-writer-wins: whoever announced an account first kept it, which was the most
// that could be done while identity on the mesh was self-asserted. It is not any more, so an attacker
// cannot take the account by racing for it either. We check the harder order: the attacker goes
// *first*, and still does not get it.
TEST_CASE("a peer cannot speak for an account its key does not derive") {
    presence_fixture fx;
    const std::string victim(32, 'c');
    const std::string real_owner(32, '1');
    const std::string attacker(32, '2');

    // Each peer arrives owning exactly one account: the one its key derives.
    fx.meets(real_owner, victim);
    fx.meets(attacker, std::string(32, 'a'));

    // The attacker announces the victim's account first, and is refused -- there is no race to win.
    presence_info forged = remote_presence(victim, 1);
    forged.join_info = "session:attacker";
    fx.presence.on_network_message(presence_envelope(fx.settings, attacker, forged));

    EOS_Presence_HasPresenceOptions has = {};
    has.ApiVersion = EOS_PRESENCE_HASPRESENCE_API_LATEST;
    has.LocalUserId = fx.me();
    has.TargetUserId = id_registry::instance().get_epic_account_id(victim);
    CHECK(fx.presence.has_presence(&has) == EOS_FALSE);

    // The peer whose key actually derives that account is believed.
    presence_info real = remote_presence(victim, 1);
    real.join_info = "session:real";
    fx.presence.on_network_message(presence_envelope(fx.settings, real_owner, real));

    // And the attacker still cannot overwrite it afterwards.
    fx.presence.on_network_message(presence_envelope(fx.settings, attacker, forged));

    EOS_Presence_GetJoinInfoOptions get = {};
    get.ApiVersion = EOS_PRESENCE_GETJOININFO_API_LATEST;
    get.LocalUserId = fx.me();
    get.TargetUserId = id_registry::instance().get_epic_account_id(victim);
    char buffer[128];
    i32 length = sizeof(buffer);
    REQUIRE(fx.presence.get_join_info(&get, buffer, &length) == EOS_EResult::EOS_Success);
    CHECK(std::string(buffer) == "session:real");
}

TEST_CASE("an over-cap or out-of-range inbound presence is refused, not cached") {
    presence_fixture fx;
    const std::string friend_id(32, 'e');
    fx.meets(std::string(32, '1'), friend_id);

    EOS_Presence_HasPresenceOptions has = {};
    has.ApiVersion = EOS_PRESENCE_HASPRESENCE_API_LATEST;
    has.LocalUserId = fx.me();
    has.TargetUserId = id_registry::instance().get_epic_account_id(friend_id);

    presence_info too_long = remote_presence(friend_id, 1);
    too_long.rich_text = std::string(EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH + 1, 'x');
    fx.presence.on_network_message(presence_envelope(fx.settings, std::string(32, '1'), too_long));
    CHECK(fx.presence.has_presence(&has) == EOS_FALSE);

    presence_info bad_status = remote_presence(friend_id, 99); // not a real EOS_Presence_EStatus
    fx.presence.on_network_message(presence_envelope(fx.settings, std::string(32, '1'), bad_status));
    CHECK(fx.presence.has_presence(&has) == EOS_FALSE);
}

// A peer that sends rubbish must not poison the account it does own: the next thing it sends, if it
// is well formed, still lands. It is the same peer both times, because an account has exactly one
// peer whose key derives it.
TEST_CASE("a peer's invalid presence does not block its own later valid one") {
    presence_fixture fx;
    const std::string friend_id(32, 'e');

    fx.meets(std::string(32, '1'), friend_id);

    presence_info invalid = remote_presence(friend_id, 1);
    invalid.rich_text = std::string(EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH + 1, 'x');
    fx.presence.on_network_message(
        presence_envelope(fx.settings, std::string(32, '1'), invalid));

    presence_info valid = remote_presence(friend_id, 1);
    valid.rich_text = "available";
    fx.presence.on_network_message(
        presence_envelope(fx.settings, std::string(32, '1'), valid));

    EOS_Presence_HasPresenceOptions has = {};
    has.ApiVersion = EOS_PRESENCE_HASPRESENCE_API_LATEST;
    has.LocalUserId = fx.me();
    has.TargetUserId = id_registry::instance().get_epic_account_id(friend_id);
    CHECK(fx.presence.has_presence(&has) == EOS_TRUE);
}

TEST_CASE("an identical presence re-broadcast does not fire the change notification") {
    presence_fixture fx;
    g_change_count = 0;
    fx.presence.add_notify_on_presence_changed(0, count_presence_changed);
    const std::string friend_id(32, 'e');
    const std::string owner(32, '1');
    fx.meets(owner, friend_id);

    presence_info info = remote_presence(friend_id, 1);
    info.rich_text = "hi";
    fx.presence.on_network_message(presence_envelope(fx.settings, owner, info));
    CHECK(g_change_count == 1);

    // The same presence again -- a peer answering a request, or re-announcing on connect.
    fx.presence.on_network_message(presence_envelope(fx.settings, owner, info));
    CHECK(g_change_count == 1);

    // A real change fires again.
    info.rich_text = "bye";
    fx.presence.on_network_message(presence_envelope(fx.settings, owner, info));
    CHECK(g_change_count == 2);
}

TEST_CASE("a query for an account we already know resolves from cache, not a timeout") {
    presence_fixture fx;
    const std::string friend_id(32, 'e');
    fx.meets(std::string(32, '1'), friend_id);
    fx.presence.on_network_message(
        presence_envelope(fx.settings, std::string(32, '1'), remote_presence(friend_id, 1)));

    // No peer exists in this fixture, so a network round-trip could only ever time out to NotFound.
    EOS_Presence_QueryPresenceOptions query = {};
    query.ApiVersion = EOS_PRESENCE_QUERYPRESENCE_API_LATEST;
    query.LocalUserId = fx.me();
    query.TargetUserId = id_registry::instance().get_epic_account_id(friend_id);
    fx.presence.query_presence(&query, 0, on_query);
    fx.callbacks.tick();
    CHECK(g_query_fired);
    CHECK(g_query_result == EOS_EResult::EOS_Success);
}

TEST_CASE("presence data records cannot exceed the ABI cap across modifications") {
    presence_fixture fx;

    std::vector<std::string> keys;
    for (int i = 0; i < EOS_PRESENCE_DATA_MAX_KEYS; i++) {
        keys.push_back(std::string("k") + std::to_string(i));
    }
    std::vector<EOS_Presence_DataRecord> full;
    for (int i = 0; i < EOS_PRESENCE_DATA_MAX_KEYS; i++) {
        full.push_back(record(keys[i].c_str(), "v"));
    }
    EOS_HPresenceModification first = fx.begin();
    EOS_PresenceModification_SetDataOptions data = {};
    data.ApiVersion = EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST;
    data.RecordsCount = EOS_PRESENCE_DATA_MAX_KEYS;
    data.Records = full.data();
    CHECK(fx.presence.modification_set_data(first, &data) == EOS_EResult::EOS_Success);
    fx.apply(first);
    REQUIRE(g_set_result == EOS_EResult::EOS_Success);

    // One more distinct key would push the whole presence past the cap.
    EOS_HPresenceModification second = fx.begin();
    EOS_Presence_DataRecord extra = record("one-too-many", "v");
    data.RecordsCount = 1;
    data.Records = &extra;
    CHECK(fx.presence.modification_set_data(second, &data) == EOS_EResult::EOS_Success);
    fx.apply(second);
    CHECK(g_set_result == EOS_EResult::EOS_LimitExceeded);

    // The overflow changed nothing: the presence still holds exactly the cap, and the extra key
    // never made it in -- so what we broadcast stays decodable by every peer.
    EOS_Presence_CopyPresenceOptions copy = {};
    copy.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
    copy.LocalUserId = fx.me();
    copy.TargetUserId = fx.me();
    EOS_Presence_Info* info = 0;
    REQUIRE(fx.presence.copy_presence(&copy, &info) == EOS_EResult::EOS_Success);
    CHECK(info->RecordsCount == EOS_PRESENCE_DATA_MAX_KEYS);
    bool saw_extra = false;
    for (i32 i = 0; i < info->RecordsCount; i++) {
        if (std::string(info->Records[i].Key) == "one-too-many") {
            saw_extra = true;
        }
    }
    CHECK_FALSE(saw_extra);
    release_presence_info(info);
}
