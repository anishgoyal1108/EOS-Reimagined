#include "doctest.h"

#include <cstdlib>
#include <string>

#include "eos_init.h"

#include "common/log.h"
#include "core/client.h"

using namespace eosr;

namespace {

EOS_InitializeOptions good_options() {
    EOS_InitializeOptions options = {};
    options.ApiVersion = EOS_INITIALIZE_API_LATEST;
    options.ProductName = "CrabTest";
    options.ProductVersion = "1.0.0";
    return options;
}

EOS_EResult initialize_once(const EOS_InitializeOptions& options) {
    sdk_client client;
    return client.initialize(&options);
}

void* EOS_MEMORY_CALL test_allocate(std::size_t size, std::size_t) {
    return std::malloc(size);
}

void* EOS_MEMORY_CALL test_reallocate(void* pointer, std::size_t size, std::size_t) {
    return std::realloc(pointer, size);
}

void EOS_MEMORY_CALL test_release(void* pointer) {
    std::free(pointer);
}

sdk_client* g_reentrant_client = 0;
bool g_saw_initialized_state = false;
bool g_saw_shutdown_state = false;

void EOS_CALL reentrant_log_callback(const EOS_LogMessage*) {
    const bool initialized = g_reentrant_client->is_initialized();
    g_saw_initialized_state = g_saw_initialized_state || initialized;
    g_saw_shutdown_state = g_saw_shutdown_state || !initialized;
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

TEST_CASE("required initialization strings are validated") {
    sdk_client client;
    CHECK(client.initialize(0) == EOS_EResult::EOS_InvalidParameters);

    EOS_InitializeOptions options = good_options();
    options.ProductName = 0;
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
    options = good_options();
    options.ProductName = "";
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
    options = good_options();
    options.ProductVersion = 0;
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
    options = good_options();
    options.ProductVersion = "";
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
    CHECK_FALSE(client.is_initialized());
}

TEST_CASE("initialization rejects unsupported API versions") {
    EOS_InitializeOptions options = good_options();
    options.ApiVersion = 0;
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
    options.ApiVersion = EOS_INITIALIZE_API_LATEST + 1;
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
}

TEST_CASE("initialization enforces string length and character limits") {
    EOS_InitializeOptions options = good_options();
    const std::string long_name(EOS_INITIALIZEOPTIONS_PRODUCTNAME_MAX_LENGTH + 1, 'a');
    options.ProductName = long_name.c_str();
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);

    options = good_options();
    const std::string long_version(EOS_INITIALIZEOPTIONS_PRODUCTVERSION_MAX_LENGTH + 1, '1');
    options.ProductVersion = long_version.c_str();
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);

    options = good_options();
    options.ProductName = "bad\nname";
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);
}

TEST_CASE("custom memory callbacks must be supplied as a complete set") {
    EOS_InitializeOptions options = good_options();
    options.AllocateMemoryFunction = test_allocate;
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);

    options.ReallocateMemoryFunction = test_reallocate;
    CHECK(initialize_once(options) == EOS_EResult::EOS_InvalidParameters);

    options.ReleaseMemoryFunction = test_release;
    sdk_client client;
    CHECK(client.initialize(&options) == EOS_EResult::EOS_Success);
    CHECK(client.shutdown() == EOS_EResult::EOS_Success);
}

TEST_CASE("shutdown distinguishes an already shut down client") {
    sdk_client c;
    EOS_InitializeOptions o = good_options();
    REQUIRE(c.initialize(&o) == EOS_EResult::EOS_Success);
    REQUIRE(c.shutdown() == EOS_EResult::EOS_Success);
    CHECK(c.shutdown() == EOS_EResult::EOS_UnexpectedError);
}

TEST_CASE("lifecycle log callbacks can safely re-enter the client") {
    sdk_client c;
    EOS_InitializeOptions o = good_options();
    g_reentrant_client = &c;
    g_saw_initialized_state = false;
    g_saw_shutdown_state = false;
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Verbose);
    logger::instance().set_callback(reentrant_log_callback);

    REQUIRE(c.initialize(&o) == EOS_EResult::EOS_Success);
    REQUIRE(c.shutdown() == EOS_EResult::EOS_Success);
    CHECK(g_saw_initialized_state);
    CHECK(g_saw_shutdown_state);

    logger::instance().set_callback(0);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Warning);
    g_reentrant_client = 0;
}
