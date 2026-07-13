#include "doctest.h"

#include <limits>
#include <string>
#include <vector>

#include "common/byte_buffer.h"
#include "common/types.h"
#include "net/messages.h"
#include "net/wire.h"

using namespace eosr;

TEST_CASE("fixed-width integers round-trip") {
    byte_writer w;
    w.put_u8(0x12);
    w.put_u16(0x1234);
    w.put_u32(0x12345678u);
    w.put_u64(0x123456789abcdef0ull);

    byte_reader r(w.data().data(), w.size());
    u8 a = 0;
    u16 b = 0;
    u32 c = 0;
    u64 d = 0;
    REQUIRE(r.get_u8(a));
    REQUIRE(r.get_u16(b));
    REQUIRE(r.get_u32(c));
    REQUIRE(r.get_u64(d));
    CHECK(a == 0x12);
    CHECK(b == 0x1234);
    CHECK(c == 0x12345678u);
    CHECK(d == 0x123456789abcdef0ull);
    CHECK(r.at_end());
}

TEST_CASE("unsigned varint round-trips across byte boundaries") {
    const u64 values[] = {0, 1, 127, 128, 300, 16383, 16384, 0xffffffffull, 0xffffffffffffffffull};
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        byte_writer w;
        w.put_var(values[i]);
        byte_reader r(w.data().data(), w.size());
        u64 out = 0;
        REQUIRE(r.get_var(out));
        CHECK(out == values[i]);
        CHECK(r.at_end());
    }
}

TEST_CASE("zigzag signed varint round-trips") {
    const i64 values[] = {0, -1, 1, -2, 2, 63, -64, 2147483647LL, -2147483648LL,
                          9223372036854775807LL, (-9223372036854775807LL - 1)};
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        byte_writer w;
        w.put_svar(values[i]);
        byte_reader r(w.data().data(), w.size());
        i64 out = 0;
        REQUIRE(r.get_svar(out));
        CHECK(out == values[i]);
    }
}

TEST_CASE("a short buffer is reported without reading out of bounds") {
    byte_writer w;
    w.put_u32(42);
    byte_reader r(w.data().data(), 2); // only 2 of the 4 bytes
    u32 out = 0;
    CHECK_FALSE(r.get_u32(out));
    CHECK(r.remaining() == 2); // cursor untouched on failure
}

TEST_CASE("strings and bytes round-trip and reject truncation") {
    byte_writer w;
    w.put_string("hello");
    const u8 raw[] = {1, 2, 3, 4, 5};
    w.put_bytes(raw, sizeof(raw));

    byte_reader r(w.data().data(), w.size());
    std::string s;
    std::vector<u8> b;
    REQUIRE(r.get_string(s));
    REQUIRE(r.get_bytes(b));
    CHECK(s == "hello");
    REQUIRE(b.size() == 5);
    CHECK(b[0] == 1);
    CHECK(b[4] == 5);

    byte_writer w2;
    w2.put_string("hello"); // needs 1 length byte + 5 chars
    byte_reader r2(w2.data().data(), 3); // give only 3 bytes
    std::string s2;
    CHECK_FALSE(r2.get_string(s2));
}

TEST_CASE("an overlong varint is rejected") {
    std::vector<u8> bad(11, 0x80); // 11 continuation bytes cannot encode a u64
    byte_reader r(bad.data(), bad.size());
    u64 out = 0;
    CHECK_FALSE(r.get_var(out));
}

TEST_CASE("a tenth varint byte cannot overflow u64") {
    std::vector<u8> bad(9, 0x80);
    bad.push_back(0x02); // only 0 or 1 is valid in the final payload bit
    byte_reader r(bad.data(), bad.size());
    u64 out = 123;

    CHECK_FALSE(r.get_var(out));
    CHECK(r.remaining() == bad.size());
    CHECK(out == 123);
}

TEST_CASE("emu_infos round-trips") {
    emu_infos a;
    a.emulator = "1.0.0";
    a.appid = "CrabTest";
    a.country = "US";
    a.language = "en";
    a.username = "InfernusHawk";

    byte_writer w;
    serialize(w, a);
    byte_reader r(w.data().data(), w.size());
    emu_infos b;
    REQUIRE(deserialize(r, b));
    CHECK(b.emulator == a.emulator);
    CHECK(b.appid == a.appid);
    CHECK(b.username == a.username);
    CHECK(r.at_end());
}

