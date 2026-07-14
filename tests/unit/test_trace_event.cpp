#include "doctest.h"

#include <string>
#include <vector>

#include "core/trace_event.h"

using namespace eosr;

namespace {

trace_envelope make_envelope() {
    trace_envelope env;
    env.schema_version = 1;
    env.seq = 7;
    env.t_mono_ns = 123;
    env.pid = 4242;
    env.inst = "alice";
    env.tid = "t#0";
    return env;
}

// The envelope every golden begins with.
const std::string prefix =
    "{\"v\":1,\"seq\":7,\"t\":123,\"pid\":4242,\"inst\":\"alice\",\"tid\":\"t#0\",";

trace_field f(field_id id, const trace_value& value) { return make_field(id, value); }

std::vector<trace_field> none() { return std::vector<trace_field>(); }

std::size_t occurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

} // namespace

// --- Golden shapes for every body. ---

TEST_CASE("a meta record serializes its event and typed fields in order") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::dropped_files, tv_uint(2)));
    fields.push_back(f(field_id::dropped_bytes, tv_uint(65536)));
    const std::string line = serialize_meta(make_envelope(), "rotate", fields);
    CHECK(line == prefix +
                      "\"kind\":\"meta\",\"event\":\"rotate\","
                      "\"dropped_files\":2,\"dropped_bytes\":65536}");
}

TEST_CASE("a call record carries fn, api, correlation, and nested args") {
    std::vector<trace_field> args;
    args.push_back(f(field_id::cred_type, tv_enum("device")));
    const std::string line = serialize_call(make_envelope(), "EOS_Connect_Login", 3, "c#1", args);
    CHECK(line == prefix +
                      "\"kind\":\"call\",\"fn\":\"EOS_Connect_Login\",\"api\":3,"
                      "\"corr\":\"c#1\",\"args\":{\"cred_type\":\"device\"}}");
}

TEST_CASE("a call with no correlation omits corr and empties args") {
    const std::string line = serialize_call(make_envelope(), "EOS_Platform_Tick", 1, "", none());
    CHECK(line == prefix + "\"kind\":\"call\",\"fn\":\"EOS_Platform_Tick\",\"api\":1,\"args\":{}}");
}

TEST_CASE("a return can carry an EOS_EResult") {
    const trace_return value = return_result(0, "EOS_Success");
    const std::string line = serialize_return(make_envelope(), "EOS_Connect_Login", "c#1", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_Connect_Login\",\"corr\":\"c#1\","
                      "\"result\":{\"code\":0,\"name\":\"EOS_Success\"}}");
}

TEST_CASE("a return can carry a typed count value") {
    const trace_return value = return_count(3);
    const std::string line =
        serialize_return(make_envelope(), "EOS_Friends_GetFriendsCount", "", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_Friends_GetFriendsCount\","
                      "\"value\":{\"type\":\"count\",\"v\":3}}");
}

TEST_CASE("a void return is marked explicitly") {
    const std::string line =
        serialize_return(make_envelope(), "EOS_Platform_Tick", "", return_void());
    CHECK(line == prefix + "\"kind\":\"return\",\"fn\":\"EOS_Platform_Tick\",\"void\":true}");
}

TEST_CASE("a return can carry typed out-parameters") {
    trace_return value = return_result(0, "EOS_Success");
    value.out.push_back(f(field_id::handle, tv_label("userinfo#4")));
    const std::string line =
        serialize_return(make_envelope(), "EOS_UserInfo_CopyUserInfo", "", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_UserInfo_CopyUserInfo\","
                      "\"result\":{\"code\":0,\"name\":\"EOS_Success\"},"
                      "\"out\":{\"handle\":\"userinfo#4\"}}");
}

TEST_CASE("a callback carries its correlation, result, and nested payload") {
    std::vector<trace_field> payload;
    payload.push_back(f(field_id::puid, tv_label("puid#2")));
    trace_result_code result;
    result.code = 0;
    result.name = "EOS_Success";
    const std::string line =
        serialize_callback(make_envelope(), "EOS_Connect_OnLogin", "c#1", result, payload);
    CHECK(line == prefix +
                      "\"kind\":\"callback\",\"fn\":\"EOS_Connect_OnLogin\",\"corr\":\"c#1\","
                      "\"result\":{\"code\":0,\"name\":\"EOS_Success\"},"
                      "\"payload\":{\"puid\":\"puid#2\"}}");
}

