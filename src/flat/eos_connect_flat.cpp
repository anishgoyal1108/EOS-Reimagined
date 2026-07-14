// Flat C ABI trampolines for the Connect interface. Each validates the handle against the live
// platform's Connect object, then calls through. Deferred methods report EOS_NotImplemented (or
// an empty result) so a game never hangs or dereferences an unimplemented interface.
#include "eos_connect.h"

#include <string>
#include <vector>

#include "common/eos_names.h"
#include "common/types.h"
#include "core/frame_result.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/trace_event.h"
#include "core/tracer.h"
#include "interfaces/connect.h"

namespace {

// Resolve the Connect handle to the live platform's Connect object, or null if it does not match.
eosr::sdk_connect* checked_connect(EOS_HConnect handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HConnect>(platform->interface_handle(eosr::if_connect))) {
        return 0;
    }
    return reinterpret_cast<eosr::sdk_connect*>(handle);
}

// Queue a NotImplemented completion for a deferred async method, given its callback-info size.
template <class Delegate>
void stub_async(EOS_HConnect handle, void* client_data, Delegate delegate, std::size_t info_size) {
    eosr::sdk_connect* connect = checked_connect(handle);
    if (connect != 0) {
        connect->queue_stub_result(client_data, reinterpret_cast<eosr::completion_delegate>(delegate),
                                   info_size);
    }
}

} // namespace

// --- Implemented ---

EOS_DECLARE_FUNC(void) EOS_Connect_Login(EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options,
                                         void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0) {
        return;
    }
    // An asynchronous call: it mints a correlation id and holds it for the duration, so the result
    // queued inside login() inherits it and the callback firing a tick later stitches back to here.
    // Only the *kind* of credential is recorded -- never the token.
    eosr::tracer& trace = eosr::global_tracer();
    std::vector<eosr::trace_field> args;
    if (trace.enabled() && Options != 0 && Options->Credentials != 0) {
        args.push_back(eosr::make_field(
            eosr::field_id::cred_type,
            eosr::tv_enum(eosr::credential_type_name(Options->Credentials->Type))));
    }
    const i32 api = (Options != 0) ? Options->ApiVersion : 0;
    const std::string corr = trace.begin_async_call("EOS_Connect_Login", api, args);

    connect->login(Options, ClientData, CompletionDelegate);

    trace.end_async_call("EOS_Connect_Login", corr);
}

