#ifndef EOSR_CORE_CONFIG_FILE_H
#define EOSR_CORE_CONFIG_FILE_H

#include <map>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/config.h"

namespace eosr {

// A parsed eosr.json reduced to the shapes config reads: a string, an integer, a boolean, a uniform
// array of integers or strings, or "some other JSON type". The reader classifies each top-level value
// into one of these, and the typed reads answer
// missing / ok / wrong_type from it -- exactly what config_source's file_* methods expect, so a real
// source is a thin wrapper of getenv plus one of these.
// Spec: wiki/developers/internals/alpha-tracing.qmd §7.
class config_file {
public:
    lookup get_string(const std::string& key, std::string& out) const;
    lookup get_int(const std::string& key, i64& out) const;
    lookup get_int_pair(const std::string& key, i64& first, i64& second) const;
    lookup get_bool(const std::string& key, bool& out) const;
    lookup get_string_array(const std::string& key, std::vector<std::string>& out) const;
    std::size_t size() const { return nodes_.size(); }

    // Populate the file. The reader calls these; tests may too.
    void set_string(const std::string& key, const std::string& value);
    void set_int(const std::string& key, i64 value);
    void set_int_array(const std::string& key, const std::vector<i64>& values);
    void set_string_array(const std::string& key, const std::vector<std::string>& values);
    void set_bool(const std::string& key, bool value);
    void set_other(const std::string& key);

private:
    struct node {
        enum kind { k_string, k_int, k_int_array, k_string_array, k_bool, k_other };
        kind type;
        std::string str;
        i64 integer;
        std::vector<i64> ints;
        std::vector<std::string> strings;
        bool flag;
    };
    std::map<std::string, node> nodes_;
};

// Parse `bytes` as the config file. Returns true and fills `out` on success. On any parse error --
// malformed JSON, a duplicate key, an over-long token, too many array elements, or too deep nesting
// -- returns false with `error` set and `out` left empty, so the caller treats the file as absent.
// A leading UTF-8 BOM is skipped; the top-level value must be an object. Bounds: input <= 64 KiB, any
// string or number token <= 4 KiB, <= 64 array elements, nesting depth <= 8.
// Spec: wiki/developers/internals/alpha-tracing.qmd §7.
bool parse_config_file(const std::string& bytes, config_file& out, std::string& error);

} // namespace eosr

#endif
