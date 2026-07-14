#ifndef EOSR_CORE_TRACE_EVENT_H
#define EOSR_CORE_TRACE_EVENT_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// A semantic trace value. There is no generic string: text can enter a trace only as a validated
// label, a 16-hex fingerprint, a short enum token, or explicitly-sanitized diagnostic text. So a
// credential, a raw account id, or a payload cannot be passed as free text -- and a value that fails
// its format check is marked invalid and dropped at serialization rather than emitted.
// Spec: docs/alpha-tracing.md §4, §5.
struct trace_value {
    enum kind { v_int, v_uint, v_flag, v_label, v_fingerprint, v_enum, v_diag };
    kind type = v_int;
    i64 int_value = 0;
    u64 uint_value = 0;
    bool flag_value = false;
    std::string text;      // label / fingerprint / enum / diag
    bool valid = false;
};

trace_value tv_int(i64 value);
trace_value tv_uint(u64 value);
trace_value tv_flag(bool value);
trace_value tv_label(const std::string& value);        // <letter><word chars>#<digits>, e.g. session#3
trace_value tv_fingerprint(const std::string& value);  // exactly 16 lowercase hex characters
trace_value tv_enum(const std::string& value);         // a short [a-z0-9_.] token, e.g. reliable
trace_value tv_diag(const std::string& value);         // explicitly-safe diagnostic text, bounded

// The schema field names a body may carry. Each has exactly one required value kind, so a field can
// never appear with the wrong type, under an arbitrary key, or shadowing an envelope/body field.
enum class field_id {
    peer, peer_fp, bytes, channel, reliability, port_first, port_last, reason,
    handle, local, target, puid, eaid, account, socket, lobby, session,
    cred_type, status, index, count, len,
    dropped_files, dropped_bytes, level, detail, message
};

struct trace_field {
    field_id id;
    trace_value value;
};

// A body field in one call. The id fixes the JSON key and the required value kind.
trace_field make_field(field_id id, const trace_value& value);

// The envelope every record shares. `inst` empty is emitted as null; `tid` is a logical thread label.
// Spec: docs/alpha-tracing.md §4.
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

// What a return produced: an EOS_EResult, a typed scalar value, or nothing (void), plus any
// out-parameters.
struct trace_return {
    enum kind { r_result, r_value, r_void };
    kind type = r_void;
    trace_result_code result;       // r_result
    std::string value_type;         // r_value: bool / count / handle / enum / notification_id
    trace_value value;              // r_value
    std::vector<trace_field> out;   // may be empty
};

// Serialize one record to a single JSON-object line (no trailing newline), with fixed field order.
// Fields whose value fails validation or whose id repeats are dropped, and if the writer could not
// complete one bounded, well-formed document the result is the empty string -- never a partial line.
// Spec: docs/alpha-tracing.md §4.
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
