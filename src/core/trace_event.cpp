#include "core/trace_event.h"

#include <cstddef>
#include <string>
#include <vector>

#include "common/json_writer.h"

namespace eosr {

namespace {

// The writer is capped below the 64 KiB sink minimum, so a record that would overrun fails to complete
// and is dropped rather than persisted. Spec: wiki/internals/alpha-tracing.md §4, §7.
const std::size_t max_record_bytes = 60000;
const std::size_t max_body_fields = 32;
const std::size_t max_label_bytes = 64;
const std::size_t max_name_bytes = 128;

// The record bodies, used to restrict which fields each may carry.
enum record_kind { rk_meta, rk_call, rk_callback, rk_return, rk_notify, rk_net };

bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

// An identifier: an EOS symbol, an event, or an enum token, e.g. reliable or EOS_UNL_BottomRight.
bool valid_ident(const std::string& text, std::size_t cap) {
    if (text.empty() || text.size() > cap || !is_ident_start(text[0])) {
        return false;
    }
    for (std::size_t i = 1; i < text.size(); i++) {
        if (!is_ident_char(text[i])) {
            return false;
        }
    }
    return true;
}

// A label is <lower-letter><word chars>#<digits>, e.g. session#3 -- a raw hex id (no '#') is not one.
bool valid_label(const std::string& text) {
    if (text.empty() || text.size() > max_label_bytes) {
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

// Resolve a field id to its JSON key and required value kind. Returns false for the invalid sentinel
// or a cast/uninitialized id, so an out-of-schema field is dropped rather than serialized.
bool field_info(field_id id, const char*& name, trace_value::kind& kind) {
    switch (id) {
        case field_id::peer: name = "peer"; kind = trace_value::v_label; return true;
        case field_id::peer_fp: name = "peer_fp"; kind = trace_value::v_fingerprint; return true;
        case field_id::bytes: name = "bytes"; kind = trace_value::v_uint; return true;
        case field_id::channel: name = "channel"; kind = trace_value::v_int; return true;
        case field_id::reliability: name = "reliability"; kind = trace_value::v_enum; return true;
        case field_id::port_first: name = "port_first"; kind = trace_value::v_uint; return true;
        case field_id::port_last: name = "port_last"; kind = trace_value::v_uint; return true;
        case field_id::reason: name = "reason"; kind = trace_value::v_enum; return true;
        case field_id::handle: name = "handle"; kind = trace_value::v_label; return true;
        case field_id::local: name = "local"; kind = trace_value::v_label; return true;
        case field_id::target: name = "target"; kind = trace_value::v_label; return true;
        case field_id::puid: name = "puid"; kind = trace_value::v_label; return true;
        case field_id::eaid: name = "eaid"; kind = trace_value::v_label; return true;
        case field_id::account: name = "account"; kind = trace_value::v_label; return true;
        case field_id::socket: name = "socket"; kind = trace_value::v_label; return true;
        case field_id::lobby: name = "lobby"; kind = trace_value::v_label; return true;
        case field_id::session: name = "session"; kind = trace_value::v_label; return true;
        case field_id::cred_type: name = "cred_type"; kind = trace_value::v_enum; return true;
        case field_id::status: name = "status"; kind = trace_value::v_enum; return true;
        case field_id::index: name = "index"; kind = trace_value::v_uint; return true;
        case field_id::count: name = "count"; kind = trace_value::v_uint; return true;
        case field_id::len: name = "len"; kind = trace_value::v_uint; return true;
        case field_id::dropped_files: name = "dropped_files"; kind = trace_value::v_uint; return true;
        case field_id::dropped_bytes: name = "dropped_bytes"; kind = trace_value::v_uint; return true;
        case field_id::source: name = "source"; kind = trace_value::v_enum; return true;
        case field_id::level: name = "level"; kind = trace_value::v_enum; return true;
        case field_id::config_field: name = "field"; kind = trace_value::v_enum; return true;
        case field_id::action: name = "action"; kind = trace_value::v_enum; return true;
        case field_id::port: name = "port"; kind = trace_value::v_uint; return true;
        case field_id::invalid:
        default:
            return false;
    }
}

// Which fields each body may carry, so a field cannot drift into a body it does not belong to.
bool field_allowed(record_kind body, field_id id) {
    switch (body) {
        case rk_meta:
            // peer_fp rides the meta/profile record (the local pseudonymous fingerprint once the
            // profile is loaded); field/source/reason/action carry the config record; and
            // dropped_files/dropped_bytes carry the rotate record.
            return id == field_id::peer_fp || id == field_id::config_field ||
                   id == field_id::source || id == field_id::reason || id == field_id::action ||
                   id == field_id::level || id == field_id::dropped_files ||
                   id == field_id::dropped_bytes;
        case rk_net:
            return id == field_id::peer || id == field_id::peer_fp || id == field_id::bytes ||
                   id == field_id::channel || id == field_id::reliability ||
                   id == field_id::port || id == field_id::port_first ||
                   id == field_id::port_last || id == field_id::reason || id == field_id::socket ||
                   id == field_id::count;
        case rk_call:
            return id == field_id::cred_type || id == field_id::local || id == field_id::target ||
                   id == field_id::account || id == field_id::socket || id == field_id::channel ||
                   id == field_id::reliability || id == field_id::status || id == field_id::index ||
                   id == field_id::count || id == field_id::len || id == field_id::port_first ||
                   id == field_id::port_last;
        case rk_callback:
            return id == field_id::puid || id == field_id::eaid || id == field_id::local ||
                   id == field_id::target || id == field_id::account || id == field_id::handle ||
                   id == field_id::status || id == field_id::count || id == field_id::session ||
                   id == field_id::lobby;
        case rk_return:
            return id == field_id::handle || id == field_id::session || id == field_id::lobby ||
                   id == field_id::len || id == field_id::count;
        case rk_notify:
            return id == field_id::target || id == field_id::local || id == field_id::status;
    }
    return false;
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
            writer.value_string(value.text);
            break;
    }
}

// Emit the caller's fields, dropping any that is unknown, does not belong to this body, carries the
// wrong type, fails validation, or repeats. So a body always has correctly-typed, in-schema,
// non-duplicated fields.
void write_fields(json_writer& writer, record_kind body, const std::vector<trace_field>& fields) {
    std::vector<field_id> seen;
    for (std::size_t i = 0; i < fields.size() && seen.size() < max_body_fields; i++) {
        const trace_field& field = fields[i];
        const char* name;
        trace_value::kind kind;
        if (!field_info(field.id, name, kind)) {
            continue;
        }
        if (!field_allowed(body, field.id)) {
            continue;
        }
        if (field.value.type != kind || !field.value.valid) {
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
        writer.key(name);
        write_value(writer, field.value);
        seen.push_back(field.id);
    }
}

void write_group(json_writer& writer, const char* name, record_kind body,
                 const std::vector<trace_field>& fields) {
    writer.key(name);
    writer.begin_object();
    write_fields(writer, body, fields);
    writer.end_object();
}

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

std::string finish(json_writer& writer) {
    return writer.ok() ? writer.str() : std::string();
}

// The value-type / value-kind pairing a return must satisfy.
bool return_value_ok(const trace_return& value) {
    if (!value.value.valid) {
        return false;
    }
    const std::string& t = value.value_type;
    const trace_value::kind k = value.value.type;
    if (t == "bool") return k == trace_value::v_flag;
    if (t == "count") return k == trace_value::v_uint;
    if (t == "handle" || t == "notification_id") return k == trace_value::v_label;
    if (t == "enum") return k == trace_value::v_enum;
    return false;
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
    v.valid = valid_ident(value, max_label_bytes);
    return v;
}

trace_field make_field(field_id id, const trace_value& value) {
    trace_field field;
    field.id = id;
    field.value = value;
    return field;
}

trace_return return_result(i32 code, const std::string& name) {
    trace_return r;
    r.type = trace_return::r_result;
    r.result.code = code;
    r.result.name = name;
    return r;
}
trace_return return_void() {
    trace_return r;
    r.type = trace_return::r_void;
    return r;
}
trace_return return_bool(bool value) {
    trace_return r;
    r.type = trace_return::r_value;
    r.value_type = "bool";
    r.value = tv_flag(value);
    return r;
}
trace_return return_count(u64 value) {
    trace_return r;
    r.type = trace_return::r_value;
    r.value_type = "count";
    r.value = tv_uint(value);
    return r;
}
trace_return return_handle(const std::string& label) {
    trace_return r;
    r.type = trace_return::r_value;
    r.value_type = "handle";
    r.value = tv_label(label);
    return r;
}
trace_return return_enum(const std::string& symbol) {
    trace_return r;
    r.type = trace_return::r_value;
    r.value_type = "enum";
    r.value = tv_enum(symbol);
    return r;
}
trace_return return_notification_id(const std::string& label) {
    trace_return r;
    r.type = trace_return::r_value;
    r.value_type = "notification_id";
    r.value = tv_label(label);
    return r;
}

std::string serialize_meta(const trace_envelope& env, const std::string& event,
                           const std::vector<trace_field>& fields) {
    if (!valid_ident(event, max_name_bytes)) {
        return std::string();
    }
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "meta");
    writer.field_string("event", event);
    write_fields(writer, rk_meta, fields);
    writer.end_object();
    return finish(writer);
}

std::string serialize_call(const trace_envelope& env, const std::string& fn, i32 api_version,
                           const std::string& corr, const std::vector<trace_field>& args) {
    if (!valid_ident(fn, max_name_bytes) || (!corr.empty() && !valid_label(corr))) {
        return std::string();
    }
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "call");
    writer.field_string("fn", fn);
    writer.field_int("api", api_version);
    if (!corr.empty()) {
        writer.field_string("corr", corr);
    }
    write_group(writer, "args", rk_call, args);
    writer.end_object();
    return finish(writer);
}

std::string serialize_return(const trace_envelope& env, const std::string& fn,
                             const std::string& corr, const trace_return& value) {
    if (!valid_ident(fn, max_name_bytes) || (!corr.empty() && !valid_label(corr))) {
        return std::string();
    }
    if (value.type == trace_return::r_value && !return_value_ok(value)) {
        return std::string(); // a declared value type and its value must agree
    }
    if (value.type == trace_return::r_result && !valid_ident(value.result.name, max_name_bytes)) {
        return std::string();
    }
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "return");
    writer.field_string("fn", fn);
    if (!corr.empty()) {
        writer.field_string("corr", corr);
    }
    if (value.type == trace_return::r_result) {
        writer.key("result");
        writer.begin_object();
        writer.field_int("code", value.result.code);
        writer.field_string("name", value.result.name);
        writer.end_object();
    } else if (value.type == trace_return::r_value) {
        writer.key("value");
        writer.begin_object();
        writer.field_string("type", value.value_type);
        writer.key("v");
        write_value(writer, value.value);
        writer.end_object();
    } else {
        writer.field_bool("void", true);
    }
    if (!value.out.empty()) {
        write_group(writer, "out", rk_return, value.out);
    }
    writer.end_object();
    return finish(writer);
}

std::string serialize_callback(const trace_envelope& env, const std::string& fn,
                               const std::string& corr, const trace_result_code& result,
                               const std::vector<trace_field>& payload) {
    if (!valid_ident(fn, max_name_bytes) || (!corr.empty() && !valid_label(corr))) {
        return std::string();
    }
    if (!result.name.empty() && !valid_ident(result.name, max_name_bytes)) {
        return std::string();
    }
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "callback");
    writer.field_string("fn", fn);
    if (!corr.empty()) {
        writer.field_string("corr", corr);
    }
    writer.key("result");
    writer.begin_object();
    writer.field_int("code", result.code);
    if (result.name.empty()) {
        writer.field_null("name");
    } else {
        writer.field_string("name", result.name);
    }
    writer.end_object();
    write_group(writer, "payload", rk_callback, payload);
    writer.end_object();
    return finish(writer);
}

std::string serialize_notify(const trace_envelope& env, const std::string& event,
                             const std::string& action, const std::string& id,
                             const std::vector<trace_field>& fields) {
    const bool action_ok = (action == "register" || action == "remove" || action == "fire");
    if (!valid_ident(event, max_name_bytes) || !action_ok || !valid_label(id)) {
        return std::string();
    }
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "notify");
    writer.field_string("event", event);
    writer.field_string("action", action);
    writer.field_string("id", id);
    write_fields(writer, rk_notify, fields);
    writer.end_object();
    return finish(writer);
}

std::string serialize_net(const trace_envelope& env, const std::string& event,
                          const std::vector<trace_field>& fields) {
    if (!valid_ident(event, max_name_bytes)) {
        return std::string();
    }
    json_writer writer(max_record_bytes);
    writer.begin_object();
    write_envelope(writer, env, "net");
    writer.field_string("event", event);
    write_fields(writer, rk_net, fields);
    writer.end_object();
    return finish(writer);
}

} // namespace eosr
