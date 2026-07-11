#include "doctest.h"

#include <memory>
#include <vector>

#include "eos_types.h"

#include "core/callback_manager.h"
#include "core/frame_result.h"
#include "core/platform.h"

using namespace eosr;

namespace {

// An owner that reports every queued callback ready, so delivery hinges only on the tick.
struct ready_owner : i_run_callback {
    bool cb_run_frame() { return false; }
    bool run_callbacks(frame_result&) { return true; }
    void free_callback(frame_result&) {}
};

bool g_fired;
void EOS_CALL on_fire(const void*) { g_fired = true; }

EOS_Platform_Options options_with_product(const char* product) {
    EOS_Platform_Options o = {};
    o.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    o.ProductId = product;
    return o;
}

} // namespace

TEST_CASE("create brings the platform up and release tears it down") {
    sdk_platform p;
    CHECK_FALSE(p.is_created());

    EOS_Platform_Options o = options_with_product("game-1");
    REQUIRE(p.create(&o));
    CHECK(p.is_created());
    CHECK((p.interface_handle(if_connect) != 0));
    CHECK(p.settings().product_id() == "game-1");

    p.release();
    CHECK_FALSE(p.is_created());
    CHECK((p.interface_handle(if_connect) == 0));
}

TEST_CASE("every interface handle is non-null and distinct") {
    sdk_platform p;
    EOS_Platform_Options o = options_with_product("game-2");
    REQUIRE(p.create(&o));

    std::vector<void*> handles;
    for (int i = 0; i < if_count; i++) {
        void* h = p.interface_handle(static_cast<interface_id>(i));
        CHECK((h != 0));
        handles.push_back(h);
    }
    for (std::size_t i = 0; i < handles.size(); i++) {
        for (std::size_t j = i + 1; j < handles.size(); j++) {
            CHECK((handles[i] != handles[j]));
        }
    }
    // A getter is idempotent: the same interface always resolves to the same handle.
    CHECK((p.interface_handle(if_p2p) == p.interface_handle(if_p2p)));
    p.release();
}

TEST_CASE("ticking before create is a safe no-op") {
    sdk_platform p;
    p.tick();
    CHECK_FALSE(p.is_created());
}

TEST_CASE("platform tick drives the callback manager to deliver a callback") {
    g_fired = false;
    sdk_platform p;
    EOS_Platform_Options o = options_with_product("game-3");
    REQUIRE(p.create(&o));

    ready_owner owner;
    p.callbacks().register_callbacks(&owner);
    std::unique_ptr<frame_result> result(new frame_result());
    result->create_callback(1, 8, on_fire);
    result->set_done(true);
    p.callbacks().add_callback(&owner, std::move(result));

    CHECK_FALSE(g_fired);
    p.tick(); // closed loop: platform -> callback_manager -> the game delegate
    CHECK(g_fired);

    p.callbacks().unregister_callbacks(&owner);
    p.release();
}

TEST_CASE("the platform can be recreated after release") {
    sdk_platform p;
    EOS_Platform_Options o = options_with_product("game-4");
    REQUIRE(p.create(&o));
    p.release();

    REQUIRE(p.create(&o));
    CHECK(p.is_created());
    CHECK((p.interface_handle(if_lobby) != 0));
    p.release();
}
