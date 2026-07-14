#include "doctest.h"

#include <limits>
#include <string>

#include "common/types.h"
#include "core/config.h"
#include "core/config_file.h"

using namespace eosr;

namespace {

// Parse and require success, returning the file. Fails the test if parsing was rejected.
config_file parse_ok(const std::string& bytes) {
    config_file file;
    std::string error;
    REQUIRE(parse_config_file(bytes, file, error));
    return file;
}

// Parse and require rejection, returning the error text.
std::string parse_err(const std::string& bytes) {
    config_file file;
    std::string error;
    REQUIRE_FALSE(parse_config_file(bytes, file, error));
    CHECK_FALSE(error.empty());
    return error;
}

std::string big(std::size_t bytes, char fill) {
    return std::string(bytes, fill);
}

} // namespace

TEST_CASE("a well-formed config parses to its typed values") {
    const config_file file = parse_ok(
        "{ \"display_name\": \"Marlowe\", \"trace_level\": \"lifecycle\","
        "  \"trace_max_bytes\": 131072, \"discovery_ports\": [45700, 45703] }");

    std::string s;
    CHECK(file.get_string("display_name", s) == lookup::ok);
    CHECK(s == "Marlowe");
    CHECK(file.get_string("trace_level", s) == lookup::ok);
    CHECK(s == "lifecycle");

    i64 n = 0;
    CHECK(file.get_int("trace_max_bytes", n) == lookup::ok);
    CHECK(n == 131072);

    i64 first = 0;
    i64 last = 0;
    CHECK(file.get_int_pair("discovery_ports", first, last) == lookup::ok);
    CHECK(first == 45700);
    CHECK(last == 45703);
}

TEST_CASE("a missing key is distinct from a present key of the wrong type") {
    const config_file file = parse_ok(
        "{ \"display_name\": \"Marlowe\", \"trace_max_bytes\": \"not-a-number\","
        "  \"discovery_ports\": {} }");

    std::string s;
    i64 n = 0;
    i64 a = 0;
    i64 b = 0;

    // Absent everywhere.
    CHECK(file.get_string("nope", s) == lookup::missing);
    CHECK(file.get_int("nope", n) == lookup::missing);

    // Present, but the wrong JSON type for the read.
    CHECK(file.get_int("display_name", n) == lookup::wrong_type);       // string, not int
    CHECK(file.get_int("trace_max_bytes", n) == lookup::wrong_type);    // string, not int
    CHECK(file.get_string("trace_max_bytes", s) == lookup::ok);         // it IS a string
    CHECK(file.get_int_pair("discovery_ports", a, b) == lookup::wrong_type); // object, not pair
}

TEST_CASE("integer syntax is required for integer reads") {
    // Fractional and exponent forms are numbers, but not integers, so an integer read rejects them.
    const config_file file = parse_ok(
        "{ \"whole\": 42, \"fraction\": 42.0, \"exponent\": 4e2 }");
    i64 n = 0;
    CHECK(file.get_int("whole", n) == lookup::ok);
    CHECK(n == 42);
    CHECK(file.get_int("fraction", n) == lookup::wrong_type);
    CHECK(file.get_int("exponent", n) == lookup::wrong_type);
}

TEST_CASE("an int pair requires exactly two integers") {
    const config_file file = parse_ok(
        "{ \"two\": [1, 2], \"three\": [1, 2, 3], \"mixed\": [1, \"x\"], \"empty\": [] }");
    i64 a = 0;
    i64 b = 0;
    CHECK(file.get_int_pair("two", a, b) == lookup::ok);
    CHECK(file.get_int_pair("three", a, b) == lookup::wrong_type);
    CHECK(file.get_int_pair("mixed", a, b) == lookup::wrong_type);
    CHECK(file.get_int_pair("empty", a, b) == lookup::wrong_type);
}

TEST_CASE("unknown keys are ignored, not rejected") {
    const config_file file = parse_ok(
        "{ \"display_name\": \"Marlowe\", \"future_option\": true, \"nested\": {\"a\": [1, 2]} }");
    std::string s;
    CHECK(file.get_string("display_name", s) == lookup::ok);
    CHECK(file.get_string("future_option", s) == lookup::wrong_type); // present but not a string
}

TEST_CASE("a duplicate key rejects the whole file") {
    const std::string error = parse_err("{ \"display_name\": \"a\", \"display_name\": \"b\" }");
    CHECK(error.find("duplicate") != std::string::npos);
}

TEST_CASE("a leading UTF-8 BOM is skipped") {
    const config_file file = parse_ok("\xEF\xBB\xBF{ \"display_name\": \"Marlowe\" }");
    std::string s;
    CHECK(file.get_string("display_name", s) == lookup::ok);
    CHECK(s == "Marlowe");
}

TEST_CASE("the top-level value must be an object") {
    parse_err("[1, 2, 3]");
    parse_err("\"just a string\"");
    parse_err("42");
    parse_err("");
}

TEST_CASE("trailing content after the object is rejected") {
    parse_err("{ \"display_name\": \"a\" } extra");
    parse_err("{ \"display_name\": \"a\" } {}");
}

