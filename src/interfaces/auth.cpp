#include "interfaces/auth.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "common/crypto.h"
#include "common/ids.h"
#include "common/log.h"
#include "common/types.h"
#include "core/callback_manager.h"
#include "core/frame_result.h"
#include "core/settings.h"

namespace eosr {

namespace {

const callback_type_id cb_login = 1;
const callback_type_id cb_logout = 2;
const callback_type_id cb_login_status_changed = 3;
const callback_type_id cb_stub = 4;

struct common_completion_prefix {
    EOS_EResult result_code;
    void* client_data;
};

// Token lifetime and expiry we report; the emulator's tokens do not actually expire.
const double token_lifetime_seconds = 3600.0;
const char* token_expiry_placeholder = "2099-12-31T23:59:59.000Z";

// A fixed key shared by every emulator instance. Tokens are self-issued: this is not a secret
// against a real backend, but it lets any peer running this SDK verify a token another peer
// signed, which is the self-consistent scheme the LAN use case needs.
const char* self_signing_key = "eos-reimagined-shared-signing-key";

// A JWT signed with HS256 (HMAC-SHA256) under the shared self-signing key. It is genuinely
// signed and self-verifiable across peers, but it is self-issued, not backed by Epic's servers.
std::string make_signed_jwt(const std::string& subject) {
    const std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    const std::string payload = "{\"iss\":\"eosr\",\"sub\":\"" + subject + "\"}";
    const std::string signing_input = base64url_encode(header) + "." + base64url_encode(payload);
    const std::vector<u8> signature =
        hmac_sha256(reinterpret_cast<const u8*>(self_signing_key), std::strlen(self_signing_key),
                    reinterpret_cast<const u8*>(signing_input.data()), signing_input.size());
    return signing_input + "." + base64url_encode(signature.data(), signature.size());
}

// A minted auth token plus the strings it points at. The token points into these, so the holder
// must outlive it; we hand back &holder->token and free the whole holder on release.
struct auth_token_holder {
    EOS_Auth_Token token;
    std::string app;
    std::string client_id;
    std::string access_token;
    std::string expires_at;
    std::string refresh_token;
    std::string refresh_expires_at;
};

struct id_token_holder {
    EOS_Auth_IdToken token;
    std::string json_web_token;
};

// Minted token holders are retained for the life of the process and never freed, so a token's
// address is never returned to the allocator and can never be reused by a later token. That is
// what makes a stale-pointer release harmless: it can never collide with a live token. Games copy
// tokens rarely, so this holds a handful of small objects in practice. The stores are immortal so
// leak checkers see the holders as intentionally reachable; the live sets track which tokens have
// not yet been released, so releasing a null, foreign, or already-released pointer is a no-op.
std::mutex g_token_mutex;

std::vector<std::unique_ptr<auth_token_holder> >& auth_token_store() {
    static std::vector<std::unique_ptr<auth_token_holder> >* store =
        new std::vector<std::unique_ptr<auth_token_holder> >();
    return *store;
}
std::vector<std::unique_ptr<id_token_holder> >& id_token_store() {
    static std::vector<std::unique_ptr<id_token_holder> >* store =
        new std::vector<std::unique_ptr<id_token_holder> >();
    return *store;
}
std::set<EOS_Auth_Token*>& live_auth_tokens() {
    static std::set<EOS_Auth_Token*>* live = new std::set<EOS_Auth_Token*>();
    return *live;
}
std::set<EOS_Auth_IdToken*>& live_id_tokens() {
    static std::set<EOS_Auth_IdToken*>* live = new std::set<EOS_Auth_IdToken*>();
    return *live;
}

// The token/id requirements differ per credential type. We do not authenticate the values, but
// we do reject a request whose required fields are missing or whose forbidden fields are set, and
// we reject the credential types the SDK marks unsupported (Device Code).
bool credentials_are_valid(const EOS_Auth_Credentials& credentials) {
    const bool has_id = credentials.Id != 0;
    const bool has_token = credentials.Token != 0;
    switch (credentials.Type) {
        case EOS_ELoginCredentialType::EOS_LCT_Password:
            return has_id && has_token; // account email + password
        case EOS_ELoginCredentialType::EOS_LCT_ExchangeCode:
            return has_token && !has_id; // exchange-code token only
        case EOS_ELoginCredentialType::EOS_LCT_PersistentAuth:
            return true; // the refresh token is SDK-managed, so no field is required
        case EOS_ELoginCredentialType::EOS_LCT_Developer:
            return has_id && has_token; // dev-tool host + credential name
        case EOS_ELoginCredentialType::EOS_LCT_RefreshToken:
            return has_token; // the refresh token
        case EOS_ELoginCredentialType::EOS_LCT_AccountPortal:
            return !has_id && !has_token; // credentials are unused
        case EOS_ELoginCredentialType::EOS_LCT_ExternalAuth:
            return has_token; // the external auth token
        case EOS_ELoginCredentialType::EOS_LCT_DeviceCode:
        default:
            return false; // unsupported or unknown credential type
    }
}

bool login_options_are_valid(const EOS_Auth_LoginOptions* options) {
    if (options == 0 || options->Credentials == 0) {
        return false;
    }
    if (options->ApiVersion <= 0 || options->ApiVersion > EOS_AUTH_LOGIN_API_LATEST) {
        return false;
    }
    const EOS_Auth_Credentials* credentials = options->Credentials;
    if (credentials->ApiVersion <= 0 || credentials->ApiVersion > EOS_AUTH_CREDENTIALS_API_LATEST) {
        return false;
    }
    return credentials_are_valid(*credentials);
}

} // namespace

sdk_auth::sdk_auth(sdk_settings& settings, callback_manager& callbacks)
    : settings_(settings), callbacks_(callbacks), registered_(false) {
}

sdk_auth::~sdk_auth() {
    emu_deinit();
}

void sdk_auth::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    registered_ = true;
}

