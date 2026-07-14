// Flat C ABI trampolines for the IntegratedPlatform interface and its options container.
//
// The container's create/release are free functions -- they take no platform handle, because a game
// builds a container *before* the platform it will create from it -- so they go through the process
// global store rather than an interface object.
#include "eos_integratedplatform.h"

#include "common/eos_names.h"
#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
#include "interfaces/integratedplatform.h"

namespace {

eosr::sdk_integrated_platform* checked_integrated(EOS_HIntegratedPlatform handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HIntegratedPlatform>(
                      platform->interface_handle(eosr::if_integratedplatform))) {
        return 0;
    }
    return &platform->integrated_platform();
}

} // namespace

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainer(
    const EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions* Options,
    EOS_HIntegratedPlatformOptionsContainer* OutIntegratedPlatformOptionsContainerHandle) {
    eosr::tracer& trace = eosr::global_tracer();
    eosr::trace_scope eosr_trace(
        trace, "EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainer",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    const EOS_EResult result = eosr::create_integrated_platform_container(
        Options, OutIntegratedPlatformOptionsContainerHandle);
    eosr::trace_return value =
        eosr::return_result(static_cast<i32>(result), eosr::result_name(result));
    if (result == EOS_EResult::EOS_Success &&
        OutIntegratedPlatformOptionsContainerHandle != 0 &&
        *OutIntegratedPlatformOptionsContainerHandle != 0) {
        value.out.push_back(eosr::make_field(
            eosr::field_id::handle,
            eosr::tv_label(trace.label_pointer(
                eosr::label_kind::handle, *OutIntegratedPlatformOptionsContainerHandle))));
    }
    eosr_trace.returns(value);
    return result;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatformOptionsContainer_Release(
    EOS_HIntegratedPlatformOptionsContainer IntegratedPlatformOptionsContainerHandle) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_IntegratedPlatformOptionsContainer_Release", 0,
        eosr::call_mode::sync);
    eosr::release_integrated_platform_container(IntegratedPlatformOptionsContainerHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatformOptionsContainer_Add(
    EOS_HIntegratedPlatformOptionsContainer Handle,
    const EOS_IntegratedPlatformOptionsContainer_AddOptions* InOptions) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_IntegratedPlatformOptionsContainer_Add",
        InOptions != 0 ? InOptions->ApiVersion : 0, eosr::call_mode::sync);
    return eosr::traced_result(eosr_trace, eosr::add_container_entry(Handle, InOptions));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserLoginStatus(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_SetUserLoginStatusOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_IntegratedPlatform_SetUserLoginStatus",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_integrated_platform* integrated = checked_integrated(Handle);
    const EOS_EResult result =
        (integrated != 0) ? integrated->set_user_login_status(Options)
                          : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_FinalizeDeferredUserLogout(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_IntegratedPlatform_FinalizeDeferredUserLogout",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_integrated_platform* integrated = checked_integrated(Handle);
    const EOS_EResult result =
        (integrated != 0) ? integrated->finalize_deferred_user_logout(Options)
                          : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_IntegratedPlatform_AddNotifyUserLoginStatusChanged(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions* Options, void* ClientData,
    const EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback CallbackFunction) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_IntegratedPlatform_AddNotifyUserLoginStatusChanged",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    eosr::sdk_integrated_platform* integrated = checked_integrated(Handle);
    const EOS_NotificationId result =
        (integrated != 0)
            ? integrated->add_notify_user_login_status_changed(
                  Options, ClientData, CallbackFunction)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_RemoveNotifyUserLoginStatusChanged(
    EOS_HIntegratedPlatform Handle, EOS_NotificationId NotificationId) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_IntegratedPlatform_RemoveNotifyUserLoginStatusChanged", 0,
        eosr::call_mode::sync);
    eosr::sdk_integrated_platform* integrated = checked_integrated(Handle);
    if (integrated != 0) {
        integrated->remove_notify_user_login_status_changed(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserPreLogoutCallback(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions* Options, void* ClientData,
    EOS_IntegratedPlatform_OnUserPreLogoutCallback CallbackFunction) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_IntegratedPlatform_SetUserPreLogoutCallback",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    eosr::sdk_integrated_platform* integrated = checked_integrated(Handle);
    const EOS_EResult result =
        (integrated != 0)
            ? integrated->set_user_pre_logout_callback(Options, ClientData, CallbackFunction)
            : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_ClearUserPreLogoutCallback(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions* Options) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_IntegratedPlatform_ClearUserPreLogoutCallback",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    eosr::sdk_integrated_platform* integrated = checked_integrated(Handle);
    if (integrated != 0) {
        integrated->clear_user_pre_logout_callback(Options);
    }
}
