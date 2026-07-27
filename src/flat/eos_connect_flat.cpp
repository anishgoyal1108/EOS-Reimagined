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
    // The trace scope opens at the ABI boundary, ahead of every check: a call rejected for a null,
    // stale, or foreign handle is exactly what an in-game probe must be able to explain, so it is
    // recorded like any other. The scope's destructor pairs it with a return on whichever way we exit.
    // It also holds the correlation id for the duration, so the result queued inside login() inherits
    // it and the callback firing a tick later stitches back to here. Only the *kind* of credential is
    // ever recorded -- never the token.
    eosr::tracer& trace = eosr::global_tracer();
    std::vector<eosr::trace_field> args;
    if (trace.enabled() && Options != 0 && Options->Credentials != 0) {
        args.push_back(eosr::make_field(
            eosr::field_id::cred_type,
            eosr::tv_enum(eosr::credential_type_name(Options->Credentials->Type))));
    }
    const i32 api = (Options != 0) ? Options->ApiVersion : 0;
    eosr::trace_scope scope(trace, "EOS_Connect_Login", api, args, eosr::call_mode::async);

    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0) {
        return;
    }
    connect->login(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Connect_Logout(EOS_HConnect Handle, const EOS_Connect_LogoutOptions* Options,
                                          void* ClientData, const EOS_Connect_OnLogoutCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_Logout",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->logout(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_Connect_QueryProductUserIdMappings(
    EOS_HConnect Handle, const EOS_Connect_QueryProductUserIdMappingsOptions* Options,
    void* ClientData, const EOS_Connect_OnQueryProductUserIdMappingsCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_QueryProductUserIdMappings",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->query_product_user_id_mappings(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_GetProductUserIdMapping(
    EOS_HConnect Handle, const EOS_Connect_GetProductUserIdMappingOptions* Options,
    char* OutBuffer, int32_t* InOutBufferLength) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_GetProductUserIdMapping",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_InvalidParameters);
    }
    return eosr::traced_result(
        eosr_trace, connect->get_product_user_id_mapping(Options, OutBuffer, InOutBufferLength));
}

EOS_DECLARE_FUNC(int32_t) EOS_Connect_GetLoggedInUsersCount(EOS_HConnect Handle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_GetLoggedInUsersCount", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    return eosr::traced_count(eosr_trace,
                              (connect != 0) ? connect->logged_in_users_count() : 0);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetLoggedInUserByIndex(EOS_HConnect Handle, int32_t Index) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_GetLoggedInUserByIndex", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    const EOS_ProductUserId result =
        (connect != 0) ? connect->logged_in_user_by_index(Index) : 0;
    return eosr::traced_handle(eosr_trace, result, eosr::label_kind::puid);
}

EOS_DECLARE_FUNC(EOS_ELoginStatus) EOS_Connect_GetLoginStatus(EOS_HConnect Handle, EOS_ProductUserId LocalUserId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_GetLoginStatus", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    const EOS_ELoginStatus result = (connect != 0)
                                        ? connect->login_status(LocalUserId)
                                        : EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    return eosr::traced_enum(eosr_trace, result, eosr::login_status_name(result));
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyLoginStatusChanged(
    EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options,
    void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback Notification) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_AddNotifyLoginStatusChanged",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    const EOS_NotificationId result =
        (connect != 0) ? connect->add_notify_login_status_changed(ClientData, Notification)
                       : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Connect_RemoveNotifyLoginStatusChanged(EOS_HConnect Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_RemoveNotifyLoginStatusChanged", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->remove_notify_login_status_changed(InId);
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyAuthExpiration(
    EOS_HConnect Handle, const EOS_Connect_AddNotifyAuthExpirationOptions* Options,
    void* ClientData, const EOS_Connect_OnAuthExpirationCallback Notification) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_AddNotifyAuthExpiration",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    const EOS_NotificationId result =
        (connect != 0) ? connect->add_notify_auth_expiration(ClientData, Notification)
                       : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_Connect_RemoveNotifyAuthExpiration(EOS_HConnect Handle, EOS_NotificationId InId) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_RemoveNotifyAuthExpiration",
                                 0, eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect != 0) {
        connect->remove_notify_auth_expiration(InId);
    }
}

// --- Deferred: async methods report NotImplemented so a caller never hangs ---

