#include "doctest.h"

#include <cstring>
#include <string>

#include "eos_common.h"
#include "eos_auth_types.h"

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/auth.h"

using namespace eosr;

namespace {

struct login_capture {
    bool fired;
    EOS_EResult result;
    EOS_EpicAccountId user;
    EOS_EpicAccountId selected;
    void* client_data;
};
login_capture g_login;
login_capture g_logout;

int g_status_count;
EOS_ELoginStatus g_status_prev;
EOS_ELoginStatus g_status_curr;

void reset_captures() {
    std::memset(&g_login, 0, sizeof(g_login));
    std::memset(&g_logout, 0, sizeof(g_logout));
    g_status_count = 0;
    g_status_prev = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    g_status_curr = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

void EOS_CALL on_login(const EOS_Auth_LoginCallbackInfo* info) {
    g_login.fired = true;
    g_login.result = info->ResultCode;
    g_login.user = info->LocalUserId;
    g_login.selected = info->SelectedAccountId;
    g_login.client_data = info->ClientData;
}

void EOS_CALL on_logout(const EOS_Auth_LogoutCallbackInfo* info) {
    g_logout.fired = true;
    g_logout.result = info->ResultCode;
    g_logout.user = info->LocalUserId;
}

void EOS_CALL on_status(const EOS_Auth_LoginStatusChangedCallbackInfo* info) {
    g_status_count++;
    g_status_prev = info->PrevStatus;
    g_status_curr = info->CurrentStatus;
}

bool jwt_has_signature(const char* token) {
    if (token == 0) {
        return false;
    }
    const std::string value(token);
    const std::size_t separator = value.rfind('.');
    return separator != std::string::npos && separator + 1 < value.size();
}

EOS_Auth_LoginOptions login_options(EOS_Auth_Credentials& credentials) {
    credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    credentials.Id = 0;
    credentials.Token = "ticket";
    credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
    EOS_Auth_LoginOptions options = {};
    options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    options.Credentials = &credentials;
    return options;
}

struct auth_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    sdk_auth auth;

    auth_fixture() : auth(settings, callbacks) {
        auth.emu_init();
        reset_captures();
    }
    ~auth_fixture() { auth.emu_deinit(); }

    void do_login(void* client_data) {
        EOS_Auth_Credentials credentials;
        EOS_Auth_LoginOptions options = login_options(credentials);
        auth.login(&options, client_data, on_login);
    }
};

} // namespace

TEST_CASE("auth login completes on the next tick with the local Epic account") {
    auth_fixture fx;
    int context = 0;
    fx.do_login(&context);

    CHECK_FALSE(g_login.fired);
    fx.callbacks.tick();

    CHECK(g_login.fired);
    CHECK(g_login.result == EOS_EResult::EOS_Success);
    CHECK((g_login.client_data == &context));
    CHECK((g_login.user != 0));
    CHECK(g_login.user->valid);
    CHECK((g_login.selected == g_login.user)); // no merged accounts, so the selected id is self

    CHECK(fx.auth.logged_in_accounts_count() == 1);
    CHECK((fx.auth.logged_in_account_by_index(0) == g_login.user));
    CHECK((fx.auth.logged_in_account_by_index(1) == 0));
    CHECK(fx.auth.login_status(g_login.user) == EOS_ELoginStatus::EOS_LS_LoggedIn);
}

TEST_CASE("auth login derives the stable EpicAccountId from settings") {
    auth_fixture fx;
    fx.settings.set_username("InfernusHawk");
    fx.do_login(0);
    fx.callbacks.tick();

    EOS_EpicAccountId expected =
        id_registry::instance().get_epic_account_id(fx.settings.epic_account_id());
    CHECK((g_login.user == expected));
}

