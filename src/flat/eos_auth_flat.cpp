// Flat C ABI trampolines for the Auth interface. Each validates the handle against the live
// platform's Auth object, then calls through. Deferred methods report EOS_NotImplemented (or an
// empty result). The token-release helpers actually free the objects the copy calls hand out.
#include "eos_auth.h"

#include "common/eos_names.h"
#include "core/frame_result.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
#include "interfaces/auth.h"

namespace {

eosr::sdk_auth* checked_auth(EOS_HAuth handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HAuth>(platform->interface_handle(eosr::if_auth))) {
        return 0;
    }
    return reinterpret_cast<eosr::sdk_auth*>(handle);
}

template <class delegate_type>
void stub_async(EOS_HAuth handle, void* client_data, delegate_type delegate, std::size_t info_size) {
    eosr::sdk_auth* auth = checked_auth(handle);
    if (auth != 0) {
        auth->queue_stub_result(client_data, reinterpret_cast<eosr::completion_delegate>(delegate),
                                info_size);
    }
}

} // namespace

// --- Implemented ---

EOS_DECLARE_FUNC(void) EOS_Auth_Login(EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options,
                                      void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_Login",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth != 0) {
        auth->login(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Auth_Logout(EOS_HAuth Handle, const EOS_Auth_LogoutOptions* Options,
                                       void* ClientData, const EOS_Auth_OnLogoutCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_Logout",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth != 0) {
        auth->logout(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(int32_t) EOS_Auth_GetLoggedInAccountsCount(EOS_HAuth Handle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_GetLoggedInAccountsCount", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    return eosr::traced_count(eosr_trace,
                              (auth != 0) ? auth->logged_in_accounts_count() : 0);
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Auth_GetLoggedInAccountByIndex(EOS_HAuth Handle, int32_t Index) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_GetLoggedInAccountByIndex", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    const EOS_EpicAccountId result =
        (auth != 0) ? auth->logged_in_account_by_index(Index) : 0;
    return eosr::traced_handle(eosr_trace, result, eosr::label_kind::eaid);
}

EOS_DECLARE_FUNC(EOS_ELoginStatus) EOS_Auth_GetLoginStatus(EOS_HAuth Handle, EOS_EpicAccountId LocalUserId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_GetLoginStatus", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    const EOS_ELoginStatus result = (auth != 0)
                                        ? auth->login_status(LocalUserId)
                                        : EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    return eosr::traced_enum(eosr_trace, result, eosr::login_status_name(result));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_GetSelectedAccountId(EOS_HAuth Handle, const EOS_EpicAccountId LocalUserId,
                                                           EOS_EpicAccountId* OutSelectedAccountId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_GetSelectedAccountId", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth == 0) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    return eosr::traced_result(
        eosr_trace, auth->selected_account_id(LocalUserId, OutSelectedAccountId));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_CopyUserAuthToken(EOS_HAuth Handle, const EOS_Auth_CopyUserAuthTokenOptions* Options,
                                                        EOS_EpicAccountId LocalUserId, EOS_Auth_Token** OutUserAuthToken) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_CopyUserAuthToken",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth == 0 || OutUserAuthToken == 0 || Options == 0) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    if (Options->ApiVersion != EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_VersionMismatch);
    }
    return eosr::traced_result(
        eosr_trace, auth->copy_user_auth_token(LocalUserId, OutUserAuthToken));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_CopyIdToken(EOS_HAuth Handle, const EOS_Auth_CopyIdTokenOptions* Options,
                                                   EOS_Auth_IdToken** OutIdToken) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_CopyIdToken",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth == 0 || OutIdToken == 0 || Options == 0) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    if (Options->ApiVersion != EOS_AUTH_COPYIDTOKEN_API_LATEST) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_VersionMismatch);
    }
    return eosr::traced_result(eosr_trace, auth->copy_id_token(Options->AccountId, OutIdToken));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Auth_AddNotifyLoginStatusChanged(
    EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options,
    void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback Notification) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Auth_AddNotifyLoginStatusChanged",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth == 0 || Options == 0 ||
        Options->ApiVersion != EOS_AUTH_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST) {
        return eosr::traced_notification(eosr_trace, EOS_INVALID_NOTIFICATIONID);
    }
    return eosr::traced_notification(
        eosr_trace, auth->add_notify_login_status_changed(ClientData, Notification));
}

EOS_DECLARE_FUNC(void) EOS_Auth_RemoveNotifyLoginStatusChanged(EOS_HAuth Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Auth_RemoveNotifyLoginStatusChanged", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_auth* auth = checked_auth(Handle);
    if (auth != 0) {
        auth->remove_notify_login_status_changed(InId);
    }
}

// --- Token release: free the heap objects the copy calls handed out ---

EOS_DECLARE_FUNC(void) EOS_Auth_Token_Release(EOS_Auth_Token* AuthToken) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_Token_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_auth_token(AuthToken);
}

EOS_DECLARE_FUNC(void) EOS_Auth_IdToken_Release(EOS_Auth_IdToken* IdToken) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_IdToken_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_id_token(IdToken);
}

// --- Deferred: async methods report NotImplemented so a caller never hangs ---

EOS_DECLARE_FUNC(void) EOS_Auth_LinkAccount(EOS_HAuth Handle, const EOS_Auth_LinkAccountOptions* Options,
                                            void* ClientData, const EOS_Auth_OnLinkAccountCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_LinkAccount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Auth_LinkAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Auth_DeletePersistentAuth(EOS_HAuth Handle, const EOS_Auth_DeletePersistentAuthOptions* Options,
                                                     void* ClientData, const EOS_Auth_OnDeletePersistentAuthCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_DeletePersistentAuth",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Auth_DeletePersistentAuthCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Auth_VerifyUserAuth(EOS_HAuth Handle, const EOS_Auth_VerifyUserAuthOptions* Options,
                                               void* ClientData, const EOS_Auth_OnVerifyUserAuthCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_VerifyUserAuth",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Auth_VerifyUserAuthCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Auth_QueryIdToken(EOS_HAuth Handle, const EOS_Auth_QueryIdTokenOptions* Options,
                                             void* ClientData, const EOS_Auth_OnQueryIdTokenCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_QueryIdToken",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Auth_QueryIdTokenCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Auth_VerifyIdToken(EOS_HAuth Handle, const EOS_Auth_VerifyIdTokenOptions* Options,
                                              void* ClientData, const EOS_Auth_OnVerifyIdTokenCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_VerifyIdToken",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Auth_VerifyIdTokenCallbackInfo));
}

// --- Deferred: synchronous merged-account queries report empty ---

EOS_DECLARE_FUNC(uint32_t) EOS_Auth_GetMergedAccountsCount(EOS_HAuth Handle, const EOS_EpicAccountId LocalUserId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_GetMergedAccountsCount", 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    (void)LocalUserId;
    return eosr::traced_count(eosr_trace, static_cast<uint32_t>(0));
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Auth_GetMergedAccountByIndex(EOS_HAuth Handle, const EOS_EpicAccountId LocalUserId,
                                                                    const uint32_t Index) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Auth_GetMergedAccountByIndex", 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    (void)LocalUserId;
    (void)Index;
    return eosr::traced_handle(eosr_trace, static_cast<EOS_EpicAccountId>(0),
                               eosr::label_kind::eaid);
}
