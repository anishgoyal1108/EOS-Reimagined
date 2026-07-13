#include "doctest.h"

#include <string>
#include <vector>

#include "common/crypto.h"
#include "common/types.h"
#define private public
#include "net/secure_channel.h"
#undef private

using namespace eosr;

namespace {

#if defined(EOSR_TEST_WRAP_RANDOM_BYTES)
bool force_random_bytes_failure = false;
#endif

std::vector<u8> unhex(const std::string& hex) {
    std::vector<u8> out;
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

std::string tohex(const std::vector<u8>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < v.size(); i++) {
        s += d[v[i] >> 4];
        s += d[v[i] & 0xf];
    }
    return s;
}

std::vector<u8> pub_of(const std::vector<u8>& priv) {
    std::vector<u8> pub(32);
    x25519_public_key(pub.data(), priv.data());
    return pub;
}

} // namespace

#if defined(EOSR_TEST_WRAP_RANDOM_BYTES)
#if defined(_WIN32)
extern "C" bool __real__ZN4eosr8platform12random_bytesEPhy(u8* out, std::size_t len);
extern "C" bool __wrap__ZN4eosr8platform12random_bytesEPhy(u8* out, std::size_t len) {
    return force_random_bytes_failure ? false
                                      : __real__ZN4eosr8platform12random_bytesEPhy(out, len);
}
#else
extern "C" bool __real__ZN4eosr8platform12random_bytesEPhm(u8* out, std::size_t len);
extern "C" bool __wrap__ZN4eosr8platform12random_bytesEPhm(u8* out, std::size_t len) {
    return force_random_bytes_failure ? false
                                      : __real__ZN4eosr8platform12random_bytesEPhm(out, len);
}
#endif
#endif

