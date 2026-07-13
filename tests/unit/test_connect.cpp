#include "doctest.h"

#include <cstring>
#include <string>
#include <vector>

#include "eos_common.h"
#include "eos_connect_types.h"

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/connect.h"
#include "net/messages.h"
#include "net/message_router.h"
#include "net/wire.h"

using namespace eosr;

namespace {

// Captured login/logout/status results, reset per case.
struct login_capture {
    bool fired;
    EOS_EResult result;
    EOS_ProductUserId user;
    void* client_data;
};
login_capture g_login;
login_capture g_logout;

int g_status_count;
EOS_ELoginStatus g_status_prev;
EOS_ELoginStatus g_status_curr;

int g_query_count;
EOS_EResult g_query_result;

void reset_captures() {
    std::memset(&g_login, 0, sizeof(g_login));
    std::memset(&g_logout, 0, sizeof(g_logout));
    g_status_count = 0;
    g_status_prev = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    g_status_curr = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    g_query_count = 0;
    g_query_result = EOS_EResult::EOS_UnexpectedError;
}

void EOS_CALL on_login(const EOS_Connect_LoginCallbackInfo* info) {
    g_login.fired = true;
    g_login.result = info->ResultCode;
    g_login.user = info->LocalUserId;
    g_login.client_data = info->ClientData;
}

void EOS_CALL on_logout(const EOS_Connect_LogoutCallbackInfo* info) {
    g_logout.fired = true;
    g_logout.result = info->ResultCode;
    g_logout.user = info->LocalUserId;
    g_logout.client_data = info->ClientData;
}

void EOS_CALL on_status(const EOS_Connect_LoginStatusChangedCallbackInfo* info) {
    g_status_count++;
    g_status_prev = info->PreviousStatus;
    g_status_curr = info->CurrentStatus;
}

void EOS_CALL on_query(const EOS_Connect_QueryProductUserIdMappingsCallbackInfo* info) {
    g_query_count++;
    g_query_result = info->ResultCode;
}

// For the removal-during-firing regression: the first notification removes the second.
sdk_connect* g_remover_connect = 0;
EOS_NotificationId g_remover_target = 0;
int g_remover_fired = 0;
int g_victim_fired = 0;

void EOS_CALL on_status_remover(const EOS_Connect_LoginStatusChangedCallbackInfo*) {
    g_remover_fired++;
    if (g_remover_connect != 0) {
        g_remover_connect->remove_notify_login_status_changed(g_remover_target);
    }
}

void EOS_CALL on_status_victim(const EOS_Connect_LoginStatusChangedCallbackInfo*) {
    g_victim_fired++;
}

EOS_Connect_LoginOptions login_options(EOS_Connect_Credentials& credentials) {
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Token = "token";
    credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_LoginOptions options = {};
    options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    options.Credentials = &credentials;
    return options;
}

// A fixture bundling the engine pieces sdk_connect depends on.
struct connect_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    message_router network;
    sdk_connect connect;

    connect_fixture() : connect(settings, callbacks, network) {
        connect.emu_init();
        reset_captures();
    }
    ~connect_fixture() { connect.emu_deinit(); }

    void do_login(void* client_data) {
        EOS_Connect_Credentials credentials;
        EOS_Connect_LoginOptions options = login_options(credentials);
        connect.login(&options, client_data, on_login);
    }
};

} // namespace

TEST_CASE("login completes on the next tick and populates the local user") {
    connect_fixture fx;
    int context = 0;
    fx.do_login(&context);

    // The completion is deferred: nothing fires until a tick.
    CHECK_FALSE(g_login.fired);
    fx.callbacks.tick();

    CHECK(g_login.fired);
    CHECK(g_login.result == EOS_EResult::EOS_Success);
    CHECK((g_login.client_data == &context));
    CHECK((g_login.user != 0));
    CHECK(g_login.user->valid);

    CHECK(fx.connect.logged_in_users_count() == 1);
    CHECK((fx.connect.logged_in_user_by_index(0) == g_login.user));
    CHECK((fx.connect.logged_in_user_by_index(1) == 0));
    CHECK(fx.connect.login_status(g_login.user) == EOS_ELoginStatus::EOS_LS_LoggedIn);
}

TEST_CASE("login derives the stable ProductUserId from settings") {
    connect_fixture fx;
    fx.settings.set_username("InfernusHawk");
    fx.do_login(0);
    fx.callbacks.tick();

    EOS_ProductUserId expected =
        id_registry::instance().get_product_user_id(fx.settings.product_user_id());
    CHECK((g_login.user == expected));
}

