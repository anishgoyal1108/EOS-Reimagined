#ifndef EOSR_COMMON_JSON_WRITER_H
#define EOSR_COMMON_JSON_WRITER_H

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// A small streaming JSON serializer. Keys are emitted in the order they are added, so a record's field
// order is deterministic; string output is always valid UTF-8, with control characters and the JSON
// metacharacters escaped and any invalid input byte replaced by U+FFFD rather than passed through. We
// only ever write JSON (the trace and the owned files), so there is no reader here.
// Spec: wiki/developers/internals/alpha-tracing.qmd §7.
class json_writer {
public:
    // `max_bytes` caps the serialized output: once it would be exceeded the writer goes invalid and
    // stops emitting, so a consumer can refuse to persist an over-long record. 0 means no cap.
    explicit json_writer(std::size_t max_bytes = 0);

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

    // Whether the calls so far formed exactly one complete, well-formed document: one root value,
    // every container closed, no mismatched close, no key outside an object, no value without a key,
    // no dangling key, and the output cap not exceeded. A fresh (untouched) writer is not a document,
    // so this is false until a root value completes. A consumer checks it before persisting.
    bool ok() const { return valid_ && root_done_ && levels_.empty(); }

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

    // Reserve `n` more output bytes: true if they fit within the cap (and the writer is still valid);
    // otherwise mark the writer invalid and return false, so no append ever overruns the budget.
    bool reserve(std::size_t n);
    // Mark the single root value complete when a top-level scalar was just written.
    void mark_root_if_top();

    std::string out_;
    std::vector<level> levels_;
    std::size_t max_bytes_;
    bool after_key_;
    bool valid_;
    bool root_done_;
};

} // namespace eosr

#endif
