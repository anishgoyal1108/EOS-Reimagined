#include "core/trace_event.h"

#include <cstddef>

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

void write_envelope(json_writer& writer, const trace_envelope& env, const char* kind) {
    writer.field_uint("v", env.schema_version);
    writer.field_uint("seq", env.seq);
    writer.field_uint("t", env.t_mono_ns);
    writer.field_uint("pid", env.pid);
    if (env.inst.empty()) {
        writer.field_null("inst");
    } else {
        writer.field_string("inst", env.inst);
    }
    writer.field_string("tid", env.tid);
    writer.field_string("kind", kind);
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
            writer.value_string(scalar.string_value);
            break;
    }
}

void write_fields(json_writer& writer, const std::vector<trace_field>& fields) {
    for (std::size_t i = 0; i < fields.size(); i++) {
        writer.key(fields[i].key);
        write_scalar_value(writer, fields[i].value);
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
    writer.field_string("name", result.name);
    writer.end_object();
}

} // namespace

std::string serialize_meta(const trace_envelope& env, const std::string& event,
                           const std::vector<trace_field>& fields) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "meta");
    writer.field_string("event", event);
    write_fields(writer, fields);
    writer.end_object();
    return writer.str();
}

std::string serialize_call(const trace_envelope& env, const std::string& fn, i32 api_version,
                           const std::string& corr, const std::vector<trace_field>& args) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "call");
    writer.field_string("fn", fn);
    writer.field_int("api", api_version);
    if (!corr.empty()) {
        writer.field_string("corr", corr);
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
    writer.field_string("fn", fn);
    if (!corr.empty()) {
        writer.field_string("corr", corr);
    }
    if (value.type == trace_return::r_result) {
        write_result(writer, value.result);
    } else if (value.type == trace_return::r_value) {
        writer.key("value");
        writer.begin_object();
        writer.field_string("type", value.value_type);
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
    writer.field_string("fn", fn);
    if (!corr.empty()) {
        writer.field_string("corr", corr);
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
    writer.field_string("event", event);
    writer.field_string("action", action);
    writer.field_string("id", id);
    write_fields(writer, fields);
    writer.end_object();
    return writer.str();
}

std::string serialize_net(const trace_envelope& env, const std::string& event,
                          const std::vector<trace_field>& fields) {
    json_writer writer;
    writer.begin_object();
    write_envelope(writer, env, "net");
    writer.field_string("event", event);
    write_fields(writer, fields);
    writer.end_object();
    return writer.str();
}

} // namespace eosr
