#include "doctest.h"

#include <cstring>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/identity.h"
#include "net/peer_channel.h"

#include "fixed_profile.h"

using namespace eosr;

namespace {

const std::string game = "co-op-game";

// Drive the three-message XX pattern between two channels, exactly as the router does across a TCP
// stream. Returns false the moment either side refuses a message.
bool shake_hands(peer_channel& dialer, peer_channel& accepter) {
    std::vector<u8> opening;
    if (!dialer.open(opening)) {
        return false;
    }
    std::vector<u8> answer;
    if (accepter.read_handshake(opening.data(), opening.size(), answer) !=
        peer_channel::step_continue) {
        return false;
    }
    std::vector<u8> proof;
    if (dialer.read_handshake(answer.data(), answer.size(), proof) != peer_channel::step_done) {
        return false;
    }
    std::vector<u8> nothing;
    if (accepter.read_handshake(proof.data(), proof.size(), nothing) != peer_channel::step_done) {
        return false;
    }
    return dialer.established() && accepter.established() && nothing.empty();
}

std::vector<u8> bytes(const char* text) {
    const u8* start = reinterpret_cast<const u8*>(text);
    return std::vector<u8>(start, start + std::strlen(text));
}

} // namespace

TEST_CASE("two peers complete the handshake and each learns the other's real key") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    // Each side holds the key the other actually proved -- not one it was told about.
    CHECK(std::memcmp(dialer.remote_static(), bob.public_key(), profile_key_len) == 0);
    CHECK(std::memcmp(accepter.remote_static(), alice.public_key(), profile_key_len) == 0);
}

// The id a peer answers to is recomputed from the key it proved. There is no step at which a peer
// gets to say who it is, so there is nothing to lie about: an impostor who wants Alice's id would
// have to hold Alice's key, and holding it is what being Alice means.
// Spec: identity binding (wiki/internals/adr/0001 §11)
TEST_CASE("the id a peer gets is the one its key derives, and it never says it") {
    identity alice;
    identity impostor;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(impostor, 0x66);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, impostor.secret_key(), impostor.public_key(), prologue);
    peer_channel accepter(false, alice.secret_key(), alice.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    // Alice's side recomputes who dialed her. It is the impostor's id, and there was no field in
    // which the impostor could have claimed otherwise.
    const std::string proved = derive_product_user_id(accepter.remote_static(), game, "", "");
    CHECK(proved == test::id_in(impostor, game));
    CHECK(proved != test::id_in(alice, game));
}

TEST_CASE("the handshake is three messages of exactly the sizes we check for") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);

    std::vector<u8> opening;
    REQUIRE(dialer.open(opening));
    CHECK(opening.size() == 32); // e

    std::vector<u8> answer;
    REQUIRE(accepter.read_handshake(opening.data(), opening.size(), answer) ==
            peer_channel::step_continue);
    CHECK(answer.size() == 96); // e, ee, s, es -- the static is encrypted from here on

    std::vector<u8> proof;
    REQUIRE(dialer.read_handshake(answer.data(), answer.size(), proof) == peer_channel::step_done);
    CHECK(proof.size() == 64); // s, se
}

// The title is bound into the transcript, so two peers running different games do not fail to
// understand each other -- they fail to authenticate each other, which is the stronger thing.
TEST_CASE("a peer of another game cannot finish the handshake") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), mesh_prologue("game-one", "", ""));
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), mesh_prologue("game-two", "", ""));
    CHECK_FALSE(shake_hands(dialer, accepter));
    CHECK_FALSE(accepter.established());
}

TEST_CASE("a peer of another sandbox or deployment cannot finish the handshake either") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);

    peer_channel dialer(true, alice.secret_key(), alice.public_key(),
                        mesh_prologue(game, "live", "deploy-1"));
    peer_channel accepter(false, bob.secret_key(), bob.public_key(),
                          mesh_prologue(game, "live", "deploy-2"));
    CHECK_FALSE(shake_hands(dialer, accepter));
}

// The old mesh let a peer introduce itself with a plaintext advertisement, and believed it. That
// path is gone: the first thing off a connection has to be a handshake message of exactly the right
// size, so an unauthenticated hello is not a downgrade to refuse -- it is simply not a handshake.
// Spec: no downgrade (wiki/internals/adr/0001 §8, §11)
TEST_CASE("a plaintext hello is not mistaken for a handshake") {
    identity bob;
    test::seed_profile(bob, 0xb2);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), mesh_prologue(game, "", ""));

    const std::vector<u8> hello = bytes("i am alice, honestly, and here is my whole advertisement");
    std::vector<u8> reply;
    CHECK(accepter.read_handshake(hello.data(), hello.size(), reply) == peer_channel::step_failed);
    CHECK_FALSE(accepter.established());
    CHECK(reply.empty());
}

