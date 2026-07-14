#include "doctest.h"

#include <string>

#include "eos_common.h"
#include "eos_userinfo_types.h"

#include "common/byte_buffer.h"
#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/connect.h"
#include "interfaces/userinfo.h"
#include "net/message_router.h"
#include "net/messages.h"
#include "net/wire.h"

using namespace eosr;

namespace {

int g_query_count;
EOS_EResult g_query_result;
std::string g_query_target;
void EOS_CALL on_query(const EOS_UserInfo_QueryUserInfoCallbackInfo* info) {
    g_query_count++;
    g_query_result = info->ResultCode;
    g_query_target = (info->TargetUserId != 0) ? info->TargetUserId->id_str : std::string();
}

int g_by_name_count;
EOS_EResult g_by_name_result;
std::string g_by_name_target;
std::string g_by_name_echo;
void EOS_CALL on_query_by_name(const EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo* info) {
    g_by_name_count++;
    g_by_name_result = info->ResultCode;
    g_by_name_target = (info->TargetUserId != 0) ? info->TargetUserId->id_str : std::string();
    g_by_name_echo = (info->DisplayName != 0) ? info->DisplayName : std::string();
}

EOS_EResult g_by_external_result;
void EOS_CALL on_query_by_external(
    const EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo* info) {
    g_by_external_result = info->ResultCode;
}

struct userinfo_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    message_router network;
    sdk_connect connect;
    sdk_userinfo userinfo;

    userinfo_fixture()
        : connect(settings, callbacks, network), userinfo(settings, callbacks, network, connect) {
        connect.emu_init();
        userinfo.emu_init();
        g_query_count = 0;
        g_query_result = EOS_EResult::EOS_UnexpectedError;
        g_query_target.clear();
        g_by_name_count = 0;
        g_by_name_result = EOS_EResult::EOS_UnexpectedError;
        g_by_name_target.clear();
        g_by_name_echo.clear();
        g_by_external_result = EOS_EResult::EOS_UnexpectedError;
    }
    ~userinfo_fixture() {
        userinfo.emu_deinit();
        connect.emu_deinit();
    }

    EOS_EpicAccountId me() {
        return id_registry::instance().get_epic_account_id(settings.epic_account_id());
    }
    EOS_EpicAccountId id(const std::string& s) {
        return id_registry::instance().get_epic_account_id(s);
    }

    // A peer joins carrying its key-derived epic id, and announces its display name over the roster
    // the way Connect learns it.
    void meets(const std::string& peer_puid, const std::string& epic_id, const std::string& name) {
        net_envelope join;
        join.type_tag = static_cast<u16>(message_type::peer_connected);
        join.source_id = peer_puid;
        join.game_id = settings.product_id();
        join.payload.assign(epic_id.begin(), epic_id.end());
        userinfo.on_network_message(join);

        connect_infos infos;
        infos.product_user_id = peer_puid;
        infos.display_name = name;
        byte_writer writer;
        serialize(writer, infos);
        net_envelope response;
        response.type_tag = static_cast<u16>(message_type::connect_response);
        response.source_id = peer_puid;
        response.game_id = settings.product_id();
        response.payload = writer.data();
        connect.on_network_message(response);
    }
    void leaves(const std::string& peer_puid) {
        net_envelope event;
        event.type_tag = static_cast<u16>(message_type::peer_disconnected);
        event.source_id = peer_puid;
        event.game_id = settings.product_id();
        userinfo.on_network_message(event);
    }

    EOS_EResult query(EOS_EpicAccountId target) {
        EOS_UserInfo_QueryUserInfoOptions options = {};
        options.ApiVersion = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
        options.LocalUserId = me();
        options.TargetUserId = target;
        userinfo.query_user_info(&options, 0, on_query);
        callbacks.tick();
        return g_query_result;
    }
};

const char* peer_puid = "11111111111111111111111111111111";
const char* peer_epic = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

} // namespace

TEST_CASE("the local user's own info is seeded from settings") {
    userinfo_fixture fx;
    fx.settings.set_username("Marlowe");
    fx.settings.set_override_country("US");
    fx.settings.set_override_locale("en");

    CHECK(fx.query(fx.me()) == EOS_EResult::EOS_Success);

    EOS_UserInfo_CopyUserInfoOptions options = {};
    options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.me();
    EOS_UserInfo* out = 0;
    REQUIRE(fx.userinfo.copy_user_info(&options, &out) == EOS_EResult::EOS_Success);
    REQUIRE((out != 0));
    CHECK(std::string(out->DisplayName) == "Marlowe");
    CHECK(std::string(out->Country) == "US");
    CHECK(std::string(out->PreferredLanguage) == "en");
    CHECK(out->UserId == fx.me());
    release_user_info(out);
}

TEST_CASE("a peer's display name is resolved from the authenticated roster") {
    userinfo_fixture fx;
    fx.meets(peer_puid, peer_epic, "Kasparov");

    CHECK(fx.query(fx.id(peer_epic)) == EOS_EResult::EOS_Success);

    EOS_UserInfo_CopyUserInfoOptions options = {};
    options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.id(peer_epic);
    EOS_UserInfo* out = 0;
    REQUIRE(fx.userinfo.copy_user_info(&options, &out) == EOS_EResult::EOS_Success);
    REQUIRE((out != 0));
    CHECK(std::string(out->DisplayName) == "Kasparov");
    release_user_info(out);
}

