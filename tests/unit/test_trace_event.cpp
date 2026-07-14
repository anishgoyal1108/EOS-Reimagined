#include "doctest.h"

#include <string>
#include <vector>

#include "common/types.h"
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
    trace_return value;
    value.type = trace_return::r_result;
    value.result.code = 0;
    value.result.name = "EOS_Success";
    const std::string line = serialize_return(make_envelope(), "EOS_Connect_Login", "c#1", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_Connect_Login\",\"corr\":\"c#1\","
                      "\"result\":{\"code\":0,\"name\":\"EOS_Success\"}}");
}

TEST_CASE("a return can carry a typed scalar value") {
    trace_return value;
    value.type = trace_return::r_value;
    value.value_type = "count";
    value.value = tv_uint(3);
    const std::string line =
        serialize_return(make_envelope(), "EOS_Friends_GetFriendsCount", "", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_Friends_GetFriendsCount\","
                      "\"value\":{\"type\":\"count\",\"v\":3}}");
}

TEST_CASE("a void return is marked explicitly") {
    trace_return value;
    value.type = trace_return::r_void;
    const std::string line = serialize_return(make_envelope(), "EOS_Platform_Tick", "", value);
    CHECK(line == prefix + "\"kind\":\"return\",\"fn\":\"EOS_Platform_Tick\",\"void\":true}");
}

TEST_CASE("a return can carry typed out-parameters") {
    trace_return value;
    value.type = trace_return::r_result;
    value.result.code = 0;
    value.result.name = "EOS_Success";
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

TEST_CASE("an absent instance label is emitted as null") {
    trace_envelope env = make_envelope();
    env.inst.clear();
    const std::string line = serialize_meta(env, "run_start", none());
    CHECK(line ==
          "{\"v\":1,\"seq\":7,\"t\":123,\"pid\":4242,\"inst\":null,\"tid\":\"t#0\","
          "\"kind\":\"meta\",\"event\":\"run_start\"}");
}

TEST_CASE("sanitized diagnostic text is escaped, and an invalid byte replaced") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::detail, tv_diag(std::string("bad\xFF", 4))));
    const std::string line = serialize_meta(make_envelope(), "config", fields);
    CHECK(line == prefix + "\"kind\":\"meta\",\"event\":\"config\","
                           "\"detail\":\"bad\xEF\xBF\xBD\"}");
}

// --- The structural privacy and bounding guarantees. ---

TEST_CASE("arbitrary text cannot enter a typed field") {
    const std::string token = "credential-token-that-must-not-enter-a-trace";
    std::vector<trace_field> args;
    args.push_back(f(field_id::cred_type, tv_enum(token))); // dashes + length -> not a valid enum
    args.push_back(f(field_id::puid, tv_label(token)));     // no '#' -> not a valid label
    const std::string line = serialize_call(make_envelope(), "EOS_Connect_Login", 3, "c#1", args);
    CHECK(line.find(token) == std::string::npos);
}

TEST_CASE("a label field accepts only an opaque label, never a raw id") {
    const std::string raw_puid = "00112233445566778899aabbccddeeff"; // 32 hex, no '#'
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::puid, tv_label(raw_puid)));
    const std::string line =
        serialize_callback(make_envelope(), "EOS_Connect_Login", "c#1", trace_result_code(), fields);
    CHECK(line.find(raw_puid) == std::string::npos);
}

TEST_CASE("a fingerprint field rejects anything but sixteen lowercase hex") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::peer_fp, tv_fingerprint("EBF65ED621BA531B"))); // uppercase
    fields.push_back(f(field_id::peer_fp, tv_fingerprint("deadbeef")));         // too short
    const std::string line = serialize_net(make_envelope(), "adopt", fields);
    CHECK(line == prefix + "\"kind\":\"net\",\"event\":\"adopt\"}"); // both dropped
}

TEST_CASE("duplicate fields cannot create duplicate JSON members") {
    std::vector<trace_field> fields;
    fields.push_back(f(field_id::detail, tv_diag("first")));
    fields.push_back(f(field_id::detail, tv_diag("second")));
    const std::string line = serialize_meta(make_envelope(), "config", fields);
    CHECK(occurrences(line, "\"detail\":") == 1);
    CHECK(line.find("first") != std::string::npos);   // the first wins
    CHECK(line.find("second") == std::string::npos);
}

TEST_CASE("a mistyped field value is dropped, never a partial record") {
    trace_field corrupt;
    corrupt.id = field_id::detail; // expects a diag
    corrupt.value = tv_uint(5);    // but carries a uint
    std::vector<trace_field> fields;
    fields.push_back(corrupt);
    const std::string line = serialize_meta(make_envelope(), "config", fields);
    REQUIRE_FALSE(line.empty());
    CHECK(line[line.size() - 1] == '}');                      // complete, not truncated
    CHECK(line == prefix + "\"kind\":\"meta\",\"event\":\"config\"}"); // the field was dropped
}

TEST_CASE("one record cannot exceed the minimum sink capacity, worst-case escaping included") {
    std::vector<trace_field> fields;
    // The only free-text fields, each filled with control bytes that escape to six bytes apiece; the
    // diagnostic cap keeps even these small.
    fields.push_back(f(field_id::detail, tv_diag(std::string(70000, '\x01'))));
    fields.push_back(f(field_id::message, tv_diag(std::string(70000, '\x01'))));
    const std::string line = serialize_meta(make_envelope(), "config", fields);
    CHECK(line.size() < 65536u);
}