TEST_CASE("a handshake message of the right size but the wrong content is refused") {
    identity bob;
    test::seed_profile(bob, 0xb2);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), mesh_prologue(game, "", ""));

    // 32 bytes is the size of an opening `e`, so this gets past the length check and has to fail on
    // its own merits: it is a low-order point, and the DH it forces is the all-zero secret.
    std::vector<u8> low_order(32, 0);
    std::vector<u8> answer;
    const peer_channel::step first =
        accepter.read_handshake(low_order.data(), low_order.size(), answer);
    // Reading `e` cannot fail on its own -- the ephemeral is only mixed in on the next DH -- so the
    // refusal lands when the responder tries to use it.
    CHECK(first == peer_channel::step_failed);
    CHECK_FALSE(accepter.established());
}

TEST_CASE("a peer that speaks out of turn is refused") {
    identity alice;
    test::seed_profile(alice, 0xa1);
    peer_channel dialer(true, alice.secret_key(), alice.public_key(), mesh_prologue(game, "", ""));

    // The dialer speaks first. A message arriving before it has said anything is out of turn.
    std::vector<u8> premature(96, 0x41);
    std::vector<u8> reply;
    CHECK(dialer.read_handshake(premature.data(), premature.size(), reply) ==
          peer_channel::step_failed);
}

TEST_CASE("a sealed frame opens, and only for the peer it was sealed for") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    const std::vector<u8> plain = bytes("the host says the lobby is full");
    const u8 ad[4] = {0, 0, 0, 47};

    std::vector<u8> sealed;
    REQUIRE(dialer.seal(plain, ad, sizeof(ad), sealed));
    CHECK(sealed.size() == plain.size() + 16); // the tag rides along
    CHECK(sealed != plain);                    // and it is not the plaintext with a tag stapled on

    std::vector<u8> opened;
    REQUIRE(accepter.unseal(sealed.data(), sealed.size(), ad, sizeof(ad), opened));
    CHECK(opened == plain);
}

TEST_CASE("a tampered frame does not open") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    const std::vector<u8> plain = bytes("kick nobody");
    const u8 ad[4] = {0, 0, 0, 27};

    SUBCASE("a flipped bit in the body") {
        std::vector<u8> sealed;
        REQUIRE(dialer.seal(plain, ad, sizeof(ad), sealed));
        sealed[0] ^= 0x01;
        std::vector<u8> opened;
        CHECK_FALSE(accepter.unseal(sealed.data(), sealed.size(), ad, sizeof(ad), opened));
        CHECK(opened.empty()); // and it never hands back plaintext it could not verify
    }

    SUBCASE("a flipped bit in the tag") {
        std::vector<u8> sealed;
        REQUIRE(dialer.seal(plain, ad, sizeof(ad), sealed));
        sealed[sealed.size() - 1] ^= 0x80;
        std::vector<u8> opened;
        CHECK_FALSE(accepter.unseal(sealed.data(), sealed.size(), ad, sizeof(ad), opened));
    }

    SUBCASE("a frame re-cut to a different declared length") {
        std::vector<u8> sealed;
        REQUIRE(dialer.seal(plain, ad, sizeof(ad), sealed));
        const u8 lying_ad[4] = {0, 0, 0, 99}; // the length prefix is the associated data
        std::vector<u8> opened;
        CHECK_FALSE(
            accepter.unseal(sealed.data(), sealed.size(), lying_ad, sizeof(lying_ad), opened));
    }
}

// The counter is the nonce, so a frame only opens in the position it was sealed for. That is what
// makes a replayed or reordered frame fail rather than be acted on twice -- and there is no way to
// resynchronize, which is why the router ends the connection instead of trying.
TEST_CASE("a replayed or reordered frame does not open") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    const u8 ad[4] = {0, 0, 0, 21};
    std::vector<u8> first;
    std::vector<u8> second;
    REQUIRE(dialer.seal(bytes("promote me"), ad, sizeof(ad), first));
    REQUIRE(dialer.seal(bytes("promote me"), ad, sizeof(ad), second));
    // Identical plaintext, but the counter moved, so the bytes on the wire are not the same.
    CHECK(first != second);

    SUBCASE("the second frame cannot arrive first") {
        std::vector<u8> opened;
        CHECK_FALSE(accepter.unseal(second.data(), second.size(), ad, sizeof(ad), opened));
    }

    SUBCASE("the first frame cannot arrive twice") {
        std::vector<u8> opened;
        REQUIRE(accepter.unseal(first.data(), first.size(), ad, sizeof(ad), opened));
        CHECK_FALSE(accepter.unseal(first.data(), first.size(), ad, sizeof(ad), opened));
    }
}

TEST_CASE("nothing is sealed or opened before the peer is authenticated") {
    identity alice;
    test::seed_profile(alice, 0xa1);
    peer_channel dialer(true, alice.secret_key(), alice.public_key(), mesh_prologue(game, "", ""));

    std::vector<u8> sealed;
    CHECK_FALSE(dialer.seal(bytes("too early"), 0, 0, sealed));
    std::vector<u8> opened;
    const std::vector<u8> anything(32, 0x7f);
    CHECK_FALSE(dialer.unseal(anything.data(), anything.size(), 0, 0, opened));
}

