#include "net/wire.h"

#include <limits>

namespace eosr {

namespace {

// EOS_EAttributeType, as it travels on the wire.
const i64 attribute_bool = 0;
const i64 attribute_int64 = 1;
const i64 attribute_double = 2;
const i64 attribute_string = 3;

// Upper bounds on how many elements an inbound list may claim. The byte-count bound alone is far
// too loose -- a std::string is 32 bytes, so a 4 MiB message could ask us to reserve room for four
// million of them (~128 MB), and reserve() throwing bad_alloc would cross the C ABI as a crash.
// These mirror the EOS ABI maxima; a peer that exceeds them is malformed and rejected.
const u64 max_players_in_list = 1000;   // EOS_SESSIONS_MAXREGISTEREDPLAYERS
const u64 max_attributes_in_list = 64;  // EOS_SESSIONMODIFICATION_MAX_SESSION_ATTRIBUTES
const u64 max_search_parameters = 64;   // one condition per attribute is already generous
const u64 max_search_results = 200;     // EOS_SESSIONS/LOBBY_MAX_SEARCH_RESULTS
const u64 max_data_records = 32;        // EOS_PRESENCE_DATA_MAX_KEYS
const u64 max_lobby_members = 64;       // EOS_LOBBY_MAX_LOBBY_MEMBERS

// Read a list length that is safe to reserve: it must fit both the bytes left (every element costs
// at least one) and the ABI cap for this kind of list.
bool read_list_count(byte_reader& reader, u64& count, u64 cap) {
    return reader.get_var(count) && count <= cap && count <= reader.remaining();
}

} // namespace

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

void serialize(byte_writer& writer, const net_advertise& msg) {
    writer.put_string(msg.product_user_id);
    writer.put_string(msg.game_id);
    writer.put_u16(msg.tcp_port);
    writer.put_u16(msg.udp_port);
}
bool deserialize(byte_reader& reader, net_advertise& msg) {
    return reader.get_string(msg.product_user_id)
        && reader.get_string(msg.game_id)
        && reader.get_u16(msg.tcp_port)
        && reader.get_u16(msg.udp_port);
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
    if (channel < std::numeric_limits<i32>::min() ||
        channel > std::numeric_limits<i32>::max()) {
        return false;
    }
    if (!reader.get_bytes(msg.data)) {
        return false;
    }
    msg.channel = static_cast<i32>(channel);
    return true;
}

void serialize(byte_writer& writer, const session_attribute& msg) {
    writer.put_string(msg.key);
    writer.put_svar(msg.value_type);
    writer.put_svar(msg.advertisement);
    // Only the value the type names is on the wire; the others are never read.
    switch (msg.value_type) {
        case attribute_bool: writer.put_bool(msg.as_bool); break;
        case attribute_int64: writer.put_svar(msg.as_int64); break;
        case attribute_double: writer.put_f64(msg.as_double); break;
        default: writer.put_string(msg.as_string); break;
    }
}

bool deserialize(byte_reader& reader, session_attribute& msg) {
    i64 value_type = 0;
    i64 advertisement = 0;
    if (!reader.get_string(msg.key) || !reader.get_svar(value_type) ||
        !reader.get_svar(advertisement)) {
        return false;
    }
    // A peer picks the type, so a type we do not know is malformed rather than something to guess.
    if (value_type < attribute_bool || value_type > attribute_string) {
        return false;
    }
    msg.value_type = static_cast<i32>(value_type);
    msg.advertisement = static_cast<i32>(advertisement);
    switch (msg.value_type) {
        case attribute_bool: return reader.get_bool(msg.as_bool);
        case attribute_int64: return reader.get_svar(msg.as_int64);
        case attribute_double: return reader.get_f64(msg.as_double);
        default: return reader.get_string(msg.as_string);
    }
}

void serialize(byte_writer& writer, const session_infos& msg) {
    writer.put_string(msg.session_id);
    writer.put_string(msg.owner_id);
    writer.put_string(msg.bucket_id);
    writer.put_string(msg.host_address);
    writer.put_u32(msg.max_players);
    writer.put_u32(msg.open_slots);
    writer.put_svar(msg.permission_level);
    writer.put_svar(msg.state);
    writer.put_bool(msg.allow_join_in_progress);
    writer.put_bool(msg.invites_allowed);
    writer.put_bool(msg.sanctions_enabled);

    writer.put_var(static_cast<u64>(msg.registered_players.size()));
    for (std::size_t i = 0; i < msg.registered_players.size(); i++) {
        writer.put_string(msg.registered_players[i]);
    }
    writer.put_var(static_cast<u64>(msg.attributes.size()));
    for (std::size_t i = 0; i < msg.attributes.size(); i++) {
        serialize(writer, msg.attributes[i]);
    }
}

bool deserialize(byte_reader& reader, session_infos& msg) {
    i64 permission_level = 0;
    i64 state = 0;
    if (!reader.get_string(msg.session_id) || !reader.get_string(msg.owner_id) ||
        !reader.get_string(msg.bucket_id) || !reader.get_string(msg.host_address) ||
        !reader.get_u32(msg.max_players) || !reader.get_u32(msg.open_slots) ||
        !reader.get_svar(permission_level) || !reader.get_svar(state) ||
        !reader.get_bool(msg.allow_join_in_progress) || !reader.get_bool(msg.invites_allowed) ||
        !reader.get_bool(msg.sanctions_enabled)) {
        return false;
    }
    msg.permission_level = static_cast<i32>(permission_level);
    msg.state = static_cast<i32>(state);

    u64 count = 0;
    if (!read_list_count(reader, count, max_players_in_list)) {
        return false;
    }
    msg.registered_players.clear();
    msg.registered_players.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        std::string player;
        if (!reader.get_string(player)) {
            return false;
        }
        msg.registered_players.push_back(player);
    }

