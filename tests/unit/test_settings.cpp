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
    opts.EncryptionKey = "aabbccdd";
    opts.OverrideCountryCode = "US";
    opts.OverrideLocaleCode = "en";
    opts.bIsServer = EOS_TRUE;
    opts.Flags = 0x3;
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

TEST_CASE("identity is deterministic for a given username and product") {
    sdk_settings a;
    sdk_settings b;
    CHECK(a.product_user_id() == b.product_user_id());

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
    const std::string before = s.product_user_id();

    EOS_Platform_Options opts = make_options();
    s.apply_platform_options(&opts);

    CHECK(s.product_id() == "prod-123");
    CHECK(s.sandbox_id() == "sandbox-9");
    CHECK(s.deployment_id() == "deploy-7");
    CHECK(s.client_id() == "client-abc");
    CHECK(s.encryption_key() == "aabbccdd");
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
    opts.ProductId = "only-product";
    // Every other string, including ClientCredentials.ClientId, stays null.
    s.apply_platform_options(&opts);
    CHECK(s.product_id() == "only-product");
    CHECK(s.sandbox_id().empty());
    CHECK(s.client_id().empty());
    CHECK(s.encryption_key().empty());
}
