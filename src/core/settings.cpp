#include "core/settings.h"

#include "common/log.h"

namespace eosr {

namespace {

// Placeholder display name for an unconfigured install; the game or config overrides it.
const char* default_username = "DefaultName";

void assign_or_empty(std::string& out, const char* value) {
    out = (value != 0) ? value : "";
}

} // namespace

sdk_settings::sdk_settings()
    : username_(default_username),
      is_server_(false),
      flags_(0),
      tick_budget_ms_(0) {
    // Start on an ephemeral key so an instance always has an identity of its own, even before the
    // platform hands it a directory to keep one in -- and so two copies of a game never collide on
    // an id and mistake each other's advertisement for their own.
    if (!identity_.generate_ephemeral()) {
        log_error("settings: no system random source; this instance has no identity");
    }
}

bool sdk_settings::load_identity(const std::string& directory) {
    if (!identity_.load_or_create(directory)) {
        return false;
    }
    identity_.bind_product(product_id_, sandbox_id_, deployment_id_);
    return true;
}

void sdk_settings::apply_platform_options(const EOS_Platform_Options* options) {
    // Start from defaults so re-applying options never leaves a stale field behind.
    clear_platform_options();
    if (options == 0) {
        identity_.bind_product(product_id_, sandbox_id_, deployment_id_);
        return;
    }

    // EOS_Platform_Options only ever gains fields, so a field is present exactly when the
    // caller's ApiVersion is at least the version that introduced it. A game built against an
    // older SDK passes a shorter struct, so reading a newer field would run off its end. These
    // cutoffs are the reversed platform-options version cascade.
    const i32 version = options->ApiVersion;
    const i32 version_with_deployment = 5; // + EncryptionKey, country, locale, Flags
    const i32 version_with_cache = 6;      // + CacheDirectory
    const i32 version_with_tick_budget = 7; // + TickBudgetInMilliseconds

    if (version < 1) {
        identity_.bind_product(product_id_, sandbox_id_, deployment_id_);
        return;
    }

    assign_or_empty(product_id_, options->ProductId);
    assign_or_empty(sandbox_id_, options->SandboxId);
    assign_or_empty(client_id_, options->ClientCredentials.ClientId);
    assign_or_empty(client_secret_, options->ClientCredentials.ClientSecret);
    is_server_ = (options->bIsServer == EOS_TRUE);

    if (version >= version_with_deployment) {
        assign_or_empty(encryption_key_, options->EncryptionKey);
        assign_or_empty(override_country_, options->OverrideCountryCode);
        assign_or_empty(override_locale_, options->OverrideLocaleCode);
        assign_or_empty(deployment_id_, options->DeploymentId);
        flags_ = options->Flags;
    }
    if (version >= version_with_cache) {
        assign_or_empty(cache_directory_, options->CacheDirectory);
    }
    if (version >= version_with_tick_budget) {
        tick_budget_ms_ = options->TickBudgetInMilliseconds;
    }

    // The product user id folds in the title, so one profile is a distinct product-user per game,
    // exactly as EOS does. The epic account id is product-independent and does not move.
    identity_.bind_product(product_id_, sandbox_id_, deployment_id_);
}

void sdk_settings::clear_platform_options() {
    product_id_.clear();
    sandbox_id_.clear();
    deployment_id_.clear();
    client_id_.clear();
    client_secret_.clear();
    encryption_key_.clear();
    cache_directory_.clear();
    override_country_.clear();
    override_locale_.clear();
    is_server_ = false;
    flags_ = 0;
    tick_budget_ms_ = 0;
}

void sdk_settings::set_username(const std::string& username) {
    username_ = username;
}

} // namespace eosr
