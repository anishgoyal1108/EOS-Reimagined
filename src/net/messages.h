#ifndef EOSR_NET_MESSAGES_H
#define EOSR_NET_MESSAGES_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// Bumped when the envelope or message layout changes in an incompatible way.
constexpr u8 wire_protocol_version = 1;

// Largest message body we accept from a peer; a larger declared frame is treated as
// malformed rather than buffered, so a peer cannot exhaust memory.
constexpr u32 max_message_size = 4u * 1024u * 1024u;

// The kind of message an envelope carries. Replaces the protobuf oneof discriminator.
// More tags are added as their interfaces are implemented.
enum class message_type : u16 {
    none = 0,

    emu_infos_request = 1,
    emu_infos_response = 2,

    connect_request = 10,
    connect_response = 11,
    connect_infos = 12,

    p2p_connect_request = 20,
    p2p_connect_response = 21,
    p2p_data = 22,
    p2p_data_ack = 23,
    p2p_connection_close = 24,

    session_infos = 30,
    session_search = 31,
    session_search_response = 32,

    // Discovery and peer lifecycle. net_advertise travels over UDP and is consumed by the
    // router itself; the peer_connected / peer_disconnected envelopes are synthesized by the
    // router and dispatched to the interfaces so they can track the roster.
    net_advertise = 40,
    peer_connected = 41,
    peer_disconnected = 42
};

// The top-level frame that carries one sub-message between peers. `payload` holds the
// already-serialized sub-message; the router dispatches on `type_tag` and the interface
// decodes the payload. `dest_id` empty means broadcast.
struct net_envelope {
    u16 type_tag = 0;
    std::string source_id;
    std::string dest_id;
    std::string game_id;
    i64 timestamp = 0; // milliseconds since the Unix epoch
    std::vector<u8> payload;
};

// What an instance broadcasts so peers can find it: who it is, which game it is running, and
// the TCP port its mesh listener is accepting on.
struct net_advertise {
    std::string product_user_id;
    std::string game_id;
    u16 tcp_port = 0;
};

// The emu-info handshake peers exchange on connect so each learns the other's app and name.
struct emu_infos {
    std::string emulator;
    std::string appid;
    std::string country;
    std::string language;
    std::string username;
};

// A peer's Connect identity.
struct connect_infos {
    std::string product_user_id;
    std::string display_name;
};

// A P2P data packet.
struct p2p_data {
    std::string socket_name;
    i32 channel = 0;
    std::vector<u8> data;
};

// An advertised session. `players` is a repeated field, which exercises the codec's lists.
struct session_infos {
    std::string session_id;
    std::string bucket_id;
    u32 max_players = 0;
    std::string host_address;
    std::vector<std::string> players;
};

} // namespace eosr

#endif
