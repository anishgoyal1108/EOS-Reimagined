#ifndef EOSR_CORE_SETTINGS_H
#define EOSR_CORE_SETTINGS_H

#include <string>

#include "eos_types.h"

#include "common/types.h"
#include "core/identity.h"

namespace eosr {

// The per-platform configuration and local identity. The application-supplied half comes from
// EOS_Platform_Options (product/sandbox/deployment ids, encryption key, flags); the identity half
// comes from the profile key, which is what a peer proves it holds during the handshake. A settings
// object starts with an ephemeral key so it always has a usable identity, and the platform upgrades
// it to the persistent profile on disk when it is created.
//
// The username is a display name and nothing more. It used to seed the identity, which meant anyone
// who knew a name could answer to that player's id; the key replaced it.
// Spec: Settings (wiki/internals/architecture.md), EOSSDK_Platform option members (wiki/internals/client.md),
// self-certifying identity (wiki/internals/adr/0001)
class sdk_settings {
public:
    sdk_settings();

    // Copy the application-supplied option strings and fold the title into the product user id.
    // Ignores a null pointer; treats any null option string as empty.
    void apply_platform_options(const EOS_Platform_Options* options);

    // Take the persistent profile under `directory`, replacing the ephemeral key. False when there
    // is no profile to be had, in which case the ephemeral key stands and only persistence is lost.
    bool load_identity(const std::string& directory);

    void set_username(const std::string& username);

    // The game may override the country and locale it is treated as being in, at any time. Neither
    // reaches anything -- there is no service to send them to -- but the game can set one and read
    // it back, and a game that cannot is a game that thinks the SDK is broken.
    void set_override_country(const std::string& country) { override_country_ = country; }
    void set_override_locale(const std::string& locale) { override_locale_ = locale; }

    const std::string& username() const { return username_; }
    // 32 lowercase hex characters, derived from the profile key (and, for the product user id, the
    // title), so the same profile is the same player on every run.
    const std::string& epic_account_id() const { return identity_.epic_account_id(); }
    const std::string& product_user_id() const { return identity_.product_user_id(); }

    // The profile itself, for the handshake: it needs the secret key to prove this identity.
    identity& profile() { return identity_; }
    const identity& profile() const { return identity_; }

    const std::string& product_id() const { return product_id_; }
    const std::string& sandbox_id() const { return sandbox_id_; }
    const std::string& deployment_id() const { return deployment_id_; }
    const std::string& client_id() const { return client_id_; }
    const std::string& client_secret() const { return client_secret_; }
    const std::string& encryption_key() const { return encryption_key_; }
    const std::string& cache_directory() const { return cache_directory_; }
    const std::string& override_country() const { return override_country_; }
    const std::string& override_locale() const { return override_locale_; }
    bool is_server() const { return is_server_; }
    u64 flags() const { return flags_; }
    u32 tick_budget_ms() const { return tick_budget_ms_; }

private:
    void clear_platform_options();

    identity identity_;
    std::string username_;

    std::string product_id_;
    std::string sandbox_id_;
    std::string deployment_id_;
    std::string client_id_;
    std::string client_secret_;
    std::string encryption_key_;
    std::string cache_directory_;
    std::string override_country_;
    std::string override_locale_;
    bool is_server_;
    u64 flags_;
    u32 tick_budget_ms_;
};

} // namespace eosr

#endif
