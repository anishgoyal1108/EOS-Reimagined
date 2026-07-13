#ifndef EOSR_CORE_IDENTITY_H
#define EOSR_CORE_IDENTITY_H

#include <cstddef>
#include <string>

#include "common/types.h"
#include "platform/paths.h"

namespace eosr {

// A profile is an X25519 keypair, so its secret half is one X25519 secret key.
constexpr std::size_t profile_key_len = 32;

// How many copies of a game on one machine can each hold their own profile. It matches the ten
// discovery slots, so an instance that found a discovery port can always find a profile too.
constexpr int max_local_profiles = 10;

// The ids a static public key certifies. The router recomputes these from the key the handshake
// proved a peer holds, so an id a peer merely claims is never believed.
// Spec: identity derivation (docs/adr/0001 §4)
std::string derive_epic_account_id(const u8 public_key[profile_key_len]);
std::string derive_product_user_id(const u8 public_key[profile_key_len],
                                   const std::string& product_id, const std::string& sandbox_id,
                                   const std::string& deployment_id);

// This instance's persistent profile: an X25519 keypair that *is* its mesh identity. The public half
// derives the ids and the private half proves them in the handshake, so no peer can answer to an
// identity whose key it does not hold.
//
// Several copies of one game on one machine each take an exclusive profile slot, exactly as they
// each take a discovery slot, and for the same reason: sharing one profile would give them one id,
// and they would each mistake the other's advertisement for their own and never meet.
// Spec: docs/adr/0001 §4, §9
class identity {
public:
    identity();
    ~identity();

    identity(const identity&) = delete;
    identity& operator=(const identity&) = delete;

    // Take a free profile slot under `directory`, loading its key or minting and persisting one.
    // False when no slot is free or we can neither read nor write a profile; the caller then keeps
    // whatever ephemeral key it has, so the mesh still works and only the *persistence* is lost.
    bool load_or_create(const std::string& directory);

    // A key with nowhere to persist it: what an instance falls back to when it has no writable
    // directory, and how a test or an orchestrator provisions one directly.
    bool generate_ephemeral();
    bool adopt_key(const u8 secret[profile_key_len]);

    // Fold the title into the product user id, so one profile is a distinct product-user per game,
    // as EOS does. The epic account id is product-independent and does not move.
    void bind_product(const std::string& product_id, const std::string& sandbox_id,
                      const std::string& deployment_id);

    bool has_key() const { return has_key_; }
    // False when the identity lasts only for this run because nothing could be written to disk.
    bool is_persistent() const { return persistent_; }

    const u8* secret_key() const { return secret_; }
    const u8* public_key() const { return public_; }

    const std::string& epic_account_id() const { return epic_account_id_; }
    const std::string& product_user_id() const { return product_user_id_; }

    // The profile as 64 lowercase hex characters. The key is the whole identity, so this is also the
    // export format: carrying these characters to another machine carries the player with them.
    // Importing adopts the key in memory; persisting it means putting it in the profile directory.
    std::string export_key() const;
    bool import_key(const std::string& hex);

private:
    void rederive();

    u8 secret_[profile_key_len];
    u8 public_[profile_key_len];
    bool has_key_;
    bool persistent_;
    platform::file_lock slot_;

    std::string product_id_;
    std::string sandbox_id_;
    std::string deployment_id_;
    std::string epic_account_id_;
    std::string product_user_id_;
};

} // namespace eosr

#endif