TEST_CASE("a notify distinguishes register, remove, and fire") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::target, tv_label("eaid#2")));
    const std::string line =
        serialize_notify(make_envelope(), "FriendsUpdate", "fire", "notif#3", fields);
    CHECK(line == prefix +
                      "\"kind\":\"notify\",\"event\":\"FriendsUpdate\",\"action\":\"fire\","
                      "\"id\":\"notif#3\",\"target\":\"eaid#2\"}");
}

TEST_CASE("a net record carries a peer label, a fingerprint, and lengths, never bytes") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::peer, tv_label("puid#2")));
    fields.push_back(f(field_id::peer_fp, tv_fingerprint("ebf65ed621ba531b")));
    fields.push_back(f(field_id::bytes, tv_uint(128)));
    const std::string line = serialize_net(make_envelope(), "p2p_open", fields);
    CHECK(line == prefix +
                      "\"kind\":\"net\",\"event\":\"p2p_open\","
                      "\"peer\":\"puid#2\",\"peer_fp\":\"ebf65ed621ba531b\",\"bytes\":128}");
}

TEST_CASE("a net record carries bounded endpoint and reason metadata") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::port, tv_uint(55789)));
    fields.push_back(f(field_id::socket, tv_label("socket#1")));
    fields.push_back(f(field_id::reason, tv_enum("local_shutdown")));
    const std::string line = serialize_net(make_envelope(), "drop", fields);
    const std::string expected = prefix + "\"kind\":\"net\",\"event\":\"drop\"," +
                                 "\"port\":55789,\"socket\":\"socket#1\"," +
                                 "\"reason\":\"local_shutdown\"}";
    CHECK(line == expected);
}

TEST_CASE("an absent instance label is emitted as null") {
    trace_envelope env = make_envelope();
    env.inst.clear();
    const std::string line = serialize_meta(env, "run_start", none());
    CHECK(line ==
          "{\"v\":1,\"seq\":7,\"t\":123,\"pid\":4242,\"inst\":null,\"tid\":\"t#0\","
          "\"kind\":\"meta\",\"event\":\"run_start\"}");
}

// --- Structural privacy, typing, and bounding guarantees. ---

TEST_CASE("arbitrary text cannot enter any typed field") {
    const std::string token = "credential-token-that-must-not-enter-a-trace";
    std::vector<trace_field> args;
    args.push_back(f(field_id::cred_type, tv_enum(token))); // has dashes -> not a valid enum
    args.push_back(f(field_id::target, tv_label(token)));   // no '#' -> not a valid label
    const std::string line = serialize_call(make_envelope(), "EOS_Connect_Login", 3, "c#1", args);
    CHECK(line.find(token) == std::string::npos);
}

TEST_CASE("a label field accepts only an opaque label, never a raw id") {
    const std::string raw_puid = "00112233445566778899aabbccddeeff"; // 32 hex, no '#'
    std::vector<trace_field> payload;
    payload.push_back(f(field_id::puid, tv_label(raw_puid)));
    const std::string line =
        serialize_callback(make_envelope(), "EOS_Connect_OnLogin", "c#1", trace_result_code(),
                           payload);
    CHECK(line.find(raw_puid) == std::string::npos);
}

TEST_CASE("a fingerprint field rejects anything but sixteen lowercase hex") {
    std::vector<trace_field> upper;
    upper.push_back(f(field_id::peer_fp, tv_fingerprint("EBF65ED621BA531B"))); // uppercase
    CHECK(serialize_net(make_envelope(), "adopt", upper) ==
          prefix + "\"kind\":\"net\",\"event\":\"adopt\"}");
    std::vector<trace_field> short_fp;
    short_fp.push_back(f(field_id::peer_fp, tv_fingerprint("deadbeef"))); // too short
    CHECK(serialize_net(make_envelope(), "adopt", short_fp) ==
          prefix + "\"kind\":\"net\",\"event\":\"adopt\"}");
}

