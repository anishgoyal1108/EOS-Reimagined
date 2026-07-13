#include "net/secure_channel.h"

#include <cstring>

#include "common/crypto.h"
#include "platform/rng.h"

namespace eosr {

namespace {

// Noise_XX_25519_ChaChaPoly_SHA256 is exactly 32 bytes, so the initial hash is the name itself
// (no padding, no pre-hash), per Noise's InitializeSymmetric.
const char* const protocol_name = "Noise_XX_25519_ChaChaPoly_SHA256";
const std::size_t hash_len = 32;
const std::size_t dh_len = 32;
const std::size_t tag_len = 16;

// The tokens of a Noise message pattern.
enum token { tok_e, tok_s, tok_ee, tok_es, tok_se };

// The XX pattern: three messages. The direction alternates (initiator writes 0 and 2, reads 1).
const std::vector<token>& xx_message(int index) {
    static const std::vector<token> m0{tok_e};
    static const std::vector<token> m1{tok_e, tok_ee, tok_s, tok_es};
    static const std::vector<token> m2{tok_s, tok_se};
    static const std::vector<token> empty;
    switch (index) {
        case 0: return m0;
        case 1: return m1;
        case 2: return m2;
        default: return empty;
    }
}

} // namespace

// --- cipher_state ---

cipher_state::cipher_state() : nonce_(0), has_key_(false) {
    std::memset(key_, 0, sizeof(key_));
}

cipher_state::~cipher_state() {
    wipe();
}

void cipher_state::wipe() {
    secure_wipe(key_, sizeof(key_)); // the crypto wrapper's wipe (Monocypher crypto_wipe)
    nonce_ = 0;
    has_key_ = false;
}

void cipher_state::init_key(const u8 key[32]) {
    std::memcpy(key_, key, sizeof(key_));
    nonce_ = 0;
    has_key_ = true;
}

void cipher_state::build_nonce(u8 out[12]) const {
    // Noise: 32 bits of zeros, then little-endian n.
    std::memset(out, 0, 4);
    for (int i = 0; i < 8; i++) {
        out[4 + i] = static_cast<u8>((nonce_ >> (8 * i)) & 0xff);
    }
}

void cipher_state::encrypt(u8* cipher, const u8* ad, std::size_t ad_len, const u8* plain,
                           std::size_t len) {
    if (!has_key_) {
        if (len != 0 && cipher != plain) {
            std::memcpy(cipher, plain, len);
        }
        return;
    }
    u8 nonce[12];
    build_nonce(nonce);
    aead_encrypt(cipher, cipher + len, key_, nonce, ad, ad_len, plain, len);
    nonce_++;
}

bool cipher_state::decrypt(u8* plain, const u8* ad, std::size_t ad_len, const u8* cipher,
                           std::size_t len) {
    if (!has_key_) {
        if (len != 0 && plain != cipher) {
            std::memcpy(plain, cipher, len);
        }
        return true;
    }
    if (len < tag_len) {
        return false;
    }
    u8 nonce[12];
    build_nonce(nonce);
    const std::size_t body = len - tag_len;
    if (!aead_decrypt(plain, key_, nonce, cipher + body, ad, ad_len, cipher, body)) {
        return false;
    }
    nonce_++;
    return true;
}

// --- noise_handshake ---

noise_handshake::noise_handshake(bool initiator, const u8 static_priv[32], const u8 static_pub[32],
                                 const u8* prologue, std::size_t prologue_len)
    : initiator_(initiator), have_e_(false), have_rs_(false), have_re_(false),
      fixed_ephemeral_(false), message_index_(0), done_(false) {
    std::memcpy(s_priv_, static_priv, dh_len);
    std::memcpy(s_pub_, static_pub, dh_len);
    std::memset(e_priv_, 0, sizeof(e_priv_));
    std::memset(e_pub_, 0, sizeof(e_pub_));
    std::memset(remote_static_, 0, sizeof(remote_static_));
    std::memset(re_, 0, sizeof(re_));
    std::memset(fixed_e_priv_, 0, sizeof(fixed_e_priv_));
    std::memset(handshake_hash_, 0, sizeof(handshake_hash_));

    // InitializeSymmetric: h = protocol_name (exactly HASHLEN), ck = h, cipher empty.
    std::memcpy(h_, protocol_name, hash_len);
    std::memcpy(ck_, h_, hash_len);
    // MixHash(prologue). (XX has no pre-message public keys.)
    mix_hash(prologue, prologue_len);
}

noise_handshake::~noise_handshake() {
    eosr::secure_wipe(ck_, sizeof(ck_));
    eosr::secure_wipe(s_priv_, sizeof(s_priv_));
    eosr::secure_wipe(e_priv_, sizeof(e_priv_));
    eosr::secure_wipe(fixed_e_priv_, sizeof(fixed_e_priv_));
}

void noise_handshake::set_fixed_ephemeral(const u8 ephemeral_priv[32]) {
    std::memcpy(fixed_e_priv_, ephemeral_priv, dh_len);
    fixed_ephemeral_ = true;
}

void noise_handshake::mix_hash(const u8* data, std::size_t len) {
    std::vector<u8> input;
    input.reserve(hash_len + len);
    input.insert(input.end(), h_, h_ + hash_len);
    if (len != 0) {
        input.insert(input.end(), data, data + len);
    }
    const std::vector<u8> digest = sha256(input.data(), input.size());
    std::memcpy(h_, digest.data(), hash_len);
}

bool noise_handshake::mix_key(const u8* ikm, std::size_t len) {
    std::vector<std::vector<u8> > out;
    hkdf_sha256(ck_, hash_len, ikm, len, 2, out);
    if (out.size() != 2) {
        return false;
    }
    std::memcpy(ck_, out[0].data(), hash_len);
    cs_.init_key(out[1].data());
    eosr::secure_wipe(out[0].data(), out[0].size());
    eosr::secure_wipe(out[1].data(), out[1].size());
    return true;
}

void noise_handshake::encrypt_and_hash(const u8* plain, std::size_t len, std::vector<u8>& out) {
    const std::size_t extra = cs_.has_key() ? tag_len : 0;
    out.resize(len + extra);
    cs_.encrypt(out.data(), h_, hash_len, plain, len);
    mix_hash(out.data(), out.size());
}

bool noise_handshake::decrypt_and_hash(const u8* cipher, std::size_t len, std::vector<u8>& out) {
    const std::size_t extra = cs_.has_key() ? tag_len : 0;
    if (len < extra) {
        return false;
    }
    out.resize(len - extra);
    if (!cs_.decrypt(out.data(), h_, hash_len, cipher, len)) {
        return false;
    }
    mix_hash(cipher, len); // hash the ciphertext, per Noise (before or after decrypt is equivalent)
    return true;
}

bool noise_handshake::dh(const u8 our_priv[32], const u8 peer_pub[32], u8 out[32]) {
    return x25519_shared(out, our_priv, peer_pub);
}

void noise_handshake::ensure_ephemeral() {
    if (have_e_) {
        return;
    }
    if (fixed_ephemeral_) {
        std::memcpy(e_priv_, fixed_e_priv_, dh_len);
    } else {
        platform::random_bytes(e_priv_, dh_len);
    }
    x25519_public_key(e_pub_, e_priv_);
    have_e_ = true;
}

bool noise_handshake::write_message(const u8* payload, std::size_t payload_len,
                                    std::vector<u8>& out) {
    out.clear();
    if (done_) {
        return false;
    }
    const std::vector<token>& tokens = xx_message(message_index_);
    for (std::size_t t = 0; t < tokens.size(); t++) {
        u8 shared[32];
        switch (tokens[t]) {
            case tok_e:
                ensure_ephemeral();
                out.insert(out.end(), e_pub_, e_pub_ + dh_len);
                mix_hash(e_pub_, dh_len);
                break;
            case tok_s: {
                std::vector<u8> enc;
                encrypt_and_hash(s_pub_, dh_len, enc);
                out.insert(out.end(), enc.begin(), enc.end());
                break;
            }
            case tok_ee:
                if (!dh(e_priv_, re_, shared) || !mix_key(shared, dh_len)) {
                    return false;
                }
                break;
            case tok_es:
                if (!dh(initiator_ ? e_priv_ : s_priv_, initiator_ ? remote_static_ : re_, shared) ||
                    !mix_key(shared, dh_len)) {
                    return false;
                }
                break;
            case tok_se:
                if (!dh(initiator_ ? s_priv_ : e_priv_, initiator_ ? re_ : remote_static_, shared) ||
                    !mix_key(shared, dh_len)) {
                    return false;
                }
                break;
        }
        eosr::secure_wipe(shared, sizeof(shared));
    }
    std::vector<u8> enc_payload;
    encrypt_and_hash(payload, payload_len, enc_payload);
    out.insert(out.end(), enc_payload.begin(), enc_payload.end());

    if (message_index_ == 2) {
        done_ = true;
        std::memcpy(handshake_hash_, h_, hash_len);
    }
    message_index_++;
    return true;
}

bool noise_handshake::read_message(const u8* message, std::size_t message_len,
                                   std::vector<u8>& payload) {
    payload.clear();
    if (done_) {
        return false;
    }
    std::size_t offset = 0;
    const std::vector<token>& tokens = xx_message(message_index_);
    for (std::size_t t = 0; t < tokens.size(); t++) {
        u8 shared[32];
        switch (tokens[t]) {
            case tok_e:
                if (message_len - offset < dh_len) {
                    return false;
                }
                std::memcpy(re_, message + offset, dh_len);
                have_re_ = true;
                offset += dh_len;
                mix_hash(re_, dh_len);
                break;
            case tok_s: {
                const std::size_t slen = cs_.has_key() ? dh_len + tag_len : dh_len;
                if (message_len - offset < slen) {
                    return false;
                }
                std::vector<u8> rs;
                if (!decrypt_and_hash(message + offset, slen, rs) || rs.size() != dh_len) {
                    return false;
                }
                std::memcpy(remote_static_, rs.data(), dh_len);
                have_rs_ = true;
                offset += slen;
                break;
            }
            case tok_ee:
                if (!dh(e_priv_, re_, shared) || !mix_key(shared, dh_len)) {
                    return false;
                }
                break;
            case tok_es:
                if (!dh(initiator_ ? e_priv_ : s_priv_, initiator_ ? remote_static_ : re_, shared) ||
                    !mix_key(shared, dh_len)) {
                    return false;
                }
                break;
            case tok_se:
                if (!dh(initiator_ ? s_priv_ : e_priv_, initiator_ ? re_ : remote_static_, shared) ||
                    !mix_key(shared, dh_len)) {
                    return false;
                }
                break;
        }
        eosr::secure_wipe(shared, sizeof(shared));
    }
    if (offset > message_len) {
        return false;
    }
    if (!decrypt_and_hash(message + offset, message_len - offset, payload)) {
        return false;
    }

    if (message_index_ == 2) {
        done_ = true;
        std::memcpy(handshake_hash_, h_, hash_len);
    }
    message_index_++;
    return true;
}

void noise_handshake::split(cipher_state& send, cipher_state& recv) {
    std::vector<std::vector<u8> > out;
    hkdf_sha256(ck_, hash_len, 0, 0, 2, out);
    if (out.size() != 2) {
        return;
    }
    // The initiator sends with the first key and receives with the second; the responder is the
    // mirror. Both sides agree on which key is which.
    if (initiator_) {
        send.init_key(out[0].data());
        recv.init_key(out[1].data());
    } else {
        send.init_key(out[1].data());
        recv.init_key(out[0].data());
    }
    eosr::secure_wipe(out[0].data(), out[0].size());
    eosr::secure_wipe(out[1].data(), out[1].size());
}

} // namespace eosr
