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

trace_field f_str(const std::string& key, const std::string& value) {
    trace_field field;
    field.key = key;
    field.value = scalar_string(value);
    return field;
}
trace_field f_int(const std::string& key, i64 value) {
    trace_field field;
    field.key = key;
    field.value = scalar_int(value);
    return field;
}
trace_field f_uint(const std::string& key, u64 value) {
    trace_field field;
    field.key = key;
    field.value = scalar_uint(value);
    return field;
}

std::vector<trace_field> none() { return std::vector<trace_field>(); }

} // namespace

TEST_CASE("a meta record serializes its event and inline fields in order") {
    std::vector<trace_field> fields;
    fields.push_back(f_uint("dropped_files", 2));
    fields.push_back(f_uint("dropped_bytes", 65536));
    const std::string line = serialize_meta(make_envelope(), "rotate", fields);
    CHECK(line == prefix +
                      "\"kind\":\"meta\",\"event\":\"rotate\","
                      "\"dropped_files\":2,\"dropped_bytes\":65536}");
}

TEST_CASE("a call record carries fn, api, correlation, and nested args") {
    std::vector<trace_field> args;
    args.push_back(f_str("cred_type", "device"));
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

TEST_CASE("a return can carry a scalar value") {
    trace_return value;
    value.type = trace_return::r_value;
    value.value_type = "count";
    value.value = scalar_int(3);
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

TEST_CASE("a return can carry allow-listed out-parameters") {
    trace_return value;
    value.type = trace_return::r_result;
    value.result.code = 0;
    value.result.name = "EOS_Success";
    value.out.push_back(f_str("handle", "userinfo#4"));
    const std::string line =
        serialize_return(make_envelope(), "EOS_UserInfo_CopyUserInfo", "", value);
    CHECK(line == prefix +
                      "\"kind\":\"return\",\"fn\":\"EOS_UserInfo_CopyUserInfo\","
                      "\"result\":{\"code\":0,\"name\":\"EOS_Success\"},"
                      "\"out\":{\"handle\":\"userinfo#4\"}}");
}

TEST_CASE("a callback carries its correlation, result, and nested payload") {
    std::vector<trace_field> payload;
    payload.push_back(f_str("puid", "puid#2"));
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
    fields.push_back(f_str("target", "eaid#2"));
    const std::string line =
        serialize_notify(make_envelope(), "FriendsUpdate", "fire", "notif#3", fields);
    CHECK(line == prefix +
                      "\"kind\":\"notify\",\"event\":\"FriendsUpdate\",\"action\":\"fire\","
                      "\"id\":\"notif#3\",\"target\":\"eaid#2\"}");
}

TEST_CASE("a net record carries a peer label, a fingerprint, and lengths, never bytes") {
    std::vector<trace_field> fields;
    fields.push_back(f_str("peer", "puid#2"));
    fields.push_back(f_str("peer_fp", "ebf65ed621ba531b"));
    fields.push_back(f_uint("bytes", 128));
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

TEST_CASE("a string field with an invalid byte is made safe by the writer") {
    std::vector<trace_field> fields;
    fields.push_back(f_str("detail", std::string("bad\xFF", 4)));
    const std::string line = serialize_meta(make_envelope(), "config", fields);
    CHECK(line == prefix + "\"kind\":\"meta\",\"event\":\"config\","
                           "\"detail\":\"bad\xEF\xBF\xBD\"}");
}
