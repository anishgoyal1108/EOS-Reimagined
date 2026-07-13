#include "core/settings.h"

#include <chrono>
#include <cstdint>

#include "platform/rng.h"

namespace eosr {

namespace {

// Placeholder identity for an unconfigured install; the game or config overrides it.
const char* default_username = "DefaultName";

void assign_or_empty(std::string& out, const char* value) {
    out = (value != 0) ? value : "";
}

u64 fnv1a_64(const std::string& data) {
    u64 hash = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < data.size(); i++) {
        hash ^= static_cast<u8>(data[i]);
        hash *= 0x00000100000001b3ull;
    }
    return hash;
}

std::string to_hex16(u64 value) {
    static const char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; i--) {
        out[static_cast<std::size_t>(i)] = digits[value & 0xf];
        value >>= 4;
    }
    return out;
}

// A random 32-hex-character id, for an instance with no configured user to identify itself by.
// If the system random source is unavailable we still must not collide with another instance, so
// we fall back on this instance's own address and a clock reading, which differ between processes.
std::string random_id() {
    const std::size_t id_bytes = 16; // 16 bytes render as the 32 hex characters an id is
    u8 bytes[id_bytes];
    if (!platform::random_bytes(bytes, id_bytes)) {
        const u64 here = static_cast<u64>(reinterpret_cast<std::uintptr_t>(&bytes[0]));
        const u64 now = static_cast<u64>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const u64 mixed[2] = {fnv1a_64(to_hex16(here)), fnv1a_64(to_hex16(now ^ here))};
        return to_hex16(mixed[0]) + to_hex16(mixed[1]);
    }
    u64 halves[2] = {0, 0};
    for (std::size_t i = 0; i < id_bytes; i++) {
        halves[i / 8] = (halves[i / 8] << 8) | bytes[i];
    }
    // An all-zero id is the null sentinel, so never hand one back.
    halves[0] |= 1;
    return to_hex16(halves[0]) + to_hex16(halves[1]);
}

// A stable 32-hex-character id from the username, the product, and a per-kind discriminator.
// The two halves hash distinct inputs so the epic-account and product-user ids never collide.
std::string derive_id(const std::string& username, const std::string& product_id, const char* kind) {
    const std::string base = username + "\x1f" + product_id + "\x1f" + kind;
    return to_hex16(fnv1a_64(base)) + to_hex16(fnv1a_64(base + "\x1f" "tail"));
}

} // namespace

sdk_settings::sdk_settings()
    : username_(default_username),
      minted_identity_(false),
      is_server_(false),
      flags_(0),
      tick_budget_ms_(0) {
    derive_identity();
}

void sdk_settings::apply_platform_options(const EOS_Platform_Options* options) {
    // Start from defaults so re-applying options never leaves a stale field behind.
    clear_platform_options();
    if (options == 0) {
        derive_identity();
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
        derive_identity();
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
    derive_identity();
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
    derive_identity();
}

void sdk_settings::derive_identity() {
    // With a configured user we derive the identity from the name, so the same person is the same
    // player on every machine and every run. With no user configured we have nothing to derive
    // from, and deriving from the placeholder would hand every copy of the game the same id: two
    // instances would then see each other's advertisement as their own and never meet. So we mint
    // a random identity instead, once, and keep it for the life of this instance.
    if (username_ == default_username) {
        if (!minted_identity_) {
            epic_account_id_ = random_id();
            product_user_id_ = random_id();
            minted_identity_ = true;
        }
        return;
    }
    minted_identity_ = false;
    epic_account_id_ = derive_id(username_, product_id_, "eaid");
    product_user_id_ = derive_id(username_, product_id_, "puid");
}

} // namespace eosr
