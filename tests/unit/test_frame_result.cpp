#include "doctest.h"

#include <chrono>
#include <cstdint>
#include <thread>

#include "core/frame_result.h"

using namespace eosr;

namespace {

int g_fired = 0;
const void* g_last_payload = nullptr;

void on_cb(const void* info) {
    g_fired++;
    g_last_payload = info;
}

struct sample_info {
    int api_version;
    int value;
};

const int sample_type_id = 42;

} // namespace

TEST_CASE("create_callback allocates a zeroed payload tagged with the type id") {
    frame_result r;
    void* p = r.create_callback(sample_type_id, sizeof(sample_info), on_cb);
    CHECK(p != nullptr);
    CHECK(r.type_id() == sample_type_id);

    sample_info* info = r.get_callback<sample_info>();
    CHECK(static_cast<void*>(info) == p);
    CHECK(info->api_version == 0);
    CHECK(info->value == 0);
}

TEST_CASE("fire invokes the delegate with the payload pointer") {
    g_fired = 0;
    g_last_payload = nullptr;

    frame_result r;
    void* p = r.create_callback(sample_type_id, sizeof(sample_info), on_cb);
    r.fire();
    CHECK(g_fired == 1);
    CHECK(g_last_payload == p);
}

TEST_CASE("fire with a null delegate is a no-op") {
    frame_result r;
    r.create_callback(sample_type_id, sizeof(sample_info), nullptr);
    r.fire();
    CHECK(true);
}

TEST_CASE("callback_ok_timeout ages from creation") {
    frame_result ready;
    ready.create_callback(sample_type_id, sizeof(sample_info), on_cb, std::chrono::milliseconds(0));
    CHECK(ready.callback_ok_timeout() == true);

    frame_result pending;
    pending.create_callback(sample_type_id, sizeof(sample_info), on_cb, std::chrono::hours(1));
    CHECK(pending.callback_ok_timeout() == false);
}

TEST_CASE("recreating a callback resets its timeout clock") {
    frame_result r;
    r.create_callback(sample_type_id, sizeof(sample_info), on_cb, std::chrono::milliseconds(5));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    r.create_callback(sample_type_id, sizeof(sample_info), on_cb, std::chrono::milliseconds(5));
    CHECK(r.callback_ok_timeout() == false);
}

TEST_CASE("recreating a callback clears stale delivery state") {
    frame_result r;
    r.create_callback(sample_type_id, sizeof(sample_info), on_cb);
    r.set_done(true);
    r.set_remove_on_timeout(true);

    r.create_callback(sample_type_id, sizeof(sample_info), on_cb);
    CHECK(r.done() == false);
    CHECK(r.remove_on_timeout() == false);
}

TEST_CASE("copy deep-copies the payload") {
    frame_result r;
    sample_info* info = static_cast<sample_info*>(
        r.create_callback(sample_type_id, sizeof(sample_info), on_cb));
    info->value = 7;

    frame_result copy(r);
    sample_info* copy_info = copy.get_callback<sample_info>();
    CHECK(copy_info != info);
    CHECK(copy_info->value == 7);
}

TEST_CASE("payload storage is aligned for an over-aligned callback info") {
    struct wide_info {
        u64 a;
        double b;
        void* c;
    };
    frame_result r;
    void* p = r.create_callback(sample_type_id, sizeof(wide_info), on_cb);
    CHECK((reinterpret_cast<std::uintptr_t>(p) % alignof(wide_info)) == 0);

    wide_info* wi = r.get_callback<wide_info>();
    CHECK((reinterpret_cast<std::uintptr_t>(wi) % alignof(wide_info)) == 0);
}
