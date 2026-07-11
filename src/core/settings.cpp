#include "core/settings.h"

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

// A stable 32-hex-character id from the username, the product, and a per-kind discriminator.
// The two halves hash distinct inputs so the epic-account and product-user ids never collide.
std::string derive_id(const std::string& username, const std::string& product_id, const char* kind) {
    const std::string base = username + "\x1f" + product_id + "\x1f" + kind;
    return to_hex16(fnv1a_64(base)) + to_hex16(fnv1a_64(base + "\x1f" "tail"));
}

} // namespace

sdk_settings::sdk_settings()
    : username_(default_username),
      is_server_(false),
      flags_(0),
      tick_budget_ms_(0) {
    derive_identity();
}

void sdk_settings::apply_platform_options(const EOS_Platform_Options* options) {
    if (options == 0) {
        return;
    }
    assign_or_empty(product_id_, options->ProductId);
    assign_or_empty(sandbox_id_, options->SandboxId);
    assign_or_empty(deployment_id_, options->DeploymentId);
    assign_or_empty(client_id_, options->ClientCredentials.ClientId);
    assign_or_empty(encryption_key_, options->EncryptionKey);
    assign_or_empty(override_country_, options->OverrideCountryCode);
    assign_or_empty(override_locale_, options->OverrideLocaleCode);
    is_server_ = (options->bIsServer == EOS_TRUE);
    flags_ = options->Flags;
    tick_budget_ms_ = options->TickBudgetInMilliseconds;
    derive_identity();
}

void sdk_settings::set_username(const std::string& username) {
    username_ = username;
    derive_identity();
}

void sdk_settings::derive_identity() {
    epic_account_id_ = derive_id(username_, product_id_, "eaid");
    product_user_id_ = derive_id(username_, product_id_, "puid");
}

} // namespace eosr
