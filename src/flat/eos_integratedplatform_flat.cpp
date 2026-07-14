// Flat C ABI trampolines for the IntegratedPlatform interface and its options container.
//
// The container's create/release are free functions -- they take no platform handle, because a game
// builds a container *before* the platform it will create from it -- so they go through the process
// global store rather than an interface object.
#include "eos_integratedplatform.h"

#include "core/platform.h"
#include "interfaces/integratedplatform.h"

namespace {

eosr::sdk_integrated_platform* integrated_of(EOS_HIntegratedPlatform handle) {
    return reinterpret_cast<eosr::sdk_integrated_platform*>(handle);
}

} // namespace

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainer(
    const EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions* Options,
    EOS_HIntegratedPlatformOptionsContainer* OutIntegratedPlatformOptionsContainerHandle) {
    return eosr::create_integrated_platform_container(
        Options, OutIntegratedPlatformOptionsContainerHandle);
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatformOptionsContainer_Release(
    EOS_HIntegratedPlatformOptionsContainer IntegratedPlatformOptionsContainerHandle) {
    eosr::release_integrated_platform_container(IntegratedPlatformOptionsContainerHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatformOptionsContainer_Add(
    EOS_HIntegratedPlatformOptionsContainer Handle,
    const EOS_IntegratedPlatformOptionsContainer_AddOptions* InOptions) {
    return eosr::add_container_entry(Handle, InOptions);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserLoginStatus(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_SetUserLoginStatusOptions* Options) {
    return (Handle != 0) ? integrated_of(Handle)->set_user_login_status(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_FinalizeDeferredUserLogout(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions* Options) {
    return (Handle != 0) ? integrated_of(Handle)->finalize_deferred_user_logout(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_IntegratedPlatform_AddNotifyUserLoginStatusChanged(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions* Options, void* ClientData,
    const EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback CallbackFunction) {
    return (Handle != 0) ? integrated_of(Handle)->add_notify_user_login_status_changed(
                               Options, ClientData, CallbackFunction)
                         : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_RemoveNotifyUserLoginStatusChanged(
    EOS_HIntegratedPlatform Handle, EOS_NotificationId NotificationId) {
    if (Handle != 0) {
        integrated_of(Handle)->remove_notify_user_login_status_changed(NotificationId);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserPreLogoutCallback(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions* Options, void* ClientData,
    EOS_IntegratedPlatform_OnUserPreLogoutCallback CallbackFunction) {
    return (Handle != 0) ? integrated_of(Handle)->set_user_pre_logout_callback(Options, ClientData,
                                                                              CallbackFunction)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_ClearUserPreLogoutCallback(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions* Options) {
    if (Handle != 0) {
        integrated_of(Handle)->clear_user_pre_logout_callback(Options);
    }
}
