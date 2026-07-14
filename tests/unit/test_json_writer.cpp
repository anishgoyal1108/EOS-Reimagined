#include "doctest.h"

#include <limits>
#include <string>

#include "common/json_writer.h"
#include "common/types.h"

using namespace eosr;

TEST_CASE("an empty object serializes to braces") {
    json_writer writer;
    writer.begin_object();
    writer.end_object();
    CHECK(writer.str() == "{}");
}

TEST_CASE("fields are emitted in the order they are added") {
    json_writer writer;
    writer.begin_object();
    writer.field_string("first", "a");
    writer.field_int("second", 5);
    writer.field_string("third", "c");
    writer.end_object();
    CHECK(writer.str() == "{\"first\":\"a\",\"second\":5,\"third\":\"c\"}");
}

TEST_CASE("every scalar type serializes") {
    json_writer writer;
    writer.begin_object();
    writer.field_int("i", -3);
    writer.field_uint("u", std::numeric_limits<u64>::max());
    writer.field_bool("b", true);
    writer.field_null("z");
    writer.field_string("s", "hi");
    writer.end_object();
    CHECK(writer.str() ==
          "{\"i\":-3,\"u\":18446744073709551615,\"b\":true,\"z\":null,\"s\":\"hi\"}");
}

TEST_CASE("a nested object serializes with its own ordered fields") {
    json_writer writer;
    writer.begin_object();
    writer.key("result");
    writer.begin_object();
    writer.field_int("code", 0);
    writer.field_string("name", "EOS_Success");
    writer.end_object();
    writer.end_object();
    CHECK(writer.str() == "{\"result\":{\"code\":0,\"name\":\"EOS_Success\"}}");
}

TEST_CASE("a nested array serializes its values in order") {
    json_writer writer;
    writer.begin_object();
    writer.key("ports");
    writer.begin_array();
    writer.value_int(45700);
    writer.value_int(45703);
    writer.end_array();
    writer.end_object();
    CHECK(writer.str() == "{\"ports\":[45700,45703]}");
}

TEST_CASE("json metacharacters and control characters are escaped") {
    json_writer writer;
    writer.begin_object();
    // a " b \ c <LF> d <TAB> e
    writer.field_string("s", "a\"b\\c\nd\te");
    writer.end_object();
    CHECK(writer.str() == "{\"s\":\"a\\\"b\\\\c\\nd\\te\"}");
}

TEST_CASE("an other control character becomes a unicode escape") {
    json_writer writer;
    writer.begin_object();
    writer.field_string("s", std::string("x\x01y", 3)); // U+0001, not one of the named escapes
    writer.end_object();
    CHECK(writer.str() == "{\"s\":\"x\\u0001y\"}");
}

TEST_CASE("valid multibyte UTF-8 passes through unescaped") {
    json_writer writer;
    writer.begin_object();
    writer.field_string("s", "\xF0\x9F\x98\x80"); // U+1F600
    writer.end_object();
    CHECK(writer.str() == "{\"s\":\"\xF0\x9F\x98\x80\"}");
}

TEST_CASE("an invalid UTF-8 byte is replaced, never passed through") {
    json_writer writer;
    writer.begin_object();
    writer.field_string("s", std::string("a\xFF" "b", 3)); // 0xFF is not valid UTF-8
    writer.end_object();
    // U+FFFD (EF BF BD) stands in for the bad byte; the output is always valid UTF-8.
    CHECK(writer.str() == "{\"s\":\"a\xEF\xBF\xBD" "b\"}");
}

TEST_CASE("a truncated multibyte sequence is replaced") {
    json_writer writer;
    writer.begin_object();
    writer.field_string("s", std::string("\xE2\x82", 2)); // start of a 3-byte sequence, cut short
    writer.end_object();
    CHECK(writer.str() == "{\"s\":\"\xEF\xBF\xBD\xEF\xBF\xBD\"}");
}

TEST_CASE("a well-formed document reports ok") {
    json_writer writer;
    writer.begin_object();
    writer.field_int("n", 1);
    writer.key("a");
    writer.begin_array();
    writer.value_int(2);
    writer.end_array();
    writer.end_object();
    CHECK(writer.ok());
}

TEST_CASE("an untouched writer is not a complete JSON document") {
    json_writer writer;
    CHECK_FALSE(writer.ok());
}

TEST_CASE("the output cap makes an over-long document not ok") {
    json_writer writer(16); // a tiny ceiling
    writer.begin_object();
    writer.field_string("k", "a value long enough to exceed sixteen bytes");
    writer.end_object();
    CHECK_FALSE(writer.ok());
}

TEST_CASE("the output cap also bounds buffered bytes") {
    const std::size_t cap = 16;
    json_writer writer(cap);
    writer.value_string(std::string(1024 * 1024, 'x'));
    CHECK_FALSE(writer.ok());
    CHECK(writer.str().size() <= cap);
}

TEST_CASE("a document exactly at the output cap is accepted") {
    json_writer writer(4);
    writer.value_string("ab"); // "ab" is exactly four encoded bytes.
    CHECK(writer.str().size() == 4);
    CHECK(writer.ok());
}

TEST_CASE("misuse leaves the writer not ok") {
    SUBCASE("a mismatched close") {
        json_writer writer;
        writer.begin_object();
        writer.end_array(); // closing an object as an array
        CHECK_FALSE(writer.ok());
    }
    SUBCASE("a key inside an array") {
        json_writer writer;
        writer.begin_array();
        writer.key("nope");
        CHECK_FALSE(writer.ok());
    }
    SUBCASE("a value with no key inside an object") {
        json_writer writer;
        writer.begin_object();
        writer.value_int(5);
        CHECK_FALSE(writer.ok());
    }
    SUBCASE("a dangling key") {
        json_writer writer;
        writer.begin_object();
        writer.key("k");
        writer.end_object(); // no value for k
        CHECK_FALSE(writer.ok());
    }
    SUBCASE("an unclosed container") {
        json_writer writer;
        writer.begin_object();
        CHECK_FALSE(writer.ok());
    }
    SUBCASE("a second top-level value") {
        json_writer writer;
        writer.value_int(1);
        writer.value_int(2);
        CHECK_FALSE(writer.ok());
    }
}