TEST_CASE("p2p_data round-trips a binary payload with zero bytes") {
    p2p_data a;
    a.socket_name = "game";
    a.channel = 3;
    a.data.push_back(0);
    a.data.push_back(0xff);
    a.data.push_back(0);

    byte_writer w;
    serialize(w, a);
    byte_reader r(w.data().data(), w.size());
    p2p_data b;
    REQUIRE(deserialize(r, b));
    CHECK(b.socket_name == "game");
    CHECK(b.channel == 3);
    REQUIRE(b.data.size() == 3);
    CHECK(b.data[0] == 0);
    CHECK(b.data[1] == 0xff);
    CHECK(b.data[2] == 0);
}

TEST_CASE("p2p_data rejects channels outside the i32 range") {
    const i64 invalid_channels[] = {
        static_cast<i64>(std::numeric_limits<i32>::max()) + 1,
        static_cast<i64>(std::numeric_limits<i32>::min()) - 1
    };
    for (std::size_t i = 0; i < sizeof(invalid_channels) / sizeof(invalid_channels[0]); i++) {
        byte_writer w;
        w.put_string("game");
        w.put_svar(invalid_channels[i]);
        const u8 payload[] = {1, 2, 3};
        w.put_bytes(payload, sizeof(payload));

        p2p_data out;
        out.channel = 17;
        out.data.push_back(9);
        byte_reader r(w.data().data(), w.size());
        CHECK_FALSE(deserialize(r, out));
        CHECK(out.channel == 17);
        REQUIRE(out.data.size() == 1);
        CHECK(out.data[0] == 9);
    }
}

TEST_CASE("session_infos round-trips its lists and every attribute type") {
    session_infos a;
    a.session_id = "S1";
    a.owner_id = "0123456789abcdef0123456789abcdef";
    a.bucket_id = "B1";
    a.host_address = "1.2.3.4:7777";
    a.max_players = 4;
    a.open_slots = 2;
    a.permission_level = 1;
    a.state = 4;
    a.allow_join_in_progress = true;
    a.invites_allowed = true;
    a.sanctions_enabled = false;
    a.registered_players.push_back("p1");
    a.registered_players.push_back("p2");

    // One of every attribute type, since the value on the wire is chosen by the type tag.
    session_attribute flag;
    flag.key = "hardcore";
    flag.value_type = 0;
    flag.as_bool = true;
    session_attribute level;
    level.key = "level";
    level.value_type = 1;
    level.as_int64 = -42;
    session_attribute ratio;
    ratio.key = "ratio";
    ratio.value_type = 2;
    ratio.as_double = 0.5;
    session_attribute map_name;
    map_name.key = "map";
    map_name.value_type = 3;
    map_name.as_string = "crab-island";
    map_name.advertisement = 1;
    a.attributes.push_back(flag);
    a.attributes.push_back(level);
    a.attributes.push_back(ratio);
    a.attributes.push_back(map_name);

    byte_writer w;
    serialize(w, a);
    byte_reader r(w.data().data(), w.size());
    session_infos b;
    REQUIRE(deserialize(r, b));

    CHECK(b.session_id == "S1");
    CHECK(b.owner_id == a.owner_id);
    CHECK(b.max_players == 4);
    CHECK(b.open_slots == 2);
    CHECK(b.permission_level == 1);
    CHECK(b.state == 4);
    CHECK(b.allow_join_in_progress);
    CHECK(b.invites_allowed);
    CHECK_FALSE(b.sanctions_enabled);
    REQUIRE(b.registered_players.size() == 2);
    CHECK(b.registered_players[0] == "p1");
    CHECK(b.registered_players[1] == "p2");

    REQUIRE(b.attributes.size() == 4);
    CHECK(b.attributes[0].key == "hardcore");
    CHECK(b.attributes[0].as_bool);
    CHECK(b.attributes[1].as_int64 == -42);
    CHECK(b.attributes[2].as_double == 0.5);
    CHECK(b.attributes[3].as_string == "crab-island");
    CHECK(b.attributes[3].advertisement == 1);
    CHECK(r.at_end());
}

TEST_CASE("an attribute with an unknown type is rejected rather than guessed") {
    byte_writer w;
    w.put_string("key");
    w.put_svar(99); // not one of the four EOS attribute types
    w.put_svar(0);
    w.put_string("value");

    byte_reader r(w.data().data(), w.size());
    session_attribute out;
    CHECK_FALSE(deserialize(r, out));
}