TEST_CASE("auth login rejects malformed credentials without logging in") {
    auto expect_rejected = [](EOS_Auth_LoginOptions options, EOS_Auth_Credentials* creds) {
        auth_fixture fx;
        options.Credentials = creds;
        fx.auth.login(&options, 0, on_login);
        fx.callbacks.tick();
        CHECK(g_login.fired);
        CHECK(g_login.result == EOS_EResult::EOS_InvalidParameters);
        CHECK(fx.auth.logged_in_accounts_count() == 0);
    };

    SUBCASE("null credentials") {
        EOS_Auth_LoginOptions options = {};
        options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
        expect_rejected(options, 0);
    }
    SUBCASE("unsupported login option version") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST + 1;
        expect_rejected(options, &creds);
    }
    SUBCASE("zero login option version") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        options.ApiVersion = 0;
        expect_rejected(options, &creds);
    }
    SUBCASE("zero credentials version") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.ApiVersion = 0;
        expect_rejected(options, &creds);
    }
    SUBCASE("unsupported credentials version") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST + 1;
        expect_rejected(options, &creds);
    }
    SUBCASE("credential type out of range") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Type = static_cast<EOS_ELoginCredentialType>(9999);
        expect_rejected(options, &creds);
    }
    SUBCASE("unsupported device-code login") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Id = 0;
        creds.Token = 0;
        creds.Type = EOS_ELoginCredentialType::EOS_LCT_DeviceCode;
        expect_rejected(options, &creds);
    }
    SUBCASE("exchange code without a token") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Token = 0;
        expect_rejected(options, &creds);
    }
    SUBCASE("exchange code with an unused id") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Id = "must-be-null";
        expect_rejected(options, &creds);
    }
    SUBCASE("password without an email address") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Id = 0;
        creds.Token = "password";
        creds.Type = EOS_ELoginCredentialType::EOS_LCT_Password;
        expect_rejected(options, &creds);
    }
    SUBCASE("developer login without a credential name") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Id = "localhost:6547";
        creds.Token = 0;
        creds.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
        expect_rejected(options, &creds);
    }
    SUBCASE("refresh login without a refresh token") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Token = 0;
        creds.Type = EOS_ELoginCredentialType::EOS_LCT_RefreshToken;
        expect_rejected(options, &creds);
    }
    SUBCASE("account portal with unused credentials") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Id = "must-be-null";
        creds.Token = "must-be-null";
        creds.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
        expect_rejected(options, &creds);
    }
    SUBCASE("external auth without an external token") {
        EOS_Auth_Credentials creds = {};
        EOS_Auth_LoginOptions options = login_options(creds);
        creds.Token = 0;
        creds.Type = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
        expect_rejected(options, &creds);
    }
}