// The canonical noise-c / cacophony known-answer for Noise_XX_25519_ChaChaPoly_SHA256, with fixed
// static and ephemeral keys. If the transcript deviates from the spec by a single byte, a message
// ciphertext or the handshake hash will not match, and we would fail to interoperate with any real
// Noise implementation.
TEST_CASE("Noise XX reproduces the official Noise_XX_25519_ChaChaPoly_SHA256 vector") {
    const std::vector<u8> prologue = unhex("50726f6c6f677565313233"); // "Prologue123"
    const std::vector<u8> init_s =
        unhex("e61ef9919cde45dd5f82166404bd08e38bceb5dfdfded0a34c8df7ed542214d1");
    const std::vector<u8> init_e =
        unhex("893e28b9dc6ca8d611ab664754b8ceb7bac5117349a4439a6b0569da977c464a");
    const std::vector<u8> resp_s =
        unhex("4a3acbfdb163dec651dfa3194dece676d437029c62a408b4c5ea9114246e4893");
    const std::vector<u8> resp_e =
        unhex("bbdb4cdbd309f1a1f2e1456967fe288cadd6f712d65dc7b7793d5e63da6b375b");

    const std::vector<u8> init_s_pub = pub_of(init_s);
    const std::vector<u8> resp_s_pub = pub_of(resp_s);

    noise_handshake initiator(true, init_s.data(), init_s_pub.data(), prologue.data(),
                              prologue.size());
    noise_handshake responder(false, resp_s.data(), resp_s_pub.data(), prologue.data(),
                              prologue.size());
    initiator.set_fixed_ephemeral(init_e.data());
    responder.set_fixed_ephemeral(resp_e.data());

    struct handshake_case {
        const char* payload;
        const char* ciphertext;
    };
    const handshake_case handshake[] = {
        {"4c756477696720766f6e204d69736573",
         "ca35def5ae56cec33dc2036731ab14896bc4c75dbb07a61f879f8e3afa4c7944"
         "4c756477696720766f6e204d69736573"},
        {"4d757272617920526f746862617264",
         "95ebc60d2b1fa672c1f46a8aa265ef51bfe38e7ccb39ec5be34069f144808843"
         "81cbad1f276e038c48378ffce2b65285e08d6b68aaa3629a5a8639392490e5b9"
         "4a6d8798832d5372f220f161d9c2df035528f8982ffe09be9b5c412f8a0db5d2"
         "1351f20af1370d0bf8ef1a8c59a30e"},
        {"462e20412e20486179656b",
         "c7195ffacac1307ff99046f219750fc47693e23c3cb08b89c2af808b444850a8"
         "589981dfbd651e6ff4724a781cc2aa6158c9fea0d4ec82a286427484c5b8c812"
         "3a7a6002b1de9f9775fc97"},
    };

    // -> e ; <- e,ee,s,es ; -> s,se
    for (int i = 0; i < 3; i++) {
        const std::vector<u8> payload = unhex(handshake[i].payload);
        noise_handshake& writer = (i % 2 == 0) ? initiator : responder;
        noise_handshake& reader = (i % 2 == 0) ? responder : initiator;

        std::vector<u8> message;
        REQUIRE(writer.write_message(payload.data(), payload.size(), message));
        CHECK(tohex(message) == handshake[i].ciphertext);

        std::vector<u8> recovered;
        REQUIRE(reader.read_message(message.data(), message.size(), recovered));
        CHECK(recovered == payload);
    }

    REQUIRE(initiator.done());
    REQUIRE(responder.done());
    const std::string expected_hash =
        "852a28d2146785c54bd8334f4e460c80d7fe4fd0cc5bc0abef2a24a3c4d44d5f";
    CHECK(tohex(std::vector<u8>(initiator.handshake_hash(), initiator.handshake_hash() + 32)) ==
          expected_hash);
    CHECK(tohex(std::vector<u8>(responder.handshake_hash(), responder.handshake_hash() + 32)) ==
          expected_hash);

    // Each side learned the other's real static public key.
    CHECK(std::vector<u8>(initiator.remote_static(), initiator.remote_static() + 32) == resp_s_pub);
    CHECK(std::vector<u8>(responder.remote_static(), responder.remote_static() + 32) == init_s_pub);

    // Transport: the vector continues the initiator/responder alternation (resp, init, resp).
    cipher_state init_send;
    cipher_state init_recv;
    cipher_state resp_send;
    cipher_state resp_recv;
    initiator.split(init_send, init_recv);
    responder.split(resp_send, resp_recv);

    struct transport_case {
        bool responder_sends;
        const char* payload;
        const char* ciphertext;
    };
    const transport_case transport[] = {
        {true, "4361726c204d656e676572", "96763ed773f8e47bb3712f0e29b3060ffc956ffc146cee53d5e1df"},
        {false, "4a65616e2d426170746973746520536179",
         "3e40f15f6f3a46ae446b253bf8b1d9ffb6ed9b174d272328ff91a7e2e5c79c07f5"},
        {true, "457567656e2042f6686d20766f6e2042617765726b",
         "eb3f3515110702e047a6c9da4478b6ead94873c11c0f2d710ddb3f09fce024b3a58502ae3f"},
    };
    for (int i = 0; i < 3; i++) {
        const std::vector<u8> payload = unhex(transport[i].payload);
        cipher_state& sender = transport[i].responder_sends ? resp_send : init_send;
        cipher_state& receiver = transport[i].responder_sends ? init_recv : resp_recv;
        std::vector<u8> out(payload.size() + 16);
        sender.encrypt(out.data(), 0, 0, payload.data(), payload.size());
        CHECK(tohex(out) == transport[i].ciphertext);
        // And the intended receiver recovers it, confirming the split is oriented correctly.
        std::vector<u8> got(payload.size());
        REQUIRE(receiver.decrypt(got.data(), 0, 0, out.data(), out.size()));
        CHECK(got == payload);
    }
}

