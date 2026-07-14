#include "core/trace_event.h"

#include <cstddef>
#include <string>

#include "common/json_writer.h"

namespace eosr {

trace_scalar scalar_int(i64 value) {
    trace_scalar s;
    s.type = trace_scalar::s_int;
    s.int_value = value;
    return s;
}
trace_scalar scalar_uint(u64 value) {
    trace_scalar s;
    s.type = trace_scalar::s_uint;
    s.uint_value = value;
    return s;
}
trace_scalar scalar_bool(bool value) {
    trace_scalar s;
    s.type = trace_scalar::s_bool;
    s.bool_value = value;
    return s;
}
trace_scalar scalar_string(const std::string& value) {
    trace_scalar s;
    s.type = trace_scalar::s_string;
    s.string_value = value;
    return s;
}

namespace {

// Record-size discipline. With at most this many body fields, each a string no longer than this, plus
// the short fixed fields, one record stays far under the 64 KiB sink minimum (which must hold a full
// record and the rotate record), so no single event can outgrow the sink. Spec: docs/alpha-tracing.md
// §4, §7.
const std::size_t max_string_bytes = 512;
const std::size_t max_body_fields = 32;

// The body field names a caller may attach. A key that is not here -- an arbitrary label, or one that
// would shadow an envelope or body field like seq/kind/event/fn/result -- is dropped, so a record
// always has exactly one unambiguous set of schema fields and no smuggled-in key.
bool is_allowed_field(const std::string& key) {
    static const char* const allowed[] = {
        // net / discovery / transport
        "peer", "peer_fp", "bytes", "channel", "reliability", "port_first", "port_last", "reason",
        "game", "sandbox", "deployment",
        // ids and handles, as labels
        "handle", "local", "target", "puid", "eaid", "account", "socket", "lobby", "session",
        // async and callback payload
        "cred_type", "status", "index", "count", "len",
        // meta / diagnostics
        "dropped_files", "dropped_bytes", "level", "detail", "message"};
    for (std::size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (key == allowed[i]) {
            return true;
        }
    }
    return false;
}

// Truncate a string to the byte cap. A cut that lands mid-codepoint is harmless: the writer replaces
// any resulting stray byte with U+FFFD, so the output stays valid UTF-8.
std::string bounded(const std::string& text) {
    return (text.size() <= max_string_bytes) ? text : text.substr(0, max_string_bytes);
}

void write_bounded_string(json_writer& writer, const char* key, const std::string& value) {
    writer.field_string(key, bounded(value));
}

void write_envelope(json_writer& writer, const trace_envelope& env, const char* kind) {
    writer.field_uint("v", env.schema_version);
    writer.field_uint("seq", env.seq);
    writer.field_uint("t", env.t_mono_ns);
    writer.field_uint("pid", env.pid);
    if (env.inst.empty()) {
        writer.field_null("inst");
    } else {
        write_bounded_string(writer, "inst", env.inst);
    }
    write_bounded_string(writer, "tid", env.tid);
    writer.field_string("kind", kind);
}

// A notification action is exactly register / remove / fire; anything else is a programming error, so
// it becomes a fixed safe token rather than an arbitrary string in the schema.
void write_action(json_writer& writer, const std::string& action) {
    const bool known = (action == "register" || action == "remove" || action == "fire");
    writer.field_string("action", known ? action : std::string("invalid"));
}

// A return value's type is one of a fixed set; an unknown one becomes a safe token.
void write_value_type(json_writer& writer, const std::string& value_type) {
    const bool known = (value_type == "bool" || value_type == "count" || value_type == "handle" ||
                        value_type == "enum" || value_type == "notification_id");
    writer.field_string("type", known ? value_type : std::string("invalid"));
}

void write_scalar_value(json_writer& writer, const trace_scalar& scalar) {
    switch (scalar.type) {
        case trace_scalar::s_int:
            writer.value_int(scalar.int_value);
            break;
        case trace_scalar::s_uint:
            writer.value_uint(scalar.uint_value);
            break;
        case trace_scalar::s_bool:
            writer.value_bool(scalar.bool_value);
            break;
        case trace_scalar::s_string:
            writer.value_string(bounded(scalar.string_value));
            break;
    }
}

// Emit the caller's fields, dropping any whose key is not allow-listed (so no arbitrary or shadowing
// key reaches the record) and stopping at the field-count cap.
void write_fields(json_writer& writer, const std::vector<trace_field>& fields) {
    std::size_t emitted = 0;
    for (std::size_t i = 0; i < fields.size() && emitted < max_body_fields; i++) {
        if (!is_allowed_field(fields[i].key)) {
            continue;
        }
        writer.key(fields[i].key);
        write_scalar_value(writer, fields[i].value);
        emitted++;
    }
}

// A named object grouping the caller's scalars, e.g. "args":{...} or "out":{...}.
void write_group(json_writer& writer, const char* name, const std::vector<trace_field>& fields) {
    writer.key(name);
    writer.begin_object();
    write_fields(writer, fields);
    writer.end_object();
}

void write_result(json_writer& writer, const trace_result_code& result) {
    writer.key("result");
    writer.begin_object();
    writer.field_int("code", result.code);
    write_bounded_string(writer, "name", result.name);
    writer.end_object();
}

} // namespace

std::string serialize_meta(const trace_envelope& env, const std::string& event,
                           const std::vector<trace_field>& fields) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "meta");
    write_bounded_string(writer, "event", event);
    write_fields(writer, fields);
    writer.end_object();
    return writer.str();
}