void sdk_auth::emu_deinit() {
    if (!registered_) {
        return;
    }
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    accounts_.clear();
    known_accounts_.clear();
    pending_status_changes_.clear();
    registered_ = false;
}

EOS_EpicAccountId sdk_auth::local_account() const {
    return accounts_.empty() ? 0 : accounts_[0];
}

void sdk_auth::login(const EOS_Auth_LoginOptions* options, void* client_data,
                     EOS_Auth_OnLoginCallback delegate) {
    if (delegate == 0) {
        return;
    }

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Auth_LoginCallbackInfo* info = static_cast<EOS_Auth_LoginCallbackInfo*>(
        result->create_callback(cb_login, sizeof(EOS_Auth_LoginCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;

    if (!login_options_are_valid(options)) {
        info->ResultCode = EOS_EResult::EOS_InvalidParameters;
        info->LocalUserId = 0;
        info->SelectedAccountId = 0;
        result->set_done(true);
        callbacks_.add_callback(this, std::move(result));
        return;
    }

    // A single active user is supported; a second login while one is active is refused rather
    // than silently succeeding, matching the emulator's "multiple login not implemented".
    if (is_logged_in()) {
        info->ResultCode = EOS_EResult::EOS_LimitExceeded;
        info->LocalUserId = local_account();
        info->SelectedAccountId = local_account();
        result->set_done(true);
        callbacks_.add_callback(this, std::move(result));
        return;
    }

    // The emulator does not authenticate against a backend: any supported credential type
    // resolves to the one stable local Epic account derived from the configured user.
    EOS_EpicAccountId self =
        id_registry::instance().get_epic_account_id(settings_.epic_account_id());
    accounts_.push_back(self);
    known_accounts_.insert(self);

    info->ResultCode = EOS_EResult::EOS_Success;
    info->LocalUserId = self;
    info->SelectedAccountId = self;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));

    status_transition change;
    change.user = self;
    change.previous = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    change.current = EOS_ELoginStatus::EOS_LS_LoggedIn;
    pending_status_changes_.push_back(change);
    log_info("auth: logged in " + settings_.epic_account_id());
}

void sdk_auth::logout(const EOS_Auth_LogoutOptions* options, void* client_data,
                      EOS_Auth_OnLogoutCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid_options =
        options != 0 && options->ApiVersion > 0 && options->ApiVersion <= EOS_AUTH_LOGOUT_API_LATEST;
    EOS_EpicAccountId user = (options != 0) ? options->LocalUserId : 0;
    const bool matches_self = valid_options && is_logged_in() && user == local_account();

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Auth_LogoutCallbackInfo* info = static_cast<EOS_Auth_LogoutCallbackInfo*>(
        result->create_callback(cb_logout, sizeof(EOS_Auth_LogoutCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    if (!valid_options) {
        info->ResultCode = EOS_EResult::EOS_InvalidParameters;
    } else {
        info->ResultCode = matches_self ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidUser;
    }
    info->ClientData = client_data;
    info->LocalUserId = user;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));

    if (matches_self) {
        accounts_.clear();
        status_transition change;
        change.user = user;
        change.previous = EOS_ELoginStatus::EOS_LS_LoggedIn;
        change.current = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
        pending_status_changes_.push_back(change);
        log_info("auth: logged out");
    }
}

i32 sdk_auth::logged_in_accounts_count() const {
    return static_cast<i32>(accounts_.size());
}

EOS_EpicAccountId sdk_auth::logged_in_account_by_index(i32 index) const {
    if (index < 0 || index >= static_cast<i32>(accounts_.size())) {
        return 0;
    }
    return accounts_[static_cast<std::size_t>(index)];
}

EOS_ELoginStatus sdk_auth::login_status(EOS_EpicAccountId local_user_id) const {
    if (is_logged_in() && local_user_id == local_account()) {
        return EOS_ELoginStatus::EOS_LS_LoggedIn;
    }
    return EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

EOS_EResult sdk_auth::selected_account_id(EOS_EpicAccountId local_user_id,
                                          EOS_EpicAccountId* out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // An account we have never seen is InvalidUser; a known account that is simply not logged in
    // right now is InvalidAuth. These are distinct cases in the public contract.
    if (known_accounts_.find(local_user_id) == known_accounts_.end()) {
        *out = 0;
        return EOS_EResult::EOS_InvalidUser;
    }
    if (!is_logged_in() || local_user_id != local_account()) {
        *out = 0;
        return EOS_EResult::EOS_InvalidAuth;
    }
    // The emulator never merges accounts, so the selected account is the local account.
    *out = local_account();
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_auth::copy_user_auth_token(EOS_EpicAccountId local_user_id,
                                           EOS_Auth_Token** out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (!is_logged_in() || local_user_id != local_account()) {
        return EOS_EResult::EOS_NotFound;
    }

    // The token is self-issued and HS256-signed; see make_signed_jwt. It stands in for the account
    // without being backed by Epic's servers.
    std::unique_ptr<auth_token_holder> holder(new auth_token_holder());
    holder->app = settings_.product_id();
    holder->client_id = settings_.client_id();
    holder->access_token = make_signed_jwt(settings_.epic_account_id());
    holder->expires_at = token_expiry_placeholder;
    holder->refresh_token = make_signed_jwt(settings_.epic_account_id() + ".refresh");
    holder->refresh_expires_at = token_expiry_placeholder;

    EOS_Auth_Token& token = holder->token;
    token.ApiVersion = EOS_AUTH_TOKEN_API_LATEST;
    token.App = holder->app.c_str();
    token.ClientId = holder->client_id.c_str();
    token.AccountId = local_account();
    token.AccessToken = holder->access_token.c_str();
    token.ExpiresIn = token_lifetime_seconds;
    token.ExpiresAt = holder->expires_at.c_str();
    token.AuthType = EOS_EAuthTokenType::EOS_ATT_User;
    token.RefreshToken = holder->refresh_token.c_str();
    token.RefreshExpiresIn = token_lifetime_seconds;
    token.RefreshExpiresAt = holder->refresh_expires_at.c_str();

    EOS_Auth_Token* handle = &holder->token;
    {
        std::lock_guard<std::mutex> lock(g_token_mutex);
        auth_token_store().push_back(std::move(holder));
        live_auth_tokens().insert(handle);
    }
    *out = handle;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_auth::copy_id_token(EOS_EpicAccountId account_id, EOS_Auth_IdToken** out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (!is_logged_in() || account_id != local_account()) {
        return EOS_EResult::EOS_NotFound;
    }

    std::unique_ptr<id_token_holder> holder(new id_token_holder());
    holder->json_web_token = make_signed_jwt(settings_.epic_account_id());
    holder->token.ApiVersion = EOS_AUTH_IDTOKEN_API_LATEST;
    holder->token.AccountId = local_account();
    holder->token.JsonWebToken = holder->json_web_token.c_str();

    EOS_Auth_IdToken* handle = &holder->token;
    {
        std::lock_guard<std::mutex> lock(g_token_mutex);
        id_token_store().push_back(std::move(holder));
        live_id_tokens().insert(handle);
    }
    *out = handle;
    return EOS_EResult::EOS_Success;
}

EOS_NotificationId sdk_auth::add_notify_login_status_changed(
    void* client_data, EOS_Auth_OnLoginStatusChangedCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Auth_LoginStatusChangedCallbackInfo* info =
        static_cast<EOS_Auth_LoginStatusChangedCallbackInfo*>(
            result->create_callback(cb_login_status_changed,
                                    sizeof(EOS_Auth_LoginStatusChangedCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->LocalUserId = 0;
    info->PrevStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    info->CurrentStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_auth::remove_notify_login_status_changed(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

void sdk_auth::queue_stub_result(void* client_data, completion_delegate delegate,
                                 std::size_t info_size) {
    if (delegate == 0) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_stub, info_size, delegate);
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->result_code = EOS_EResult::EOS_NotImplemented;
    prefix->client_data = client_data;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

bool sdk_auth::cb_run_frame() {
    if (pending_status_changes_.empty()) {
        return false;
    }
    std::vector<status_transition> changes;
    changes.swap(pending_status_changes_);
    for (std::size_t i = 0; i < changes.size(); i++) {
        // Re-look-up each notification by id before firing: a fired callback may remove another.
        std::vector<EOS_NotificationId> ids =
            callbacks_.notification_ids(this, cb_login_status_changed);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            EOS_Auth_LoginStatusChangedCallbackInfo* info =
                note->get_callback<EOS_Auth_LoginStatusChangedCallbackInfo>();
            info->LocalUserId = changes[i].user;
            info->PrevStatus = changes[i].previous;
            info->CurrentStatus = changes[i].current;
            note->fire();
        }
    }
    return false;
}

bool sdk_auth::run_callbacks(frame_result&) {
    return false;
}

void sdk_auth::free_callback(frame_result&) {
}

void release_auth_token(EOS_Auth_Token* token) {
    if (token == 0) {
        return;
    }
    // A released token is dropped from the live set; its holder stays retained in the store so its
    // address is never reused. Releasing a null, foreign, or already-released pointer changes
    // nothing.
    std::lock_guard<std::mutex> lock(g_token_mutex);
    live_auth_tokens().erase(token);
}

void release_id_token(EOS_Auth_IdToken* token) {
    if (token == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_token_mutex);
    live_id_tokens().erase(token);
}

} // namespace eosr