// Two fresh peers, random keys, must complete and agree -- and then talk over the transport.
TEST_CASE("a fresh Noise XX handshake agrees and carries transport traffic both ways") {
    u8 a_s[32];
    u8 b_s[32];
    for (int i = 0; i < 32; i++) {
        a_s[i] = static_cast<u8>(0x21 + i);
        b_s[i] = static_cast<u8>(0x90 - i);
    }
    u8 a_pub[32];
    u8 b_pub[32];
    x25519_public_key(a_pub, a_s);
    x25519_public_key(b_pub, b_s);
    const std::vector<u8> prologue = {'e', 'o', 's', 'r'};

    noise_handshake a(true, a_s, a_pub, prologue.data(), prologue.size());
    noise_handshake b(false, b_s, b_pub, prologue.data(), prologue.size());

    std::vector<u8> m, p;
    const u8 nothing = 0;
    REQUIRE(a.write_message(&nothing, 0, m));
    REQUIRE(b.read_message(m.data(), m.size(), p));
    REQUIRE(b.write_message(&nothing, 0, m));
    REQUIRE(a.read_message(m.data(), m.size(), p));
    REQUIRE(a.write_message(&nothing, 0, m));
    REQUIRE(b.read_message(m.data(), m.size(), p));

    REQUIRE(a.done());
    REQUIRE(b.done());
    CHECK(std::vector<u8>(a.handshake_hash(), a.handshake_hash() + 32) ==
          std::vector<u8>(b.handshake_hash(), b.handshake_hash() + 32));
    // Each side recomputed the other's real static key -- the property identity derivation rests on.
    CHECK(std::vector<u8>(a.remote_static(), a.remote_static() + 32) ==
          std::vector<u8>(b_pub, b_pub + 32));
    CHECK(std::vector<u8>(b.remote_static(), b.remote_static() + 32) ==
          std::vector<u8>(a_pub, a_pub + 32));

    cipher_state a_send, a_recv, b_send, b_recv;
    a.split(a_send, a_recv);
    b.split(b_send, b_recv);

    const std::string hello = "hello over the mesh";
    const std::vector<u8> plain(hello.begin(), hello.end());
    std::vector<u8> ct(plain.size() + 16);
    a_send.encrypt(ct.data(), 0, 0, plain.data(), plain.size());
    std::vector<u8> back(plain.size());
    REQUIRE(b_recv.decrypt(back.data(), 0, 0, ct.data(), ct.size()));
    CHECK(back == plain);

    // The other direction, and a tampered frame is rejected.
    b_send.encrypt(ct.data(), 0, 0, plain.data(), plain.size());
    ct[0] ^= 1;
    CHECK_FALSE(a_recv.decrypt(back.data(), 0, 0, ct.data(), ct.size()));
}

TEST_CASE("a tampered handshake message fails to read") {
    u8 a_s[32];
    u8 b_s[32];
    for (int i = 0; i < 32; i++) {
        a_s[i] = static_cast<u8>(0x40 + i);
        b_s[i] = static_cast<u8>(0x11 + i);
    }
    u8 a_pub[32];
    u8 b_pub[32];
    x25519_public_key(a_pub, a_s);
    x25519_public_key(b_pub, b_s);

    noise_handshake a(true, a_s, a_pub, 0, 0);
    noise_handshake b(false, b_s, b_pub, 0, 0);

    std::vector<u8> m, p;
    const u8 payload = 7;
    REQUIRE(a.write_message(&payload, 1, m));
    REQUIRE(b.read_message(m.data(), m.size(), p));
    // Second message carries an encrypted static; flipping a byte must break authentication.
    REQUIRE(b.write_message(&payload, 1, m));
    m[m.size() - 1] ^= 1;
    CHECK_FALSE(a.read_message(m.data(), m.size(), p));
}

#if defined(EOSR_TEST_WRAP_RANDOM_BYTES)
TEST_CASE("Noise handshake fails closed when ephemeral randomness is unavailable") {
    const std::vector<u8> static_priv(32, 0x42);
    const std::vector<u8> static_pub = pub_of(static_priv);
    noise_handshake handshake(true, static_priv.data(), static_pub.data(), 0, 0);
    std::vector<u8> message;

    force_random_bytes_failure = true;
    const bool wrote = handshake.write_message(0, 0, message);
    force_random_bytes_failure = false;

    CHECK_FALSE(wrote);
    CHECK(message.empty());
}
#endif