TEST_CASE("a logged-out status is reported for unknown users and before login") {
    connect_fixture fx;
    CHECK(fx.connect.logged_in_users_count() == 0);
    CHECK(fx.connect.login_status(0) == EOS_ELoginStatus::EOS_LS_NotLoggedIn);

    EOS_ProductUserId stranger =
        id_registry::instance().get_product_user_id("0123456789abcdef0123456789abcdef");
    fx.do_login(0);
    fx.callbacks.tick();
    CHECK(fx.connect.login_status(stranger) == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
}

TEST_CASE("logout clears the local user and reports success") {
    connect_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    REQUIRE(fx.connect.logged_in_users_count() == 1);
    EOS_ProductUserId self = fx.connect.logged_in_user_by_index(0);

    EOS_Connect_LogoutOptions options = {};
    options.ApiVersion = EOS_CONNECT_LOGOUT_API_LATEST;
    options.LocalUserId = self;
    fx.connect.logout(&options, 0, on_logout);
    fx.callbacks.tick();

    CHECK(g_logout.fired);
    CHECK(g_logout.result == EOS_EResult::EOS_Success);
    CHECK(fx.connect.logged_in_users_count() == 0);
    CHECK(fx.connect.login_status(self) == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
}

TEST_CASE("logging out a user who is not logged in is rejected") {
    connect_fixture fx;
    EOS_ProductUserId stranger =
        id_registry::instance().get_product_user_id("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EOS_Connect_LogoutOptions options = {};
    options.ApiVersion = EOS_CONNECT_LOGOUT_API_LATEST;
    options.LocalUserId = stranger;
    fx.connect.logout(&options, 0, on_logout);
    fx.callbacks.tick();

    CHECK(g_logout.fired);
    CHECK(g_logout.result == EOS_EResult::EOS_InvalidUser);
}

TEST_CASE("the login-status-changed notification fires on login and logout") {
    connect_fixture fx;
    const EOS_NotificationId id = fx.connect.add_notify_login_status_changed(0, on_status);
    CHECK(id != 0);

    fx.do_login(0);
    fx.callbacks.tick();
    CHECK(g_status_count == 1);
    CHECK(g_status_prev == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
    CHECK(g_status_curr == EOS_ELoginStatus::EOS_LS_LoggedIn);

    EOS_Connect_LogoutOptions options = {};
    options.ApiVersion = EOS_CONNECT_LOGOUT_API_LATEST;
    options.LocalUserId = fx.connect.logged_in_user_by_index(0);
    fx.connect.logout(&options, 0, on_logout);
    fx.callbacks.tick();
    CHECK(g_status_count == 2);
    CHECK(g_status_prev == EOS_ELoginStatus::EOS_LS_LoggedIn);
    CHECK(g_status_curr == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
}

TEST_CASE("a removed login-status notification no longer fires") {
    connect_fixture fx;
    const EOS_NotificationId id = fx.connect.add_notify_login_status_changed(0, on_status);
    fx.connect.remove_notify_login_status_changed(id);

    fx.do_login(0);
    fx.callbacks.tick();
    CHECK(g_status_count == 0);
}

TEST_CASE("a notification that removes another while firing does not use freed memory") {
    connect_fixture fx;
    g_remover_connect = &fx.connect;
    g_remover_fired = 0;
    g_victim_fired = 0;

    // The lower id fires first; register the remover first so it runs before its victim, and
    // have it remove the victim mid-batch. Before the fix this dangled and freed the victim.
    fx.connect.add_notify_login_status_changed(0, on_status_remover);
    g_remover_target = fx.connect.add_notify_login_status_changed(0, on_status_victim);

    fx.do_login(0);
    fx.callbacks.tick();

    CHECK(g_remover_fired == 1);
    CHECK(g_victim_fired == 0); // removed before its turn, never fired, never a use-after-free
    g_remover_connect = 0;
}

TEST_CASE("query product user id mappings completes with success") {
    connect_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();

    EOS_ProductUserId self = fx.connect.logged_in_user_by_index(0);
    EOS_Connect_QueryProductUserIdMappingsOptions options = {};
    options.ApiVersion = EOS_CONNECT_QUERYPRODUCTUSERIDMAPPINGS_API_LATEST;
    options.LocalUserId = self;
    fx.connect.query_product_user_id_mappings(&options, 0, on_query);
    fx.callbacks.tick();

    CHECK(g_query_count == 1);
    CHECK(g_query_result == EOS_EResult::EOS_Success);
}

TEST_CASE("a peer announcement adds the peer to the roster") {
    connect_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    CHECK(fx.connect.known_peer_count() == 0);

    // Craft an inbound connect_response from a peer and dispatch it directly.
    connect_infos peer;
    peer.product_user_id = "0123456789abcdef0123456789abcdef";
    peer.display_name = "RemotePlayer";
    byte_writer writer;
    serialize(writer, peer);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::connect_response);
    envelope.source_id = peer.product_user_id;
    envelope.payload = writer.data();
    CHECK(fx.connect.on_network_message(envelope));
    CHECK(fx.connect.known_peer_count() == 1);

    // We do not yet learn peers' external accounts, so the mapping is reported as not found
    // rather than standing in the display name.
    EOS_ProductUserId peer_id = id_registry::instance().get_product_user_id(peer.product_user_id);
    EOS_Connect_GetProductUserIdMappingOptions options = {};
    options.ApiVersion = EOS_CONNECT_GETPRODUCTUSERIDMAPPING_API_LATEST;
    options.LocalUserId = fx.connect.logged_in_user_by_index(0);
    options.AccountIdType = EOS_EExternalAccountType::EOS_EAT_EPIC;
    options.TargetProductUserId = peer_id;

    char buffer[64];
    int32_t length = static_cast<int32_t>(sizeof(buffer));
    CHECK(fx.connect.get_product_user_id_mapping(&options, buffer, &length) == EOS_EResult::EOS_NotFound);
}

TEST_CASE("the roster ignores our own looped-back announcement") {
    connect_fixture fx;
    fx.settings.set_username("SelfPlayer");
    fx.do_login(0);
    fx.callbacks.tick();

    // An envelope whose source is our own ProductUserId must be dropped, adding no peer.
    connect_infos self;
    self.product_user_id = fx.settings.product_user_id();
    self.display_name = "SelfPlayer";
    byte_writer writer;
    serialize(writer, self);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::connect_response);
    envelope.source_id = fx.settings.product_user_id();
    envelope.payload = writer.data();
    CHECK(fx.connect.on_network_message(envelope));
    CHECK(fx.connect.known_peer_count() == 0);
}

TEST_CASE("login is idempotent for the same local user") {
    connect_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    fx.do_login(0);
    fx.callbacks.tick();
    // A second login of the same derived user does not create a duplicate entry.
    CHECK(fx.connect.logged_in_users_count() == 1);
}

TEST_CASE("login rejects malformed credentials without logging in") {
    // Each case starts from a valid request and breaks exactly one field, expecting the login
    // callback to report InvalidParameters and leave no local user behind.
    auto expect_rejected = [](EOS_Connect_LoginOptions options, EOS_Connect_Credentials* creds) {
        connect_fixture fx;
        options.Credentials = creds;
        fx.connect.login(&options, 0, on_login);
        fx.callbacks.tick();
        CHECK(g_login.fired);
        CHECK(g_login.result == EOS_EResult::EOS_InvalidParameters);
        CHECK(fx.connect.logged_in_users_count() == 0);
    };

    SUBCASE("null credentials") {
        EOS_Connect_LoginOptions options = {};
        options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
        expect_rejected(options, 0);
    }
    SUBCASE("unsupported login option version") {
        EOS_Connect_Credentials creds = {};
        EOS_Connect_LoginOptions options = login_options(creds);
        options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST + 1;
        expect_rejected(options, &creds);
    }
    SUBCASE("unsupported credentials version") {
        EOS_Connect_Credentials creds = {};
        EOS_Connect_LoginOptions options = login_options(creds);
        creds.ApiVersion = 0;
        expect_rejected(options, &creds);
    }
    SUBCASE("null token") {
        EOS_Connect_Credentials creds = {};
        EOS_Connect_LoginOptions options = login_options(creds);
        creds.Token = 0;
        expect_rejected(options, &creds);
    }
    SUBCASE("empty token") {
        EOS_Connect_Credentials creds = {};
        EOS_Connect_LoginOptions options = login_options(creds);
        creds.Token = "";
        expect_rejected(options, &creds);
    }
    SUBCASE("credential type out of range") {
        EOS_Connect_Credentials creds = {};
        EOS_Connect_LoginOptions options = login_options(creds);
        creds.Type = static_cast<EOS_EExternalCredentialType>(9999);
        expect_rejected(options, &creds);
    }
}

// The roster used to be keyed on the product user id inside the message rather than the one the
// connection proved. A peer could therefore write any *other* player's roster entry -- and once
// Friends and UserInfo read this roster, that entry is that player's name as everyone on the mesh
// sees it. The connection says who a peer is; the payload does not get a say.
TEST_CASE("a peer cannot write another player's roster entry") {
    connect_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();

    const std::string victim = "0123456789abcdef0123456789abcdef";
    const std::string attacker = "fedcba9876543210fedcba9876543210";

    // The attacker announces itself under the victim's id.
    connect_infos forged;
    forged.product_user_id = victim;
    forged.display_name = "NotTheVictim";
    byte_writer writer;
    serialize(writer, forged);
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::connect_response);
    envelope.source_id = attacker; // the id the handshake proved, and the only one that counts
    envelope.payload = writer.data();
    CHECK(fx.connect.on_network_message(envelope));

    // It named someone else, so it named nobody: neither player is on the roster under that name.
    CHECK_FALSE(fx.connect.is_known_peer(victim));
    CHECK(fx.connect.known_peer_count() == 0);

    // The same peer speaking for itself is believed, and lands under the id it proved.
    connect_infos honest;
    honest.product_user_id = attacker;
    honest.display_name = "Attacker";
    byte_writer honest_writer;
    serialize(honest_writer, honest);
    net_envelope honest_envelope;
    honest_envelope.type_tag = static_cast<u16>(message_type::connect_response);
    honest_envelope.source_id = attacker;
    honest_envelope.payload = honest_writer.data();
    CHECK(fx.connect.on_network_message(honest_envelope));
    CHECK(fx.connect.is_known_peer(attacker));
    CHECK(fx.connect.known_peer_count() == 1);
}
