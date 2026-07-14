// Flat C ABI trampolines for the UI interface. Thin: null-check the handle, cast, call.
//
// A game resolves every EOS_UI_* symbol it imports when the library loads. One it cannot find kills
// the process in the loader, before the SDK has run a line -- so these exports have to exist even
// though there is no overlay behind them.
#include "eos_ui.h"

#include "core/platform.h"
#include "core/runtime.h"
#include "core/tracer.h"
#include "interfaces/ui.h"

namespace {

eosr::sdk_ui* checked_ui(EOS_HUI handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HUI>(platform->interface_handle(eosr::if_ui))) {
        return 0;
    }
    return &platform->ui();
}

const char* notification_location_name(EOS_UI_ENotificationLocation location) {
    switch (location) {
        case EOS_UI_ENotificationLocation::EOS_UNL_TopLeft: return "EOS_UNL_TopLeft";
        case EOS_UI_ENotificationLocation::EOS_UNL_TopRight: return "EOS_UNL_TopRight";
        case EOS_UI_ENotificationLocation::EOS_UNL_BottomLeft: return "EOS_UNL_BottomLeft";
        case EOS_UI_ENotificationLocation::EOS_UNL_BottomRight: return "EOS_UNL_BottomRight";
    }
    return "EOS_UNL_BottomRight";
}

} // namespace

EOS_DECLARE_FUNC(void) EOS_UI_ShowFriends(EOS_HUI Handle, const EOS_UI_ShowFriendsOptions* Options,
                                          void* ClientData,
                                          const EOS_UI_OnShowFriendsCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_ShowFriends",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->show_friends(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_UI_HideFriends(EOS_HUI Handle, const EOS_UI_HideFriendsOptions* Options,
                                          void* ClientData,
                                          const EOS_UI_OnHideFriendsCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_HideFriends",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->hide_friends(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_GetFriendsVisible(EOS_HUI Handle,
                                                    const EOS_UI_GetFriendsVisibleOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_GetFriendsVisible",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    return eosr::traced_bool(
        eosr_trace, (ui != 0) ? ui->friends_visible(Options) : EOS_FALSE);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_GetFriendsExclusiveInput(
    EOS_HUI Handle, const EOS_UI_GetFriendsExclusiveInputOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_GetFriendsExclusiveInput",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    return eosr::traced_bool(
        eosr_trace, (ui != 0) ? ui->friends_exclusive_input(Options) : EOS_FALSE);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyDisplaySettingsUpdated(
    EOS_HUI Handle, const EOS_UI_AddNotifyDisplaySettingsUpdatedOptions* Options, void* ClientData,
    const EOS_UI_OnDisplaySettingsUpdatedCallback NotificationFn) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_AddNotifyDisplaySettingsUpdated",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_NotificationId result =
        (ui != 0)
            ? ui->add_notify_display_settings_updated(Options, ClientData, NotificationFn)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyDisplaySettingsUpdated(EOS_HUI Handle,
                                                                 EOS_NotificationId Id) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_RemoveNotifyDisplaySettingsUpdated", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->remove_notify(Id);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetToggleFriendsKey(
    EOS_HUI Handle, const EOS_UI_SetToggleFriendsKeyOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_SetToggleFriendsKey",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->set_toggle_friends_key(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_UI_EKeyCombination) EOS_UI_GetToggleFriendsKey(
    EOS_HUI Handle, const EOS_UI_GetToggleFriendsKeyOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_GetToggleFriendsKey",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_UI_EKeyCombination result =
        (ui != 0) ? ui->toggle_friends_key(Options)
                  : EOS_UI_EKeyCombination::EOS_UIK_None;
    return eosr::traced_flags(eosr_trace, result);
}

