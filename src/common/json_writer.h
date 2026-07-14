#ifndef EOSR_COMMON_JSON_WRITER_H
#define EOSR_COMMON_JSON_WRITER_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// A small streaming JSON serializer. Keys are emitted in the order they are added, so a record's field
// order is deterministic; string output is always valid UTF-8, with control characters and the JSON
// metacharacters escaped and any invalid input byte replaced by U+FFFD rather than passed through. We
// only ever write JSON (the trace and the owned files), so there is no reader here.
// Spec: docs/alpha-tracing.md §7.
class json_writer {
public:
    json_writer();

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    // A key inside the current object. The next value method supplies its value.
    void key(const std::string& name);

    void value_string(const std::string& value);
    void value_int(i64 value);
    void value_uint(u64 value);
    void value_bool(bool value);
    void value_null();

    // key + scalar in one call.
    void field_string(const std::string& name, const std::string& value);
    void field_int(const std::string& name, i64 value);
    void field_uint(const std::string& name, u64 value);
    void field_bool(const std::string& name, bool value);
    void field_null(const std::string& name);

    // The serialized JSON built so far.
    const std::string& str() const { return out_; }

    // Whether every call so far formed a well-formed, complete document: no mismatched close, no key
    // outside an object, no value without a key, no dangling key, and every container closed. A
    // misused writer stops emitting once invalid, so a consumer can check this before persisting.
    bool ok() const { return valid_ && levels_.empty(); }

private:
    // Prepare to emit a value: validate the position and emit the separator a value needs (a comma
    // unless it is first in its container or directly follows a key). Sets the writer invalid on
    // misuse.
    void pre_value();
    // Write a JSON string literal: quoted, escaped, and guaranteed-valid UTF-8.
    void write_string(const std::string& value);

    struct level {
        bool is_object;
        bool first;
    };

    std::string out_;
    std::vector<level> levels_;
    bool after_key_;
    bool valid_;
};

} // namespace eosr

#endif
