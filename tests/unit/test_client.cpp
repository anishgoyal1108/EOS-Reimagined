#include "doctest.h"

#include "eos_init.h"

#include "core/client.h"

using namespace eosr;

namespace {

EOS_InitializeOptions good_options() {
    EOS_InitializeOptions o = {};
    o.ApiVersion = EOS_INITIALIZE_API_LATEST;
    o.ProductName = "CrabTest";
    o.ProductVersion = "1.0.0";
    return o;
}

} // namespace

TEST_CASE("initialize then shutdown drives the SDK state") {
    sdk_client c;
    CHECK_FALSE(c.is_initialized());

    EOS_InitializeOptions o = good_options();
    CHECK(c.initialize(&o) == EOS_EResult::EOS_Success);
    CHECK(c.is_initialized());
    CHECK(c.product_name() == "CrabTest");
    CHECK(c.product_version() == "1.0.0");

    CHECK(c.shutdown() == EOS_EResult::EOS_Success);
    CHECK_FALSE(c.is_initialized());
}

TEST_CASE("a second initialize is rejected as already configured") {
    sdk_client c;
    EOS_InitializeOptions o = good_options();
    REQUIRE(c.initialize(&o) == EOS_EResult::EOS_Success);
    CHECK(c.initialize(&o) == EOS_EResult::EOS_AlreadyConfigured);
    CHECK(c.is_initialized());
    c.shutdown();
}

TEST_CASE("shutdown before initialize is rejected as not configured") {
    sdk_client c;
    CHECK(c.shutdown() == EOS_EResult::EOS_NotConfigured);
}

TEST_CASE("null options and an empty product name are rejected") {
    sdk_client c;
    CHECK(c.initialize(0) == EOS_EResult::EOS_InvalidParameters);

    EOS_InitializeOptions o = good_options();
    o.ProductName = 0;
    CHECK(c.initialize(&o) == EOS_EResult::EOS_InvalidParameters);
    o.ProductName = "";
    CHECK(c.initialize(&o) == EOS_EResult::EOS_InvalidParameters);
    CHECK_FALSE(c.is_initialized());
}

TEST_CASE("the SDK can be re-initialized after a clean shutdown") {
    sdk_client c;
    EOS_InitializeOptions o = good_options();
    REQUIRE(c.initialize(&o) == EOS_EResult::EOS_Success);
    REQUIRE(c.shutdown() == EOS_EResult::EOS_Success);
    CHECK(c.initialize(&o) == EOS_EResult::EOS_Success);
    c.shutdown();
}