// The handle is in the signature but the answer does not depend on it: whether a key combination is
// well formed is a property of the combination, not of any platform.
EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsValidKeyCombination(EOS_HUI Handle,
                                                        EOS_UI_EKeyCombination KeyCombination) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_IsValidKeyCombination", 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    const EOS_Bool result =
        eosr::sdk_ui::key_combination_valid(KeyCombination) ? EOS_TRUE : EOS_FALSE;
    return eosr::traced_bool(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetToggleFriendsButton(
    EOS_HUI Handle, const EOS_UI_SetToggleFriendsButtonOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_SetToggleFriendsButton",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->set_toggle_friends_button(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_UI_EInputStateButtonFlags) EOS_UI_GetToggleFriendsButton(
    EOS_HUI Handle, const EOS_UI_GetToggleFriendsButtonOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_GetToggleFriendsButton",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_UI_EInputStateButtonFlags result =
        (ui != 0) ? ui->toggle_friends_button(Options)
                  : EOS_UI_EInputStateButtonFlags::EOS_UISBF_None;
    return eosr::traced_flags(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsValidButtonCombination(
    EOS_HUI Handle, EOS_UI_EInputStateButtonFlags ButtonCombination) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_IsValidButtonCombination", 0,
                                 eosr::call_mode::sync);
    (void)Handle;
    const EOS_Bool result =
        eosr::sdk_ui::button_combination_valid(ButtonCombination) ? EOS_TRUE : EOS_FALSE;
    return eosr::traced_bool(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetDisplayPreference(
    EOS_HUI Handle, const EOS_UI_SetDisplayPreferenceOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_SetDisplayPreference",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->set_display_preference(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_UI_ENotificationLocation) EOS_UI_GetNotificationLocationPreference(
    EOS_HUI Handle) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_GetNotificationLocationPreference", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_UI_ENotificationLocation result =
        (ui != 0) ? ui->notification_location()
                  : EOS_UI_ENotificationLocation::EOS_UNL_BottomRight;
    return eosr::traced_enum(eosr_trace, result, notification_location_name(result));
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_AcknowledgeEventId(
    EOS_HUI Handle, const EOS_UI_AcknowledgeEventIdOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_AcknowledgeEventId",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->acknowledge_event_id(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_ReportInputState(
    EOS_HUI Handle, const EOS_UI_ReportInputStateOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_ReportInputState",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->report_input_state(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_PrePresent(EOS_HUI Handle,
                                                const EOS_UI_PrePresentOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_PrePresent",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->pre_present(Options) : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowBlockPlayer(
    EOS_HUI Handle, const EOS_UI_ShowBlockPlayerOptions* Options, void* ClientData,
    const EOS_UI_OnShowBlockPlayerCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_ShowBlockPlayer",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->show_block_player(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowReportPlayer(
    EOS_HUI Handle, const EOS_UI_ShowReportPlayerOptions* Options, void* ClientData,
    const EOS_UI_OnShowReportPlayerCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_ShowReportPlayer",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->show_report_player(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_PauseSocialOverlay(
    EOS_HUI Handle, const EOS_UI_PauseSocialOverlayOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_PauseSocialOverlay",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->pause_social_overlay(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsSocialOverlayPaused(
    EOS_HUI Handle, const EOS_UI_IsSocialOverlayPausedOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_IsSocialOverlayPaused",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    return eosr::traced_bool(
        eosr_trace, (ui != 0) ? ui->social_overlay_paused(Options) : EOS_FALSE);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyMemoryMonitor(
    EOS_HUI Handle, const EOS_UI_AddNotifyMemoryMonitorOptions* Options, void* ClientData,
    const EOS_UI_OnMemoryMonitorCallback NotificationFn) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_AddNotifyMemoryMonitor",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_NotificationId result =
        (ui != 0) ? ui->add_notify_memory_monitor(Options, ClientData, NotificationFn)
                  : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyMemoryMonitor(EOS_HUI Handle, EOS_NotificationId Id) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_RemoveNotifyMemoryMonitor", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->remove_notify(Id);
    }
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowNativeProfile(
    EOS_HUI Handle, const EOS_UI_ShowNativeProfileOptions* Options, void* ClientData,
    const EOS_UI_OnShowNativeProfileCallback CompletionDelegate) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_UI_ShowNativeProfile",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::async);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->show_native_profile(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_ConfigureOnScreenKeyboard(
    EOS_HUI Handle, const EOS_UI_ConfigureOnScreenKeyboardOptions* Options) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_ConfigureOnScreenKeyboard",
                                 Options != 0 ? Options->ApiVersion : 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_EResult result =
        (ui != 0) ? ui->configure_on_screen_keyboard(Options)
                  : EOS_EResult::EOS_InvalidParameters;
    return eosr::traced_result(eosr_trace, result);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyOnScreenKeyboardRequested(
    EOS_HUI Handle, const EOS_UI_AddNotifyOnScreenKeyboardRequestedOptions* Options,
    void* ClientData, const EOS_UI_OnScreenKeyboardRequestedCallback NotificationFn) {
    eosr::trace_scope eosr_trace(
        eosr::global_tracer(), "EOS_UI_AddNotifyOnScreenKeyboardRequested",
        Options != 0 ? Options->ApiVersion : 0, eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    const EOS_NotificationId result =
        (ui != 0)
            ? ui->add_notify_on_screen_keyboard_requested(Options, ClientData, NotificationFn)
            : EOS_INVALID_NOTIFICATIONID;
    return eosr::traced_notification(eosr_trace, result);
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyOnScreenKeyboardRequested(EOS_HUI Handle,
                                                                    EOS_NotificationId Id) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(),
                                 "EOS_UI_RemoveNotifyOnScreenKeyboardRequested", 0,
                                 eosr::call_mode::sync);
    eosr::sdk_ui* ui = checked_ui(Handle);
    if (ui != 0) {
        ui->remove_notify(Id);
    }
}