TEST_CASE("a session search and its answer round-trip") {
    session_search a;
    a.search_id = "q1";
    a.session_id = "";
    a.target_user_id = "0123456789abcdef0123456789abcdef";
    a.max_results = 10;

    search_parameter parameter;
    parameter.attribute.key = "map";
    parameter.attribute.value_type = 3;
    parameter.attribute.as_string = "crab-island";
    parameter.comparison_op = 0; // EOS_CO_EQUAL
    a.parameters.push_back(parameter);

    byte_writer w;
    serialize(w, a);
    byte_reader r(w.data().data(), w.size());
    session_search b;
    REQUIRE(deserialize(r, b));
    CHECK(b.search_id == "q1");
    CHECK(b.target_user_id == a.target_user_id);
    CHECK(b.max_results == 10);
    REQUIRE(b.parameters.size() == 1);
    CHECK(b.parameters[0].attribute.key == "map");
    CHECK(b.parameters[0].attribute.as_string == "crab-island");
    CHECK(b.parameters[0].comparison_op == 0);

    session_search_response answer;
    answer.search_id = "q1";
    session_infos first;
    first.session_id = "S9";
    first.max_players = 8;
    session_infos second;
    second.session_id = "S10";
    second.max_players = 2;
    answer.sessions.push_back(first);
    answer.sessions.push_back(second);
    byte_writer w2;
    serialize(w2, answer);
    byte_reader r2(w2.data().data(), w2.size());
    session_search_response decoded;
    REQUIRE(deserialize(r2, decoded));
    CHECK(decoded.search_id == "q1");
    // Both of a peer's matching sessions ride back in one reply, so none can be lost.
    REQUIRE(decoded.sessions.size() == 2);
    CHECK(decoded.sessions[0].session_id == "S9");
    CHECK(decoded.sessions[0].max_players == 8);
    CHECK(decoded.sessions[1].session_id == "S10");
}

TEST_CASE("a session roster longer than the ABI allows is rejected, not reserved") {
    // The count fits the bytes left, but far exceeds EOS_SESSIONS_MAXREGISTEREDPLAYERS. Without the
    // element cap this would reserve room for millions of strings; with it, the frame is refused.
    byte_writer w;
    w.put_string("S1");
    w.put_string("owner");
    w.put_string("bucket");
    w.put_string("addr");
    w.put_u32(4);        // max_players
    w.put_u32(0);        // open_slots
    w.put_svar(0);       // permission
    w.put_svar(0);       // state
    w.put_bool(false);
    w.put_bool(false);
    w.put_bool(false);
    w.put_var(static_cast<u64>(50000)); // registered_players count, well past the 1000 cap
    for (int i = 0; i < 50000; i++) {
        w.put_string("x"); // enough bytes that the count passes the byte-bound
    }
    byte_reader r(w.data().data(), w.size());
    session_infos decoded;
    CHECK_FALSE(deserialize(r, decoded));
}

TEST_CASE("a search with an absurd parameter count is rejected") {
    byte_writer w;
    w.put_string("q");
    w.put_string("");
    w.put_string("");
    w.put_u32(10);
    w.put_var(0xffffffffull); // far more parameters than there are bytes left

    byte_reader r(w.data().data(), w.size());
    session_search out;
    CHECK_FALSE(deserialize(r, out));
}

TEST_CASE("an envelope wraps a payload and round-trips") {
    emu_infos info;
    info.appid = "CrabTest";
    info.username = "InfernusHawk";
    byte_writer payload_writer;
    serialize(payload_writer, info);

    net_envelope e;
    e.type_tag = static_cast<u16>(message_type::emu_infos_response);
    e.source_id = "0123456789abcdef0123456789abcdef";
    e.game_id = "CrabTest";
    e.timestamp = 123456789;
    e.payload = payload_writer.data();

    byte_writer w;
    serialize(w, e);
    byte_reader r(w.data().data(), w.size());
    net_envelope out;
    REQUIRE(deserialize(r, out));
    CHECK(out.type_tag == static_cast<u16>(message_type::emu_infos_response));
    CHECK(out.source_id == e.source_id);
    CHECK(out.dest_id.empty());
    CHECK(out.game_id == "CrabTest");
    CHECK(out.timestamp == 123456789);

    byte_reader payload_reader(out.payload.data(), out.payload.size());
    emu_infos decoded;
    REQUIRE(deserialize(payload_reader, decoded));
    CHECK(decoded.appid == "CrabTest");
    CHECK(decoded.username == "InfernusHawk");
}