    if (!read_list_count(reader, count, max_attributes_in_list)) {
        return false;
    }
    msg.attributes.clear();
    msg.attributes.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        session_attribute attribute;
        if (!deserialize(reader, attribute)) {
            return false;
        }
        msg.attributes.push_back(attribute);
    }
    return true;
}

void serialize(byte_writer& writer, const session_search& msg) {
    writer.put_string(msg.search_id);
    writer.put_string(msg.session_id);
    writer.put_string(msg.target_user_id);
    writer.put_u32(msg.max_results);
    writer.put_var(static_cast<u64>(msg.parameters.size()));
    for (std::size_t i = 0; i < msg.parameters.size(); i++) {
        serialize(writer, msg.parameters[i].attribute);
        writer.put_svar(msg.parameters[i].comparison_op);
    }
}

bool deserialize(byte_reader& reader, session_search& msg) {
    if (!reader.get_string(msg.search_id) || !reader.get_string(msg.session_id) ||
        !reader.get_string(msg.target_user_id) || !reader.get_u32(msg.max_results)) {
        return false;
    }
    u64 count = 0;
    if (!read_list_count(reader, count, max_search_parameters)) {
        return false;
    }
    msg.parameters.clear();
    msg.parameters.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        search_parameter parameter;
        i64 comparison_op = 0;
        if (!deserialize(reader, parameter.attribute) || !reader.get_svar(comparison_op)) {
            return false;
        }
        parameter.comparison_op = static_cast<i32>(comparison_op);
        msg.parameters.push_back(parameter);
    }
    return true;
}

void serialize(byte_writer& writer, const session_search_response& msg) {
    writer.put_string(msg.search_id);
    writer.put_var(static_cast<u64>(msg.sessions.size()));
    for (std::size_t i = 0; i < msg.sessions.size(); i++) {
        serialize(writer, msg.sessions[i]);
    }
}

bool deserialize(byte_reader& reader, session_search_response& msg) {
    if (!reader.get_string(msg.search_id)) {
        return false;
    }
    u64 count = 0;
    if (!read_list_count(reader, count, max_search_results)) {
        return false;
    }
    msg.sessions.clear();
    msg.sessions.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        session_infos session;
        if (!deserialize(reader, session)) {
            return false;
        }
        msg.sessions.push_back(session);
    }
    return true;
}

void serialize(byte_writer& writer, const session_join_request& msg) {
    writer.put_string(msg.session_id);
}

bool deserialize(byte_reader& reader, session_join_request& msg) {
    return reader.get_string(msg.session_id);
}

void serialize(byte_writer& writer, const session_join_response& msg) {
    writer.put_string(msg.session_id);
    writer.put_string(msg.player_id);
    writer.put_svar(msg.reason);
}

bool deserialize(byte_reader& reader, session_join_response& msg) {
    i64 reason = 0;
    if (!reader.get_string(msg.session_id) || !reader.get_string(msg.player_id) ||
        !reader.get_svar(reason)) {
        return false;
    }
    msg.reason = static_cast<i32>(reason);
    return true;
}

void serialize(byte_writer& writer, const session_destroy& msg) {
    writer.put_string(msg.session_id);
}

bool deserialize(byte_reader& reader, session_destroy& msg) {
    return reader.get_string(msg.session_id);
}

void serialize(byte_writer& writer, const session_members& msg) {
    writer.put_string(msg.session_id);
    writer.put_var(static_cast<u64>(msg.player_ids.size()));
    for (std::size_t i = 0; i < msg.player_ids.size(); i++) {
        writer.put_string(msg.player_ids[i]);
    }
}

