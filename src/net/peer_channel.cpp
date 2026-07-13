#include "net/peer_channel.h"

#include <cstring>

#include "common/crypto.h"
#include "net/messages.h"

namespace eosr {

namespace {

const char* const prologue_domain = "eosr-noise-v1";
const std::size_t key_len = 32;

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

peer_channel::peer_channel(bool initiator, const u8 static_priv[32], const u8 static_pub[32],
                           const std::vector<u8>& prologue)
    : handshake_(new noise_handshake(initiator, static_priv, static_pub, prologue.data(),
                                     prologue.size())),
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

} // namespace eosr
