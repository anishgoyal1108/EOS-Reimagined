#ifndef EOSR_TESTS_FIXED_PROFILE_H
#define EOSR_TESTS_FIXED_PROFILE_H

#include <cstddef>
#include <string>

#include "common/types.h"
#include "core/identity.h"

namespace eosr {
namespace test {

// A profile with a fixed key, so a test's identity is stable across runs and the handshake has a
// real key to prove. Nothing here touches the disk.
inline void seed_profile(identity& profile, u8 seed) {
    u8 key[profile_key_len];
    for (std::size_t i = 0; i < profile_key_len; i++) {
        key[i] = static_cast<u8>(seed + i);
    }
    key[0] = static_cast<u8>(seed | 1); // never the all-zero key, which is not a key at all
    profile.adopt_key(key);
}

// The id `profile` answers to in `game`. An id is derived from a key now, so a test can no more
// pick one than a game can: it takes a profile and asks what that profile answers to -- the same
// derivation every peer applies to the key it is shown.
inline std::string id_in(const identity& profile, const std::string& game) {
    return derive_product_user_id(profile.public_key(), game, "", "");
}

} // namespace test
} // namespace eosr

#endif