std::string serialize_call(const trace_envelope& env, const std::string& fn, i32 api_version,
                           const std::string& corr, const std::vector<trace_field>& args) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "call");
    write_bounded_string(writer, "fn", fn);
    writer.field_int("api", api_version);
    if (!corr.empty()) {
        write_bounded_string(writer, "corr", corr);
    }
    write_group(writer, "args", args);
    writer.end_object();
    return writer.str();
}

std::string serialize_return(const trace_envelope& env, const std::string& fn,
                             const std::string& corr, const trace_return& value) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "return");
    write_bounded_string(writer, "fn", fn);
    if (!corr.empty()) {
        write_bounded_string(writer, "corr", corr);
    }
    if (value.type == trace_return::r_result) {
        write_result(writer, value.result);
    } else if (value.type == trace_return::r_value) {
        writer.key("value");
        writer.begin_object();
        write_value_type(writer, value.value_type);
        writer.key("v");
        write_scalar_value(writer, value.value);
        writer.end_object();
    } else {
        writer.field_bool("void", true);
    }
    if (!value.out.empty()) {
        write_group(writer, "out", value.out);
    }
    writer.end_object();
    return writer.str();
}

std::string serialize_callback(const trace_envelope& env, const std::string& fn,
                               const std::string& corr, const trace_result_code& result,
                               const std::vector<trace_field>& payload) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "callback");
    write_bounded_string(writer, "fn", fn);
    if (!corr.empty()) {
        write_bounded_string(writer, "corr", corr);
    }
    write_result(writer, result);
    write_group(writer, "payload", payload);
    writer.end_object();
    return writer.str();
}

std::string serialize_notify(const trace_envelope& env, const std::string& event,
                             const std::string& action, const std::string& id,
                             const std::vector<trace_field>& fields) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "notify");
    write_bounded_string(writer, "event", event);
    write_action(writer, action);
    write_bounded_string(writer, "id", id);
    write_fields(writer, fields);
    writer.end_object();
    return writer.str();
}

std::string serialize_net(const trace_envelope& env, const std::string& event,
                          const std::vector<trace_field>& fields) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "net");
    write_bounded_string(writer, "event", event);
    write_fields(writer, fields);
    writer.end_object();
    return writer.str();
}

} // namespace eosr
