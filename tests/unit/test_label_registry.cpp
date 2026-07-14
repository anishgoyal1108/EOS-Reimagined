#include "doctest.h"

#include <set>
#include <string>

#include "core/label_registry.h"

using namespace eosr;

TEST_CASE("a token keeps its label, and distinct tokens get distinct labels") {
    label_registry registry;
    const std::string a = registry.label(label_kind::puid, "00112233445566778899aabbccddeeff");
    const std::string b = registry.label(label_kind::puid, "ffeeddccbbaa99887766554433221100");

    CHECK(a == "puid#0");
    CHECK(b == "puid#1");
    CHECK(registry.label(label_kind::puid, "00112233445566778899aabbccddeeff") == a); // stable
    CHECK(a != b);
}

TEST_CASE("each kind counts on its own") {
    label_registry registry;
    CHECK(registry.label(label_kind::puid, "x") == "puid#0");
    CHECK(registry.label(label_kind::session, "x") == "session#0"); // same token, different kind
    CHECK(registry.label(label_kind::lobby, "x") == "lobby#0");
    CHECK(registry.label(label_kind::puid, "y") == "puid#1");
}

TEST_CASE("an absent id yields no label rather than a made-up one") {
    label_registry registry;
    CHECK(registry.label(label_kind::puid, "").empty());
    CHECK(registry.size(label_kind::puid) == 0);
}

TEST_CASE("the registry is bounded and evicts the coldest entry") {
    label_registry registry(3);
    registry.label(label_kind::lobby, "a");
    registry.label(label_kind::lobby, "b");
    registry.label(label_kind::lobby, "c");
    CHECK(registry.size(label_kind::lobby) == 3);

    // Touch a and c, leaving b the coldest, then overflow.
    registry.label(label_kind::lobby, "a");
    registry.label(label_kind::lobby, "c");
    registry.label(label_kind::lobby, "d");

    CHECK(registry.size(label_kind::lobby) == 3); // still bounded
    CHECK(registry.label(label_kind::lobby, "a") == "lobby#0"); // survivors keep their labels
    CHECK(registry.label(label_kind::lobby, "c") == "lobby#2");
    CHECK(registry.label(label_kind::lobby, "d") == "lobby#3");
}

TEST_CASE("an evicted token seen again gets a fresh label, never a recycled one") {
    label_registry registry(2);
    const std::string first = registry.label(label_kind::session, "old");
    registry.label(label_kind::session, "keep");
    registry.label(label_kind::session, "keep"); // "old" is now the coldest
    registry.label(label_kind::session, "new");  // evicts "old"

    const std::string again = registry.label(label_kind::session, "old");
    CHECK(again != first); // a retired label can never name a different object later
    CHECK(again == "session#3");
}

TEST_CASE("high churn cannot grow the registry without bound") {
    label_registry registry(64);
    std::set<std::string> labels;
    for (int i = 0; i < 5000; i++) {
        labels.insert(registry.label(label_kind::handle, "token-" + std::to_string(i)));
    }
    CHECK(registry.size(label_kind::handle) == 64); // the cap holds
    CHECK(labels.size() == 5000);                   // and every one of them was a distinct label
}

TEST_CASE("clear rewinds the run") {
    label_registry registry;
    CHECK(registry.label(label_kind::eaid, "x") == "eaid#0");
    registry.clear();
    CHECK(registry.size(label_kind::eaid) == 0);
    CHECK(registry.label(label_kind::eaid, "x") == "eaid#0"); // a new run starts fresh
}
