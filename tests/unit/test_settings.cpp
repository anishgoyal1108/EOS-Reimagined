#include "doctest.h"

#include <string>

#include "eos_types.h"

#include "common/ids.h"
#include "core/settings.h"

using namespace eosr;

namespace {

EOS_Platform_Options make_options() {
    EOS_Platform_Options opts = {};
    opts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    opts.ProductId = "prod-123";
    opts.SandboxId = "sandbox-9";
    opts.DeploymentId = "deploy-7";
    opts.ClientCredentials.ClientId = "client-abc";
    opts.ClientCredentials.ClientSecret = "secret-xyz";
    opts.EncryptionKey = "aabbccdd";
    opts.OverrideCountryCode = "US";
    opts.OverrideLocaleCode = "en";
    opts.bIsServer = EOS_TRUE;
    opts.Flags = 0x3;
    opts.CacheDirectory = "/tmp/eos-cache";
    opts.TickBudgetInMilliseconds = 5;
    return opts;
}

} // namespace

TEST_CASE("fresh settings carry a valid, stable default identity") {
    sdk_settings s;
    CHECK(s.username() == "DefaultName");
    CHECK(s.epic_account_id().size() == 32);
    CHECK(s.product_user_id().size() == 32);
    CHECK(id_string_is_valid(s.epic_account_id()));
    CHECK(id_string_is_valid(s.product_user_id()));
    // The two identities are derived from distinct seeds and never collide.
    CHECK(s.epic_account_id() != s.product_user_id());
}

// Two copies of a game with nobody configured must not answer to the same id: each would see the
// other's advertisement as its own and the two would never discover each other.
TEST_CASE("two unconfigured instances get different identities") {
    sdk_settings a;
    sdk_settings b;
    CHECK(a.product_user_id() != b.product_user_id());
    CHECK(a.epic_account_id() != b.epic_account_id());
    CHECK(id_string_is_valid(a.product_user_id()));
    CHECK(id_string_is_valid(b.product_user_id()));

    // Applying options must not disturb an identity we already minted.
    const std::string before = a.product_user_id();
    EOS_Platform_Options opts = make_options();
    a.apply_platform_options(&opts);
    CHECK(a.product_user_id() == before);
}

TEST_CASE("identity is deterministic for a given username and product") {
    sdk_settings a;
    sdk_settings b;

    a.set_username("InfernusHawk");
    b.set_username("InfernusHawk");
    CHECK(a.product_user_id() == b.product_user_id());
    CHECK(a.epic_account_id() == b.epic_account_id());
}

TEST_CASE("a different username yields a different identity") {
    sdk_settings a;
    a.set_username("PlayerOne");
    sdk_settings b;
    b.set_username("PlayerTwo");
    CHECK(a.product_user_id() != b.product_user_id());
    CHECK(a.epic_account_id() != b.epic_account_id());
}

TEST_CASE("platform options are captured and re-derive the identity") {
    sdk_settings s;
    // A configured user is what makes the identity derived rather than minted, so this is the case
    // where the product takes part in the seed.
    s.set_username("InfernusHawk");
    const std::string before = s.product_user_id();

    EOS_Platform_Options opts = make_options();
    s.apply_platform_options(&opts);

    CHECK(s.product_id() == "prod-123");
    CHECK(s.sandbox_id() == "sandbox-9");
    CHECK(s.deployment_id() == "deploy-7");
    CHECK(s.client_id() == "client-abc");
    CHECK(s.client_secret() == "secret-xyz");
    CHECK(s.encryption_key() == "aabbccdd");
    CHECK(s.cache_directory() == "/tmp/eos-cache");
    CHECK(s.override_country() == "US");
    CHECK(s.override_locale() == "en");
    CHECK(s.is_server());
    CHECK(s.flags() == 0x3u);
    CHECK(s.tick_budget_ms() == 5u);
    // product_id feeds the identity seed, so the id changed but stays valid.
    CHECK(s.product_user_id() != before);
    CHECK(id_string_is_valid(s.product_user_id()));
}

TEST_CASE("null options are ignored and leave defaults intact") {
    sdk_settings s;
    const std::string id = s.product_user_id();
    s.apply_platform_options(0);
    CHECK(s.product_user_id() == id);
    CHECK(s.product_id().empty());
}

TEST_CASE("a null option string is treated as empty, not dereferenced") {
    sdk_settings s;
    EOS_Platform_Options opts = {};
    opts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    opts.ProductId = "only-product";
    // Every other string, including ClientCredentials.ClientId, stays null.
    s.apply_platform_options(&opts);
    CHECK(s.product_id() == "only-product");
    CHECK(s.sandbox_id().empty());
    CHECK(s.client_id().empty());
    CHECK(s.encryption_key().empty());
}

TEST_CASE("applying new platform options replaces all previous values") {
    sdk_settings settings;
    settings.set_username("InfernusHawk");
    EOS_Platform_Options first_options = make_options();
    settings.apply_platform_options(&first_options);
    const std::string first_id = settings.product_user_id();

    EOS_Platform_Options second_options = {};
    second_options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    second_options.ProductId = "replacement-product";
    settings.apply_platform_options(&second_options);

    CHECK(settings.product_id() == "replacement-product");
    CHECK(settings.sandbox_id().empty());
    CHECK(settings.deployment_id().empty());
    CHECK(settings.client_id().empty());
    CHECK(settings.encryption_key().empty());
    CHECK(settings.override_country().empty());
    CHECK(settings.override_locale().empty());
    CHECK_FALSE(settings.is_server());
    CHECK(settings.flags() == 0);
    CHECK(settings.tick_budget_ms() == 0);
    CHECK(settings.product_user_id() != first_id);
}

// A game built against an older SDK passes a shorter EOS_Platform_Options, so a field added in
// a later version is absent from its struct. We must read a field only when the caller's
// ApiVersion guarantees it exists; reading past the caller's struct would be undefined. We test
// the behavior with a fully-populated struct (safe to read) and confirm the older versions
// still decline to capture the newer fields.
TEST_CASE("older option versions do not capture newer fields") {
    SUBCASE("version 1 captures only the base fields") {
        EOS_Platform_Options opts = make_options();
        opts.ApiVersion = 1;
        sdk_settings s;
        s.apply_platform_options(&opts);

        CHECK(s.product_id() == "prod-123");
        CHECK(s.client_id() == "client-abc");
        CHECK(s.client_secret() == "secret-xyz");
        CHECK(s.is_server());
        // Added at version 5 and later, so absent from a version-1 struct.
        CHECK(s.deployment_id().empty());
        CHECK(s.encryption_key().empty());
        CHECK(s.flags() == 0);
        CHECK(s.cache_directory().empty());
        CHECK(s.tick_budget_ms() == 0);
    }

    SUBCASE("version 5 adds deployment, flags, and encryption but not the tick budget") {
        EOS_Platform_Options opts = make_options();
        opts.ApiVersion = 5;
        sdk_settings s;
        s.apply_platform_options(&opts);

        CHECK(s.deployment_id() == "deploy-7");
        CHECK(s.encryption_key() == "aabbccdd");
        CHECK(s.flags() == 0x3u);
        // Cache (v6) and tick budget (v7) are still newer than version 5.
        CHECK(s.cache_directory().empty());
        CHECK(s.tick_budget_ms() == 0);
    }

    SUBCASE("the latest version captures the cache directory and tick budget") {
        EOS_Platform_Options opts = make_options();
        sdk_settings s;
        s.apply_platform_options(&opts);
        CHECK(s.cache_directory() == "/tmp/eos-cache");
        CHECK(s.tick_budget_ms() == 5u);
    }
}
