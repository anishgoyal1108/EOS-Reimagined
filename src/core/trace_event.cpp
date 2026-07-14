#include "core/trace_event.h"

#include <cstddef>
#include <string>
#include <vector>

#include "common/json_writer.h"

namespace eosr {

namespace {

// Record-size discipline. The writer is capped below the 64 KiB sink minimum, so a record that would
// overrun fails to complete and is dropped rather than persisted; the per-value caps keep an ordinary
// record far smaller. Spec: docs/alpha-tracing.md §4, §7.
const std::size_t max_record_bytes = 60000;
const std::size_t max_body_fields = 32;
const std::size_t max_diag_bytes = 200;
const std::size_t max_name_bytes = 128;

std::string bounded(const std::string& text, std::size_t cap) {
    return (text.size() <= cap) ? text : text.substr(0, cap);
}

bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// A label is <letter><word chars>#<digits>, e.g. session#3 or puid#2 -- so a raw hex id (no '#') is
// not a label and is rejected.
bool valid_label(const std::string& text) {
    if (text.empty() || text.size() > 64) {
        return false;
    }
    const std::size_t hash = text.find('#');
    if (hash == std::string::npos || hash == 0 || hash + 1 >= text.size()) {
        return false;
    }
    if (!(text[0] >= 'a' && text[0] <= 'z')) {
        return false;
    }
    for (std::size_t i = 0; i < hash; i++) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    for (std::size_t i = hash + 1; i < text.size(); i++) {
        if (!(text[i] >= '0' && text[i] <= '9')) {
            return false;
        }
    }
    return true;
}

bool valid_fingerprint(const std::string& text) {
    if (text.size() != 16) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); i++) {
        if (!is_lower_hex(text[i])) {
            return false;
        }
    }
    return true;
}

bool valid_enum(const std::string& text) {
    if (text.empty() || text.size() > 48 || !(text[0] >= 'a' && text[0] <= 'z')) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

const char* field_name(field_id id) {
    switch (id) {
        case field_id::peer: return "peer";
        case field_id::peer_fp: return "peer_fp";
        case field_id::bytes: return "bytes";
        case field_id::channel: return "channel";
        case field_id::reliability: return "reliability";
        case field_id::port_first: return "port_first";
        case field_id::port_last: return "port_last";
        case field_id::reason: return "reason";
        case field_id::handle: return "handle";
        case field_id::local: return "local";
        case field_id::target: return "target";
        case field_id::puid: return "puid";
        case field_id::eaid: return "eaid";
        case field_id::account: return "account";
        case field_id::socket: return "socket";
        case field_id::lobby: return "lobby";
        case field_id::session: return "session";
        case field_id::cred_type: return "cred_type";
        case field_id::status: return "status";
        case field_id::index: return "index";
        case field_id::count: return "count";
        case field_id::len: return "len";
        case field_id::dropped_files: return "dropped_files";
        case field_id::dropped_bytes: return "dropped_bytes";
        case field_id::level: return "level";
        case field_id::detail: return "detail";
        case field_id::message: return "message";
    }
    return "unknown";
}

trace_value::kind expected_kind(field_id id) {
    switch (id) {
        case field_id::peer_fp:
            return trace_value::v_fingerprint;
        case field_id::bytes:
        case field_id::port_first:
        case field_id::port_last:
        case field_id::index:
        case field_id::count:
        case field_id::len:
        case field_id::dropped_files:
        case field_id::dropped_bytes:
            return trace_value::v_uint;
        case field_id::channel:
            return trace_value::v_int;
        case field_id::reliability:
        case field_id::reason:
        case field_id::cred_type:
        case field_id::status:
        case field_id::level:
            return trace_value::v_enum;
        case field_id::detail:
        case field_id::message:
            return trace_value::v_diag;
        default:
            return trace_value::v_label;
    }
}

void write_value(json_writer& writer, const trace_value& value) {
    switch (value.type) {
        case trace_value::v_int:
            writer.value_int(value.int_value);
            break;
        case trace_value::v_uint:
            writer.value_uint(value.uint_value);
            break;
        case trace_value::v_flag:
            writer.value_bool(value.flag_value);
            break;
        case trace_value::v_label:
        case trace_value::v_fingerprint:
        case trace_value::v_enum:
        case trace_value::v_diag:
            writer.value_string(value.text);
            break;
    }
}

// Emit the caller's fields, dropping any whose value fails its type/format check or whose id repeats,
// and stopping at the field-count cap. So a body always has correctly-typed, non-duplicated fields.
void write_fields(json_writer& writer, const std::vector<trace_field>& fields) {
    std::vector<field_id> seen;
    for (std::size_t i = 0; i < fields.size() && seen.size() < max_body_fields; i++) {
        const trace_field& field = fields[i];
        if (field.value.type != expected_kind(field.id) || !field.value.valid) {
            continue;
        }
        bool duplicate = false;
        for (std::size_t k = 0; k < seen.size(); k++) {
            if (seen[k] == field.id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        writer.key(field_name(field.id));
        write_value(writer, field.value);
        seen.push_back(field.id);
    }
}

void write_group(json_writer& writer, const char* name, const std::vector<trace_field>& fields) {
    writer.key(name);
    writer.begin_object();
    write_fields(writer, fields);
    writer.end_object();
}

void write_name(json_writer& writer, const char* key, const std::string& value) {
    writer.field_string(key, bounded(value, max_name_bytes));
}

void write_envelope(json_writer& writer, const trace_envelope& env, const char* kind) {
    writer.field_uint("v", env.schema_version);
    writer.field_uint("seq", env.seq);
    writer.field_uint("t", env.t_mono_ns);
    writer.field_uint("pid", env.pid);
    if (env.inst.empty()) {
        writer.field_null("inst");
    } else {
        write_name(writer, "inst", env.inst);
    }
    write_name(writer, "tid", env.tid);
    writer.field_string("kind", kind);
}

void write_result(json_writer& writer, const trace_result_code& result) {
    writer.key("result");
    writer.begin_object();
    writer.field_int("code", result.code);
    write_name(writer, "name", result.name);
    writer.end_object();
}

// A notification action is exactly register / remove / fire; a return value's type is one of a fixed
// set. An unknown one is a bug, so it becomes a fixed safe token rather than an arbitrary string.
void write_action(json_writer& writer, const std::string& action) {
    const bool known = (action == "register" || action == "remove" || action == "fire");
    writer.field_string("action", known ? action : std::string("invalid"));
}

void write_value_type(json_writer& writer, const std::string& value_type) {
    const bool known = (value_type == "bool" || value_type == "count" || value_type == "handle" ||
                        value_type == "enum" || value_type == "notification_id");
    writer.field_string("type", known ? value_type : std::string("invalid"));
}

// Return the line only if the writer completed one bounded, well-formed document; otherwise the empty
// string, so a consumer never persists a partial or over-long record.
std::string finish(json_writer& writer) {
    return writer.ok() ? writer.str() : std::string();
}

} // namespace

trace_value tv_int(i64 value) {
    trace_value v;
    v.type = trace_value::v_int;
    v.int_value = value;
    v.valid = true;
    return v;
}
trace_value tv_uint(u64 value) {
    trace_value v;
    v.type = trace_value::v_uint;
    v.uint_value = value;
    v.valid = true;
    return v;
}
trace_value tv_flag(bool value) {
    trace_value v;
    v.type = trace_value::v_flag;
    v.flag_value = value;
    v.valid = true;
    return v;
}
trace_value tv_label(const std::string& value) {
    trace_value v;
    v.type = trace_value::v_label;
    v.text = value;
    v.valid = valid_label(value);
    return v;
}
trace_value tv_fingerprint(const std::string& value) {
    trace_value v;
    v.type = trace_value::v_fingerprint;
    v.text = value;
    v.valid = valid_fingerprint(value);
    return v;
}
trace_value tv_enum(const std::string& value) {
    trace_value v;
    v.type = trace_value::v_enum;
    v.text = value;
    v.valid = valid_enum(value);
    return v;
}
trace_value tv_diag(const std::string& value) {
    trace_value v;
    v.type = trace_value::v_diag;
    // Diagnostic text is the one free-text path, and a deliberate one: it is bounded here, and the
    // writer escapes it and replaces any invalid byte, so it is always safe JSON.
    v.text = bounded(value, max_diag_bytes);
    v.valid = true;
    return v;
}

trace_field make_field(field_id id, const trace_value& value) {
    trace_field field;
    field.id = id;
    field.value = value;
    return field;
}

std::string serialize_meta(const trace_envelope& env, const std::string& event,
                           const std::vector<trace_field>& fields) {
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "meta");
    write_name(writer, "event", event);
    write_fields(writer, fields);
    writer.end_object();
    return finish(writer);
}

std::string serialize_call(const trace_envelope& env, const std::string& fn, i32 api_version,
                           const std::string& corr, const std::vector<trace_field>& args) {
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "call");
    write_name(writer, "fn", fn);
    writer.field_int("api", api_version);
    if (!corr.empty()) {
        write_name(writer, "corr", corr);
    }
    write_group(writer, "args", args);
    writer.end_object();
    return finish(writer);
}

