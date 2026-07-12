#ifndef EOSR_CORE_SETTINGS_H
#define EOSR_CORE_SETTINGS_H

#include <string>

#include "eos_types.h"

#include "common/types.h"

namespace eosr {

// The per-platform configuration and local identity. The application-supplied half comes
// from EOS_Platform_Options (product/sandbox/deployment ids, encryption key, flags); the
// identity half is derived deterministically from the username so the same user always
// resolves to the same ids across runs and peers. Reading the on-disk eos_reimagined.json
// is layered on with the Connect interface, where the username actually drives login.
// Spec: Settings + username->id derivation (docs/architecture.md), EOSSDK_Platform option members (docs/client.md)
class sdk_settings {
public:
    sdk_settings();

    // Copy the application-supplied option strings and re-derive the local identity. Ignores
    // a null pointer; treats any null option string as empty.
    void apply_platform_options(const EOS_Platform_Options* options);

    void set_username(const std::string& username);

    const std::string& username() const { return username_; }
    // 32 lowercase hex characters, stable for a given (username, product_id).
    const std::string& epic_account_id() const { return epic_account_id_; }
    const std::string& product_user_id() const { return product_user_id_; }

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
    void derive_identity();
    void clear_platform_options();

    std::string username_;
    std::string epic_account_id_;
    std::string product_user_id_;

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
