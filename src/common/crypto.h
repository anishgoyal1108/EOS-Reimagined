#ifndef EOSR_COMMON_CRYPTO_H
#define EOSR_COMMON_CRYPTO_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// SHA-256 of `data`, returned as 32 raw bytes. A from-scratch implementation of FIPS 180-4.
std::vector<u8> sha256(const u8* data, std::size_t len);

// HMAC-SHA256 of `message` under `key`, returned as 32 raw bytes (RFC 2104).
std::vector<u8> hmac_sha256(const u8* key, std::size_t key_len, const u8* message, std::size_t message_len);

// base64url without padding (RFC 7515), used for JWT segments.
std::string base64url_encode(const u8* data, std::size_t len);
std::string base64url_encode(const std::string& text);

// The canonical encoding every hashed input in the authenticated mesh is built with: each field is
// its byte length as a big-endian u32, then the bytes. Hashing a raw concatenation instead would
// make ("ab", "c") and ("a", "bc") the same input, so two different peers -- or two different
// games -- could derive one identity or one handshake transcript. Fixed-width numbers are fields
// too, so a value can never be mistaken for the length of the next one.
// Spec: enc()/lp() (wiki/internals/adr/0001 §4)
class canonical_encoder {
public:
    canonical_encoder& field(const u8* data, std::size_t len);
    canonical_encoder& field(const std::string& text);
    canonical_encoder& field_u32(u32 value);
    canonical_encoder& field_u64(u64 value);
    // A bare byte, not length-prefixed: the prologue pins the wire version this way, and a
    // fixed-width value needs no length to be unambiguous.
    canonical_encoder& raw_u8(u8 value);

    const std::vector<u8>& data() const { return buffer_; }

private:
    std::vector<u8> buffer_;
};

// --- Primitives for the authenticated mesh (wiki/internals/adr/0001) ---
//
// These wrap the vendored Monocypher; that dependency lives only in crypto.cpp and never escapes
// this interface. Key material is raw bytes so callers control its lifetime and can wipe it.

constexpr std::size_t x25519_key_len = 32; // both public and secret keys
constexpr std::size_t aead_key_len = 32;
constexpr std::size_t aead_nonce_len = 12; // IETF ChaCha20-Poly1305, RFC 8439
constexpr std::size_t aead_tag_len = 16;

// The X25519 public key for a secret key (Curve25519, RFC 7748).
void x25519_public_key(u8 out_public[x25519_key_len], const u8 secret[x25519_key_len]);

// The X25519 shared secret between our secret and a peer's public key. Returns false when the
// result is the all-zero point (a peer sending a low-order public key), which must not be used.
bool x25519_shared(u8 out_shared[x25519_key_len], const u8 secret[x25519_key_len],
                   const u8 peer_public[x25519_key_len]);

// IETF ChaCha20-Poly1305 (RFC 8439). `nonce` is 12 bytes; `mac` receives the 16-byte tag; `cipher`
// holds `len` bytes. Encryption in place is allowed (cipher == plain).
void aead_encrypt(u8* cipher, u8 mac[aead_tag_len], const u8 key[aead_key_len],
                  const u8 nonce[aead_nonce_len], const u8* ad, std::size_t ad_len,
                  const u8* plain, std::size_t len);

// Decrypt and verify. Returns false (and leaves `plain` untouched-or-zeroed) on any tag mismatch.
bool aead_decrypt(u8* plain, const u8 key[aead_key_len], const u8 nonce[aead_nonce_len],
                  const u8 mac[aead_tag_len], const u8* ad, std::size_t ad_len, const u8* cipher,
                  std::size_t len);

// HKDF-SHA256 (RFC 5869): extract with `salt` over `ikm`, then expand into `num_outputs` 32-byte
// keys (Noise uses 2 or 3). `num_outputs` must be 1..3.
void hkdf_sha256(const u8* salt, std::size_t salt_len, const u8* ikm, std::size_t ikm_len,
                 std::size_t num_outputs, std::vector<std::vector<u8> >& out);

// Overwrite `size` bytes at `secret` so it cannot be read from freed memory (not optimized away).
void secure_wipe(void* secret, std::size_t size);

} // namespace eosr

#endif
