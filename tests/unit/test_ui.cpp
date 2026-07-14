#include "doctest.h"

#include <string>

#include "eos_ui_types.h"

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/ui.h"

using namespace eosr;

namespace {

int g_show_count;
EOS_EResult g_show_result;
EOS_EpicAccountId g_show_user;
int g_display_settings_count;
void* g_display_settings_client_data;
EOS_Bool g_display_settings_visible;
EOS_Bool g_display_settings_exclusive;
int g_memory_monitor_count;
void* g_memory_monitor_client_data;
const void* g_memory_monitor_report;
int g_block_count;
EOS_EResult g_block_result;
EOS_EpicAccountId g_block_target;

void reset_captures() {
    g_show_count = 0;
    g_show_result = EOS_EResult::EOS_UnexpectedError;
    g_show_user = 0;
    g_display_settings_count = 0;
    g_block_count = 0;
    g_block_result = EOS_EResult::EOS_UnexpectedError;
    g_block_target = 0;
    g_display_settings_client_data = 0;
    g_display_settings_visible = EOS_TRUE;
    g_display_settings_exclusive = EOS_TRUE;
    g_memory_monitor_count = 0;
    g_memory_monitor_client_data = 0;
    g_memory_monitor_report = reinterpret_cast<const void*>(1);
}

void EOS_CALL on_show(const EOS_UI_ShowFriendsCallbackInfo* info) {
    g_show_count++;
    g_show_result = info->ResultCode;
    g_show_user = info->LocalUserId;
}
void EOS_CALL on_block(const EOS_UI_OnShowBlockPlayerCallbackInfo* info) {
    g_block_count++;
    g_block_result = info->ResultCode;
    g_block_target = info->TargetUserId;
}

void EOS_CALL on_display_settings(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* info) {
    g_display_settings_count++;
    g_display_settings_client_data = info->ClientData;
    g_display_settings_visible = info->bIsVisible;
    g_display_settings_exclusive = info->bIsExclusiveInput;
}
void EOS_CALL on_memory_monitor(const EOS_UI_MemoryMonitorCallbackInfo* info) {
    g_memory_monitor_count++;
    g_memory_monitor_client_data = info->ClientData;
    g_memory_monitor_report = info->SystemMemoryMonitorReport;
}

struct ui_fixture {
    sdk_settings settings;
    callback_manager callbacks;
    sdk_ui ui;

    ui_fixture() : ui(settings, callbacks) {
        ui.emu_init();
        reset_captures();
    }
    ~ui_fixture() { ui.emu_deinit(); }

    EOS_EpicAccountId me() {
        return id_registry::instance().get_epic_account_id(settings.epic_account_id());
    }
};

const i32 shift_f3 = static_cast<i32>(EOS_UI_EKeyCombination::EOS_UIK_Shift) |
                     static_cast<i32>(EOS_UI_EKeyCombination::EOS_UIK_F3);

} // namespace

// ShowFriends is accepted and answered on the tick, exactly as it would be by Epic. There is nothing
// behind it to fail.
TEST_CASE("showing the overlay is accepted and answered on the next tick") {
    ui_fixture fx;
    EOS_UI_ShowFriendsOptions options = {};
    options.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
    options.LocalUserId = fx.me();

    fx.ui.show_friends(&options, 0, on_show);
    CHECK(g_show_count == 0); // never synchronously
    fx.callbacks.tick();

    REQUIRE(g_show_count == 1);
    CHECK(g_show_result == EOS_EResult::EOS_Success);
    CHECK(g_show_user == fx.me());
}

// The one that would actually hang a game. Epic's overlay opens, and the game waits for the player
// to close it. Ours never opens -- so if we ever claimed it had, the game would wait for a player
// action that cannot happen. It is never visible, and it never takes the input.
TEST_CASE("the overlay is never visible and never takes exclusive input") {
    ui_fixture fx;
    EOS_UI_ShowFriendsOptions show = {};
    show.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
    show.LocalUserId = fx.me();
    fx.ui.show_friends(&show, 0, on_show);
    fx.callbacks.tick();
    REQUIRE(g_show_count == 1);

    EOS_UI_GetFriendsVisibleOptions visible = {};
    visible.ApiVersion = EOS_UI_GETFRIENDSVISIBLE_API_LATEST;
    visible.LocalUserId = fx.me();
    CHECK(fx.ui.friends_visible(&visible) == EOS_FALSE);

    EOS_UI_GetFriendsExclusiveInputOptions exclusive = {};
    exclusive.ApiVersion = EOS_UI_GETFRIENDSEXCLUSIVEINPUT_API_LATEST;
    exclusive.LocalUserId = fx.me();
    CHECK(fx.ui.friends_exclusive_input(&exclusive) == EOS_FALSE);
}