TEST_CASE("auth logout clears the account and fires the status change") {
    auth_fixture fx;
    const EOS_NotificationId id = fx.auth.add_notify_login_status_changed(0, on_status);
    CHECK(id != 0);

    fx.do_login(0);
    fx.callbacks.tick();
    CHECK(g_status_count == 1);
    CHECK(g_status_prev == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
    CHECK(g_status_curr == EOS_ELoginStatus::EOS_LS_LoggedIn);

    EOS_Auth_LogoutOptions options = {};
    options.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
    options.LocalUserId = fx.auth.logged_in_account_by_index(0);
    fx.auth.logout(&options, 0, on_logout);
    fx.callbacks.tick();

    CHECK(g_logout.fired);
    CHECK(g_logout.result == EOS_EResult::EOS_Success);
    CHECK(fx.auth.logged_in_accounts_count() == 0);
    CHECK(g_status_count == 2);
    CHECK(g_status_curr == EOS_ELoginStatus::EOS_LS_NotLoggedIn);
}

TEST_CASE("auth logout rejects unsupported option versions without changing state") {
    auth_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    REQUIRE(fx.auth.logged_in_accounts_count() == 1);

    EOS_Auth_LogoutOptions options = {};
    options.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST + 1;
    options.LocalUserId = fx.auth.logged_in_account_by_index(0);
    fx.auth.logout(&options, 0, on_logout);
    fx.callbacks.tick();

    CHECK(g_logout.fired);
    CHECK(g_logout.result != EOS_EResult::EOS_Success);
    CHECK(fx.auth.logged_in_accounts_count() == 1);
}

TEST_CASE("get selected account id distinguishes not-logged-in from unknown user") {
    auth_fixture fx;
    EOS_EpicAccountId out = 0;
    EOS_EpicAccountId stranger =
        id_registry::instance().get_epic_account_id("0123456789abcdef0123456789abcdef");
    CHECK(fx.auth.selected_account_id(0, &out) == EOS_EResult::EOS_InvalidUser);
    CHECK(fx.auth.selected_account_id(stranger, &out) == EOS_EResult::EOS_InvalidUser);

    fx.do_login(0);
    fx.callbacks.tick();
    EOS_EpicAccountId self = fx.auth.logged_in_account_by_index(0);

    CHECK(fx.auth.selected_account_id(self, &out) == EOS_EResult::EOS_Success);
    CHECK((out == self));

    CHECK(fx.auth.selected_account_id(stranger, &out) == EOS_EResult::EOS_InvalidUser);
    CHECK(fx.auth.selected_account_id(self, 0) == EOS_EResult::EOS_InvalidParameters);

    EOS_Auth_LogoutOptions options = {};
    options.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
    options.LocalUserId = self;
    fx.auth.logout(&options, 0, on_logout);
    fx.callbacks.tick();
    CHECK(fx.auth.selected_account_id(self, &out) == EOS_EResult::EOS_InvalidAuth);
}

TEST_CASE("copy user auth token mints a token that release then frees") {
    auth_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    EOS_EpicAccountId self = fx.auth.logged_in_account_by_index(0);

    EOS_Auth_Token* token = 0;
    REQUIRE(fx.auth.copy_user_auth_token(self, &token) == EOS_EResult::EOS_Success);
    REQUIRE((token != 0));
    CHECK((token->AccountId == self));
    CHECK(token->AuthType == EOS_EAuthTokenType::EOS_ATT_User);
    REQUIRE((token->AccessToken != 0));
    CHECK(std::string(token->AccessToken).substr(0, 3) == "eyJ");
    CHECK((token->ExpiresAt != 0));
    CHECK(std::string(token->ExpiresAt).size() > 0);
    CHECK((token->RefreshToken != 0));
    CHECK((token->RefreshExpiresAt != 0));
    CHECK(std::string(token->RefreshExpiresAt).size() > 0);
    release_auth_token(token); // under ASan this proves the holder and its strings are freed

    // An unknown user has no token.
    EOS_EpicAccountId stranger =
        id_registry::instance().get_epic_account_id("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EOS_Auth_Token* none = 0;
    CHECK(fx.auth.copy_user_auth_token(stranger, &none) == EOS_EResult::EOS_NotFound);
    CHECK((none == 0));
}

TEST_CASE("copy id token mints a JWT that release then frees") {
    auth_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    EOS_EpicAccountId self = fx.auth.logged_in_account_by_index(0);

    EOS_Auth_IdToken* token = 0;
    REQUIRE(fx.auth.copy_id_token(self, &token) == EOS_EResult::EOS_Success);
    REQUIRE((token != 0));
    CHECK((token->AccountId == self));
    REQUIRE((token->JsonWebToken != 0));
    CHECK(std::string(token->JsonWebToken).substr(0, 3) == "eyJ");
    CHECK(jwt_has_signature(token->JsonWebToken));
    release_id_token(token);

    EOS_EpicAccountId stranger =
        id_registry::instance().get_epic_account_id("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    EOS_Auth_IdToken* none = 0;
    CHECK(fx.auth.copy_id_token(stranger, &none) == EOS_EResult::EOS_NotFound);
    CHECK((none == 0));
}

TEST_CASE("releasing a null or unknown token is a safe no-op") {
    release_auth_token(0);
    release_id_token(0);
    EOS_Auth_Token bogus = {};
    release_auth_token(&bogus); // never handed out by us, so ignored rather than freed
    EOS_Auth_IdToken bogus_id = {};
    release_id_token(&bogus_id);
}

TEST_CASE("a stale auth-token pointer cannot release a newer live token") {
    auth_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    const EOS_EpicAccountId self = fx.auth.logged_in_account_by_index(0);

    const int max_attempts = 1024;
    for (int i = 0; i < max_attempts; i++) {
        EOS_Auth_Token* stale = 0;
        REQUIRE(fx.auth.copy_user_auth_token(self, &stale) == EOS_EResult::EOS_Success);
        release_auth_token(stale);

        EOS_Auth_Token* live = 0;
        REQUIRE(fx.auth.copy_user_auth_token(self, &live) == EOS_EResult::EOS_Success);
        if (stale != live) {
            release_auth_token(live);
            continue;
        }

        release_auth_token(stale);

        EOS_Auth_Token* replacement = 0;
        REQUIRE(fx.auth.copy_user_auth_token(self, &replacement) == EOS_EResult::EOS_Success);
        CHECK((replacement != live));
        if (replacement == live) {
            release_auth_token(replacement);
        } else {
            release_auth_token(live);
            release_auth_token(replacement);
        }
        break;
    }

    // Allocators with a quarantine may not reuse an address during this test. In that case the
    // dangerous stale-pointer branch was not reachable, but all allocated tokens were released.
}

TEST_CASE("a stale id-token pointer cannot release a newer live token") {
    auth_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    const EOS_EpicAccountId self = fx.auth.logged_in_account_by_index(0);

    const int max_attempts = 1024;
    for (int i = 0; i < max_attempts; i++) {
        EOS_Auth_IdToken* stale = 0;
        REQUIRE(fx.auth.copy_id_token(self, &stale) == EOS_EResult::EOS_Success);
        release_id_token(stale);

        EOS_Auth_IdToken* live = 0;
        REQUIRE(fx.auth.copy_id_token(self, &live) == EOS_EResult::EOS_Success);
        if (stale != live) {
            release_id_token(live);
            continue;
        }

        release_id_token(stale);

        EOS_Auth_IdToken* replacement = 0;
        REQUIRE(fx.auth.copy_id_token(self, &replacement) == EOS_EResult::EOS_Success);
        CHECK((replacement != live));
        if (replacement == live) {
            release_id_token(replacement);
        } else {
            release_id_token(live);
            release_id_token(replacement);
        }
        break;
    }
}

TEST_CASE("an auth notification that removes another while firing is memory-safe") {
    // Mirror the Connect regression: the first fired notification removes the second.
    static sdk_auth* remover_auth = 0;
    static EOS_NotificationId remover_target = 0;
    static int remover_fired = 0;
    static int victim_fired = 0;
    struct handlers {
        static void EOS_CALL remover(const EOS_Auth_LoginStatusChangedCallbackInfo*) {
            remover_fired++;
            if (remover_auth != 0) {
                remover_auth->remove_notify_login_status_changed(remover_target);
            }
        }
        static void EOS_CALL victim(const EOS_Auth_LoginStatusChangedCallbackInfo*) {
            victim_fired++;
        }
    };

    auth_fixture fx;
    remover_auth = &fx.auth;
    remover_fired = 0;
    victim_fired = 0;
    fx.auth.add_notify_login_status_changed(0, handlers::remover);
    remover_target = fx.auth.add_notify_login_status_changed(0, handlers::victim);

    fx.do_login(0);
    fx.callbacks.tick();

    CHECK(remover_fired == 1);
    CHECK(victim_fired == 0);
    remover_auth = 0;
}

TEST_CASE("auth refuses a second login while an account is already active") {
    auth_fixture fx;
    fx.do_login(0);
    fx.callbacks.tick();
    fx.do_login(0);
    fx.callbacks.tick();
    CHECK(g_login.result != EOS_EResult::EOS_Success);
    CHECK(fx.auth.logged_in_accounts_count() == 1);
}
