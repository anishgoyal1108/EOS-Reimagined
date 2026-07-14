#ifndef EOSR_NET_SECURE_CHANNEL_H
#define EOSR_NET_SECURE_CHANNEL_H

#include <cstddef>
#include <vector>

#include "common/types.h"

namespace eosr {

// The Noise CipherState: a 32-byte key and a 64-bit nonce counter, encrypting with our AEAD
// (IETF ChaCha20-Poly1305). The Noise nonce is 0x00000000 || le64(counter). An unkeyed state
// passes data through unchanged, exactly as Noise's EncryptWithAd/DecryptWithAd do before a key is
// established. Spec: Noise Protocol Framework, wiki/developers/internals/adr/0001.
class cipher_state {
public:
    cipher_state();
    ~cipher_state();

    cipher_state(const cipher_state&) = delete;
    cipher_state& operator=(const cipher_state&) = delete;

    void init_key(const u8 key[32]);
    bool has_key() const { return has_key_; }

    // Encrypt `len` plaintext bytes into `cipher` (which needs len + 16 bytes for the tag). Returns
    // false once the nonce is exhausted (Noise reserves 2^64-1), so a key/nonce pair is never reused.
    bool encrypt(u8* cipher, const u8* ad, std::size_t ad_len, const u8* plain, std::size_t len);
    // Decrypt `len` bytes (ciphertext + 16-byte tag) into `plain` (needs len - 16 bytes). Returns
    // false on a tag mismatch or once the nonce is exhausted; `plain` is not left with unverified data.
    bool decrypt(u8* plain, const u8* ad, std::size_t ad_len, const u8* cipher, std::size_t len);

    void wipe();

private:
    void build_nonce(u8 out[12]) const;

    u8 key_[32];
    u64 nonce_;
    bool has_key_;
};

// The Noise XX handshake for Noise_XX_25519_ChaChaPoly_SHA256. Drive it by alternately calling
// write_message / read_message per the XX pattern (-> e / <- e,ee,s,es / -> s,se); when done(),
// split() yields the two transport cipher states and handshake_hash()/remote_static() are final.
class noise_handshake {
public:
    // `static_priv`/`static_pub` are our persistent identity key; `prologue` is bound into the
    // transcript so a mismatch fails closed.
    noise_handshake(bool initiator, const u8 static_priv[32], const u8 static_pub[32],
                    const u8* prologue, std::size_t prologue_len);
    ~noise_handshake();

    noise_handshake(const noise_handshake&) = delete;
    noise_handshake& operator=(const noise_handshake&) = delete;

    // Test seam: use a fixed ephemeral instead of a random one, for deterministic vectors. Must be
    // called before the first write on our side. Not used in production.
    void set_fixed_ephemeral(const u8 ephemeral_priv[32]);

    // Write the next handshake message (tokens then encrypted payload) into `out`. False on a DH
    // failure (a low-order peer key). Call when it is our turn per the pattern.
    bool write_message(const u8* payload, std::size_t payload_len, std::vector<u8>& out);
    // Read the next handshake message, recovering its payload. False on auth or DH failure.
    bool read_message(const u8* message, std::size_t message_len, std::vector<u8>& payload);

    bool done() const { return done_; }

    // After done(), and exactly once: key our sending and receiving transport cipher states,
    // oriented to this side, and derive the two directional UDP keys the P2P data path uses.
    // Returns false before the handshake completes or on a repeat call, and leaves the passed states
    // untouched -- so it can never hand out transport keys before the peer is authenticated, nor
    // reset a live transport back to nonce zero.
    //
    // The UDP keys come out of here rather than from the caller because they are derived from the
    // secret chaining key, which this object holds and discards. They are independent of the two
    // transport keys, so a UDP sequence starting at zero can never collide with a TCP counter at
    // zero. Spec: wiki/developers/internals/adr/0001 §7
    bool split(cipher_state& send, cipher_state& recv, u8 udp_send[32], u8 udp_recv[32]);
    // The transport keys alone, for a caller with no UDP path to key.
    bool split(cipher_state& send, cipher_state& recv);
    // 32-byte final handshake hash (valid once done()); channel-binding value.
    const u8* handshake_hash() const { return handshake_hash_; }
    // The peer's static public key, valid once its `s` token has been read.
    const u8* remote_static() const { return remote_static_; }
    bool have_remote_static() const { return have_rs_; }

private:
    // SymmetricState.
    void mix_hash(const u8* data, std::size_t len);
    bool mix_key(const u8* ikm, std::size_t len); // false if the DH input was the zero secret
    bool encrypt_and_hash(const u8* plain, std::size_t len, std::vector<u8>& out);
    bool decrypt_and_hash(const u8* cipher, std::size_t len, std::vector<u8>& out);
    bool dh(const u8 our_priv[32], const u8 peer_pub[32], u8 out[32]);
    bool ensure_ephemeral(); // false if the platform RNG fails
    // Does this side write (vs read) the message at `index`? Initiator writes even, responder odd.
    bool we_write(int index) const { return (index % 2 == 0) == initiator_; }

    u8 ck_[32];
    u8 h_[32];
    cipher_state cs_;

    bool initiator_;
    u8 s_priv_[32];
    u8 s_pub_[32];
    u8 e_priv_[32];
    u8 e_pub_[32];
    bool have_e_;
    u8 remote_static_[32];
    bool have_rs_;
    u8 re_[32];
    bool have_re_;

    bool fixed_ephemeral_;
    u8 fixed_e_priv_[32];

    int message_index_;
    bool done_;
    bool split_done_; // Split() has already handed out the transport keys; it may not run again
    u8 handshake_hash_[32];
};

} // namespace eosr

#endif
