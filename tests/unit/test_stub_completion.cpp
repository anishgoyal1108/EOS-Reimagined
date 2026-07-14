#include "doctest.h"

#include "eos_ecom_types.h"
#include "eos_types.h"

#include "core/platform.h"
#include "core/runtime.h"
#include "core/stub_completion.h"

using namespace eosr;

namespace {

// Every completion's info begins with this, which is all a stub fills.
struct info_head {
    EOS_EResult ResultCode;
    void* ClientData;
};

bool g_fired = false;
EOS_EResult g_result = EOS_EResult::EOS_Success;
void* g_client_data = 0;

void EOS_CALL on_complete(const void* data) {
    const info_head* head = static_cast<const info_head*>(data);
    g_fired = true;
    g_result = head->ResultCode;
    g_client_data = head->ClientData;
}

void EOS_CALL on_notify(const void*) {
    g_fired = true;
}

void reset() {
    g_fired = false;
    g_result = EOS_EResult::EOS_Success;
    g_client_data = 0;
}

// A live platform, because a stub delivers through the platform's async engine like any other call.
sdk_platform* bring_up(const char* product) {
    sdk_platform* platform = platform_create();
    EOS_Platform_Options options = {};
    options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId = product;
    REQUIRE(platform->create(&options));
    return platform;
}

} // namespace

TEST_CASE("a stubbed async export still completes, and says NotImplemented") {
    reset();
    sdk_platform* platform = bring_up("stub-complete");

    int client_data = 0;
    stub_complete(&client_data, reinterpret_cast<completion_delegate>(on_complete),
                  sizeof(EOS_Ecom_QueryOwnershipCallbackInfo));
    CHECK_FALSE(g_fired); // queued, not fired inside the call -- delivery is the tick's job

    platform->tick();
    CHECK(g_fired);
    CHECK(g_result == EOS_EResult::EOS_NotImplemented); // honest: we did not do it
    CHECK(g_client_data == &client_data);              // and the game gets its own pointer back

    platform_destroy();
}

TEST_CASE("a stubbed notification returns a real id and never fires") {
    reset();
    sdk_platform* platform = bring_up("stub-notify");

    int client_data = 0;
    const EOS_NotificationId id = stub_add_notification(
        &client_data, reinterpret_cast<completion_delegate>(on_notify),
        sizeof(EOS_Ecom_QueryOwnershipCallbackInfo), "StubNotification");
    CHECK(id != EOS_INVALID_NOTIFICATIONID); // a game may treat an invalid id as an error

    for (int i = 0; i < 4; i++) {
        platform->tick();
    }
    CHECK_FALSE(g_fired); // there is nothing behind it, so it never fires

    stub_remove_notification(id); // and the game can pair a Remove with the id it was given
    platform->tick();
    CHECK_FALSE(g_fired);

    platform_destroy();
}

TEST_CASE("a stub before the platform exists is a safe no-op") {
    reset();
    platform_destroy(); // make sure there is no live platform

    int client_data = 0;
    stub_complete(&client_data, reinterpret_cast<completion_delegate>(on_complete), 64);
    CHECK_FALSE(g_fired);
    CHECK(stub_add_notification(&client_data, reinterpret_cast<completion_delegate>(on_notify), 64,
                                "StubNotification") == EOS_INVALID_NOTIFICATIONID);
    stub_remove_notification(1); // no crash
}

TEST_CASE("a stub with no delegate, or an undersized payload, is refused rather than guessed at") {
    reset();
    sdk_platform* platform = bring_up("stub-guard");

    stub_complete(0, 0, sizeof(EOS_Ecom_QueryOwnershipCallbackInfo)); // no delegate to call
    // A payload too small to hold even { ResultCode; ClientData; } would be written past its end.
    stub_complete(0, reinterpret_cast<completion_delegate>(on_complete), 1);
    platform->tick();
    CHECK_FALSE(g_fired);

    platform_destroy();
}
