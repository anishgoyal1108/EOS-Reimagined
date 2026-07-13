#include "net/peer_channel.h"

#include <cstring>

#include "common/crypto.h"
#include "net/messages.h"

namespace eosr {

namespace {

const char* const prologue_domain = "eosr-noise-v1";
const char* const udp_tag_domain = "eosr-udp-tag-v1";
const std::size_t key_len = 32;

// How far out of order an unreliable path may deliver before we stop being able to tell a late
// datagram from a replayed one. Sixty-four fits a u64 bitmap and is far wider than a LAN reorders.
const u64 replay_width = 64;

// The tag that says which peer a datagram is from. It is a digest of the key the sender seals with,
// which only the two ends hold, so it identifies the peer without naming it in the clear.
u32 tag_of_key(const u8 key[32]) {
    canonical_encoder encoder;
    encoder.field(udp_tag_domain).field(key, key_len);
    const std::vector<u8> digest = sha256(encoder.data().data(), encoder.data().size());
    return (static_cast<u32>(digest[0]) << 24) | (static_cast<u32>(digest[1]) << 16) |
           (static_cast<u32>(digest[2]) << 8) | static_cast<u32>(digest[3]);
}

// What a datagram is bound to besides its contents: who sent it, who it is for, and where it sits in
// the sequence. The socket and channel are inside the sealed body, so they cannot be altered either.
std::vector<u8> datagram_ad(const std::string& sender_id, const std::string& dest_id, u64 seq) {
    canonical_encoder encoder;
    encoder.field(sender_id).field(dest_id).field_u64(seq);
    return encoder.data();
}

// The AEAD nonce: four zero bytes then the sequence, little-endian, as Noise builds its own.
void build_datagram_nonce(u8 out[aead_nonce_len], u64 seq) {
    std::memset(out, 0, 4);
    for (int i = 0; i < 8; i++) {
        out[4 + i] = static_cast<u8>((seq >> (8 * i)) & 0xff);
    }
}

// The XX pattern with empty payloads has exactly three messages of exactly these sizes: 32 for the
// bare ephemeral; 32 + (32 + 16) + 16 once the first DH has keyed the cipher; (32 + 16) + 16 for the
// last. Checking the size before a byte of it is read is what makes a frame that is not a handshake
// message -- an older peer's plaintext advertisement, say, or noise from a port scan -- fail here
// rather than be mistaken for one with a very large payload.
const std::size_t handshake_message_size[3] = {32, 96, 64};
const int handshake_message_count = 3;

bool we_write(bool initiator, int index) {
    return (index % 2 == 0) == initiator;
}

} // namespace

std::vector<u8> mesh_prologue(const std::string& product_id, const std::string& sandbox_id,
                              const std::string& deployment_id) {
    canonical_encoder encoder;
    encoder.field(prologue_domain)
        .raw_u8(wire_protocol_version)
        .field(product_id)
        .field(sandbox_id)
        .field(deployment_id);
    return encoder.data();
}

replay_window::replay_window() : highest_(0), seen_(0) {
}

bool replay_window::accept(u64 seq) {
    if (seq > highest_) {
        const u64 shift = seq - highest_;
        seen_ = (shift >= replay_width) ? 0 : (seen_ << shift);
        seen_ |= 1; // bit 0 is the new highest
        highest_ = seq;
        return true;
    }
    const u64 age = highest_ - seq;
    if (age >= replay_width) {
        return false; // older than we can account for, so we must assume we have seen it
    }
    const u64 bit = static_cast<u64>(1) << age;
    if ((seen_ & bit) != 0) {
        return false; // already delivered once
    }
    seen_ |= bit;
    return true;
}

peer_channel::peer_channel(bool initiator, const u8 static_priv[32], const u8 static_pub[32],
                           const std::vector<u8>& prologue)
    : handshake_(new noise_handshake(initiator, static_priv, static_pub, prologue.data(),
                                     prologue.size())),
      udp_send_tag_(0),
      udp_recv_tag_(0),
      udp_seq_(0),
      initiator_(initiator),
      established_(false),
      message_index_(0) {
    std::memset(remote_static_, 0, sizeof(remote_static_));
    std::memset(udp_send_, 0, sizeof(udp_send_));
    std::memset(udp_recv_, 0, sizeof(udp_recv_));
}

peer_channel::~peer_channel() {
    secure_wipe(udp_send_, sizeof(udp_send_));
    secure_wipe(udp_recv_, sizeof(udp_recv_));
}

bool peer_channel::open(std::vector<u8>& message) {
    if (!initiator_ || message_index_ != 0 || !handshake_) {
        return false;
    }
    if (!handshake_->write_message(0, 0, message)) {
        return false;
    }
    message_index_++;
    return true;
}

peer_channel::step peer_channel::read_handshake(const u8* message, std::size_t len,
                                                std::vector<u8>& reply) {
    reply.clear();
    if (established_ || !handshake_ || message_index_ >= handshake_message_count) {
        return step_failed;
    }
    if (we_write(initiator_, message_index_)) {
        return step_failed; // the peer spoke out of turn
    }
    if (len != handshake_message_size[message_index_]) {
        return step_failed;
    }

    std::vector<u8> payload;
    if (!handshake_->read_message(message, len, payload)) {
        return step_failed;
    }
    message_index_++;

    if (message_index_ < handshake_message_count) {
        if (!handshake_->write_message(0, 0, reply)) {
            return step_failed;
        }
        message_index_++;
    }
    if (!handshake_->done()) {
        return step_continue;
    }

    // The peer is authenticated. Keep the key it proved -- every id we attribute to this connection
    // from here on is recomputed from it -- then key both transports and let the handshake go, so
    // its secrets do not sit in memory for the life of the session.
    if (!handshake_->have_remote_static()) {
        return step_failed;
    }
    std::memcpy(remote_static_, handshake_->remote_static(), key_len);
    if (!handshake_->split(send_, recv_, udp_send_, udp_recv_)) {
        return step_failed;
    }
    udp_send_tag_ = tag_of_key(udp_send_);
    udp_recv_tag_ = tag_of_key(udp_recv_);
    handshake_.reset();
    established_ = true;
    return step_done;
}

bool peer_channel::seal(const std::vector<u8>& plain, const u8* ad, std::size_t ad_len,
                        std::vector<u8>& cipher) {
    if (!established_) {
        return false;
    }
    cipher.resize(plain.size() + aead_tag_len);
    if (!send_.encrypt(cipher.data(), ad, ad_len, plain.data(), plain.size())) {
        cipher.clear();
        return false; // the counter is exhausted; the session has to end rather than reuse a nonce
    }
    return true;
}

bool peer_channel::unseal(const u8* cipher, std::size_t len, const u8* ad, std::size_t ad_len,
                          std::vector<u8>& plain) {
    if (!established_ || len < aead_tag_len) {
        return false;
    }
    plain.resize(len - aead_tag_len);
    if (!recv_.decrypt(plain.data(), ad, ad_len, cipher, len)) {
        plain.clear();
        return false;
    }
    return true;
}

bool peer_channel::seal_datagram(const std::string& sender_id, const std::string& dest_id,
                                 const std::vector<u8>& plain, u64& out_seq, std::vector<u8>& out) {
    if (!established_ || udp_seq_ == ~static_cast<u64>(0)) {
        return false;
    }
    const u64 seq = udp_seq_++;
    const std::vector<u8> ad = datagram_ad(sender_id, dest_id, seq);
    u8 nonce[aead_nonce_len];
    build_datagram_nonce(nonce, seq);
    out.resize(plain.size() + aead_tag_len);
    aead_encrypt(out.data(), out.data() + plain.size(), udp_send_, nonce, ad.data(), ad.size(),
                 plain.data(), plain.size());
    out_seq = seq;
    return true;
}

bool peer_channel::open_datagram(const std::string& sender_id, const std::string& dest_id, u64 seq,
                                 const u8* cipher, std::size_t len, std::vector<u8>& plain) {
    if (!established_ || len < aead_tag_len) {
        return false;
    }
    const std::vector<u8> ad = datagram_ad(sender_id, dest_id, seq);
    u8 nonce[aead_nonce_len];
    build_datagram_nonce(nonce, seq);
    const std::size_t body = len - aead_tag_len;
    plain.resize(body);
    if (!aead_decrypt(plain.data(), udp_recv_, nonce, cipher + body, ad.data(), ad.size(), cipher,
                      body)) {
        plain.clear();
        return false;
    }
    // The window is only spent on a datagram that authenticated. Doing it the other way round would
    // let anyone shove the window to the far end of the sequence space with a forged number and take
    // every datagram still in flight down with it.
    if (!udp_replay_.accept(seq)) {
        plain.clear();
        return false;
    }
    return true;
}

} // namespace eosr
