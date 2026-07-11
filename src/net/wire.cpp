#include "net/wire.h"

namespace eosr {

static const std::size_t frame_prefix_len = 4;

void serialize(byte_writer& writer, const net_envelope& msg) {
    writer.put_u8(wire_protocol_version);
    writer.put_u16(msg.type_tag);
    writer.put_string(msg.source_id);
    writer.put_string(msg.dest_id);
    writer.put_string(msg.game_id);
    writer.put_svar(msg.timestamp);
    writer.put_bytes(msg.payload.data(), msg.payload.size());
}

bool deserialize(byte_reader& reader, net_envelope& msg) {
    u8 version = 0;
    if (!reader.get_u8(version) || version != wire_protocol_version) {
        return false;
    }
    return reader.get_u16(msg.type_tag)
        && reader.get_string(msg.source_id)
        && reader.get_string(msg.dest_id)
        && reader.get_string(msg.game_id)
        && reader.get_svar(msg.timestamp)
        && reader.get_bytes(msg.payload);
}

void serialize(byte_writer& writer, const emu_infos& msg) {
    writer.put_string(msg.emulator);
    writer.put_string(msg.appid);
    writer.put_string(msg.country);
    writer.put_string(msg.language);
    writer.put_string(msg.username);
}

bool deserialize(byte_reader& reader, emu_infos& msg) {
    return reader.get_string(msg.emulator)
        && reader.get_string(msg.appid)
        && reader.get_string(msg.country)
        && reader.get_string(msg.language)
        && reader.get_string(msg.username);
}

void serialize(byte_writer& writer, const connect_infos& msg) {
    writer.put_string(msg.product_user_id);
    writer.put_string(msg.display_name);
}

bool deserialize(byte_reader& reader, connect_infos& msg) {
    return reader.get_string(msg.product_user_id)
        && reader.get_string(msg.display_name);
}

void serialize(byte_writer& writer, const p2p_data& msg) {
    writer.put_string(msg.socket_name);
    writer.put_svar(msg.channel);
    writer.put_bytes(msg.data.data(), msg.data.size());
}

bool deserialize(byte_reader& reader, p2p_data& msg) {
    i64 channel = 0;
    if (!reader.get_string(msg.socket_name)) {
        return false;
    }
    if (!reader.get_svar(channel)) {
        return false;
    }
    if (!reader.get_bytes(msg.data)) {
        return false;
    }
    msg.channel = static_cast<i32>(channel);
    return true;
}

void serialize(byte_writer& writer, const session_infos& msg) {
    writer.put_string(msg.session_id);
    writer.put_string(msg.bucket_id);
    writer.put_u32(msg.max_players);
    writer.put_string(msg.host_address);
    writer.put_var(static_cast<u64>(msg.players.size()));
    for (std::size_t i = 0; i < msg.players.size(); i++) {
        writer.put_string(msg.players[i]);
    }
}

bool deserialize(byte_reader& reader, session_infos& msg) {
    if (!reader.get_string(msg.session_id)) {
        return false;
    }
    if (!reader.get_string(msg.bucket_id)) {
        return false;
    }
    if (!reader.get_u32(msg.max_players)) {
        return false;
    }
    if (!reader.get_string(msg.host_address)) {
        return false;
    }
    u64 count = 0;
    if (!reader.get_var(count)) {
        return false;
    }
    // Each element needs at least one byte, so a count larger than what remains is bogus.
    // This stops a malformed message from driving a huge loop or allocation.
    if (count > reader.remaining()) {
        return false;
    }
    msg.players.clear();
    for (u64 i = 0; i < count; i++) {
        std::string player;
        if (!reader.get_string(player)) {
            return false;
        }
        msg.players.push_back(player);
    }
    return true;
}

std::vector<u8> frame_message(const std::vector<u8>& body) {
    const u32 body_len = static_cast<u32>(body.size());
    std::vector<u8> out;
    out.reserve(frame_prefix_len + body.size());
    out.push_back(static_cast<u8>(body_len >> 24));
    out.push_back(static_cast<u8>(body_len >> 16));
    out.push_back(static_cast<u8>(body_len >> 8));
    out.push_back(static_cast<u8>(body_len));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool try_deframe(const u8* data, std::size_t len, std::vector<u8>& body, std::size_t& consumed) {
    if (len < frame_prefix_len) {
        return false;
    }
    const u32 body_len = (static_cast<u32>(data[0]) << 24) |
                         (static_cast<u32>(data[1]) << 16) |
                         (static_cast<u32>(data[2]) << 8) |
                         static_cast<u32>(data[3]);
    if (len - frame_prefix_len < body_len) {
        return false;
    }
    body.assign(data + frame_prefix_len, data + frame_prefix_len + body_len);
    consumed = frame_prefix_len + body_len;
    return true;
}

} // namespace eosr