TEST_CASE("an account we have never met is not found") {
    userinfo_fixture fx;
    CHECK(fx.query(fx.id(peer_epic)) == EOS_EResult::EOS_NotFound);

    EOS_UserInfo_CopyUserInfoOptions options = {};
    options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.id(peer_epic);
    EOS_UserInfo* out = reinterpret_cast<EOS_UserInfo*>(1);
    CHECK(fx.userinfo.copy_user_info(&options, &out) == EOS_EResult::EOS_NotFound);
    CHECK((out == 0)); // the header promises the out pointer is cleared on failure
}

TEST_CASE("a departed peer's info is no longer resolvable") {
    userinfo_fixture fx;
    fx.meets(peer_puid, peer_epic, "Kasparov");
    CHECK(fx.query(fx.id(peer_epic)) == EOS_EResult::EOS_Success);
    fx.leaves(peer_puid);
    CHECK(fx.query(fx.id(peer_epic)) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("a display name resolves back to its epic account id") {
    userinfo_fixture fx;
    fx.meets(peer_puid, peer_epic, "Kasparov");

    EOS_UserInfo_QueryUserInfoByDisplayNameOptions options = {};
    options.ApiVersion = EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST;
    options.LocalUserId = fx.me();
    options.DisplayName = "Kasparov";
    fx.userinfo.query_user_info_by_display_name(&options, 0, on_query_by_name);
    fx.callbacks.tick();
    CHECK(g_by_name_count == 1);
    CHECK(g_by_name_result == EOS_EResult::EOS_Success);
    CHECK(g_by_name_target == peer_epic);
    CHECK(g_by_name_echo == "Kasparov");

    options.DisplayName = "Nobody";
    fx.userinfo.query_user_info_by_display_name(&options, 0, on_query_by_name);
    fx.callbacks.tick();
    CHECK(g_by_name_result == EOS_EResult::EOS_NotFound);
}

TEST_CASE("there are no external accounts to query or copy") {
    userinfo_fixture fx;
    EOS_UserInfo_QueryUserInfoByExternalAccountOptions query = {};
    query.ApiVersion = EOS_USERINFO_QUERYUSERINFOBYEXTERNALACCOUNT_API_LATEST;
    query.LocalUserId = fx.me();
    query.ExternalAccountId = "steam:76561198000000000";
    query.AccountType = EOS_EExternalAccountType::EOS_EAT_STEAM;
    fx.userinfo.query_user_info_by_external_account(&query, 0, on_query_by_external);
    fx.callbacks.tick();
    CHECK(g_by_external_result == EOS_EResult::EOS_NotFound);

    EOS_UserInfo_GetExternalUserInfoCountOptions count = {};
    count.ApiVersion = EOS_USERINFO_GETEXTERNALUSERINFOCOUNT_API_LATEST;
    count.LocalUserId = fx.me();
    count.TargetUserId = fx.me();
    CHECK(fx.userinfo.get_external_user_info_count(&count) == 0);

    EOS_UserInfo_CopyExternalUserInfoByIndexOptions by_index = {};
    by_index.ApiVersion = EOS_USERINFO_COPYEXTERNALUSERINFOBYINDEX_API_LATEST;
    by_index.LocalUserId = fx.me();
    by_index.TargetUserId = fx.me();
    by_index.Index = 0;
    EOS_UserInfo_ExternalUserInfo* ext = reinterpret_cast<EOS_UserInfo_ExternalUserInfo*>(1);
    CHECK(fx.userinfo.copy_external_user_info_by_index(&by_index, &ext) == EOS_EResult::EOS_NotFound);
    CHECK((ext == 0));
}

TEST_CASE("the best display name is the epic one, on the epic platform") {
    userinfo_fixture fx;
    fx.meets(peer_puid, peer_epic, "Kasparov");

    EOS_UserInfo_CopyBestDisplayNameOptions options = {};
    options.ApiVersion = EOS_USERINFO_COPYBESTDISPLAYNAME_API_LATEST;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.id(peer_epic);
    EOS_UserInfo_BestDisplayName* out = 0;
    REQUIRE(fx.userinfo.copy_best_display_name(&options, &out) == EOS_EResult::EOS_Success);
    REQUIRE((out != 0));
    CHECK(std::string(out->DisplayName) == "Kasparov");
    CHECK(out->PlatformType == EOS_OPT_Epic);
    release_best_display_name(out);
}

TEST_CASE("the local platform type is epic") {
    userinfo_fixture fx;
    EOS_UserInfo_GetLocalPlatformTypeOptions options = {};
    options.ApiVersion = EOS_USERINFO_GETLOCALPLATFORMTYPE_API_LATEST;
    CHECK(fx.userinfo.get_local_platform_type(&options) == EOS_OPT_Epic);
}

TEST_CASE("copy user info rejects an incompatible version") {
    userinfo_fixture fx;
    EOS_UserInfo_CopyUserInfoOptions options = {};
    options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST + 1;
    options.LocalUserId = fx.me();
    options.TargetUserId = fx.me();
    EOS_UserInfo* out = reinterpret_cast<EOS_UserInfo*>(1);
    CHECK(fx.userinfo.copy_user_info(&options, &out) == EOS_EResult::EOS_InvalidParameters);
    CHECK((out == 0));
}

TEST_CASE("releasing a null or unknown user info is a safe no-op") {
    release_user_info(0);
    release_best_display_name(0);
    release_external_user_info(0);
}
