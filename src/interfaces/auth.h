#ifndef EOSR_INTERFACES_AUTH_H
#define EOSR_INTERFACES_AUTH_H

#include <cstddef>
#include <set>
#include <vector>

#include "eos_common.h"
#include "eos_auth_types.h"

#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"

namespace eosr {

class sdk_settings;
class callback_manager;
class frame_result;

// The Auth interface: the local user's Epic Games account identity, the sibling of Connect's
// ProductUserId. Login derives a stable local EpicAccountId from the configured user and
// completes asynchronously; the interface also mints the auth and id tokens a game copies out.
// The emulator does not authenticate against a backend, so login always resolves to the local
// account and the tokens are self-issued.
// Spec: EOSSDK_Auth (docs/auth.md)
class sdk_auth : public i_run_callback {
public:
    sdk_auth(sdk_settings& settings, callback_manager& callbacks);
    ~sdk_auth();

    sdk_auth(const sdk_auth&) = delete;
    sdk_auth& operator=(const sdk_auth&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Flat API surface (called by the trampolines in flat/eos_auth_flat.cpp) ---

    void login(const EOS_Auth_LoginOptions* options, void* client_data,
               EOS_Auth_OnLoginCallback delegate);
    void logout(const EOS_Auth_LogoutOptions* options, void* client_data,
                EOS_Auth_OnLogoutCallback delegate);

    i32 logged_in_accounts_count() const;
    EOS_EpicAccountId logged_in_account_by_index(i32 index) const;
    EOS_ELoginStatus login_status(EOS_EpicAccountId local_user_id) const;
    EOS_EResult selected_account_id(EOS_EpicAccountId local_user_id, EOS_EpicAccountId* out) const;

    // Mint the local user's auth token / id token. The returned object owns its strings and must
    // be freed with the matching release_* free function below.
    EOS_EResult copy_user_auth_token(EOS_EpicAccountId local_user_id, EOS_Auth_Token** out) const;
    EOS_EResult copy_id_token(EOS_EpicAccountId account_id, EOS_Auth_IdToken** out) const;

    EOS_NotificationId add_notify_login_status_changed(
        void* client_data, EOS_Auth_OnLoginStatusChangedCallback delegate);
    void remove_notify_login_status_changed(EOS_NotificationId id);

    // Queue a stubbed async completion reporting EOS_NotImplemented, for methods not yet built.
    // `info_size` is the size of the specific EOS_*CallbackInfo the delegate expects; they all
    // begin with { EOS_EResult ResultCode; void* ClientData; }, which is all we fill.
    void queue_stub_result(void* client_data, completion_delegate delegate, std::size_t info_size);

    // --- i_run_callback ---
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

private:
    struct status_transition {
        EOS_EpicAccountId user;
        EOS_ELoginStatus previous;
        EOS_ELoginStatus current;
    };

    bool is_logged_in() const { return !accounts_.empty(); }
    EOS_EpicAccountId local_account() const;

    sdk_settings& settings_;
    callback_manager& callbacks_;

    // Logged-in Epic accounts, self at index 0. Empty means not logged in.
    std::vector<EOS_EpicAccountId> accounts_;
    // Every account that has logged in during this session, retained across logout so we can tell
    // an unknown account (never seen) apart from a known one that is currently logged out.
    std::set<EOS_EpicAccountId> known_accounts_;
    std::vector<status_transition> pending_status_changes_;
    bool registered_;
};

// Free an auth / id token minted by the interface. Declared here so the handle-free flat
// release trampolines can call them; they own the whole heap holder behind the token pointer.
void release_auth_token(EOS_Auth_Token* token);
void release_id_token(EOS_Auth_IdToken* token);

} // namespace eosr

#endif
