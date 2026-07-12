#include "interfaces/auth.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

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

// base64url without padding, per RFC 7515. Used to mint the unsigned tokens below.
std::string base64url_encode(const std::string& in) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const u32 n = (static_cast<u8>(in[i]) << 16) | (static_cast<u8>(in[i + 1]) << 8) |
                      static_cast<u8>(in[i + 2]);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += table[n & 63];
    }
    const std::size_t remaining = in.size() - i;
    if (remaining == 1) {
        const u32 n = static_cast<u8>(in[i]) << 16;
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
    } else if (remaining == 2) {
        const u32 n = (static_cast<u8>(in[i]) << 16) | (static_cast<u8>(in[i + 1]) << 8);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
    }
    return out;
}

// An unsigned (alg:none) JWT for the given subject. We have no backend to sign against, so the
// foundation issues unsigned tokens; real signing is a later refinement.
std::string make_unsigned_jwt(const std::string& subject) {
    const std::string header = "{\"alg\":\"none\",\"typ\":\"JWT\"}";
    const std::string payload = "{\"iss\":\"eosr\",\"sub\":\"" + subject + "\"}";
    return base64url_encode(header) + "." + base64url_encode(payload) + ".";
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

// Live minted tokens, keyed by the exact pointer we handed out. Looking the holder up by that
// pointer (rather than casting the token pointer back to the holder) keeps release well-defined
// and makes releasing an unknown or already-freed pointer a safe no-op.
std::mutex g_token_mutex;
std::map<EOS_Auth_Token*, auth_token_holder*> g_auth_tokens;
std::map<EOS_Auth_IdToken*, id_token_holder*> g_id_tokens;

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
    const i32 type = static_cast<i32>(credentials->Type);
    const i32 lowest = static_cast<i32>(EOS_ELoginCredentialType::EOS_LCT_Password);
    const i32 highest = static_cast<i32>(EOS_ELoginCredentialType::EOS_LCT_ExternalAuth);
    if (type < lowest || type > highest) {
        return false;
    }
    return true;
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

    // The emulator does not authenticate against a backend: any supported credential type
    // resolves to the one stable local Epic account derived from the configured user.
    EOS_EpicAccountId self =
        id_registry::instance().get_epic_account_id(settings_.epic_account_id());
    const bool was_logged_in = is_logged_in();
    if (!was_logged_in) {
        accounts_.push_back(self);
    }

    info->ResultCode = EOS_EResult::EOS_Success;
    info->LocalUserId = self;
    info->SelectedAccountId = self;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));

    if (!was_logged_in) {
        status_transition change;
        change.user = self;
        change.previous = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
        change.current = EOS_ELoginStatus::EOS_LS_LoggedIn;
        pending_status_changes_.push_back(change);
        log_info("auth: logged in " + settings_.epic_account_id());
    }
}

void sdk_auth::logout(const EOS_Auth_LogoutOptions* options, void* client_data,
                      EOS_Auth_OnLogoutCallback delegate) {
    if (delegate == 0) {
        return;
    }
    EOS_EpicAccountId user = (options != 0) ? options->LocalUserId : 0;
    const bool matches_self = is_logged_in() && user == local_account();

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Auth_LogoutCallbackInfo* info = static_cast<EOS_Auth_LogoutCallbackInfo*>(
        result->create_callback(cb_logout, sizeof(EOS_Auth_LogoutCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = matches_self ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidUser;
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
    if (!is_logged_in()) {
        *out = 0;
        return EOS_EResult::EOS_InvalidAuth;
    }
    if (local_user_id != local_account()) {
        *out = 0;
        return EOS_EResult::EOS_InvalidUser;
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
        return EOS_EResult::EOS_InvalidUser;
    }

    // The token is self-issued: we have no backend to mint a real signed token against, so the
    // access and refresh tokens are deterministic placeholders for the local account.
    std::unique_ptr<auth_token_holder> holder(new auth_token_holder());
    holder->app = settings_.product_id();
    holder->client_id = settings_.client_id();
    holder->access_token = make_unsigned_jwt(settings_.epic_account_id());
    holder->expires_at = "";
    holder->refresh_token = make_unsigned_jwt(settings_.epic_account_id() + ".refresh");
    holder->refresh_expires_at = "";

    EOS_Auth_Token& token = holder->token;
    token.ApiVersion = EOS_AUTH_TOKEN_API_LATEST;
    token.App = holder->app.c_str();
    token.ClientId = holder->client_id.c_str();
    token.AccountId = local_account();
    token.AccessToken = holder->access_token.c_str();
    token.ExpiresIn = 3600.0;
    token.ExpiresAt = holder->expires_at.c_str();
    token.AuthType = EOS_EAuthTokenType::EOS_ATT_User;
    token.RefreshToken = holder->refresh_token.c_str();
    token.RefreshExpiresIn = 3600.0;
    token.RefreshExpiresAt = holder->refresh_expires_at.c_str();

    EOS_Auth_Token* handle = &holder->token;
    {
        std::lock_guard<std::mutex> lock(g_token_mutex);
        g_auth_tokens[handle] = holder.release();
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
        return EOS_EResult::EOS_InvalidUser;
    }

    std::unique_ptr<id_token_holder> holder(new id_token_holder());
    holder->json_web_token = make_unsigned_jwt(settings_.epic_account_id());
    holder->token.ApiVersion = EOS_AUTH_IDTOKEN_API_LATEST;
    holder->token.AccountId = local_account();
    holder->token.JsonWebToken = holder->json_web_token.c_str();

    EOS_Auth_IdToken* handle = &holder->token;
    {
        std::lock_guard<std::mutex> lock(g_token_mutex);
        g_id_tokens[handle] = holder.release();
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
    auth_token_holder* holder = 0;
    {
        std::lock_guard<std::mutex> lock(g_token_mutex);
        std::map<EOS_Auth_Token*, auth_token_holder*>::iterator it = g_auth_tokens.find(token);
        if (it == g_auth_tokens.end()) {
            return;
        }
        holder = it->second;
        g_auth_tokens.erase(it);
    }
    delete holder;
}

void release_id_token(EOS_Auth_IdToken* token) {
    if (token == 0) {
        return;
    }
    id_token_holder* holder = 0;
    {
        std::lock_guard<std::mutex> lock(g_token_mutex);
        std::map<EOS_Auth_IdToken*, id_token_holder*>::iterator it = g_id_tokens.find(token);
        if (it == g_id_tokens.end()) {
            return;
        }
        holder = it->second;
        g_id_tokens.erase(it);
    }
    delete holder;
}

} // namespace eosr