TEST_CASE("Noise handshake rejects an operation from the wrong role") {
    const std::vector<u8> static_priv(32, 0x24);
    const std::vector<u8> static_pub = pub_of(static_priv);
    noise_handshake responder(false, static_priv.data(), static_pub.data(), 0, 0);
    std::vector<u8> message;

    CHECK_FALSE(responder.write_message(0, 0, message));
    CHECK(message.empty());
}

TEST_CASE("Noise handshake does not expose transport keys before authentication completes") {
    const std::vector<u8> static_priv(32, 0x31);
    const std::vector<u8> static_pub = pub_of(static_priv);
    noise_handshake handshake(true, static_priv.data(), static_pub.data(), 0, 0);
    cipher_state send;
    cipher_state recv;

    handshake.split(send, recv);

    CHECK_FALSE(send.has_key());
    CHECK_FALSE(recv.has_key());
}

TEST_CASE("Noise Split cannot reset a live transport nonce") {
    const std::vector<u8> a_priv(32, 0x51);
    const std::vector<u8> b_priv(32, 0x62);
    const std::vector<u8> a_pub = pub_of(a_priv);
    const std::vector<u8> b_pub = pub_of(b_priv);
    const std::vector<u8> a_e(32, 0x73);
    const std::vector<u8> b_e(32, 0x84);
    noise_handshake a(true, a_priv.data(), a_pub.data(), 0, 0);
    noise_handshake b(false, b_priv.data(), b_pub.data(), 0, 0);
    a.set_fixed_ephemeral(a_e.data());
    b.set_fixed_ephemeral(b_e.data());

    std::vector<u8> message;
    std::vector<u8> payload;
    REQUIRE(a.write_message(0, 0, message));
    REQUIRE(b.read_message(message.data(), message.size(), payload));
    REQUIRE(b.write_message(0, 0, message));
    REQUIRE(a.read_message(message.data(), message.size(), payload));
    REQUIRE(a.write_message(0, 0, message));
    REQUIRE(b.read_message(message.data(), message.size(), payload));
    REQUIRE(a.done());

    cipher_state send;
    cipher_state recv;
    a.split(send, recv);
    const u8 plain[] = {1, 2, 3, 4};
    std::vector<u8> first(sizeof(plain) + 16);
    std::vector<u8> second(sizeof(plain) + 16);
    send.encrypt(first.data(), 0, 0, plain, sizeof(plain));

    // A second Split must not reinitialize the same key at nonce zero.
    a.split(send, recv);
    send.encrypt(second.data(), 0, 0, plain, sizeof(plain));
    CHECK(second != first);
}

TEST_CASE("Noise handshake enforces the 65535-byte message limit") {
    const std::vector<u8> static_priv(32, 0x19);
    const std::vector<u8> static_pub = pub_of(static_priv);
    const std::vector<u8> ephemeral(32, 0x2a);
    noise_handshake handshake(true, static_priv.data(), static_pub.data(), 0, 0);
    handshake.set_fixed_ephemeral(ephemeral.data());

    // Message zero contains a 32-byte ephemeral followed by its cleartext payload.
    std::vector<u8> payload(65504, 0x7f);
    std::vector<u8> message;
    CHECK_FALSE(handshake.write_message(payload.data(), payload.size(), message));
    CHECK(message.empty());
}

TEST_CASE("Noise transport counter never wraps to zero") {
    cipher_state state;
    u8 key[32] = {0};
    state.init_key(key);
    state.nonce_ = ~static_cast<u64>(0) - 1;

    const u8 plain = 0x5a;
    u8 first[17];
    u8 second[17];
    state.encrypt(first, 0, 0, &plain, 1); // nonce 2^64 - 2 is the last usable value
    state.encrypt(second, 0, 0, &plain, 1);

    const bool stopped_before_wrap = !state.has_key() || state.nonce_ == ~static_cast<u64>(0);
    CHECK(stopped_before_wrap);
}
