#ifndef EOSR_CORE_TRACE_EVENT_H
#define EOSR_CORE_TRACE_EVENT_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// A semantic trace value. There is no free-text form at all: text can enter a trace only as a
// validated label, a 16-hex fingerprint, or an enum symbol (an identifier such as "device" or
// "EOS_UNL_BottomRight"). So a credential, a continuance token, a raw account id, or a payload cannot
// be represented as a value -- and a value that fails its format check is marked invalid and dropped.
// Spec: wiki/developers/internals/alpha-tracing.qmd §4, §5.
struct trace_value {
    enum kind { v_int, v_uint, v_flag, v_label, v_fingerprint, v_enum };
    kind type = v_int;
    i64 int_value = 0;
    u64 uint_value = 0;
    bool flag_value = false;
    std::string text;      // label / fingerprint / enum
    bool valid = false;
};

trace_value tv_int(i64 value);
trace_value tv_uint(u64 value);
trace_value tv_flag(bool value);
trace_value tv_label(const std::string& value);        // <letter><word>#<digits>, e.g. session#3
trace_value tv_fingerprint(const std::string& value);  // exactly 16 lowercase hex characters
trace_value tv_enum(const std::string& value);         // an identifier, e.g. reliable or EOS_UNL_Foo

// The schema field names a body may carry, with an explicit `invalid` sentinel so a default-built or
// cast field id is rejected rather than serialized. Each id has one required value kind and belongs to
// specific record bodies (see trace_event.cpp), so a field cannot appear under an arbitrary key, carry
// the wrong type, or drift into a body it does not belong to.
enum class field_id {
    invalid = 0,
    peer, peer_fp, bytes, channel, reliability, port_first, port_last, reason,
    handle, local, target, puid, eaid, account, socket, lobby, session,
    cred_type, status, index, count, len,
    dropped_files, dropped_bytes, source, level,
    config_field, action, port
};

struct trace_field {
    field_id id = field_id::invalid;
    trace_value value;
};

trace_field make_field(field_id id, const trace_value& value);

// The envelope every record shares. `inst` empty is emitted as null; `tid` is a logical thread label.
// Spec: wiki/developers/internals/alpha-tracing.qmd §4.
struct trace_envelope {
    u32 schema_version = 1;
    u64 seq = 0;
    u64 t_mono_ns = 0;
    u64 pid = 0;
    std::string inst;
    std::string tid;
};

// An EOS_EResult, both numeric and symbolic.
struct trace_result_code {
    i32 code = 0;
    std::string name;
};

// What a return produced. Build it through the factories below so the declared type and its value can
// never contradict; the serializer rejects the whole record if they do.
struct trace_return {
    enum kind { r_result, r_value, r_void };
    kind type = r_void;
    trace_result_code result;       // r_result
    std::string value_type;         // r_value: bool / count / handle / enum / notification_id
    trace_value value;              // r_value
    bool value_is_null = false;     // r_value: an absent handle / notification id
    std::vector<trace_field> out;   // may be empty
};

trace_return return_result(i32 code, const std::string& name);
trace_return return_void();
trace_return return_bool(bool value);
trace_return return_count(u64 value);
trace_return return_length(u64 value);
trace_return return_handle(const std::string& label);
trace_return return_null_handle();
trace_return return_enum(const std::string& symbol);
trace_return return_notification_id(const std::string& label);
trace_return return_null_notification_id();

// Serialize one record to a single JSON-object line (no trailing newline), with fixed field order. A
// field whose value fails validation, repeats, or does not belong to this body is dropped; an invalid
// action, return value, correlation id, notification id, function, or event name rejects the whole
// record; and the result is the empty string whenever the writer could not complete exactly one
// bounded, well-formed document -- never a partial or over-long line.
// Spec: wiki/developers/internals/alpha-tracing.qmd §4.
std::string serialize_meta(const trace_envelope& env, const std::string& event,
                           const std::vector<trace_field>& fields);
std::string serialize_call(const trace_envelope& env, const std::string& fn, i32 api_version,
                           const std::string& corr, const std::vector<trace_field>& args);
std::string serialize_return(const trace_envelope& env, const std::string& fn,
                             const std::string& corr, const trace_return& value);
std::string serialize_callback(const trace_envelope& env, const std::string& fn,
                               const std::string& corr, const trace_result_code& result,
                               const std::vector<trace_field>& payload);
std::string serialize_notify(const trace_envelope& env, const std::string& event,
                             const std::string& action, const std::string& id,
                             const std::vector<trace_field>& fields);
std::string serialize_net(const trace_envelope& env, const std::string& event,
                          const std::vector<trace_field>& fields);

} // namespace eosr

#endif
