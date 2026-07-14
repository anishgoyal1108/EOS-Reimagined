#include "common/crypto.h"

#include <cstring>

#include "monocypher.h"

namespace eosr {

namespace {

const std::size_t sha256_block_size = 64;
const std::size_t sha256_digest_size = 32;

u32 rotr(u32 value, u32 bits) {
    return (value >> bits) | (value << (32 - bits));
}

// One SHA-256 compression over a 64-byte block, updating the eight state words.
void sha256_compress(u32 state[8], const u8 block[64]) {
    static const u32 k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    u32 w[64];
    for (u32 i = 0; i < 16; i++) {
        w[i] = (static_cast<u32>(block[i * 4]) << 24) | (static_cast<u32>(block[i * 4 + 1]) << 16) |
               (static_cast<u32>(block[i * 4 + 2]) << 8) | static_cast<u32>(block[i * 4 + 3]);
    }
    for (u32 i = 16; i < 64; i++) {
        const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = state[0], b = state[1], c = state[2], d = state[3];
    u32 e = state[4], f = state[5], g = state[6], h = state[7];
    for (u32 i = 0; i < 64; i++) {
        const u32 s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const u32 ch = (e & f) ^ (~e & g);
        const u32 t1 = h + s1 + ch + k[i] + w[i];
        const u32 s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const u32 maj = (a & b) ^ (a & c) ^ (b & c);
        const u32 t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

const char* base64url_table() {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
}

} // namespace

std::vector<u8> sha256(const u8* data, std::size_t len) {
    u32 state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    std::vector<u8> message(data, data + len);
    const u64 bit_length = static_cast<u64>(len) * 8;
    message.push_back(0x80);
    while (message.size() % sha256_block_size != 56) {
        message.push_back(0);
    }
    for (int i = 7; i >= 0; i--) {
        message.push_back(static_cast<u8>((bit_length >> (i * 8)) & 0xff));
    }

    for (std::size_t offset = 0; offset < message.size(); offset += sha256_block_size) {
        sha256_compress(state, &message[offset]);
    }

    std::vector<u8> digest(sha256_digest_size);
    for (u32 i = 0; i < 8; i++) {
        digest[i * 4] = static_cast<u8>((state[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<u8>((state[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<u8>((state[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<u8>(state[i] & 0xff);
    }
    // When hashing secret input (HMAC's key-derived blocks), the padded copy and the running state
    // are secret too. Wipe them rather than leave them in freed memory. The digest is the output.
    if (!message.empty()) {
        secure_wipe(message.data(), message.size());
    }
    secure_wipe(state, sizeof(state));
    return digest;
}

std::vector<u8> hmac_sha256(const u8* key, std::size_t key_len, const u8* message,
                            std::size_t message_len) {
    u8 block_key[sha256_block_size];
    std::memset(block_key, 0, sizeof(block_key));
    if (key_len > sha256_block_size) {
        const std::vector<u8> hashed = sha256(key, key_len);
        std::memcpy(block_key, hashed.data(), hashed.size());
    } else if (key_len != 0) {
        // A zero-length key (e.g. HKDF with an empty salt) leaves the block all-zero; copying from a
        // possibly-null pointer, even zero bytes, is undefined, so we skip it.
        std::memcpy(block_key, key, key_len);
    }

    std::vector<u8> inner;
    inner.reserve(sha256_block_size + message_len);
    for (std::size_t i = 0; i < sha256_block_size; i++) {
        inner.push_back(block_key[i] ^ 0x36);
    }
    inner.insert(inner.end(), message, message + message_len);
    std::vector<u8> inner_hash = sha256(inner.data(), inner.size());

    std::vector<u8> outer;
    outer.reserve(sha256_block_size + sha256_digest_size);
    for (std::size_t i = 0; i < sha256_block_size; i++) {
        outer.push_back(block_key[i] ^ 0x5c);
    }
    outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
    std::vector<u8> result = sha256(outer.data(), outer.size());

    // The padded key and the intermediate hash are derived from the key, so they are wiped rather
    // than left in freed memory. The result is the MAC and is the caller's to keep.
    secure_wipe(block_key, sizeof(block_key));
    if (!inner.empty()) {
        secure_wipe(inner.data(), inner.size());
    }
    if (!inner_hash.empty()) {
        secure_wipe(inner_hash.data(), inner_hash.size());
    }
    if (!outer.empty()) {
        secure_wipe(outer.data(), outer.size());
    }
    return result;
}

std::string base64url_encode(const u8* data, std::size_t len) {
    const char* table = base64url_table();
    const u32 sextet_mask = 0x3f;
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const u32 n = (static_cast<u32>(data[i]) << 16) | (static_cast<u32>(data[i + 1]) << 8) |
                      static_cast<u32>(data[i + 2]);
        out += table[(n >> 18) & sextet_mask];
        out += table[(n >> 12) & sextet_mask];
        out += table[(n >> 6) & sextet_mask];
        out += table[n & sextet_mask];
    }
    const std::size_t remaining = len - i;
    if (remaining == 1) {
        const u32 n = static_cast<u32>(data[i]) << 16;
        out += table[(n >> 18) & sextet_mask];
        out += table[(n >> 12) & sextet_mask];
    } else if (remaining == 2) {
        const u32 n = (static_cast<u32>(data[i]) << 16) | (static_cast<u32>(data[i + 1]) << 8);
        out += table[(n >> 18) & sextet_mask];
        out += table[(n >> 12) & sextet_mask];
        out += table[(n >> 6) & sextet_mask];
    }
    return out;
}

std::string base64url_encode(const std::string& text) {
    return base64url_encode(reinterpret_cast<const u8*>(text.data()), text.size());
}

// --- Authenticated-mesh primitives, over the vendored Monocypher (wiki/internals/adr/0001) ---

void x25519_public_key(u8 out_public[x25519_key_len], const u8 secret[x25519_key_len]) {
    crypto_x25519_public_key(out_public, secret);
}

bool x25519_shared(u8 out_shared[x25519_key_len], const u8 secret[x25519_key_len],
                   const u8 peer_public[x25519_key_len]) {
    crypto_x25519(out_shared, secret, peer_public);
    // A low-order peer public key yields the all-zero shared secret; reject it rather than key a
    // session off a value the peer forced. (Constant-time-ish: we always do the DH first.)
    u8 zero[x25519_key_len] = {0};
    return crypto_verify32(out_shared, zero) != 0; // verify32 returns 0 when equal
}

void aead_encrypt(u8* cipher, u8 mac[aead_tag_len], const u8 key[aead_key_len],
                  const u8 nonce[aead_nonce_len], const u8* ad, std::size_t ad_len,
                  const u8* plain, std::size_t len) {
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, cipher, mac, ad, ad_len, plain, len);
    crypto_wipe(&ctx, sizeof(ctx));
}

bool aead_decrypt(u8* plain, const u8 key[aead_key_len], const u8 nonce[aead_nonce_len],
                  const u8 mac[aead_tag_len], const u8* ad, std::size_t ad_len, const u8* cipher,
                  std::size_t len) {
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    const int ok = crypto_aead_read(&ctx, plain, mac, ad, ad_len, cipher, len);
    crypto_wipe(&ctx, sizeof(ctx));
    if (ok != 0) {
        // Never expose unverified plaintext.
        if (plain != 0 && len != 0) {
            crypto_wipe(plain, len);
        }
        return false;
    }
    return true;
}

void hkdf_sha256(const u8* salt, std::size_t salt_len, const u8* ikm, std::size_t ikm_len,
                 std::size_t num_outputs, std::vector<std::vector<u8> >& out) {
    // RFC 5869 with an empty info, which is exactly Noise's HKDF: extract, then chain
    // T(i) = HMAC(prk, T(i-1) || i) with a one-byte counter starting at 1.
    out.clear();
    if (num_outputs < 1 || num_outputs > 3) {
        return;
    }
    std::vector<u8> prk = hmac_sha256(salt, salt_len, ikm, ikm_len);
    std::vector<u8> previous;
    for (std::size_t i = 0; i < num_outputs; i++) {
        std::vector<u8> message = previous;
        message.push_back(static_cast<u8>(i + 1));
        std::vector<u8> block = hmac_sha256(prk.data(), prk.size(), message.data(), message.size());
        previous = block;
        out.push_back(block);
        // `block` and `message` are copies of key material now held in `out`/`previous`; wipe these.
        if (!block.empty()) {
            secure_wipe(block.data(), block.size());
        }
        if (!message.empty()) {
            secure_wipe(message.data(), message.size());
        }
    }
    // The extract key and the last chaining block are secret; the outputs belong to the caller.
    if (!prk.empty()) {
        secure_wipe(prk.data(), prk.size());
    }
    if (!previous.empty()) {
        secure_wipe(previous.data(), previous.size());
    }
}

void secure_wipe(void* secret, std::size_t size) {
    crypto_wipe(secret, size);
}

canonical_encoder& canonical_encoder::field(const u8* data, std::size_t len) {
    const u32 length = static_cast<u32>(len);
    buffer_.push_back(static_cast<u8>((length >> 24) & 0xff));
    buffer_.push_back(static_cast<u8>((length >> 16) & 0xff));
    buffer_.push_back(static_cast<u8>((length >> 8) & 0xff));
    buffer_.push_back(static_cast<u8>(length & 0xff));
    if (len != 0) {
        buffer_.insert(buffer_.end(), data, data + len);
    }
    return *this;
}

canonical_encoder& canonical_encoder::field(const std::string& text) {
    return field(reinterpret_cast<const u8*>(text.data()), text.size());
}

canonical_encoder& canonical_encoder::field_u32(u32 value) {
    const u8 bytes[4] = {
        static_cast<u8>((value >> 24) & 0xff), static_cast<u8>((value >> 16) & 0xff),
        static_cast<u8>((value >> 8) & 0xff), static_cast<u8>(value & 0xff)};
    return field(bytes, sizeof(bytes));
}

canonical_encoder& canonical_encoder::field_u64(u64 value) {
    u8 bytes[8];
    for (int i = 0; i < 8; i++) {
        bytes[i] = static_cast<u8>((value >> (56 - 8 * i)) & 0xff);
    }
    return field(bytes, sizeof(bytes));
}

canonical_encoder& canonical_encoder::raw_u8(u8 value) {
    buffer_.push_back(value);
    return *this;
}

} // namespace eosr