bool deserialize(byte_reader& reader, session_members& msg) {
    if (!reader.get_string(msg.session_id)) {
        return false;
    }
    u64 count = 0;
    if (!read_list_count(reader, count, max_players_in_list)) {
        return false;
    }
    msg.player_ids.clear();
    msg.player_ids.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        std::string player;
        if (!reader.get_string(player)) {
            return false;
        }
        msg.player_ids.push_back(player);
    }
    return true;
}

void serialize(byte_writer& writer, const presence_info& msg) {
    writer.put_string(msg.epic_id);
    writer.put_svar(msg.status);
    writer.put_string(msg.product_id);
    writer.put_string(msg.product_version);
    writer.put_string(msg.platform);
    writer.put_string(msg.rich_text);
    writer.put_string(msg.product_name);
    writer.put_string(msg.integrated_platform);
    writer.put_string(msg.join_info);
    writer.put_var(static_cast<u64>(msg.records.size()));
    for (std::size_t i = 0; i < msg.records.size(); i++) {
        writer.put_string(msg.records[i].key);
        writer.put_string(msg.records[i].value);
    }
}

bool deserialize(byte_reader& reader, presence_info& msg) {
    i64 status = 0;
    if (!reader.get_string(msg.epic_id) || !reader.get_svar(status) ||
        !reader.get_string(msg.product_id) || !reader.get_string(msg.product_version) ||
        !reader.get_string(msg.platform) || !reader.get_string(msg.rich_text) ||
        !reader.get_string(msg.product_name) || !reader.get_string(msg.integrated_platform) ||
        !reader.get_string(msg.join_info)) {
        return false;
    }
    msg.status = static_cast<i32>(status);

    u64 count = 0;
    if (!read_list_count(reader, count, max_data_records)) {
        return false;
    }
    msg.records.clear();
    msg.records.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        presence_data_record record;
        if (!reader.get_string(record.key) || !reader.get_string(record.value)) {
            return false;
        }
        msg.records.push_back(record);
    }
    return true;
}

void serialize(byte_writer& writer, const presence_request& msg) {
    writer.put_string(msg.target_epic_id);
}

bool deserialize(byte_reader& reader, presence_request& msg) {
    return reader.get_string(msg.target_epic_id);
}

namespace {

void write_attributes(byte_writer& writer, const std::vector<session_attribute>& attrs) {
    writer.put_var(static_cast<u64>(attrs.size()));
    for (std::size_t i = 0; i < attrs.size(); i++) {
        serialize(writer, attrs[i]);
    }
}

bool read_attributes(byte_reader& reader, std::vector<session_attribute>& attrs) {
    u64 count = 0;
    if (!read_list_count(reader, count, max_attributes_in_list)) {
        return false;
    }
    attrs.clear();
    attrs.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        session_attribute attribute;
        if (!deserialize(reader, attribute)) {
            return false;
        }
        attrs.push_back(attribute);
    }
    return true;
}

void write_member(byte_writer& writer, const lobby_member& member) {
    writer.put_string(member.user_id);
    writer.put_svar(member.platform);
    write_attributes(writer, member.attributes);
}

bool read_member(byte_reader& reader, lobby_member& member) {
    i64 platform = 0;
    if (!reader.get_string(member.user_id) || !reader.get_svar(platform)) {
        return false;
    }
    member.platform = static_cast<i32>(platform);
    return read_attributes(reader, member.attributes);
}

} // namespace

void serialize(byte_writer& writer, const lobby_infos& msg) {
    writer.put_string(msg.lobby_id);
    writer.put_string(msg.owner_id);
    writer.put_string(msg.bucket_id);
    writer.put_svar(msg.permission_level);
    writer.put_u32(msg.max_members);
    writer.put_u32(msg.available_slots);
    writer.put_bool(msg.allow_invites);
    writer.put_bool(msg.allow_host_migration);
    writer.put_bool(msg.allow_join_by_id);
    writer.put_bool(msg.rtc_enabled);
    write_attributes(writer, msg.attributes);
    writer.put_var(static_cast<u64>(msg.members.size()));
    for (std::size_t i = 0; i < msg.members.size(); i++) {
        write_member(writer, msg.members[i]);
    }
}

bool deserialize(byte_reader& reader, lobby_infos& msg) {
    i64 permission_level = 0;
    if (!reader.get_string(msg.lobby_id) || !reader.get_string(msg.owner_id) ||
        !reader.get_string(msg.bucket_id) || !reader.get_svar(permission_level) ||
        !reader.get_u32(msg.max_members) || !reader.get_u32(msg.available_slots) ||
        !reader.get_bool(msg.allow_invites) || !reader.get_bool(msg.allow_host_migration) ||
        !reader.get_bool(msg.allow_join_by_id) || !reader.get_bool(msg.rtc_enabled)) {
        return false;
    }
    msg.permission_level = static_cast<i32>(permission_level);
    if (!read_attributes(reader, msg.attributes)) {
        return false;
    }
    u64 count = 0;
    if (!read_list_count(reader, count, max_lobby_members)) {
        return false;
    }
    msg.members.clear();
    msg.members.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        lobby_member member;
        if (!read_member(reader, member)) {
            return false;
        }
        msg.members.push_back(member);
    }
    return true;
}

