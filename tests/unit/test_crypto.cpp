#include "doctest.h"

#include <cstring>
#include <string>
#include <vector>

#include "common/crypto.h"
#include "common/types.h"

using namespace eosr;

namespace {

std::vector<u8> unhex(const std::string& hex) {
    std::vector<u8> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        out.push_back(static_cast<u8>((nib(hex[i]) << 4) | nib(hex[i + 1])));
    }
    return out;
}

std::string tohex(const u8* data, std::size_t len) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < len; i++) {
        s += d[data[i] >> 4];
        s += d[data[i] & 0xf];
    }
    return s;
}

} // namespace

// RFC 7748 section 6.1: the Curve25519 Diffie-Hellman known-answer.
TEST_CASE("X25519 matches the RFC 7748 test vector") {
    const std::vector<u8> a_priv =
        unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const std::vector<u8> b_priv =
        unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");

    u8 a_pub[32];
    u8 b_pub[32];
    x25519_public_key(a_pub, a_priv.data());
    x25519_public_key(b_pub, b_priv.data());
    CHECK(tohex(a_pub, 32) ==
          "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    CHECK(tohex(b_pub, 32) ==
          "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");

    // The shared secret is the same computed from either side.
    u8 ab[32];
    u8 ba[32];
    REQUIRE(x25519_shared(ab, a_priv.data(), b_pub));
    REQUIRE(x25519_shared(ba, b_priv.data(), a_pub));
    CHECK(tohex(ab, 32) ==
          "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    CHECK(tohex(ba, 32) == tohex(ab, 32));
}

TEST_CASE("X25519 rejects a low-order public key") {
    const std::vector<u8> priv =
        unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    u8 zero_point[32] = {0}; // a low-order point yields the all-zero shared secret
    u8 out[32];
    CHECK_FALSE(x25519_shared(out, priv.data(), zero_point));
}

// RFC 8439 section 2.8.2: the IETF ChaCha20-Poly1305 AEAD known-answer.
TEST_CASE("ChaCha20-Poly1305 matches the RFC 8439 AEAD test vector") {
    const std::string plaintext_str =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one "
        "tip for the future, sunscreen would be it.";
    const std::vector<u8> plain(plaintext_str.begin(), plaintext_str.end());
    const std::vector<u8> ad = unhex("50515253c0c1c2c3c4c5c6c7");
    const std::vector<u8> key =
        unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    const std::vector<u8> nonce = unhex("070000004041424344454647");

    std::vector<u8> cipher(plain.size());
    u8 mac[16];
    aead_encrypt(cipher.data(), mac, key.data(), nonce.data(), ad.data(), ad.size(), plain.data(),
                 plain.size());
    CHECK(tohex(cipher.data(), cipher.size()) ==
          "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fa"
          "fb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4"
          "fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116");
    CHECK(tohex(mac, 16) == "1ae10b594f09e26a7e902ecbd0600691");

    // It round-trips, and a single flipped tag or ciphertext bit fails to verify.
    std::vector<u8> recovered(cipher.size());
    REQUIRE(aead_decrypt(recovered.data(), key.data(), nonce.data(), mac, ad.data(), ad.size(),
                         cipher.data(), cipher.size()));
    CHECK(std::vector<u8>(recovered.begin(), recovered.end()) == plain);

    u8 bad_mac[16];
    std::memcpy(bad_mac, mac, 16);
    bad_mac[0] ^= 1;
    CHECK_FALSE(aead_decrypt(recovered.data(), key.data(), nonce.data(), bad_mac, ad.data(),
                             ad.size(), cipher.data(), cipher.size()));

    std::vector<u8> tampered = cipher;
    tampered[0] ^= 1;
    CHECK_FALSE(aead_decrypt(recovered.data(), key.data(), nonce.data(), mac, ad.data(), ad.size(),
                             tampered.data(), tampered.size()));
}

// RFC 5869 Test Case 3 (empty salt and info) exercises exactly Noise's HKDF chaining.
TEST_CASE("HKDF-SHA256 matches the RFC 5869 (empty salt/info) vector") {
    const std::vector<u8> ikm =
        unhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    std::vector<std::vector<u8> > out;
    hkdf_sha256(0, 0, ikm.data(), ikm.size(), 2, out);
    REQUIRE(out.size() == 2);
    // The first 32 bytes of the RFC's 42-byte OKM.
    CHECK(tohex(out[0].data(), out[0].size()) ==
          "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d");
    // The two outputs chain distinctly.
    CHECK(tohex(out[1].data(), out[1].size()) != tohex(out[0].data(), out[0].size()));
}

TEST_CASE("a generated X25519 keypair agrees on a shared secret") {
    // Two fresh keypairs Diffie-Hellman to the same secret -- the property the handshake relies on.
    u8 a_priv[32];
    u8 b_priv[32];
    for (int i = 0; i < 32; i++) {
        a_priv[i] = static_cast<u8>(0x11 * (i + 1));
        b_priv[i] = static_cast<u8>(0x07 * (i + 3));
    }
    u8 a_pub[32];
    u8 b_pub[32];
    x25519_public_key(a_pub, a_priv);
    x25519_public_key(b_pub, b_priv);
    u8 ab[32];
    u8 ba[32];
    REQUIRE(x25519_shared(ab, a_priv, b_pub));
    REQUIRE(x25519_shared(ba, b_priv, a_pub));
    CHECK(tohex(ab, 32) == tohex(ba, 32));
}
