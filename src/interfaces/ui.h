#ifndef EOSR_INTERFACES_UI_H
#define EOSR_INTERFACES_UI_H

#include <vector>

#include "eos_common.h"
#include "eos_ui_types.h"

#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"

namespace eosr {

class sdk_settings;
class callback_manager;

// The UI interface: Epic's social overlay.
//
// The overlay is two things -- a renderer drawn over the game, and the place a player clicks "join".
// We supply neither, and we are honest about it: nothing is ever shown, so GetFriendsVisible is
// always false and we never take exclusive input. A game that opens the overlay and then waits for
// it to close would otherwise wait for something that is never going to happen.
//
// What we do supply is the *surface*. A game resolves every EOS_UI_* symbol it imports when the
// library loads, and one it cannot find kills the process before a line of the SDK runs -- so an
// export we do not have is not a missing feature, it is a game that never starts. The settings a
// game writes here (toggle key, notification corner, paused state) are read back exactly as written,
// because a game that sets one and cannot read it back has reason to think the SDK is broken.
//
// The join click is the part that actually matters for multiplayer, and it is not tied to a
// renderer: the plan is an external companion that sends the click over a local channel and lets the
// SDK fire the ordinary notification on the game's own tick. Until that lands, the three accepted
// notifications register and never fire, and AcknowledgeEventId knows no events.
// Spec: EOSSDK_UI (docs/integratedplatform-ui-overlay.md), docs/companion-client.md
class sdk_ui : public i_run_callback {
public:
    sdk_ui(sdk_settings& settings, callback_manager& callbacks);
    ~sdk_ui();

    sdk_ui(const sdk_ui&) = delete;
    sdk_ui& operator=(const sdk_ui&) = delete;

    void emu_init();
    void emu_deinit();

    // These two are handle-free -- the game may ask them before it has a platform -- so they are
    // static, exactly as their exports are.
    //
    // A toggle key is one key from a small set with at least one modifier; the header names both
    // sets. A button combination is any subset of the sixteen gamepad-button flags.
    static bool key_combination_valid(EOS_UI_EKeyCombination combination);
    static bool button_combination_valid(EOS_UI_EInputStateButtonFlags combination);

    // --- Flat API surface (called by the trampolines in flat/eos_ui_flat.cpp) ---

    void show_friends(const EOS_UI_ShowFriendsOptions* options, void* client_data,
                      EOS_UI_OnShowFriendsCallback delegate);
    void hide_friends(const EOS_UI_HideFriendsOptions* options, void* client_data,
                      EOS_UI_OnHideFriendsCallback delegate);
    EOS_Bool friends_visible(const EOS_UI_GetFriendsVisibleOptions* options) const;
    EOS_Bool friends_exclusive_input(const EOS_UI_GetFriendsExclusiveInputOptions* options) const;

    void show_block_player(const EOS_UI_ShowBlockPlayerOptions* options, void* client_data,
                           EOS_UI_OnShowBlockPlayerCallback delegate);
    void show_report_player(const EOS_UI_ShowReportPlayerOptions* options, void* client_data,
                            EOS_UI_OnShowReportPlayerCallback delegate);
    void show_native_profile(const EOS_UI_ShowNativeProfileOptions* options, void* client_data,
                             EOS_UI_OnShowNativeProfileCallback delegate);

    EOS_EResult set_toggle_friends_key(const EOS_UI_SetToggleFriendsKeyOptions* options);
    EOS_UI_EKeyCombination toggle_friends_key(const EOS_UI_GetToggleFriendsKeyOptions* options) const;
    EOS_EResult set_toggle_friends_button(const EOS_UI_SetToggleFriendsButtonOptions* options);
    EOS_UI_EInputStateButtonFlags toggle_friends_button(
        const EOS_UI_GetToggleFriendsButtonOptions* options) const;

    EOS_EResult set_display_preference(const EOS_UI_SetDisplayPreferenceOptions* options);
    EOS_UI_ENotificationLocation notification_location() const { return notification_location_; }

    EOS_EResult acknowledge_event_id(const EOS_UI_AcknowledgeEventIdOptions* options);
    EOS_EResult report_input_state(const EOS_UI_ReportInputStateOptions* options);
    EOS_EResult pre_present(const EOS_UI_PrePresentOptions* options);

    EOS_EResult pause_social_overlay(const EOS_UI_PauseSocialOverlayOptions* options);
    EOS_Bool social_overlay_paused(const EOS_UI_IsSocialOverlayPausedOptions* options) const;

    EOS_EResult configure_on_screen_keyboard(const EOS_UI_ConfigureOnScreenKeyboardOptions* options);

    EOS_NotificationId add_notify_display_settings_updated(
        const EOS_UI_AddNotifyDisplaySettingsUpdatedOptions* options, void* client_data,
        EOS_UI_OnDisplaySettingsUpdatedCallback delegate);
    EOS_NotificationId add_notify_memory_monitor(
        const EOS_UI_AddNotifyMemoryMonitorOptions* options, void* client_data,
        EOS_UI_OnMemoryMonitorCallback delegate);
    EOS_NotificationId add_notify_on_screen_keyboard_requested(
        const EOS_UI_AddNotifyOnScreenKeyboardRequestedOptions* options, void* client_data,
        EOS_UI_OnScreenKeyboardRequestedCallback delegate);
    void remove_notify(EOS_NotificationId id);

    // i_run_callback
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

private:
    // Queue an async completion that carries a result code and up to two account ids, which is the
    // shape every one of this interface's callbacks has.
    void deliver(callback_type_id type, std::size_t info_size, completion_delegate delegate,
                 void* client_data, EOS_EResult result_code, EOS_EpicAccountId local_user,
                 EOS_EpicAccountId target_user);

    sdk_settings& settings_;
    callback_manager& callbacks_;

    EOS_UI_ENotificationLocation notification_location_;
    EOS_UI_EKeyCombination toggle_friends_key_;
    EOS_UI_EInputStateButtonFlags toggle_friends_button_;
    EOS_UI_EOnScreenKeyboardBehavior keyboard_behavior_;
    bool keyboard_device_checks_;
    bool social_overlay_paused_;
    bool registered_;

    // Notifications registered but not yet handed their promised initial call. The header says a new
    // handler is called on the next tick with the current state; we owe that call to the notification
    // itself, not as an independent completion -- so if the game removes the notification before the
    // tick, the owed call is cancelled with it rather than firing through freed client data.
    std::vector<EOS_NotificationId> pending_initial_delivery_;
};

} // namespace eosr

#endif