TEST_CASE("an envelope with an unknown protocol version is rejected") {
    net_envelope e;
    e.type_tag = 1;
    byte_writer w;
    serialize(w, e);
    std::vector<u8> bytes = w.data();
    bytes[0] = 0xff; // corrupt the leading version byte
    byte_reader r(bytes.data(), bytes.size());
    net_envelope out;
    CHECK_FALSE(deserialize(r, out));
}

TEST_CASE("TCP framing round-trips and reports incomplete frames") {
    emu_infos info;
    info.username = "x";
    byte_writer w;
    serialize(w, info);

    std::vector<u8> framed = frame_message(w.data());
    std::vector<u8> body;
    std::size_t consumed = 0;
    REQUIRE(try_deframe(framed.data(), framed.size(), body, consumed));
    CHECK(consumed == framed.size());
    CHECK(body == w.data());

    std::vector<u8> partial(framed.begin(), framed.end() - 1); // one byte short
    std::vector<u8> body2(1, 0xaa);
    std::size_t consumed2 = 123;
    CHECK_FALSE(try_deframe(partial.data(), partial.size(), body2, consumed2));
    CHECK(consumed2 == 0);
    REQUIRE(body2.size() == 1);
    CHECK(body2[0] == 0xaa);

    std::vector<u8> prefix_only(framed.begin(), framed.begin() + 4); // length only, no body
    consumed2 = 456;
    CHECK_FALSE(try_deframe(prefix_only.data(), prefix_only.size(), body2, consumed2));
    CHECK(consumed2 == 0);
    REQUIRE(body2.size() == 1);
    CHECK(body2[0] == 0xaa);
}

TEST_CASE("deframing a short prefix resets consumed without touching the body") {
    const u8 short_prefix[] = {0, 0, 0};
    std::vector<u8> body(2, 0x55);
    std::size_t consumed = 999;

    CHECK_FALSE(try_deframe(short_prefix, sizeof(short_prefix), body, consumed));
    CHECK(consumed == 0);
    REQUIRE(body.size() == 2);
    CHECK(body[0] == 0x55);
    CHECK(body[1] == 0x55);
}

TEST_CASE("deframing rejects a body larger than the configured message limit") {
    const u32 declared = max_message_size + 1;
    std::vector<u8> framed(static_cast<std::size_t>(declared) + 4, 0);
    framed[0] = static_cast<u8>(declared >> 24);
    framed[1] = static_cast<u8>(declared >> 16);
    framed[2] = static_cast<u8>(declared >> 8);
    framed[3] = static_cast<u8>(declared);
    std::vector<u8> body(1, 0x42);
    std::size_t consumed = 17;

    CHECK_FALSE(try_deframe(framed.data(), framed.size(), body, consumed));
    CHECK(consumed == 0);
    REQUIRE(body.size() == 1);
    CHECK(body[0] == 0x42);
}

TEST_CASE("two framed messages in one buffer deframe one at a time") {
    emu_infos i1;
    i1.username = "one";
    emu_infos i2;
    i2.username = "two";
    byte_writer w1;
    serialize(w1, i1);
    byte_writer w2;
    serialize(w2, i2);

    std::vector<u8> buf = frame_message(w1.data());
    std::vector<u8> second = frame_message(w2.data());
    buf.insert(buf.end(), second.begin(), second.end());

    std::size_t offset = 0;
    std::vector<u8> body;
    std::size_t consumed = 0;

    REQUIRE(try_deframe(buf.data() + offset, buf.size() - offset, body, consumed));
    offset += consumed;
    byte_reader r1(body.data(), body.size());
    emu_infos out1;
    REQUIRE(deserialize(r1, out1));
    CHECK(out1.username == "one");

    REQUIRE(try_deframe(buf.data() + offset, buf.size() - offset, body, consumed));
    offset += consumed;
    byte_reader r2(body.data(), body.size());
    emu_infos out2;
    REQUIRE(deserialize(r2, out2));
    CHECK(out2.username == "two");

    CHECK(offset == buf.size());
}

