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
    session_join_request = 33,
    session_join_response = 34,
    session_destroy = 35,
    session_register = 36,
    session_unregister = 37,

    presence_request = 50,
    presence_info = 51,

    lobby_search = 60,
    lobby_search_response = 61,
    lobby_join_request = 62,
    lobby_join_response = 63,
    lobby_infos = 64,
    lobby_leave = 65,
    lobby_member_update = 66,
    lobby_destroy = 67,

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

// One key/value a session advertises. The value's meaning is decided by `value_type`, which is
// an EOS_EAttributeType: 0 bool, 1 int64, 2 double, 3 string.
struct session_attribute {
    std::string key;
    i32 value_type = 0;
    bool as_bool = false;
    i64 as_int64 = 0;
    f64 as_double = 0.0;
    std::string as_string;
    // EOS_ESessionAttributeAdvertisementType: whether a searcher may see this at all.
    i32 advertisement = 0;
};

// A session a host is advertising: everything a searcher needs to decide whether it wants in, and
// how to reach it.
struct session_infos {
    std::string session_id;
    std::string owner_id;   // the host's ProductUserId
    std::string bucket_id;
    std::string host_address;
    u32 max_players = 0;
    u32 open_slots = 0;
    i32 permission_level = 0; // EOS_EOnlineSessionPermissionLevel
    i32 state = 0;            // EOS_EOnlineSessionState
    bool allow_join_in_progress = false;
    bool invites_allowed = false;
    bool sanctions_enabled = false;
    // Who is in the session. This is the one roster: it is what EOS_ActiveSession reports, what
    // capacity is measured against, and who session traffic goes to. A player who leaves comes off
    // it, which is what gives their seat back. Only someone on it may move anyone else on or off.
    std::vector<std::string> registered_players;
    std::vector<session_attribute> attributes;
};

// One condition a searcher puts on a session's attributes.
struct search_parameter {
    session_attribute attribute;
    i32 comparison_op = 0; // EOS_EComparisonOp
};

// A searcher asking every peer for sessions it would want to join. `search_id` comes back on each
// answer so a searcher with several searches in flight knows which one was answered.
struct session_search {
    std::string search_id;
    std::string session_id;     // exact-id search; empty means no id filter
    std::string target_user_id; // find a particular player's session; empty means no user filter
    u32 max_results = 0;
    std::vector<search_parameter> parameters;
};

// A host answering a search with one session that matched it.
// A peer's whole answer to one search: every session it hosts that matched, in a single reply. One
// reply per peer -- even an empty one -- is what lets the searcher know that peer is done.
struct session_search_response {
    std::string search_id;
    std::vector<session_infos> sessions;
};

// A player asking a host to let it in. The host decides: it is the one that knows whether the
// session is full and whether it even knows this player.
struct session_join_request {
    std::string session_id;
};

// The host's verdict, sent to every member so they all learn who joined. `reason` is an
// EOS_EResult: Success, or why not.
struct session_join_response {
    std::string session_id;
    std::string player_id;
    i32 reason = 0;
};

// A host telling its members the session is gone.
struct session_destroy {
    std::string session_id;
};

// A member registering or unregistering players with the session.
struct session_members {
    std::string session_id;
    std::vector<std::string> player_ids;
};

// One key/value pair of a user's rich-presence data.
struct presence_data_record {
    std::string key;
    std::string value;
};

// A user's rich presence, keyed by their Epic account id. This is what one peer knows and tells
// the others about itself: its status, what it is playing, the free-text line a friend sees, and
// the opaque join string a game hands back to rejoin whatever the user is in.
struct presence_info {
    std::string epic_id;
    i32 status = 0; // EOS_Presence_EStatus
    std::string product_id;
    std::string product_version;
    std::string platform;
    std::string rich_text;
    std::string product_name;
    std::string integrated_platform;
    std::string join_info;
    std::vector<presence_data_record> records;
};

// A peer asking whoever is behind an Epic account id to send its presence back.
struct presence_request {
    std::string target_epic_id;
};

// One member of a lobby: who they are, the platform they are on, and the attributes they published
// about themselves. A lobby attribute reuses session_attribute -- the same key/typed-value shape --
// with its `advertisement` field carrying the lobby's EOS_ELobbyAttributeVisibility.
struct lobby_member {
    std::string user_id;
    i32 platform = 0;
    std::vector<session_attribute> attributes;
};

// A lobby a host is advertising: the Sessions shape, plus members that carry their own attributes
// and an owner with authority over the roster. The owner is the source of truth and broadcasts the
// whole thing on any change.
struct lobby_infos {
    std::string lobby_id;
    std::string owner_id; // the host's ProductUserId
    std::string bucket_id;
    i32 permission_level = 0; // EOS_ELobbyPermissionLevel
    u32 max_members = 0;
    u32 available_slots = 0;
    bool allow_invites = true;
    bool allow_host_migration = false;
    bool rtc_enabled = false;
    std::vector<session_attribute> attributes;
    std::vector<lobby_member> members;
};

// A searcher asking every peer for lobbies it would want to join.
struct lobby_search {
    std::string search_id;
    std::string lobby_id;       // exact-id search; empty means no id filter
    std::string target_user_id; // find a particular player's lobby; empty means no user filter
    u32 max_results = 0;
    std::vector<search_parameter> parameters;
};

// A peer's whole answer to one lobby search: every lobby it hosts that matched, in one reply.
struct lobby_search_response {
    std::string search_id;
    std::vector<lobby_infos> lobbies;
};

// A player asking a host to let it into a lobby, carrying the attributes it wants to join with.
struct lobby_join_request {
    std::string lobby_id;
    lobby_member member;
};

// The host's verdict on a join, sent to the joiner. `reason` is an EOS_EResult.
struct lobby_join_response {
    std::string lobby_id;
    std::string player_id;
    i32 reason = 0;
};

// A member telling the host it is leaving, or updating its own member attributes.
struct lobby_member_update {
    std::string lobby_id;
    lobby_member member;
};

// The host telling its members the lobby is gone.
struct lobby_destroy {
    std::string lobby_id;
};

} // namespace eosr

#endif