// A game writes these and reads them back. One that sets a value and cannot see it again has every
// reason to conclude the SDK is not listening to it.
TEST_CASE("the overlay settings a game writes are the settings it reads back") {
    ui_fixture fx;

    EOS_UI_GetToggleFriendsKeyOptions get_key = {};
    get_key.ApiVersion = EOS_UI_GETTOGGLEFRIENDSKEY_API_LATEST;
    // The header's own default: Shift + F3.
    CHECK(static_cast<i32>(fx.ui.toggle_friends_key(&get_key)) == shift_f3);

    EOS_UI_SetToggleFriendsKeyOptions set_key = {};
    set_key.ApiVersion = EOS_UI_SETTOGGLEFRIENDSKEY_API_LATEST;
    set_key.KeyCombination = static_cast<EOS_UI_EKeyCombination>(
        static_cast<i32>(EOS_UI_EKeyCombination::EOS_UIK_Control) |
        static_cast<i32>(EOS_UI_EKeyCombination::EOS_UIK_F5));
    CHECK(fx.ui.set_toggle_friends_key(&set_key) == EOS_EResult::EOS_Success);
    CHECK(fx.ui.toggle_friends_key(&get_key) == set_key.KeyCombination);
    // Setting the same key again is not a change, and the header has a code for that.
    CHECK(fx.ui.set_toggle_friends_key(&set_key) == EOS_EResult::EOS_NoChange);

    // None is special-cased by the header: it reverts to the default rather than being refused.
    set_key.KeyCombination = EOS_UI_EKeyCombination::EOS_UIK_None;
    CHECK(fx.ui.set_toggle_friends_key(&set_key) == EOS_EResult::EOS_Success);
    CHECK(static_cast<i32>(fx.ui.toggle_friends_key(&get_key)) == shift_f3);

    EOS_UI_SetDisplayPreferenceOptions preference = {};
    preference.ApiVersion = EOS_UI_SETDISPLAYPREFERENCE_API_LATEST;
    preference.NotificationLocation = EOS_UI_ENotificationLocation::EOS_UNL_TopLeft;
    CHECK(fx.ui.set_display_preference(&preference) == EOS_EResult::EOS_Success);
    CHECK(fx.ui.notification_location() == EOS_UI_ENotificationLocation::EOS_UNL_TopLeft);
    CHECK(fx.ui.set_display_preference(&preference) == EOS_EResult::EOS_NoChange);

    EOS_UI_PauseSocialOverlayOptions pause = {};
    pause.ApiVersion = EOS_UI_PAUSESOCIALOVERLAY_API_LATEST;
    pause.bIsPaused = EOS_TRUE;
    CHECK(fx.ui.pause_social_overlay(&pause) == EOS_EResult::EOS_Success);
    EOS_UI_IsSocialOverlayPausedOptions paused = {};
    paused.ApiVersion = EOS_UI_ISSOCIALOVERLAYPAUSED_API_LATEST;
    CHECK(fx.ui.social_overlay_paused(&paused) == EOS_TRUE);
}

// The header spells the rule out: one key from a small set, with at least one modifier.
TEST_CASE("a toggle key needs one real key and a modifier") {
    typedef EOS_UI_EKeyCombination k;
    const i32 shift = static_cast<i32>(k::EOS_UIK_Shift);
    const i32 ctrl = static_cast<i32>(k::EOS_UIK_Control);
    const i32 meta = static_cast<i32>(k::EOS_UIK_Meta);

    CHECK(sdk_ui::key_combination_valid(static_cast<k>(shift | static_cast<i32>(k::EOS_UIK_F3))));
    CHECK(sdk_ui::key_combination_valid(static_cast<k>(ctrl | static_cast<i32>(k::EOS_UIK_Tab))));
    CHECK(sdk_ui::key_combination_valid(
        static_cast<k>(shift | ctrl | static_cast<i32>(k::EOS_UIK_Escape))));

    // No modifier at all.
    CHECK_FALSE(sdk_ui::key_combination_valid(k::EOS_UIK_F3));
    // A modifier with no key.
    CHECK_FALSE(sdk_ui::key_combination_valid(static_cast<k>(shift)));
    // A key outside the set the header allows.
    CHECK_FALSE(sdk_ui::key_combination_valid(static_cast<k>(shift | static_cast<i32>(k::EOS_UIK_Left))));
    // Meta is in the enum, but the header does not list it as a toggle modifier.
    CHECK_FALSE(
        sdk_ui::key_combination_valid(static_cast<k>(meta | static_cast<i32>(k::EOS_UIK_F3))));
    CHECK_FALSE(sdk_ui::key_combination_valid(k::EOS_UIK_None));
}

