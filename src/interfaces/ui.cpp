#include "interfaces/ui.h"

#include <memory>

#include "core/callback_manager.h"
#include "core/frame_result.h"
#include "core/settings.h"

namespace eosr {

namespace {

const callback_type_id cb_show_friends = 1;
const callback_type_id cb_hide_friends = 2;
const callback_type_id cb_block_player = 3;
const callback_type_id cb_report_player = 4;
const callback_type_id cb_native_profile = 5;
const callback_type_id cb_display_settings = 6;
const callback_type_id cb_memory_monitor = 7;
const callback_type_id cb_keyboard_requested = 8;

// The header's own default: Shift + F3.
const i32 default_toggle_key = static_cast<i32>(EOS_UI_EKeyCombination::EOS_UIK_Shift) |
                               static_cast<i32>(EOS_UI_EKeyCombination::EOS_UIK_F3);

// Every completion this interface delivers begins with these two fields, and three of them add the
// same pair of account ids after. Writing them through one view keeps the five callbacks from
// becoming five near-identical functions.
struct show_completion {
    EOS_EResult result_code;
    void* client_data;
    EOS_EpicAccountId local_user_id;
    EOS_EpicAccountId target_user_id;
};

bool version_ok(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

// The header exposes more input-state buttons than it will accept as an overlay toggle. A toggle is
// built from the triggers, the special buttons, and the thumbsticks, and may additionally include a
// shoulder -- so a shoulder on its own is not a chord, and the D-pad and face buttons, which are
// perfectly good input-state flags, are not toggles at all.
const i32 toggle_core_buttons =
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_LeftTrigger) |
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_RightTrigger) |
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_Special_Left) |
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_Special_Right) |
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_LeftThumbstick) |
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_RightThumbstick);
const i32 toggle_shoulders =
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_LeftShoulder) |
    static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_RightShoulder);

// The header spells the toggle-key set out: F1 through F12, Space, Backspace, Escape, or Tab.
bool toggleable_key(i32 key) {
    typedef EOS_UI_EKeyCombination k;
    if (key >= static_cast<i32>(k::EOS_UIK_F1) && key <= static_cast<i32>(k::EOS_UIK_F12)) {
        return true;
    }
    return key == static_cast<i32>(k::EOS_UIK_Space) ||
           key == static_cast<i32>(k::EOS_UIK_Backspace) ||
           key == static_cast<i32>(k::EOS_UIK_Escape) ||
           key == static_cast<i32>(k::EOS_UIK_Tab);
}

} // namespace

bool sdk_ui::key_combination_valid(EOS_UI_EKeyCombination combination) {
    typedef EOS_UI_EKeyCombination k;
    const i32 value = static_cast<i32>(combination);
    const i32 key = value & static_cast<i32>(k::EOS_UIK_KeyTypeMask);
    const i32 modifiers = value & static_cast<i32>(k::EOS_UIK_ModifierMask);
    // Meta is in the enum, but the header does not list it among the modifiers a toggle key may use.
    const i32 allowed = static_cast<i32>(k::EOS_UIK_Shift) | static_cast<i32>(k::EOS_UIK_Control) |
                        static_cast<i32>(k::EOS_UIK_Alt);
    if (modifiers == 0 || (modifiers & ~allowed) != 0) {
        return false;
    }
    return toggleable_key(key);
}

bool sdk_ui::button_combination_valid(EOS_UI_EInputStateButtonFlags combination) {
    const i32 value = static_cast<i32>(combination);
    // None is the default, and the header says outright that it reverts to it.
    if (value == static_cast<i32>(EOS_UI_EInputStateButtonFlags::EOS_UISBF_None)) {
        return true;
    }
    if ((value & ~(toggle_core_buttons | toggle_shoulders)) != 0) {
        return false;
    }
    // A shoulder may join a chord but is not one by itself.
    return (value & toggle_core_buttons) != 0;
}

sdk_ui::sdk_ui(sdk_settings& settings, callback_manager& callbacks)
    : settings_(settings),
      callbacks_(callbacks),
      notification_location_(EOS_UI_ENotificationLocation::EOS_UNL_BottomRight),
      toggle_friends_key_(static_cast<EOS_UI_EKeyCombination>(default_toggle_key)),
      toggle_friends_button_(EOS_UI_EInputStateButtonFlags::EOS_UISBF_None),
      keyboard_behavior_(EOS_UI_EOnScreenKeyboardBehavior::EOS_UIOSKB_None),
      keyboard_device_checks_(false),
      social_overlay_paused_(false),
      registered_(false) {
}

sdk_ui::~sdk_ui() {
    emu_deinit();
}

