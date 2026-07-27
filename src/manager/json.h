#ifndef EOSR_MANAGER_JSON_H
#define EOSR_MANAGER_JSON_H

#include <map>
#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {
namespace manager {

enum class json_kind {
    null_value,
    boolean,
    integer,
    number,
    string,
    array,
    object
};

struct json_value {
    json_value();
    json_kind kind;
    bool boolean;
    i64 integer;
    std::string text;
    std::vector<json_value> elements;
    std::map<std::string, json_value> members;
};

bool parse_json(const std::string& bytes, json_value& out, std::string& error);
std::string serialize_json(const json_value& value);
const json_value* json_member(const json_value& object, const std::string& name);

json_value json_bool(bool value);
json_value json_int(i64 value);
json_value json_string(const std::string& value);
json_value json_array();
json_value json_object();

} // namespace manager
} // namespace eosr

#endif
