// Flat C ABI trampolines for the UI interface. Thin: null-check the handle, cast, call.
//
// A game resolves every EOS_UI_* symbol it imports when the library loads. One it cannot find kills
// the process in the loader, before the SDK has run a line -- so these exports have to exist even
// though there is no overlay behind them.
#include "eos_ui.h"

#include "core/platform.h"
#include "interfaces/ui.h"

namespace {

eosr::sdk_ui* ui_of(EOS_HUI handle) {
    return reinterpret_cast<eosr::sdk_ui*>(handle);
}

} // namespace

EOS_DECLARE_FUNC(void) EOS_UI_ShowFriends(EOS_HUI Handle, const EOS_UI_ShowFriendsOptions* Options,
                                          void* ClientData,
                                          const EOS_UI_OnShowFriendsCallback CompletionDelegate) {
    if (Handle != 0) {
        ui_of(Handle)->show_friends(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_UI_HideFriends(EOS_HUI Handle, const EOS_UI_HideFriendsOptions* Options,
                                          void* ClientData,
                                          const EOS_UI_OnHideFriendsCallback CompletionDelegate) {
    if (Handle != 0) {
        ui_of(Handle)->hide_friends(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_GetFriendsVisible(EOS_HUI Handle,
                                                    const EOS_UI_GetFriendsVisibleOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->friends_visible(Options) : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_GetFriendsExclusiveInput(
    EOS_HUI Handle, const EOS_UI_GetFriendsExclusiveInputOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->friends_exclusive_input(Options) : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyDisplaySettingsUpdated(
    EOS_HUI Handle, const EOS_UI_AddNotifyDisplaySettingsUpdatedOptions* Options, void* ClientData,
    const EOS_UI_OnDisplaySettingsUpdatedCallback NotificationFn) {
    return (Handle != 0)
               ? ui_of(Handle)->add_notify_display_settings_updated(Options, ClientData, NotificationFn)
               : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyDisplaySettingsUpdated(EOS_HUI Handle,
                                                                 EOS_NotificationId Id) {
    if (Handle != 0) {
        ui_of(Handle)->remove_notify(Id);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetToggleFriendsKey(
    EOS_HUI Handle, const EOS_UI_SetToggleFriendsKeyOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->set_toggle_friends_key(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_UI_EKeyCombination) EOS_UI_GetToggleFriendsKey(
    EOS_HUI Handle, const EOS_UI_GetToggleFriendsKeyOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->toggle_friends_key(Options)
                         : EOS_UI_EKeyCombination::EOS_UIK_None;
}

// The handle is in the signature but the answer does not depend on it: whether a key combination is
// well formed is a property of the combination, not of any platform.
EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsValidKeyCombination(EOS_HUI Handle,
                                                        EOS_UI_EKeyCombination KeyCombination) {
    (void)Handle;
    return eosr::sdk_ui::key_combination_valid(KeyCombination) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetToggleFriendsButton(
    EOS_HUI Handle, const EOS_UI_SetToggleFriendsButtonOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->set_toggle_friends_button(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_UI_EInputStateButtonFlags) EOS_UI_GetToggleFriendsButton(
    EOS_HUI Handle, const EOS_UI_GetToggleFriendsButtonOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->toggle_friends_button(Options)
                         : EOS_UI_EInputStateButtonFlags::EOS_UISBF_None;
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsValidButtonCombination(
    EOS_HUI Handle, EOS_UI_EInputStateButtonFlags ButtonCombination) {
    (void)Handle;
    return eosr::sdk_ui::button_combination_valid(ButtonCombination) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetDisplayPreference(
    EOS_HUI Handle, const EOS_UI_SetDisplayPreferenceOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->set_display_preference(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_UI_ENotificationLocation) EOS_UI_GetNotificationLocationPreference(
    EOS_HUI Handle) {
    return (Handle != 0) ? ui_of(Handle)->notification_location()
                         : EOS_UI_ENotificationLocation::EOS_UNL_BottomRight;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_AcknowledgeEventId(
    EOS_HUI Handle, const EOS_UI_AcknowledgeEventIdOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->acknowledge_event_id(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_ReportInputState(
    EOS_HUI Handle, const EOS_UI_ReportInputStateOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->report_input_state(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_PrePresent(EOS_HUI Handle,
                                                const EOS_UI_PrePresentOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->pre_present(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowBlockPlayer(
    EOS_HUI Handle, const EOS_UI_ShowBlockPlayerOptions* Options, void* ClientData,
    const EOS_UI_OnShowBlockPlayerCallback CompletionDelegate) {
    if (Handle != 0) {
        ui_of(Handle)->show_block_player(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowReportPlayer(
    EOS_HUI Handle, const EOS_UI_ShowReportPlayerOptions* Options, void* ClientData,
    const EOS_UI_OnShowReportPlayerCallback CompletionDelegate) {
    if (Handle != 0) {
        ui_of(Handle)->show_report_player(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_PauseSocialOverlay(
    EOS_HUI Handle, const EOS_UI_PauseSocialOverlayOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->pause_social_overlay(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsSocialOverlayPaused(
    EOS_HUI Handle, const EOS_UI_IsSocialOverlayPausedOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->social_overlay_paused(Options) : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyMemoryMonitor(
    EOS_HUI Handle, const EOS_UI_AddNotifyMemoryMonitorOptions* Options, void* ClientData,
    const EOS_UI_OnMemoryMonitorCallback NotificationFn) {
    return (Handle != 0) ? ui_of(Handle)->add_notify_memory_monitor(Options, ClientData, NotificationFn)
                         : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyMemoryMonitor(EOS_HUI Handle, EOS_NotificationId Id) {
    if (Handle != 0) {
        ui_of(Handle)->remove_notify(Id);
    }
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowNativeProfile(
    EOS_HUI Handle, const EOS_UI_ShowNativeProfileOptions* Options, void* ClientData,
    const EOS_UI_OnShowNativeProfileCallback CompletionDelegate) {
    if (Handle != 0) {
        ui_of(Handle)->show_native_profile(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_ConfigureOnScreenKeyboard(
    EOS_HUI Handle, const EOS_UI_ConfigureOnScreenKeyboardOptions* Options) {
    return (Handle != 0) ? ui_of(Handle)->configure_on_screen_keyboard(Options)
                         : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyOnScreenKeyboardRequested(
    EOS_HUI Handle, const EOS_UI_AddNotifyOnScreenKeyboardRequestedOptions* Options,
    void* ClientData, const EOS_UI_OnScreenKeyboardRequestedCallback NotificationFn) {
    return (Handle != 0)
               ? ui_of(Handle)->add_notify_on_screen_keyboard_requested(Options, ClientData,
                                                                        NotificationFn)
               : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyOnScreenKeyboardRequested(EOS_HUI Handle,
                                                                    EOS_NotificationId Id) {
    if (Handle != 0) {
        ui_of(Handle)->remove_notify(Id);
    }
}
