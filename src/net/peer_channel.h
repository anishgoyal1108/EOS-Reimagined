#ifndef EOSR_NET_PEER_CHANNEL_H
#define EOSR_NET_PEER_CHANNEL_H

#include <memory>
#include <string>
#include <vector>

#include "common/types.h"
#include "net/secure_channel.h"

namespace eosr {

// What the handshake binds itself to: the protocol, the wire version, and the title. Two peers that
// disagree on any of it never finish a handshake, so a different game -- or a different version of
// this one -- cannot be talked into a session by claiming otherwise.
// Spec: prologue (docs/adr/0001 §5)
std::vector<u8> mesh_prologue(const std::string& product_id, const std::string& sandbox_id,
                              const std::string& deployment_id);

// Remembers which datagram sequences we have already opened, over a sliding window of the 64 most
// recent. An unreliable path is allowed to reorder within that; a sequence older than the window is
// not late, it is a replay, and one we have already seen is a replay whether it is late or not.
// Spec: replay window (docs/adr/0001 §7)
class replay_window {
public:
    replay_window();
    // True when `seq` is new, and it is now remembered. False for a duplicate, or for one so old we
    // can no longer say.
    bool accept(u64 seq);

private:
    u64 highest_;
    u64 seen_; // a bit per sequence at or below highest_, bit 0 being highest_ itself
};

// The authenticated channel to one peer.
//
// It runs Noise XX over the connection, and once that completes it is the only thing that turns an
// envelope into bytes and back. Every mesh frame after the handshake is sealed under a key the peer
// proved it holds, with a counter that makes a replayed, reordered, or truncated frame simply fail
// to open. The peer's identity is *recomputed* from the static key the handshake authenticated -- a
// claimed id is never read off the wire and believed.
// Spec: docs/adr/0001 §5, §6, §7
class peer_channel {
public:
    enum step {
        step_continue, // the handshake has further messages to exchange
        step_done,     // the peer is authenticated and the transport is keyed
        step_failed    // authentication failed; the caller must drop the connection
    };

    peer_channel(bool initiator, const u8 static_priv[32], const u8 static_pub[32],
                 const std::vector<u8>& prologue);
    ~peer_channel();

    peer_channel(const peer_channel&) = delete;
    peer_channel& operator=(const peer_channel&) = delete;

    // The initiator's opening message. False when the platform has no secure randomness, in which
    // case we must not proceed with a guessable ephemeral.
    bool open(std::vector<u8>& message);

    // Consume one handshake message, writing our reply into `reply` when it is our turn to speak.
    // A message of the wrong size, out of turn, or with a bad tag fails: there is no recovery from a
    // handshake that did not go exactly as the pattern says.
    step read_handshake(const u8* message, std::size_t len, std::vector<u8>& reply);

    bool established() const { return established_; }

    // The peer's static public key, once established(). The ids it certifies are recomputed from it.
    const u8* remote_static() const { return remote_static_; }

    // Seal an envelope for this peer, and open one from it. `ad` is the frame's length prefix, so a
    // frame cannot be re-cut to a different length without failing its tag.
    bool seal(const std::vector<u8>& plain, const u8* ad, std::size_t ad_len,
              std::vector<u8>& cipher);
    bool unseal(const u8* cipher, std::size_t len, const u8* ad, std::size_t ad_len,
                std::vector<u8>& plain);

    // Seal a datagram for the P2P data path, under keys derived from the handshake's secret chaining
    // key and independent of the two above. The sequence is ours and strictly increasing, and it is
    // also the nonce, so a datagram only ever opens in the place it was sealed for.
    bool seal_datagram(const std::string& sender_id, const std::string& dest_id,
                       const std::vector<u8>& plain, u64& out_seq, std::vector<u8>& out);
    // Open one. False when it does not authenticate, and false for a sequence we have already seen
    // or can no longer place -- an unreliable path has nothing but the replay window between it and
    // a datagram played back at it.
    bool open_datagram(const std::string& sender_id, const std::string& dest_id, u64 seq,
                       const u8* cipher, std::size_t len, std::vector<u8>& plain);

    // Which peer a datagram is from. The tag is derived from the key it was sealed with, and only
    // the two ends hold that key, so a peer is recognized without anyone naming themselves in the
    // clear -- and a datagram tagged for us that we cannot open is simply not from the peer it
    // claims. Valid once established().
    u32 udp_send_tag() const { return udp_send_tag_; }
    u32 udp_recv_tag() const { return udp_recv_tag_; }

private:
    std::unique_ptr<noise_handshake> handshake_;
    cipher_state send_;
    cipher_state recv_;
    u8 remote_static_[32];
    u8 udp_send_[32];
    u8 udp_recv_[32];
    u32 udp_send_tag_;
    u32 udp_recv_tag_;
    u64 udp_seq_;
    replay_window udp_replay_;
    bool initiator_;
    bool established_;
    int message_index_;
};

} // namespace eosr

#endif