TEST_CASE("string escapes decode, including an escaped NUL and a surrogate pair") {
    const config_file file = parse_ok(
        "{ \"tab\": \"a\\tb\", \"letter\": \"\\u0041\", \"nul\": \"x\\u0000y\","
        "  \"emoji\": \"\\uD83D\\uDE00\" }");
    std::string s;
    CHECK(file.get_string("tab", s) == lookup::ok);
    CHECK(s == "a\tb");
    CHECK(file.get_string("letter", s) == lookup::ok);
    CHECK(s == "A");
    CHECK(file.get_string("nul", s) == lookup::ok);
    CHECK(s == std::string("x\0y", 3)); // The escape decodes to a real NUL; config rejects it later.
    CHECK(file.get_string("emoji", s) == lookup::ok);
    CHECK(s == "\xF0\x9F\x98\x80"); // U+1F600 as 4-byte UTF-8
}

TEST_CASE("malformed json is rejected") {
    parse_err("{ \"display_name\": }");            // missing value
    parse_err("{ \"display_name\" \"a\" }");       // missing colon
    parse_err("{ \"display_name\": \"a\", }");     // trailing comma
    parse_err("{ \"display_name\": \"unterminated }"); // unterminated string
    parse_err("{ display_name: \"a\" }");          // unquoted key
    parse_err("{ \"n\": 01 }");                    // leading zero
    parse_err("{ \"s\": \"a\\qb\" }");             // invalid escape
    parse_err("{ \"s\": \"\\uD83D\" }");           // lone high surrogate
}

TEST_CASE("the input size is bounded") {
    // A 64 KiB+ input is rejected before parsing.
    std::string oversized = "{ \"pad\": \"";
    oversized += big(70000, 'a');
    oversized += "\" }";
    parse_err(oversized);
}

TEST_CASE("a single token is bounded") {
    std::string long_string = "{ \"pad\": \"";
    long_string += big(5000, 'a'); // over the 4 KiB token cap, still under the input cap
    long_string += "\" }";
    const std::string error = parse_err(long_string);
    CHECK(error.find("too long") != std::string::npos);
}

TEST_CASE("array length is bounded") {
    std::string many = "{ \"a\": [";
    for (int i = 0; i < 100; i++) {
        if (i != 0) {
            many += ",";
        }
        many += "1";
    }
    many += "] }";
    parse_err(many);
}

TEST_CASE("nesting depth is bounded") {
    std::string deep = "{ \"a\":";
    for (int i = 0; i < 12; i++) {
        deep += "[";
    }
    for (int i = 0; i < 12; i++) {
        deep += "]";
    }
    deep += " }";
    parse_err(deep);
}

TEST_CASE("an empty object is valid and holds nothing") {
    const config_file file = parse_ok("{}");
    std::string s;
    CHECK(file.get_string("anything", s) == lookup::missing);
}

TEST_CASE("raw string bytes must be valid UTF-8") {
    std::string invalid = "{ \"display_name\": \"";
    invalid += "\xC0\xAF"; // overlong UTF-8 encoding
    invalid += "\" }";
    const std::string error = parse_err(invalid);
    CHECK(error.find("UTF-8") != std::string::npos);
}

TEST_CASE("the string token bound applies before escape decoding") {
    std::string escaped = "{ \"pad\": \"";
    for (int i = 0; i < 2049; i++) {
        escaped += "\\n"; // 4098 source bytes, but only 2049 decoded bytes
    }
    escaped += "\" }";
    const std::string error = parse_err(escaped);
    CHECK(error.find("too long") != std::string::npos);
}

TEST_CASE("the full signed 64-bit integer range is classified as integer") {
    const config_file file = parse_ok(
        "{ \"min\": -9223372036854775808, \"max\": 9223372036854775807 }");
    i64 value = 0;
    CHECK(file.get_int("min", value) == lookup::ok);
    CHECK(value == std::numeric_limits<i64>::min());
    CHECK(file.get_int("max", value) == lookup::ok);
    CHECK(value == std::numeric_limits<i64>::max());
}

TEST_CASE("a failed parse empties a previously populated output") {
    config_file file;
    file.set_string("display_name", "stale");
    std::string error;
    REQUIRE_FALSE(parse_config_file("{ malformed", file, error));

    std::string value;
    CHECK(file.get_string("display_name", value) == lookup::missing);
}

TEST_CASE("a JSON boolean is read as a boolean") {
    config_file file;
    std::string error;
    REQUIRE(parse_config_file("{\"enable_lan\":true,\"unlock_dlcs\":false}", file, error));

    bool value = false;
    REQUIRE(file.get_bool("enable_lan", value) == lookup::ok);
    CHECK(value);
    REQUIRE(file.get_bool("unlock_dlcs", value) == lookup::ok);
    CHECK_FALSE(value);

    CHECK(file.get_bool("absent", value) == lookup::missing);

    // A boolean is not a string and not an integer, and each mistyped read says so.
    std::string text;
    i64 number = 0;
    CHECK(file.get_string("enable_lan", text) == lookup::wrong_type);
    CHECK(file.get_int("enable_lan", number) == lookup::wrong_type);
}

TEST_CASE("a non-boolean value read as a boolean is a wrong type, not a false") {
    config_file file;
    std::string error;
    REQUIRE(parse_config_file("{\"enable_lan\":\"true\",\"count\":0}", file, error));

    bool value = true;
    CHECK(file.get_bool("enable_lan", value) == lookup::wrong_type); // a string, not a bool
    CHECK(file.get_bool("count", value) == lookup::wrong_type);      // an int, not a bool
    CHECK(value);                                                    // and `out` is untouched
}