TEST_CASE("a datagram opens for the peer it was sealed for, and its sequence is the nonce") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    // Both ends agree on the tag that says which peer a datagram came from, without either having
    // to write a name in the clear.
    CHECK(dialer.udp_send_tag() == accepter.udp_recv_tag());
    CHECK(accepter.udp_send_tag() == dialer.udp_recv_tag());

    const std::vector<u8> plain = bytes("player at 12,40 facing north");
    u64 seq = 12345;
    std::vector<u8> sealed;
    REQUIRE(dialer.seal_datagram("alice", "bob", plain, seq, sealed));
    CHECK(seq == 0); // the first datagram of the session
    CHECK(sealed.size() == plain.size() + 16);

    std::vector<u8> opened;
    REQUIRE(accepter.open_datagram("alice", "bob", seq, sealed.data(), sealed.size(), opened));
    CHECK(opened == plain);
}

// An unreliable path has nothing but the replay window between it and a datagram played back at it.
// Spec: replay window (wiki/internals/adr/0001 §7, §11)
TEST_CASE("a replayed datagram does not open twice") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    u64 seq = 0;
    std::vector<u8> sealed;
    REQUIRE(dialer.seal_datagram("alice", "bob", bytes("fire"), seq, sealed));

    std::vector<u8> opened;
    REQUIRE(accepter.open_datagram("alice", "bob", seq, sealed.data(), sealed.size(), opened));
    CHECK_FALSE(accepter.open_datagram("alice", "bob", seq, sealed.data(), sealed.size(), opened));
}

// Reordering is what an unreliable path *does*, so a late datagram is not a replayed one and must
// still be delivered. It is only once it falls out of the window that we can no longer tell.
TEST_CASE("a datagram may arrive late, but not from beyond the window") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    // Seal a run of them, then deliver out of order.
    std::vector<std::vector<u8> > sealed(80);
    std::vector<u64> seqs(80);
    for (std::size_t i = 0; i < sealed.size(); i++) {
        REQUIRE(dialer.seal_datagram("alice", "bob", bytes("tick"), seqs[i], sealed[i]));
        CHECK(seqs[i] == i);
    }

    std::vector<u8> opened;
    // The newest arrives first.
    REQUIRE(accepter.open_datagram("alice", "bob", seqs[70], sealed[70].data(), sealed[70].size(),
                                   opened));
    // One from just behind it was merely late, and is still wanted.
    CHECK(accepter.open_datagram("alice", "bob", seqs[60], sealed[60].data(), sealed[60].size(),
                                 opened));
    // But this one is 70 back, past the window, and we can no longer say whether we have had it.
    CHECK_FALSE(accepter.open_datagram("alice", "bob", seqs[0], sealed[0].data(), sealed[0].size(),
                                       opened));
}

// The window must only be spent on a datagram that authenticated. If a forged sequence could move
// it, anyone able to send us a packet could shove the window to the far end of the sequence space
// and take every datagram still in flight down with it.
TEST_CASE("a forged datagram cannot drag the replay window forward") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    u64 seq = 0;
    std::vector<u8> real;
    REQUIRE(dialer.seal_datagram("alice", "bob", bytes("move"), seq, real));

    // Garbage claiming an enormous sequence. It cannot open, so it must leave no trace.
    const std::vector<u8> junk(32, 0x5a);
    std::vector<u8> opened;
    CHECK_FALSE(accepter.open_datagram("alice", "bob", 1000000, junk.data(), junk.size(), opened));

    // The real datagram, which is far "older" than the forged one claimed to be, still lands.
    CHECK(accepter.open_datagram("alice", "bob", seq, real.data(), real.size(), opened));
}

TEST_CASE("a datagram bound to another sender, receiver, or sequence does not open") {
    identity alice;
    identity bob;
    test::seed_profile(alice, 0xa1);
    test::seed_profile(bob, 0xb2);
    const std::vector<u8> prologue = mesh_prologue(game, "", "");

    peer_channel dialer(true, alice.secret_key(), alice.public_key(), prologue);
    peer_channel accepter(false, bob.secret_key(), bob.public_key(), prologue);
    REQUIRE(shake_hands(dialer, accepter));

    u64 seq = 0;
    std::vector<u8> sealed;
    REQUIRE(dialer.seal_datagram("alice", "bob", bytes("shoot"), seq, sealed));
    std::vector<u8> opened;

    SUBCASE("a different sender") {
        CHECK_FALSE(
            accepter.open_datagram("carol", "bob", seq, sealed.data(), sealed.size(), opened));
    }
    SUBCASE("a different receiver") {
        CHECK_FALSE(
            accepter.open_datagram("alice", "carol", seq, sealed.data(), sealed.size(), opened));
    }
    SUBCASE("a different sequence") {
        CHECK_FALSE(
            accepter.open_datagram("alice", "bob", seq + 1, sealed.data(), sealed.size(), opened));
    }
    SUBCASE("a flipped bit") {
        sealed[0] ^= 0x01;
        CHECK_FALSE(
            accepter.open_datagram("alice", "bob", seq, sealed.data(), sealed.size(), opened));
    }
}
