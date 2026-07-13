#ifndef EOSR_NET_WIRE_H
#define EOSR_NET_WIRE_H

#include <cstddef>
#include <vector>

#include "common/byte_buffer.h"
#include "common/types.h"
#include "net/messages.h"

namespace eosr {

// Envelope.
void serialize(byte_writer& writer, const net_envelope& msg);
bool deserialize(byte_reader& reader, net_envelope& msg);

// Sub-messages.
void serialize(byte_writer& writer, const net_advertise& msg);
bool deserialize(byte_reader& reader, net_advertise& msg);

void serialize(byte_writer& writer, const emu_infos& msg);
bool deserialize(byte_reader& reader, emu_infos& msg);

void serialize(byte_writer& writer, const connect_infos& msg);
bool deserialize(byte_reader& reader, connect_infos& msg);

void serialize(byte_writer& writer, const p2p_data& msg);
bool deserialize(byte_reader& reader, p2p_data& msg);

void serialize(byte_writer& writer, const session_attribute& msg);
bool deserialize(byte_reader& reader, session_attribute& msg);

void serialize(byte_writer& writer, const session_infos& msg);
bool deserialize(byte_reader& reader, session_infos& msg);

void serialize(byte_writer& writer, const session_search& msg);
bool deserialize(byte_reader& reader, session_search& msg);

void serialize(byte_writer& writer, const session_search_response& msg);
bool deserialize(byte_reader& reader, session_search_response& msg);

void serialize(byte_writer& writer, const session_join_request& msg);
bool deserialize(byte_reader& reader, session_join_request& msg);

void serialize(byte_writer& writer, const session_join_response& msg);
bool deserialize(byte_reader& reader, session_join_response& msg);

void serialize(byte_writer& writer, const session_destroy& msg);
bool deserialize(byte_reader& reader, session_destroy& msg);

void serialize(byte_writer& writer, const session_members& msg);
bool deserialize(byte_reader& reader, session_members& msg);

void serialize(byte_writer& writer, const presence_info& msg);
bool deserialize(byte_reader& reader, presence_info& msg);

void serialize(byte_writer& writer, const presence_request& msg);
bool deserialize(byte_reader& reader, presence_request& msg);

void serialize(byte_writer& writer, const lobby_infos& msg);
bool deserialize(byte_reader& reader, lobby_infos& msg);

void serialize(byte_writer& writer, const lobby_search& msg);
bool deserialize(byte_reader& reader, lobby_search& msg);

void serialize(byte_writer& writer, const lobby_search_response& msg);
bool deserialize(byte_reader& reader, lobby_search_response& msg);

void serialize(byte_writer& writer, const lobby_join_request& msg);
bool deserialize(byte_reader& reader, lobby_join_request& msg);

void serialize(byte_writer& writer, const lobby_join_response& msg);
bool deserialize(byte_reader& reader, lobby_join_response& msg);

void serialize(byte_writer& writer, const lobby_member_update& msg);
bool deserialize(byte_reader& reader, lobby_member_update& msg);

void serialize(byte_writer& writer, const lobby_destroy& msg);
bool deserialize(byte_reader& reader, lobby_destroy& msg);

// One message on a TCP stream is [u32 big-endian length][body]; UDP datagrams are unframed.
std::vector<u8> frame_message(const std::vector<u8>& body);

// Extract one framed message from the front of `data`. Returns true and fills `body` +
// `consumed` when a whole frame is present. Returns false with `consumed == 0` when more bytes
// are needed or the declared body exceeds max_message_size. Never reads out of bounds.
bool try_deframe(const u8* data, std::size_t len, std::vector<u8>& body, std::size_t& consumed);

} // namespace eosr

#endif
