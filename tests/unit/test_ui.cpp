#include "doctest.h"

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

void reset_captures() {
    g_show_count = 0;
    g_show_result = EOS_EResult::EOS_UnexpectedError;
    g_show_user = 0;
    g_display_settings_count = 0;
}

void EOS_CALL on_show(const EOS_UI_ShowFriendsCallbackInfo* info) {
    g_show_count++;
    g_show_result = info->ResultCode;
    g_show_user = info->LocalUserId;
}
void EOS_CALL on_display_settings(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo*) {
    g_display_settings_count++;
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

// It registers, it unregisters, and it never fires -- because the overlay never shows or hides, and
// that is the truth. A game keying off it simply never hears from it.
TEST_CASE("the display-settings notification registers and is never fired") {
    ui_fixture fx;
    EOS_UI_AddNotifyDisplaySettingsUpdatedOptions options = {};
    options.ApiVersion = EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST;

    const EOS_NotificationId id =
        fx.ui.add_notify_display_settings_updated(&options, 0, on_display_settings);
    REQUIRE(id != EOS_INVALID_NOTIFICATIONID);

    EOS_UI_ShowFriendsOptions show = {};
    show.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
    show.LocalUserId = fx.me();
    fx.ui.show_friends(&show, 0, on_show);
    for (int i = 0; i < 5; i++) {
        fx.callbacks.tick();
    }
    CHECK(g_display_settings_count == 0);

    fx.ui.remove_notify(id);
}

// A UI event is minted when a player accepts a join from the overlay. Nothing mints one yet, so no
// id a game hands us is one we issued -- which is NotFound, not a malformed call. The external
// companion is what will start issuing them (docs/companion-client.md).
TEST_CASE("no ui event exists to acknowledge yet") {
    ui_fixture fx;
    EOS_UI_AcknowledgeEventIdOptions options = {};
    options.ApiVersion = EOS_UI_ACKNOWLEDGEEVENTID_API_LATEST;
    options.UiEventId = 1234;
    options.Result = EOS_EResult::EOS_Success;
    CHECK(fx.ui.acknowledge_event_id(&options) == EOS_EResult::EOS_NotFound);

    CHECK(fx.ui.acknowledge_event_id(0) == EOS_EResult::EOS_InvalidParameters);
}

// The game hands us its input and its frame so an overlay could draw over one and react to the
// other. We do neither -- but refusing would tell the game its own pipeline is broken, and it is not.
TEST_CASE("input and frame hand-offs are accepted rather than refused") {
    ui_fixture fx;
    EOS_UI_ReportInputStateOptions input = {};
    input.ApiVersion = EOS_UI_REPORTINPUTSTATE_API_LATEST;
    CHECK(fx.ui.report_input_state(&input) == EOS_EResult::EOS_Success);

    EOS_UI_PrePresentOptions present = {};
    present.ApiVersion = EOS_UI_PREPRESENT_API_LATEST;
    CHECK(fx.ui.pre_present(&present) == EOS_EResult::EOS_Success);

    CHECK(fx.ui.report_input_state(0) == EOS_EResult::EOS_InvalidParameters);
    CHECK(fx.ui.pre_present(0) == EOS_EResult::EOS_InvalidParameters);
}