EOS_DECLARE_FUNC(void) EOS_Connect_Logout(EOS_HConnect Handle, const EOS_Connect_LogoutOptions* Options,
                                          void* ClientData, const EOS_Connect_OnLogoutCallback CompletionDelegate) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->logout(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Connect_QueryProductUserIdMappings(
    EOS_HConnect Handle, const EOS_Connect_QueryProductUserIdMappingsOptions* Options,
    void* ClientData, const EOS_Connect_OnQueryProductUserIdMappingsCallback CompletionDelegate) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->query_product_user_id_mappings(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_GetProductUserIdMapping(
    EOS_HConnect Handle, const EOS_Connect_GetProductUserIdMappingOptions* Options,
    char* OutBuffer, int32_t* InOutBufferLength) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return connect->get_product_user_id_mapping(Options, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(int32_t) EOS_Connect_GetLoggedInUsersCount(EOS_HConnect Handle) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    return (connect != 0) ? connect->logged_in_users_count() : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetLoggedInUserByIndex(EOS_HConnect Handle, int32_t Index) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    return (connect != 0) ? connect->logged_in_user_by_index(Index) : 0;
}

EOS_DECLARE_FUNC(EOS_ELoginStatus) EOS_Connect_GetLoginStatus(EOS_HConnect Handle, EOS_ProductUserId LocalUserId) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    return (connect != 0) ? connect->login_status(LocalUserId) : EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyLoginStatusChanged(
    EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options,
    void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback Notification) {
    (void)Options;
    eosr::sdk_connect* connect = checked_connect(Handle);
    return (connect != 0) ? connect->add_notify_login_status_changed(ClientData, Notification) : 0;
}

EOS_DECLARE_FUNC(void) EOS_Connect_RemoveNotifyLoginStatusChanged(EOS_HConnect Handle, EOS_NotificationId InId) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->remove_notify_login_status_changed(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyAuthExpiration(
    EOS_HConnect Handle, const EOS_Connect_AddNotifyAuthExpirationOptions* Options,
    void* ClientData, const EOS_Connect_OnAuthExpirationCallback Notification) {
    (void)Options;
    eosr::sdk_connect* connect = checked_connect(Handle);
    return (connect != 0) ? connect->add_notify_auth_expiration(ClientData, Notification) : 0;
}

EOS_DECLARE_FUNC(void) EOS_Connect_RemoveNotifyAuthExpiration(EOS_HConnect Handle, EOS_NotificationId InId) {
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->remove_notify_auth_expiration(InId);
    }
}

// --- Deferred: async methods report NotImplemented so a caller never hangs ---

EOS_DECLARE_FUNC(void) EOS_Connect_CreateUser(EOS_HConnect Handle, const EOS_Connect_CreateUserOptions* Options,
                                              void* ClientData, const EOS_Connect_OnCreateUserCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_CreateUserCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_LinkAccount(EOS_HConnect Handle, const EOS_Connect_LinkAccountOptions* Options,
                                               void* ClientData, const EOS_Connect_OnLinkAccountCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_LinkAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_UnlinkAccount(EOS_HConnect Handle, const EOS_Connect_UnlinkAccountOptions* Options,
                                                 void* ClientData, const EOS_Connect_OnUnlinkAccountCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_UnlinkAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_CreateDeviceId(EOS_HConnect Handle, const EOS_Connect_CreateDeviceIdOptions* Options,
                                                  void* ClientData, const EOS_Connect_OnCreateDeviceIdCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_CreateDeviceIdCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_DeleteDeviceId(EOS_HConnect Handle, const EOS_Connect_DeleteDeviceIdOptions* Options,
                                                  void* ClientData, const EOS_Connect_OnDeleteDeviceIdCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_DeleteDeviceIdCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_TransferDeviceIdAccount(EOS_HConnect Handle, const EOS_Connect_TransferDeviceIdAccountOptions* Options,
                                                           void* ClientData, const EOS_Connect_OnTransferDeviceIdAccountCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_TransferDeviceIdAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_QueryExternalAccountMappings(EOS_HConnect Handle, const EOS_Connect_QueryExternalAccountMappingsOptions* Options,
                                                                void* ClientData, const EOS_Connect_OnQueryExternalAccountMappingsCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_QueryExternalAccountMappingsCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_VerifyIdToken(EOS_HConnect Handle, const EOS_Connect_VerifyIdTokenOptions* Options,
                                                 void* ClientData, const EOS_Connect_OnVerifyIdTokenCallback CompletionDelegate) {
    (void)Options;
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_VerifyIdTokenCallbackInfo));
}

// --- Deferred: synchronous methods report empty/NotFound ---

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetExternalAccountMapping(
    EOS_HConnect Handle, const EOS_Connect_GetExternalAccountMappingsOptions* Options) {
    (void)Handle;
    (void)Options;
    return 0;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Connect_GetProductUserExternalAccountCount(
    EOS_HConnect Handle, const EOS_Connect_GetProductUserExternalAccountCountOptions* Options) {
    (void)Handle;
    (void)Options;
    return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByIndex(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByIndexOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    (void)Handle;
    (void)Options;
    if (OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByAccountType(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    (void)Handle;
    (void)Options;
    if (OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByAccountId(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByAccountIdOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    (void)Handle;
    (void)Options;
    if (OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserInfo(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserInfoOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    (void)Handle;
    (void)Options;
    if (OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyIdToken(
    EOS_HConnect Handle, const EOS_Connect_CopyIdTokenOptions* Options, EOS_Connect_IdToken** OutIdToken) {
    (void)Handle;
    (void)Options;
    if (OutIdToken != 0) {
        *OutIdToken = 0;
    }
    return EOS_EResult::EOS_NotFound;
}

// --- Release helpers: we never hand out these objects, so there is nothing to free ---

EOS_DECLARE_FUNC(void) EOS_Connect_ExternalAccountInfo_Release(EOS_Connect_ExternalAccountInfo* ExternalAccountInfo) {
    (void)ExternalAccountInfo;
}

EOS_DECLARE_FUNC(void) EOS_Connect_IdToken_Release(EOS_Connect_IdToken* IdToken) {
    (void)IdToken;
}