// The header deliberately exposes more input-state buttons than it permits as an overlay toggle.
// A toggle is made from trigger/special/thumbstick buttons and may include either shoulder; D-pad
// and face buttons remain input-state flags, but are not valid overlay-opening combinations.
TEST_CASE("a toggle button uses only the subset allowed for opening the overlay") {
    typedef EOS_UI_EInputStateButtonFlags b;
    const i32 trigger = static_cast<i32>(b::EOS_UISBF_LeftTrigger);
    const i32 shoulder = static_cast<i32>(b::EOS_UISBF_RightShoulder);

    CHECK(sdk_ui::button_combination_valid(b::EOS_UISBF_None));
    CHECK(sdk_ui::button_combination_valid(b::EOS_UISBF_Special_Left));
    CHECK(sdk_ui::button_combination_valid(static_cast<b>(trigger | shoulder)));

    CHECK_FALSE(sdk_ui::button_combination_valid(b::EOS_UISBF_DPad_Up));
    CHECK_FALSE(sdk_ui::button_combination_valid(b::EOS_UISBF_FaceButton_Bottom));
    CHECK_FALSE(sdk_ui::button_combination_valid(static_cast<b>(1 << 16)));
}

// This initial notification is not contingent on the overlay changing. The 1.19 header promises
// that every newly registered handler receives the current display state on the next tick. That is
// how a game learns the initial false/false state without polling and it is deliberately stricter
// than the 2020 emulator, which stored this payload but never scheduled its first delivery.
TEST_CASE("a display-settings notification reports the current state on the next tick") {
    ui_fixture fx;
    EOS_UI_AddNotifyDisplaySettingsUpdatedOptions options = {};
    options.ApiVersion = EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST;
    int marker = 0;

    const EOS_NotificationId id =
        fx.ui.add_notify_display_settings_updated(&options, &marker, on_display_settings);
    REQUIRE(id != EOS_INVALID_NOTIFICATIONID);
    CHECK(g_display_settings_count == 0); // never synchronously

    fx.callbacks.tick();
    CHECK(g_display_settings_count == 1);
    CHECK(g_display_settings_client_data == &marker);
    CHECK(g_display_settings_visible == EOS_FALSE);
    CHECK(g_display_settings_exclusive == EOS_FALSE);

    // No state changed, so subsequent ticks do not invent additional notifications.
    fx.callbacks.tick();
    CHECK(g_display_settings_count == 1);

    fx.ui.remove_notify(id);
}

// RemoveNotify means the game no longer wishes to receive the callback. The initial-state delivery
// is part of that registration, not an independent completion: if removal happens before the next
// tick, invoking it can call through client data the game has already released.
TEST_CASE("removing a display-settings notification cancels its pending initial callback") {
    ui_fixture fx;
    EOS_UI_AddNotifyDisplaySettingsUpdatedOptions options = {};
    options.ApiVersion = EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST;

    const EOS_NotificationId id =
        fx.ui.add_notify_display_settings_updated(&options, 0, on_display_settings);
    REQUIRE(id != EOS_INVALID_NOTIFICATIONID);
    fx.ui.remove_notify(id);
    fx.callbacks.tick();
    CHECK(g_display_settings_count == 0);
}

// The memory-monitor registration carries the same explicit next-tick guarantee. A headless
// desktop implementation has no platform report to attach, but it must still publish that current
// null state once after registration.
TEST_CASE("a memory-monitor notification reports the current state on the next tick") {
    ui_fixture fx;
    EOS_UI_AddNotifyMemoryMonitorOptions options = {};
    options.ApiVersion = EOS_UI_ADDNOTIFYMEMORYMONITOR_API_LATEST;
    int marker = 0;

    const EOS_NotificationId id =
        fx.ui.add_notify_memory_monitor(&options, &marker, on_memory_monitor);
    REQUIRE(id != EOS_INVALID_NOTIFICATIONID);
    CHECK(g_memory_monitor_count == 0);

    fx.callbacks.tick();
    CHECK(g_memory_monitor_count == 1);
    CHECK(g_memory_monitor_client_data == &marker);
    CHECK(g_memory_monitor_report == 0);

    fx.callbacks.tick();
    CHECK(g_memory_monitor_count == 1);
    fx.ui.remove_notify(id);
}

// A UI event is minted when a player accepts a join from the overlay. Nothing mints one yet, so no
// id a game hands us is one we issued -- which is NotFound, not a malformed call. The external
// companion is what will start issuing them (wiki/developers/internals/companion-client.qmd).
TEST_CASE("no ui event exists to acknowledge yet") {
    ui_fixture fx;
    EOS_UI_AcknowledgeEventIdOptions options = {};
    options.ApiVersion = EOS_UI_ACKNOWLEDGEEVENTID_API_LATEST;
    options.UiEventId = 1234;
    options.Result = EOS_EResult::EOS_Success;
    CHECK(fx.ui.acknowledge_event_id(&options) == EOS_EResult::EOS_NotFound);

    CHECK(fx.ui.acknowledge_event_id(0) == EOS_EResult::EOS_InvalidParameters);
}