EOS_DECLARE_FUNC(void) EOS_Connect_CreateUser(EOS_HConnect Handle, const EOS_Connect_CreateUserOptions* Options,
                                              void* ClientData, const EOS_Connect_OnCreateUserCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_CreateUser",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_CreateUserCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_LinkAccount(EOS_HConnect Handle, const EOS_Connect_LinkAccountOptions* Options,
                                               void* ClientData, const EOS_Connect_OnLinkAccountCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_LinkAccount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_LinkAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_UnlinkAccount(EOS_HConnect Handle, const EOS_Connect_UnlinkAccountOptions* Options,
                                                 void* ClientData, const EOS_Connect_OnUnlinkAccountCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_UnlinkAccount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_UnlinkAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_CreateDeviceId(EOS_HConnect Handle, const EOS_Connect_CreateDeviceIdOptions* Options,
                                                  void* ClientData, const EOS_Connect_OnCreateDeviceIdCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_CreateDeviceId",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_CreateDeviceIdCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_DeleteDeviceId(EOS_HConnect Handle, const EOS_Connect_DeleteDeviceIdOptions* Options,
                                                  void* ClientData, const EOS_Connect_OnDeleteDeviceIdCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_DeleteDeviceId",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_DeleteDeviceIdCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_TransferDeviceIdAccount(EOS_HConnect Handle, const EOS_Connect_TransferDeviceIdAccountOptions* Options,
                                                           void* ClientData, const EOS_Connect_OnTransferDeviceIdAccountCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_TransferDeviceIdAccount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_TransferDeviceIdAccountCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_QueryExternalAccountMappings(EOS_HConnect Handle, const EOS_Connect_QueryExternalAccountMappingsOptions* Options,
                                                                void* ClientData, const EOS_Connect_OnQueryExternalAccountMappingsCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_QueryExternalAccountMappings",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_QueryExternalAccountMappingsCallbackInfo));
}

EOS_DECLARE_FUNC(void) EOS_Connect_VerifyIdToken(EOS_HConnect Handle, const EOS_Connect_VerifyIdTokenOptions* Options,
                                                 void* ClientData, const EOS_Connect_OnVerifyIdTokenCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_VerifyIdToken",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    stub_async(Handle, ClientData, CompletionDelegate, sizeof(EOS_Connect_VerifyIdTokenCallbackInfo));
}

// --- Deferred: synchronous methods report empty/NotFound ---

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetExternalAccountMapping(
    EOS_HConnect Handle, const EOS_Connect_GetExternalAccountMappingsOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_GetExternalAccountMapping",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    const EOS_ProductUserId result =
        connect != 0 ? connect->external_account_mapping(Options) : 0;
    return eosr::traced_handle(eosr_trace, result, eosr::label_kind::puid);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Connect_GetProductUserExternalAccountCount(
    EOS_HConnect Handle, const EOS_Connect_GetProductUserExternalAccountCountOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_GetProductUserExternalAccountCount",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    return eosr::traced_count(
        eosr_trace, connect != 0 ? connect->product_user_external_account_count(Options) : 0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByIndex(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByIndexOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_CopyProductUserExternalAccountByIndex",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0 && OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    const EOS_EResult result = connect != 0 ?
        connect->copy_product_user_external_account_by_index(Options, OutExternalAccountInfo) :
        EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByAccountType(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_Connect_CopyProductUserExternalAccountByAccountType",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0 && OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    const EOS_EResult result = connect != 0 ?
        connect->copy_product_user_external_account_by_type(Options, OutExternalAccountInfo) :
        EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByAccountId(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByAccountIdOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_Connect_CopyProductUserExternalAccountByAccountId",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0 && OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    const EOS_EResult result = connect != 0 ?
        connect->copy_product_user_external_account_by_id(Options, OutExternalAccountInfo) :
        EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserInfo(
    EOS_HConnect Handle, const EOS_Connect_CopyProductUserInfoOptions* Options,
    EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_CopyProductUserInfo",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_connect* connect = checked_connect(Handle);
    if (connect == 0 && OutExternalAccountInfo != 0) {
        *OutExternalAccountInfo = 0;
    }
    const EOS_EResult result = connect != 0 ?
        connect->copy_product_user_info(Options, OutExternalAccountInfo) :
        EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyIdToken(
    EOS_HConnect Handle, const EOS_Connect_CopyIdTokenOptions* Options, EOS_Connect_IdToken** OutIdToken) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_CopyIdToken",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    (void)Options;
    if (OutIdToken != 0) {
        *OutIdToken = 0;
    }
    return eosr::traced_result(eosr_trace, EOS_EResult::EOS_NotFound);
}

// --- Release helpers ---

EOS_DECLARE_FUNC(void) EOS_Connect_ExternalAccountInfo_Release(EOS_Connect_ExternalAccountInfo* ExternalAccountInfo) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_Connect_ExternalAccountInfo_Release", 0,
                                 eosr::call_mode::sync);
    eosr::release_connect_external_account_info(ExternalAccountInfo);
}

EOS_DECLARE_FUNC(void) EOS_Connect_IdToken_Release(EOS_Connect_IdToken* IdToken) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Connect_IdToken_Release", 0,
                                 eosr::call_mode::sync);
    (void)IdToken;
}
