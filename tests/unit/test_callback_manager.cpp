#include "doctest.h"

#include <memory>
#include <vector>

#include "core/callback_manager.h"
#include "core/frame_result.h"

using namespace eosr;

namespace {

// Minimal i_run_callback that records how it was driven.
struct recorder : i_run_callback {
    int frames;
    bool ready;
    int freed;

    recorder() : frames(0), ready(false), freed(0) {}

    bool cb_run_frame() { frames++; return false; }
    bool run_callbacks(frame_result&) { return ready; }
    void free_callback(frame_result&) { freed++; }
};

std::vector<int> g_fire_order;

void on_a(const void*) { g_fire_order.push_back(1); }
void on_b(const void*) { g_fire_order.push_back(2); }

const callback_type_id type_id = 7;

std::unique_ptr<frame_result> make_result(completion_delegate func, bool done) {
    std::unique_ptr<frame_result> r(new frame_result());
    r->create_callback(type_id, 8, func);
    r->set_done(done);
    return r;
}

} // namespace

TEST_CASE("a done callback fires once on the next tick, then is freed") {
    g_fire_order.clear();
    callback_manager mgr;
    recorder rec;
    mgr.register_callbacks(&rec);
    mgr.add_callback(&rec, make_result(on_a, true));

    mgr.tick();
    CHECK(g_fire_order.size() == 1);
    CHECK(rec.freed == 1);

    mgr.tick();
    CHECK(g_fire_order.size() == 1);
}

TEST_CASE("a callback waits until the owner reports it ready") {
    g_fire_order.clear();
    callback_manager mgr;
    recorder rec;
    mgr.register_callbacks(&rec);
    mgr.add_callback(&rec, make_result(on_a, false));

    mgr.tick();
    CHECK(g_fire_order.size() == 0);

    rec.ready = true;
    mgr.tick();
    CHECK(g_fire_order.size() == 1);
}

TEST_CASE("one-shot callbacks fire in FIFO order") {
    g_fire_order.clear();
    callback_manager mgr;
    recorder rec;
    mgr.register_callbacks(&rec);
    mgr.add_callback(&rec, make_result(on_a, true));
    mgr.add_callback(&rec, make_result(on_b, true));

    mgr.tick();
    REQUIRE(g_fire_order.size() == 2);
    CHECK(g_fire_order[0] == 1);
    CHECK(g_fire_order[1] == 2);
}

TEST_CASE("run_frames drives registered frames until unregistered") {
    callback_manager mgr;
    recorder rec;
    mgr.register_frame(&rec);

    mgr.tick();
    mgr.tick();
    CHECK(rec.frames == 2);

    mgr.unregister_frame(&rec);
    mgr.tick();
    CHECK(rec.frames == 2);
}

TEST_CASE("notifications: nonzero id, lookup by type, and removal") {
    callback_manager mgr;
    recorder rec;
    std::unique_ptr<frame_result> note_owner = make_result(on_a, false);
    frame_result* note = note_owner.get();
    const EOS_NotificationId id =
        mgr.add_notification(&rec, std::move(note_owner), "TestNotification");
    CHECK(id != 0);

    std::vector<EOS_NotificationId> got = mgr.notification_ids(&rec, type_id);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == id);
    CHECK((mgr.find_notification(&rec, id) == note));
    CHECK(note->notification_event() == "TestNotification");
    CHECK_FALSE(note->notification_token().empty());

    CHECK(mgr.notification_ids(&rec, type_id + 1).size() == 0);

    mgr.remove_notification(&rec, id);
    CHECK(mgr.notification_ids(&rec, type_id).size() == 0);
    CHECK((mgr.find_notification(&rec, id) == 0));
}

// Review regression: skipped until unregister_callbacks drains callback-owned payloads.
// Run explicitly with: unit_tests --no-skip --test-case="unregistering callbacks frees queued payloads"
TEST_CASE("unregistering callbacks frees queued payloads") {
    callback_manager mgr;
    recorder rec;
    mgr.register_callbacks(&rec);
    mgr.add_callback(&rec, make_result(on_a, true));

    mgr.unregister_callbacks(&rec);

    CHECK(rec.freed == 1);
}