void sdk_ui::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    registered_ = true;
}

void sdk_ui::emu_deinit() {
    if (!registered_) {
        return;
    }
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    pending_initial_delivery_.clear();
    registered_ = false;
}

void sdk_ui::deliver(callback_type_id type, std::size_t info_size, completion_delegate delegate,
                     void* client_data, EOS_EResult result_code, EOS_EpicAccountId local_user,
                     EOS_EpicAccountId target_user) {
    std::unique_ptr<frame_result> result(new frame_result());
    show_completion* info =
        static_cast<show_completion*>(result->create_callback(type, info_size, delegate));
    info->result_code = result_code;
    info->client_data = client_data;
    info->local_user_id = local_user;
    // The two-id callbacks all place the target immediately after the local user; the two that carry
    // only a local user never read past it, so writing it is safe only when the struct has room.
    if (info_size >= sizeof(show_completion)) {
        info->target_user_id = target_user;
    }
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

// Nothing is ever shown, so there is nothing to fail: the request is accepted, and the game hears
// back on the next tick exactly as it would from Epic.
void sdk_ui::show_friends(const EOS_UI_ShowFriendsOptions* options, void* client_data,
                          EOS_UI_OnShowFriendsCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid = options != 0 &&
                       version_ok(options->ApiVersion, EOS_UI_SHOWFRIENDS_API_LATEST);
    deliver(cb_show_friends, sizeof(EOS_UI_ShowFriendsCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data,
            valid ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters,
            valid ? options->LocalUserId : 0, 0);
}

void sdk_ui::hide_friends(const EOS_UI_HideFriendsOptions* options, void* client_data,
                          EOS_UI_OnHideFriendsCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid = options != 0 &&
                       version_ok(options->ApiVersion, EOS_UI_HIDEFRIENDS_API_LATEST);
    deliver(cb_hide_friends, sizeof(EOS_UI_HideFriendsCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data,
            valid ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters,
            valid ? options->LocalUserId : 0, 0);
}

// There is no overlay, so it is never on screen. Saying otherwise would be worse than useless: a
// game that opens the overlay and then waits for the player to close it would wait forever.
EOS_Bool sdk_ui::friends_visible(const EOS_UI_GetFriendsVisibleOptions* options) const {
    (void)options;
    return EOS_FALSE;
}

// And we never take the input away from the game, for the same reason.
EOS_Bool sdk_ui::friends_exclusive_input(
    const EOS_UI_GetFriendsExclusiveInputOptions* options) const {
    (void)options;
    return EOS_FALSE;
}

// Unlike ShowFriends, these three describe a flow the *player* completes -- a block confirmation, a
// report form, a profile page -- and the callback fires when they leave it. Reporting Success would
// tell the game the player finished something they were never shown. The labeled reference answers
// all three with EOS_NotConfigured (result code 0xe) while answering ShowFriends with Success, which
// is the honest split: the overlay is not set up to run these, and the game can fall back.
void sdk_ui::show_block_player(const EOS_UI_ShowBlockPlayerOptions* options, void* client_data,
                               EOS_UI_OnShowBlockPlayerCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid = options != 0 &&
                       version_ok(options->ApiVersion, EOS_UI_SHOWBLOCKPLAYER_API_LATEST) &&
                       options->LocalUserId != 0 && options->TargetUserId != 0;
    deliver(cb_block_player, sizeof(EOS_UI_OnShowBlockPlayerCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data,
            valid ? EOS_EResult::EOS_NotConfigured : EOS_EResult::EOS_InvalidParameters,
            valid ? options->LocalUserId : 0, valid ? options->TargetUserId : 0);
}

void sdk_ui::show_report_player(const EOS_UI_ShowReportPlayerOptions* options, void* client_data,
                                EOS_UI_OnShowReportPlayerCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid = options != 0 &&
                       version_ok(options->ApiVersion, EOS_UI_SHOWREPORTPLAYER_API_LATEST) &&
                       options->LocalUserId != 0 && options->TargetUserId != 0;
    deliver(cb_report_player, sizeof(EOS_UI_OnShowReportPlayerCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data,
            valid ? EOS_EResult::EOS_NotConfigured : EOS_EResult::EOS_InvalidParameters,
            valid ? options->LocalUserId : 0, valid ? options->TargetUserId : 0);
}

void sdk_ui::show_native_profile(const EOS_UI_ShowNativeProfileOptions* options, void* client_data,
                                 EOS_UI_OnShowNativeProfileCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const bool valid = options != 0 &&
                       version_ok(options->ApiVersion, EOS_UI_SHOWNATIVEPROFILE_API_LATEST) &&
                       options->LocalUserId != 0 && options->TargetUserId != 0;
    deliver(cb_native_profile, sizeof(EOS_UI_ShowNativeProfileCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data,
            valid ? EOS_EResult::EOS_NotConfigured : EOS_EResult::EOS_InvalidParameters,
            valid ? options->LocalUserId : 0, valid ? options->TargetUserId : 0);
}

// The settings below are never acted on -- there is nothing to toggle and nothing to draw -- but a
// game writes them and reads them back, and one that cannot read back what it just wrote has every
// reason to conclude the SDK is broken.

EOS_EResult sdk_ui::set_toggle_friends_key(const EOS_UI_SetToggleFriendsKeyOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_UI_SETTOGGLEFRIENDSKEY_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    // The header singles out None: it reverts to the default rather than being rejected.
    const EOS_UI_EKeyCombination wanted =
        (options->KeyCombination == EOS_UI_EKeyCombination::EOS_UIK_None)
            ? static_cast<EOS_UI_EKeyCombination>(default_toggle_key)
            : options->KeyCombination;
    if (!key_combination_valid(wanted)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (wanted == toggle_friends_key_) {
        return EOS_EResult::EOS_NoChange;
    }
    toggle_friends_key_ = wanted;
    return EOS_EResult::EOS_Success;
}

EOS_UI_EKeyCombination sdk_ui::toggle_friends_key(
    const EOS_UI_GetToggleFriendsKeyOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_UI_GETTOGGLEFRIENDSKEY_API_LATEST)) {
        return EOS_UI_EKeyCombination::EOS_UIK_None; // the header's answer for any error
    }
    return toggle_friends_key_;
}

EOS_EResult sdk_ui::set_toggle_friends_button(
    const EOS_UI_SetToggleFriendsButtonOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_UI_SETTOGGLEFRIENDSBUTTON_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    if (!button_combination_valid(options->ButtonCombination)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->ButtonCombination == toggle_friends_button_) {
        return EOS_EResult::EOS_NoChange;
    }
    toggle_friends_button_ = options->ButtonCombination;
    return EOS_EResult::EOS_Success;
}

EOS_UI_EInputStateButtonFlags sdk_ui::toggle_friends_button(
    const EOS_UI_GetToggleFriendsButtonOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_UI_GETTOGGLEFRIENDSBUTTON_API_LATEST)) {
        return EOS_UI_EInputStateButtonFlags::EOS_UISBF_None;
    }
    return toggle_friends_button_;
}

EOS_EResult sdk_ui::set_display_preference(const EOS_UI_SetDisplayPreferenceOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_UI_SETDISPLAYPREFERENCE_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    const i32 location = static_cast<i32>(options->NotificationLocation);
    if (location < static_cast<i32>(EOS_UI_ENotificationLocation::EOS_UNL_TopLeft) ||
        location > static_cast<i32>(EOS_UI_ENotificationLocation::EOS_UNL_BottomRight)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->NotificationLocation == notification_location_) {
        return EOS_EResult::EOS_NoChange;
    }
    notification_location_ = options->NotificationLocation;
    return EOS_EResult::EOS_Success;
}

// A UI event is minted when a player accepts a join from the overlay. Nothing produces one yet, so
// no id a game hands us is one we issued. The external companion is what will start issuing them --
// see docs/companion-client.md -- and this is the call that will consume them.
EOS_EResult sdk_ui::acknowledge_event_id(const EOS_UI_AcknowledgeEventIdOptions* options) {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_UI_ACKNOWLEDGEEVENTID_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_NotFound;
}

// These two are console integration points: the game routes its gamepad input and its frame through
// the SDK so an overlay can react to one and draw over the other. The header is explicit that both
// have "an empty implementation (i.e. returns EOS_NotImplemented) on all non-console platforms", and
// we are always one of those.
//
// Success would be a worse answer than it looks. It says the input and the frame were consumed, and
// a game reads that to mean the overlay is handling them -- which is exactly the branch it should
// not take when there is no overlay. NotImplemented is what sends it down its own path.
EOS_EResult sdk_ui::report_input_state(const EOS_UI_ReportInputStateOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_NotImplemented;
}

EOS_EResult sdk_ui::pre_present(const EOS_UI_PrePresentOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_NotImplemented;
}

EOS_EResult sdk_ui::pause_social_overlay(const EOS_UI_PauseSocialOverlayOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_UI_PAUSESOCIALOVERLAY_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    social_overlay_paused_ = (options->bIsPaused == EOS_TRUE);
    return EOS_EResult::EOS_Success;
}

EOS_Bool sdk_ui::social_overlay_paused(const EOS_UI_IsSocialOverlayPausedOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_UI_ISSOCIALOVERLAYPAUSED_API_LATEST)) {
        return EOS_FALSE;
    }
    return social_overlay_paused_ ? EOS_TRUE : EOS_FALSE;
}

EOS_EResult sdk_ui::configure_on_screen_keyboard(
    const EOS_UI_ConfigureOnScreenKeyboardOptions* options) {
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_UI_CONFIGUREONSCREENKEYBOARD_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    keyboard_behavior_ = options->Behavior;
    keyboard_device_checks_ = (options->bIsDeviceChecksEnabled == EOS_TRUE);
    return EOS_EResult::EOS_Success;
}

// The overlay never shows, hides, or reports memory, so after registration these never fire again.
// But the header promises something separate and unconditional: "Newly registered handlers will
// always be called the next tick with the current state." That is how a game learns the initial
// state without polling for it, and a game that waits for it would otherwise wait forever. So the
// registration persists *and* one copy of the current state is queued for the next tick.
//
// The join-accepted notifications, which are the ones that actually gate multiplayer, live on
// Presence, Lobby and Sessions and are the companion's job.

EOS_NotificationId sdk_ui::add_notify_display_settings_updated(
    const EOS_UI_AddNotifyDisplaySettingsUpdatedOptions* options, void* client_data,
    EOS_UI_OnDisplaySettingsUpdatedCallback delegate) {
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* info =
        static_cast<EOS_UI_OnDisplaySettingsUpdatedCallbackInfo*>(result->create_callback(
            cb_display_settings, sizeof(EOS_UI_OnDisplaySettingsUpdatedCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->bIsVisible = EOS_FALSE;
    info->bIsExclusiveInput = EOS_FALSE;
    const EOS_NotificationId id = callbacks_.add_notification(this, std::move(result));
    if (id != EOS_INVALID_NOTIFICATIONID) {
        pending_initial_delivery_.push_back(id);
    }
    return id;
}

EOS_NotificationId sdk_ui::add_notify_memory_monitor(
    const EOS_UI_AddNotifyMemoryMonitorOptions* options, void* client_data,
    EOS_UI_OnMemoryMonitorCallback delegate) {
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_UI_ADDNOTIFYMEMORYMONITOR_API_LATEST)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_UI_MemoryMonitorCallbackInfo* info = static_cast<EOS_UI_MemoryMonitorCallbackInfo*>(
        result->create_callback(cb_memory_monitor, sizeof(EOS_UI_MemoryMonitorCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->SystemMemoryMonitorReport = 0;
    const EOS_NotificationId id = callbacks_.add_notification(this, std::move(result));
    if (id != EOS_INVALID_NOTIFICATIONID) {
        pending_initial_delivery_.push_back(id);
    }
    return id;
}

EOS_NotificationId sdk_ui::add_notify_on_screen_keyboard_requested(
    const EOS_UI_AddNotifyOnScreenKeyboardRequestedOptions* options, void* client_data,
    EOS_UI_OnScreenKeyboardRequestedCallback delegate) {
    if (delegate == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_UI_ADDNOTIFYONSCREENKEYBOARDREQUESTED_API_LATEST)) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_UI_OnScreenKeyboardRequestedCallbackInfo* info =
        static_cast<EOS_UI_OnScreenKeyboardRequestedCallbackInfo*>(result->create_callback(
            cb_keyboard_requested, sizeof(EOS_UI_OnScreenKeyboardRequestedCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_ui::remove_notify(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

bool sdk_ui::cb_run_frame() {
    if (pending_initial_delivery_.empty()) {
        return false;
    }
    // Deliver each notification's promised initial call by firing the persistent registration once:
    // it already holds the delegate, the client data, and the zeroed current state. We re-look-up by
    // id and only fire a still-live one, so a notification removed before this tick delivers nothing
    // -- and a fired callback that removes another pending one takes effect on the re-look-up.
    std::vector<EOS_NotificationId> pending;
    pending.swap(pending_initial_delivery_);
    for (std::size_t i = 0; i < pending.size(); i++) {
        frame_result* note = callbacks_.find_notification(this, pending[i]);
        if (note != 0) {
            note->fire();
        }
    }
    return false;
}

bool sdk_ui::run_callbacks(frame_result&) {
    // Every completion here is already done when it is queued, so none of them waits on a predicate.
    return false;
}

// Nothing this interface hands a game is heap-allocated behind the callback info, so there is
// nothing to give back.
void sdk_ui::free_callback(frame_result&) {
}

} // namespace eosr