std::string serialize_return(const trace_envelope& env, const std::string& fn,
                             const std::string& corr, const trace_return& value) {
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "return");
    write_name(writer, "fn", fn);
    if (!corr.empty()) {
        write_name(writer, "corr", corr);
    }
    if (value.type == trace_return::r_result) {
        write_result(writer, value.result);
    } else if (value.type == trace_return::r_value && value.value.valid) {
        writer.key("value");
        writer.begin_object();
        write_value_type(writer, value.value_type);
        writer.key("v");
        write_value(writer, value.value);
        writer.end_object();
    } else {
        writer.field_bool("void", true);
    }
    if (!value.out.empty()) {
        write_group(writer, "out", value.out);
    }
    writer.end_object();
    return finish(writer);
}

std::string serialize_callback(const trace_envelope& env, const std::string& fn,
                               const std::string& corr, const trace_result_code& result,
                               const std::vector<trace_field>& payload) {
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "callback");
    write_name(writer, "fn", fn);
    if (!corr.empty()) {
        write_name(writer, "corr", corr);
    }
    write_result(writer, result);
    write_group(writer, "payload", payload);
    writer.end_object();
    return finish(writer);
}

std::string serialize_notify(const trace_envelope& env, const std::string& event,
                             const std::string& action, const std::string& id,
                             const std::vector<trace_field>& fields) {
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "notify");
    write_name(writer, "event", event);
    write_action(writer, action);
    write_name(writer, "id", id);
    write_fields(writer, fields);
    writer.end_object();
    return finish(writer);
}

std::string serialize_net(const trace_envelope& env, const std::string& event,
                          const std::vector<trace_field>& fields) {
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "net");
    write_name(writer, "event", event);
    write_fields(writer, fields);
    writer.end_object();
    return finish(writer);
}

} // namespace eosr