void serialize(byte_writer& writer, const lobby_search& msg) {
    writer.put_string(msg.search_id);
    writer.put_string(msg.lobby_id);
    writer.put_string(msg.target_user_id);
    writer.put_u32(msg.max_results);
    writer.put_var(static_cast<u64>(msg.parameters.size()));
    for (std::size_t i = 0; i < msg.parameters.size(); i++) {
        serialize(writer, msg.parameters[i].attribute);
        writer.put_svar(msg.parameters[i].comparison_op);
    }
}

bool deserialize(byte_reader& reader, lobby_search& msg) {
    if (!reader.get_string(msg.search_id) || !reader.get_string(msg.lobby_id) ||
        !reader.get_string(msg.target_user_id) || !reader.get_u32(msg.max_results)) {
        return false;
    }
    u64 count = 0;
    if (!read_list_count(reader, count, max_search_parameters)) {
        return false;
    }
    msg.parameters.clear();
    msg.parameters.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        search_parameter parameter;
        i64 comparison_op = 0;
        if (!deserialize(reader, parameter.attribute) || !reader.get_svar(comparison_op)) {
            return false;
        }
        parameter.comparison_op = static_cast<i32>(comparison_op);
        msg.parameters.push_back(parameter);
    }
    return true;
}

void serialize(byte_writer& writer, const lobby_search_response& msg) {
    writer.put_string(msg.search_id);
    writer.put_var(static_cast<u64>(msg.lobbies.size()));
    for (std::size_t i = 0; i < msg.lobbies.size(); i++) {
        serialize(writer, msg.lobbies[i]);
    }
}

bool deserialize(byte_reader& reader, lobby_search_response& msg) {
    if (!reader.get_string(msg.search_id)) {
        return false;
    }
    u64 count = 0;
    if (!read_list_count(reader, count, max_search_results)) {
        return false;
    }
    msg.lobbies.clear();
    msg.lobbies.reserve(static_cast<std::size_t>(count));
    for (u64 i = 0; i < count; i++) {
        lobby_infos lobby;
        if (!deserialize(reader, lobby)) {
            return false;
        }
        msg.lobbies.push_back(lobby);
    }
    return true;
}

void serialize(byte_writer& writer, const lobby_join_request& msg) {
    writer.put_string(msg.lobby_id);
    writer.put_bool(msg.by_id);
    write_member(writer, msg.member);
}

bool deserialize(byte_reader& reader, lobby_join_request& msg) {
    return reader.get_string(msg.lobby_id) && reader.get_bool(msg.by_id) &&
           read_member(reader, msg.member);
}

void serialize(byte_writer& writer, const lobby_join_response& msg) {
    writer.put_string(msg.lobby_id);
    writer.put_string(msg.player_id);
    writer.put_svar(msg.reason);
}

bool deserialize(byte_reader& reader, lobby_join_response& msg) {
    i64 reason = 0;
    if (!reader.get_string(msg.lobby_id) || !reader.get_string(msg.player_id) ||
        !reader.get_svar(reason)) {
        return false;
    }
    msg.reason = static_cast<i32>(reason);
    return true;
}

void serialize(byte_writer& writer, const lobby_member_update& msg) {
    writer.put_string(msg.lobby_id);
    write_member(writer, msg.member);
}

bool deserialize(byte_reader& reader, lobby_member_update& msg) {
    return reader.get_string(msg.lobby_id) && read_member(reader, msg.member);
}

void serialize(byte_writer& writer, const lobby_destroy& msg) {
    writer.put_string(msg.lobby_id);
    writer.put_svar(msg.reason);
}

bool deserialize(byte_reader& reader, lobby_destroy& msg) {
    i64 reason = 0;
    if (!reader.get_string(msg.lobby_id) || !reader.get_svar(reason)) {
        return false;
    }
    msg.reason = static_cast<i32>(reason);
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
    consumed = 0;
    if (len < frame_prefix_len) {
        return false;
    }
    const u32 body_len = (static_cast<u32>(data[0]) << 24) |
                         (static_cast<u32>(data[1]) << 16) |
                         (static_cast<u32>(data[2]) << 8) |
                         static_cast<u32>(data[3]);
    if (body_len > max_message_size) {
        return false;
    }
    if (len - frame_prefix_len < body_len) {
        return false;
    }
    body.assign(data + frame_prefix_len, data + frame_prefix_len + body_len);
    consumed = frame_prefix_len + body_len;
    return true;
}

} // namespace eosr
