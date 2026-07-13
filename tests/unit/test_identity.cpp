#include "doctest.h"

#include <cstdio>
#include <string>
#include <vector>

#include "common/ids.h"
#include "common/types.h"
#include "core/identity.h"
#include "platform/paths.h"

using namespace eosr;

namespace {

// A profile directory of this test's own, under the build tree. We clear it on the way in rather
// than on the way out, so a run always starts from a known state even if an earlier one was killed.
std::string fresh_profile_dir(const char* name) {
    const std::string path = std::string("eosr-test-profiles/") + name;
    platform::make_directories(path);
    for (int slot = 0; slot < max_local_profiles; slot++) {
        std::string base = path + "/profile";
        if (slot != 0) {
            base += "-";
            base += static_cast<char>('0' + slot);
        }
        std::remove((base + ".key").c_str());
        std::remove((base + ".lock").c_str());
    }
    return path;
}

std::vector<u8> key_of(u8 seed) {
    std::vector<u8> secret(profile_key_len, seed);
    secret[0] = static_cast<u8>(seed + 1); // keep two seeds from ever clamping to the same scalar
    return secret;
}

} // namespace

TEST_CASE("a profile key derives valid, stable, distinct ids") {
    const std::vector<u8> secret = key_of(7);
    identity profile;
    REQUIRE(profile.adopt_key(secret.data()));
    profile.bind_product("prod-123", "sandbox-9", "deploy-7");

    CHECK(profile.epic_account_id().size() == 32);
    CHECK(profile.product_user_id().size() == 32);
    CHECK(id_string_is_valid(profile.epic_account_id()));
    CHECK(id_string_is_valid(profile.product_user_id()));
    CHECK(profile.epic_account_id() != profile.product_user_id());

    // The same key is the same player, every run and on every machine.
    identity same;
    REQUIRE(same.adopt_key(secret.data()));
    same.bind_product("prod-123", "sandbox-9", "deploy-7");
    CHECK(same.product_user_id() == profile.product_user_id());
    CHECK(same.epic_account_id() == profile.epic_account_id());
}

TEST_CASE("a different key is a different player") {
    identity a;
    identity b;
    REQUIRE(a.adopt_key(key_of(1).data()));
    REQUIRE(b.adopt_key(key_of(2).data()));
    CHECK(a.product_user_id() != b.product_user_id());
    CHECK(a.epic_account_id() != b.epic_account_id());
}

// The product user id folds in the title, so one profile is a distinct product-user per game, as
// EOS does. The epic account id is product-independent, which is what lets Presence key on it.
TEST_CASE("the title moves the product user id but not the epic account id") {
    identity profile;
    REQUIRE(profile.adopt_key(key_of(3).data()));
    const std::string account_before = profile.epic_account_id();

    profile.bind_product("game-a", "sandbox", "deploy");
    const std::string in_game_a = profile.product_user_id();
    profile.bind_product("game-b", "sandbox", "deploy");

    CHECK(profile.product_user_id() != in_game_a);
    CHECK(profile.epic_account_id() == account_before);
}

// Every hashed input is length-prefixed, so no two distinct field lists share an encoding. Hashing
// a raw concatenation instead would give ("ab","c","") and ("a","bc","") one product user id, and
// two players in two different sandboxes could collide on a single identity.
// Spec: encoding non-ambiguity (docs/adr/0001 §11)
TEST_CASE("field boundaries cannot be shifted between product users") {
    identity profile;
    REQUIRE(profile.adopt_key(key_of(5).data()));
    const u8* key = profile.public_key();

    CHECK(derive_product_user_id(key, "ab", "c", "") != derive_product_user_id(key, "a", "bc", ""));
    CHECK(derive_product_user_id(key, "", "abc", "") != derive_product_user_id(key, "abc", "", ""));
}

TEST_CASE("an all-zero secret is refused rather than shared") {
    const std::vector<u8> zeros(profile_key_len, 0);
    identity profile;
    CHECK_FALSE(profile.adopt_key(zeros.data()));
    CHECK_FALSE(profile.has_key());
    CHECK(profile.product_user_id().empty());
}

TEST_CASE("a profile round-trips through its hex export") {
    identity source;
    REQUIRE(source.generate_ephemeral());
    source.bind_product("prod", "sand", "dep");
    const std::string exported = source.export_key();
    CHECK(exported.size() == profile_key_len * 2);

    identity carried;
    REQUIRE(carried.import_key(exported));
    carried.bind_product("prod", "sand", "dep");
    // Carrying the key to another machine carries the player with it.
    CHECK(carried.product_user_id() == source.product_user_id());
    CHECK(carried.epic_account_id() == source.epic_account_id());

    CHECK_FALSE(carried.import_key("not hex"));
    CHECK_FALSE(carried.import_key(std::string(63, 'a')));
}

TEST_CASE("a persisted profile is the same player on the next run") {
    const std::string directory = fresh_profile_dir("stable");

    std::string first_id;
    std::string first_key;
    {
        identity profile;
        REQUIRE(profile.load_or_create(directory));
        CHECK(profile.is_persistent());
        profile.bind_product("prod", "sand", "dep");
        first_id = profile.product_user_id();
        first_key = profile.export_key();
    }
    // The first instance is gone, so its slot is free again -- and the next run must land on the
    // same profile rather than mint a new one.
    identity again;
    REQUIRE(again.load_or_create(directory));
    again.bind_product("prod", "sand", "dep");
    CHECK(again.export_key() == first_key);
    CHECK(again.product_user_id() == first_id);
}

// Two copies of a game on one machine are two players. If they shared a profile they would share an
// id, each would see the other's advertisement as its own, and couch co-op would never mesh -- so
// each takes an exclusive profile slot, exactly as each takes a discovery slot.
TEST_CASE("two local instances take different profile slots") {
    const std::string directory = fresh_profile_dir("two-locals");

    identity first;
    identity second;
    REQUIRE(first.load_or_create(directory));
    REQUIRE(second.load_or_create(directory));
    first.bind_product("prod", "sand", "dep");
    second.bind_product("prod", "sand", "dep");

    CHECK(first.is_persistent());
    CHECK(second.is_persistent());
    CHECK(first.export_key() != second.export_key());
    CHECK(first.product_user_id() != second.product_user_id());
    CHECK(first.epic_account_id() != second.epic_account_id());

    // And each keeps its own slot across runs: the first instance to start is the same player it
    // was last time, not whichever profile happens to be read first.
    const std::string first_key = first.export_key();
    const std::string second_key = second.export_key();
    identity third;
    REQUIRE(third.load_or_create(directory));
    CHECK(third.export_key() != first_key);
    CHECK(third.export_key() != second_key);
}

TEST_CASE("a corrupt profile is replaced, not adopted") {
    const std::string directory = fresh_profile_dir("corrupt");
    REQUIRE(platform::write_private_file(directory + "/profile.key", "key zzzz\n"));

    identity profile;
    REQUIRE(profile.load_or_create(directory));
    CHECK(profile.has_key());
    CHECK(profile.is_persistent());
    CHECK(id_string_is_valid(profile.product_user_id()));
}

TEST_CASE("a profile with nowhere to live is refused, leaving the caller its ephemeral key") {
    identity profile;
    REQUIRE(profile.generate_ephemeral());
    const std::string ephemeral = profile.export_key();

    CHECK_FALSE(profile.load_or_create(""));
    CHECK_FALSE(profile.is_persistent());
    CHECK(profile.export_key() == ephemeral);
}