// Both functions are console integration points. The bundled header explicitly says their desktop
// implementations are empty and return NotImplemented. Success falsely claims the frame/input was
// consumed and prevents a game from taking the fallback path the result code exists to select.
TEST_CASE("console input and frame hand-offs report not implemented on desktop") {
    ui_fixture fx;
    EOS_UI_ReportInputStateOptions input = {};
    input.ApiVersion = EOS_UI_REPORTINPUTSTATE_API_LATEST;
    CHECK(fx.ui.report_input_state(&input) == EOS_EResult::EOS_NotImplemented);

    EOS_UI_PrePresentOptions present = {};
    present.ApiVersion = EOS_UI_PREPRESENT_API_LATEST;
    CHECK(fx.ui.pre_present(&present) == EOS_EResult::EOS_NotImplemented);

    CHECK(fx.ui.report_input_state(0) == EOS_EResult::EOS_InvalidParameters);
    CHECK(fx.ui.pre_present(0) == EOS_EResult::EOS_InvalidParameters);
}

// EOS reserves a distinct result for a structurally valid call made against an unsupported ABI
// version. Games use it to select an older call shape or disable a feature; folding it into
// InvalidParameters makes that compatibility path indistinguishable from a malformed request.
TEST_CASE("ui result APIs distinguish an incompatible version from bad parameters") {
    ui_fixture fx;

    EOS_UI_SetToggleFriendsKeyOptions key = {};
    key.ApiVersion = EOS_UI_SETTOGGLEFRIENDSKEY_API_LATEST + 1;
    key.KeyCombination = static_cast<EOS_UI_EKeyCombination>(shift_f3);
    CHECK(fx.ui.set_toggle_friends_key(&key) == EOS_EResult::EOS_IncompatibleVersion);

    EOS_UI_SetDisplayPreferenceOptions display = {};
    display.ApiVersion = EOS_UI_SETDISPLAYPREFERENCE_API_LATEST + 1;
    display.NotificationLocation = EOS_UI_ENotificationLocation::EOS_UNL_TopLeft;
    CHECK(fx.ui.set_display_preference(&display) == EOS_EResult::EOS_IncompatibleVersion);

    EOS_UI_PauseSocialOverlayOptions pause = {};
    pause.ApiVersion = EOS_UI_PAUSESOCIALOVERLAY_API_LATEST + 1;
    pause.bIsPaused = EOS_TRUE;
    CHECK(fx.ui.pause_social_overlay(&pause) == EOS_EResult::EOS_IncompatibleVersion);

    EOS_UI_ConfigureOnScreenKeyboardOptions keyboard = {};
    keyboard.ApiVersion = EOS_UI_CONFIGUREONSCREENKEYBOARD_API_LATEST + 1;
    keyboard.Behavior = EOS_UI_EOnScreenKeyboardBehavior::EOS_UIOSKB_None;
    CHECK(fx.ui.configure_on_screen_keyboard(&keyboard) ==
          EOS_EResult::EOS_IncompatibleVersion);
}

// ShowFriends opens a list and answers Success. These three do not: each describes a flow the
// *player* completes -- a block confirmation, a report form, a profile page -- and the callback
// fires when they leave it. Answering Success would tell the game a player finished something they
// were never shown, and a game may act on that: mark the report filed, treat the block as applied.
//
// The labeled reference draws exactly this line. ShowFriends writes result code 0; all three of
// these write 0xe, EOS_NotConfigured. The overlay is not set up to run them, which is true, and it
// leaves the game free to fall back to its own UI.
TEST_CASE("a flow the player would have to complete is not reported as completed") {
    ui_fixture fx;
    const EOS_EpicAccountId target =
        id_registry::instance().get_epic_account_id(std::string(32, 'b'));

    EOS_UI_ShowBlockPlayerOptions block = {};
    block.ApiVersion = EOS_UI_SHOWBLOCKPLAYER_API_LATEST;
    block.LocalUserId = fx.me();
    block.TargetUserId = target;

    fx.ui.show_block_player(&block, 0, on_block);
    CHECK(g_block_count == 0); // never synchronously
    fx.callbacks.tick();

    REQUIRE(g_block_count == 1);
    CHECK(g_block_result == EOS_EResult::EOS_NotConfigured);
    // The ids still come back, so the game can tell which request this answers.
    CHECK(g_block_target == target);

    // A malformed call is still a malformed call, not an unconfigured overlay.
    EOS_UI_ShowBlockPlayerOptions bad = block;
    bad.TargetUserId = 0;
    fx.ui.show_block_player(&bad, 0, on_block);
    fx.callbacks.tick();
    CHECK(g_block_count == 2);
    CHECK(g_block_result == EOS_EResult::EOS_InvalidParameters);
}
