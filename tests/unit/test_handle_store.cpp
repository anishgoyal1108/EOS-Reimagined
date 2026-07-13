#include "doctest.h"

#include <memory>

#include "common/handle_store.h"

using namespace eosr;

TEST_CASE("a handle store hands back the object, and forgets it on release") {
    handle_store<int> store;
    void* a = store.add(std::unique_ptr<int>(new int(1)));
    void* b = store.add(std::unique_ptr<int>(new int(2)));
    REQUIRE(store.live_count() == 2);
    CHECK(*store.find(a) == 1);
    CHECK(*store.find(b) == 2);

    store.release(a);
    CHECK((store.find(a) == 0));       // released: no longer found
    CHECK(store.live_count() == 1);  // and no longer tracked -- not a retained slot
    CHECK(*store.find(b) == 2);      // the other is untouched

    // A double release, a null, and a handle we never issued are each a no-op, not a second free.
    store.release(a);
    store.release(0);
    store.release(reinterpret_cast<void*>(0xabcd));
    CHECK(store.live_count() == 1);
}

TEST_CASE("a handle store does not grow with churn") {
    handle_store<int> store;
    // Thousands of create/release cycles leave nothing behind: the store tracks only what the game
    // is currently holding, not everything it has ever held.
    for (int i = 0; i < 5000; i++) {
        void* handle = store.add(std::unique_ptr<int>(new int(i)));
        CHECK((store.find(handle) != 0));
        store.release(handle);
    }
    CHECK(store.live_count() == 0);
}

TEST_CASE("handles from different stores never collide") {
    handle_store<int> first;
    handle_store<int> second;
    void* h = first.add(std::unique_ptr<int>(new int(42)));
    // A handle minted by one store is an id the other never issued, so it resolves to nothing there
    // rather than aliasing an unrelated object.
    CHECK((second.find(h) == 0));
    CHECK(*first.find(h) == 42);
    second.release(h); // releasing a foreign handle touches nothing
    CHECK(*first.find(h) == 42);
}
