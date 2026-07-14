#ifndef EOSR_CORE_TRACE_EVENT_H
#define EOSR_CORE_TRACE_EVENT_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// A scalar that may be traced: an int, unsigned, bool, or short string. There is deliberately no
// pointer or byte-buffer form -- a payload or pointer cannot be traced at all -- and serialization is
// the second line of defence: a field is emitted only if its key is on the allow-list (§4's known
// field names), never a reserved envelope/body key, and every string is length-bounded and every body
// field-count bounded, so an arbitrary token, an over-long value, or a key that would shadow the
// schema cannot reach the file. See the caps in trace_event.cpp.
// Spec: docs/alpha-tracing.md §5.
struct trace_scalar {
    enum kind { s_int, s_uint, s_bool, s_string };
    kind type = s_int;
    i64 int_value = 0;
    u64 uint_value = 0;
    bool bool_value = false;
    std::string string_value;
};

trace_scalar scalar_int(i64 value);
trace_scalar scalar_uint(u64 value);
trace_scalar scalar_bool(bool value);
trace_scalar scalar_string(const std::string& value);

// A named scalar for an args / payload / fields list.
struct trace_field {
    std::string key;
    trace_scalar value;
};

// The envelope every record shares. `inst` empty is emitted as null; `tid` is a logical thread label
// (e.g. "t#0"), never an OS id.
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

// What a return produced. Exactly one of: an EOS_EResult, a scalar value (a getter's bool / count /
// handle label / enum name / notification-id label), or nothing (void). Any allow-listed
// out-parameters go in `out`.
struct trace_return {
    enum kind { r_result, r_value, r_void };
    kind type = r_void;
    trace_result_code result;       // r_result
    std::string value_type;         // r_value: "bool" / "count" / "handle" / "enum" / "notification_id"
    trace_scalar value;             // r_value
    std::vector<trace_field> out;   // may be empty
};

// Serialize one record to a single JSON-object line (no trailing newline). Field order is fixed, so a
// record's golden form is stable. Each takes only the safe, typed parameters its kind allows.
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
