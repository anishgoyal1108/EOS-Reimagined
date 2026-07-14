#include "doctest.h"

#include <string>

#include "core/peer_fp.h"

using namespace eosr;

TEST_CASE("the peer fingerprint matches the golden vector") {
    // docs/alpha-tracing.md §4: this exact pair pins the construction so two tools agree.
    CHECK(peer_fingerprint("00112233445566778899aabbccddeeff") == "ebf65ed621ba531b");
}

TEST_CASE("the peer fingerprint is deterministic and sixteen lowercase hex") {
    const std::string fp = peer_fingerprint("0123456789abcdef0123456789abcdef");
    CHECK(fp == peer_fingerprint("0123456789abcdef0123456789abcdef"));
    CHECK(fp.size() == 16);
    for (std::size_t i = 0; i < fp.size(); i++) {
        const char c = fp[i];
        CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
    }
}

TEST_CASE("distinct ids give distinct fingerprints") {
    CHECK(peer_fingerprint("00112233445566778899aabbccddeeff") !=
          peer_fingerprint("00112233445566778899aabbccddef00"));
}

TEST_CASE("a malformed product user id yields no fingerprint") {
    CHECK(peer_fingerprint("").empty());
    CHECK(peer_fingerprint("deadbeef").empty());                              // too short
    CHECK(peer_fingerprint("00112233445566778899aabbccddeeff00").empty());    // too long
    CHECK(peer_fingerprint("00112233445566778899AABBCCDDEEFF").empty());      // uppercase, not lower-hex
    CHECK(peer_fingerprint("00112233445566778899aabbccddeezz").empty());      // non-hex characters
}