TEST_CASE("duplicate fields cannot create duplicate JSON members") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::dropped_files, tv_uint(1)));
    fields.push_back(f(field_id::dropped_files, tv_uint(2)));
    const std::string line = serialize_meta(make_envelope(), "rotate", fields);
    CHECK(occurrences(line, "\"dropped_files\":") == 1);
    CHECK(line.find("\"dropped_files\":1") != std::string::npos); // the first wins
}

TEST_CASE("a mistyped field value is dropped, never a partial record") {
    trace_field corrupt;
    corrupt.id = field_id::dropped_files; // expects a uint
    corrupt.value = tv_label("puid#2");   // but carries a label
    std::vector<trace_field> fields;
    fields.push_back(corrupt);
    const std::string line = serialize_meta(make_envelope(), "rotate", fields);
    CHECK(line == prefix + "\"kind\":\"meta\",\"event\":\"rotate\"}"); // dropped, still complete
}

TEST_CASE("an invalid field id cannot create an unknown schema member") {
    std::vector<trace_field> fields;
    fields.push_back(f(static_cast<field_id>(999), tv_label("puid#2")));
    fields.push_back(f(field_id::invalid, tv_label("puid#2")));
    const std::string line = serialize_net(make_envelope(), "adopt", fields);
    CHECK(line == prefix + "\"kind\":\"net\",\"event\":\"adopt\"}");
    CHECK(line.find("unknown") == std::string::npos);
}

TEST_CASE("a field valid in one body cannot drift into another body") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::cred_type, tv_enum("device"))); // cred_type is a call/args field
    const std::string line = serialize_net(make_envelope(), "adopt", fields);
    CHECK(line.find("cred_type") == std::string::npos);
}

TEST_CASE("a return value kind must agree with its declared type") {
    trace_return value;
    value.type = trace_return::r_value;
    value.value_type = "bool";
    value.value = tv_label("puid#2"); // a label, not a flag
    const std::string line = serialize_return(make_envelope(), "EOS_UI_GetFriendsVisible", "", value);
    CHECK(line.empty());
}

TEST_CASE("a documented EOS enum return is an enum, not void") {
    const trace_return value = return_enum("EOS_UNL_BottomRight");
    const std::string line =
        serialize_return(make_envelope(), "EOS_UI_GetNotificationLocationPreference", "", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_UI_GetNotificationLocationPreference\","
                      "\"value\":{\"type\":\"enum\",\"v\":\"EOS_UNL_BottomRight\"}}");
}

TEST_CASE("an invalid return value type is rejected, not added to the schema") {
    trace_return value;
    value.type = trace_return::r_value;
    value.value_type = "unknown";
    value.value = tv_uint(3);
    const std::string line =
        serialize_return(make_envelope(), "EOS_Friends_GetFriendsCount", "", value);
    CHECK(line.empty());
}

TEST_CASE("an invalid notification action is rejected, not recorded as invalid") {
    const std::string line =
        serialize_notify(make_envelope(), "FriendsUpdate", "unknown", "notif#3", none());
    CHECK(line.empty());
}

TEST_CASE("a notification id must be an opaque label") {
    const std::string raw_id = "00112233445566778899aabbccddeeff";
    const std::string line =
        serialize_notify(make_envelope(), "FriendsUpdate", "fire", raw_id, none());
    CHECK(line.empty());
    CHECK(line.find(raw_id) == std::string::npos);
}

TEST_CASE("a malformed function or event name rejects the record") {
    CHECK(serialize_call(make_envelope(), "not a function", 1, "", none()).empty());
    CHECK(serialize_net(make_envelope(), "bad event!", none()).empty());
}

TEST_CASE("one record stays under the sink minimum even filled to the field cap") {
    std::vector<trace_field> fields;
    const std::string long_label = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa#1"; // a max-ish label
    // Fill an args body with every allowed field several times over; duplicates and caps bound it.
    for (int i = 0; i < 200; i++) {
        fields.push_back(f(field_id::local, tv_label(long_label)));
        fields.push_back(f(field_id::target, tv_label(long_label)));
        fields.push_back(f(field_id::account, tv_label(long_label)));
    }
    const std::string line = serialize_call(make_envelope(), "EOS_Connect_Login", 3, "c#1", fields);
    CHECK(line.size() < 65536u);
}