TEST_CASE("presence info round-trips, records and all") {
    presence_info a;
    a.epic_id = std::string(32, 'a');
    a.status = 2; // away
    a.product_id = "co-op-game";
    a.product_version = "1.4";
    a.platform = "Linux";
    a.rich_text = "In the caves";
    a.product_name = "Crab Quest";
    a.integrated_platform = "STEAM";
    a.join_info = "session:crab-island";
    presence_data_record hp;
    hp.key = "hp";
    hp.value = "42";
    presence_data_record zone;
    zone.key = "zone";
    zone.value = "caves";
    a.records.push_back(hp);
    a.records.push_back(zone);

    byte_writer writer;
    serialize(writer, a);
    byte_reader reader(writer.data().data(), writer.data().size());
    presence_info b;
    REQUIRE(deserialize(reader, b));
    CHECK(b.epic_id == a.epic_id);
    CHECK(b.status == 2);
    CHECK(b.product_id == "co-op-game");
    CHECK(b.rich_text == "In the caves");
    CHECK(b.join_info == "session:crab-island");
    CHECK(b.integrated_platform == "STEAM");
    REQUIRE(b.records.size() == 2);
    CHECK(b.records[0].key == "hp");
    CHECK(b.records[0].value == "42");
    CHECK(b.records[1].key == "zone");
}

TEST_CASE("presence deserialize rejects an absurd record count") {
    byte_writer writer;
    writer.put_string(std::string(32, 'a')); // epic id
    writer.put_svar(static_cast<i64>(1));     // status
    for (int i = 0; i < 7; i++) {
        writer.put_string(""); // the seven remaining strings
    }
    writer.put_var(static_cast<u64>(1) << 40); // a record count no buffer could hold
    byte_reader reader(writer.data().data(), writer.data().size());
    presence_info b;
    CHECK_FALSE(deserialize(reader, b));
}

TEST_CASE("a presence request carries just the target") {
    presence_request a;
    a.target_epic_id = std::string(32, 'b');
    byte_writer writer;
    serialize(writer, a);
    byte_reader reader(writer.data().data(), writer.data().size());
    presence_request b;
    REQUIRE(deserialize(reader, b));
    CHECK(b.target_epic_id == a.target_epic_id);
}

TEST_CASE("a lobby with members and attributes round-trips") {
    lobby_infos a;
    a.lobby_id = "lobby-7";
    a.owner_id = std::string(32, '1');
    a.bucket_id = "Region:Coop";
    a.permission_level = 1;
    a.max_members = 8;
    a.available_slots = 6;
    a.allow_invites = true;
    a.allow_host_migration = false;
    a.rtc_enabled = true;
    session_attribute map;
    map.key = "map";
    map.value_type = 3;
    map.as_string = "crab-island";
    map.advertisement = 0; // public visibility
    a.attributes.push_back(map);

    lobby_member owner;
    owner.user_id = std::string(32, '1');
    owner.platform = 1;
    session_attribute skin;
    skin.key = "skin";
    skin.value_type = 1;
    skin.as_int64 = 42;
    owner.attributes.push_back(skin);
    lobby_member guest;
    guest.user_id = std::string(32, '2');
    a.members.push_back(owner);
    a.members.push_back(guest);

    byte_writer writer;
    serialize(writer, a);
    byte_reader reader(writer.data().data(), writer.data().size());
    lobby_infos b;
    REQUIRE(deserialize(reader, b));
    CHECK(b.lobby_id == "lobby-7");
    CHECK(b.owner_id == a.owner_id);
    CHECK(b.permission_level == 1);
    CHECK(b.max_members == 8);
    CHECK(b.rtc_enabled);
    REQUIRE(b.attributes.size() == 1);
    CHECK(b.attributes[0].as_string == "crab-island");
    REQUIRE(b.members.size() == 2);
    CHECK(b.members[0].user_id == a.owner_id);
    REQUIRE(b.members[0].attributes.size() == 1);
    CHECK(b.members[0].attributes[0].key == "skin");
    CHECK(b.members[0].attributes[0].as_int64 == 42);
    CHECK(b.members[1].user_id == guest.user_id);
    CHECK(b.members[1].attributes.empty());
}

TEST_CASE("a lobby with an absurd member count is rejected") {
    byte_writer w;
    w.put_string("L1");
    w.put_string("owner");
    w.put_string("bucket");
    w.put_svar(0);
    w.put_u32(8);          // max_members
    w.put_u32(8);          // available_slots
    w.put_bool(true);
    w.put_bool(false);
    w.put_bool(false);
    w.put_var(0);          // attributes: none
    w.put_var(static_cast<u64>(100000)); // member count past the 64 cap
    for (int i = 0; i < 100000; i++) {
        w.put_string("m");
        w.put_svar(0);
        w.put_var(0);
    }
    byte_reader r(w.data().data(), w.size());
    lobby_infos decoded;
    CHECK_FALSE(deserialize(r, decoded));
}
