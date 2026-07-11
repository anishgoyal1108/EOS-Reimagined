#include "doctest.h"

#include <string>

#include "common/ids.h"

using namespace eosr;

TEST_CASE("interning returns the same handle for the same string") {
    const std::string s = "0123456789abcdef0123456789abcdef";
    EOS_EpicAccountId a = id_registry::instance().get_epic_account_id(s);
    EOS_EpicAccountId b = id_registry::instance().get_epic_account_id(s);
    CHECK(a == b);
    CHECK(a != nullptr);
    CHECK(a->id_str == s);
    CHECK(a->valid == true);
}

TEST_CASE("epic and product id spaces are independent") {
    const std::string s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    EOS_ProductUserId a = id_registry::instance().get_product_user_id(s);
    EOS_ProductUserId b = id_registry::instance().get_product_user_id(s);
    CHECK(a == b);
    CHECK(a->valid == true);
}

TEST_CASE("id validity rules") {
    CHECK(id_string_is_valid("0123456789abcdef0123456789abcdef") == true);
    CHECK(id_string_is_valid("0123456789ABCDEF0123456789ABCDEF") == true);
    CHECK(id_string_is_valid(std::string(id_hex_length, '0')) == false);
    CHECK(id_string_is_valid("tooshort") == false);
    CHECK(id_string_is_valid("0123456789abcdef0123456789abcdeZ") == false);
}
