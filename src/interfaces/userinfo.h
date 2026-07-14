#ifndef EOSR_INTERFACES_USERINFO_H
#define EOSR_INTERFACES_USERINFO_H

#include <map>
#include <string>

#include "eos_common.h"
#include "eos_userinfo_types.h"

#include "common/types.h"
#include "core/frame_result.h"
#include "core/i_run_callback.h"
#include "core/i_run_network.h"

namespace eosr {

class sdk_settings;
class callback_manager;
class message_router;
class sdk_connect;
struct net_envelope;

// The UserInfo interface: display names and account details for the players you meet.
//
// The only account details that exist on a LAN are the ones a peer announces about itself, and it
// announces exactly one: its display name. So this is a cache keyed by the same key-derived Epic
// account id the rest of the SDK trusts, holding the name that arrived over the authenticated
// roster. Our own entry is seeded from the local settings, with the country and language the game
// configured. There is no service to query, so a query resolves from that cache: a name we hold
// succeeds, and one we do not is NotFound rather than a wait that never ends.
//
// The parts that describe a player's *other* platforms -- their Steam or console identities -- have
// no source here, so the external-account list is always empty. The best display name is simply the
// Epic one, because it is the only one there is.
// Spec: EOSSDK_UserInfo (wiki/internals/userinfo.md)
class sdk_userinfo : public i_run_callback, public i_run_network {
public:
    sdk_userinfo(sdk_settings& settings, callback_manager& callbacks, message_router& network,
                 sdk_connect& connect);
    ~sdk_userinfo();

    sdk_userinfo(const sdk_userinfo&) = delete;
    sdk_userinfo& operator=(const sdk_userinfo&) = delete;

    void emu_init();
    void emu_deinit();

    // --- Flat API surface (called by the trampolines in flat/eos_userinfo_flat.cpp) ---

    void query_user_info(const EOS_UserInfo_QueryUserInfoOptions* options, void* client_data,
                         EOS_UserInfo_OnQueryUserInfoCallback delegate);
    void query_user_info_by_display_name(
        const EOS_UserInfo_QueryUserInfoByDisplayNameOptions* options, void* client_data,
        EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback delegate);
    void query_user_info_by_external_account(
        const EOS_UserInfo_QueryUserInfoByExternalAccountOptions* options, void* client_data,
        EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback delegate);

    EOS_EResult copy_user_info(const EOS_UserInfo_CopyUserInfoOptions* options,
                               EOS_UserInfo** out) const;

    u32 get_external_user_info_count(const EOS_UserInfo_GetExternalUserInfoCountOptions* options) const;
    EOS_EResult copy_external_user_info_by_index(
        const EOS_UserInfo_CopyExternalUserInfoByIndexOptions* options,
        EOS_UserInfo_ExternalUserInfo** out) const;
    EOS_EResult copy_external_user_info_by_account_type(
        const EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions* options,
        EOS_UserInfo_ExternalUserInfo** out) const;
    EOS_EResult copy_external_user_info_by_account_id(
        const EOS_UserInfo_CopyExternalUserInfoByAccountIdOptions* options,
        EOS_UserInfo_ExternalUserInfo** out) const;

    EOS_EResult copy_best_display_name(const EOS_UserInfo_CopyBestDisplayNameOptions* options,
                                       EOS_UserInfo_BestDisplayName** out) const;
    EOS_EResult copy_best_display_name_with_platform(
        const EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions* options,
        EOS_UserInfo_BestDisplayName** out) const;
    EOS_OnlinePlatformType get_local_platform_type(
        const EOS_UserInfo_GetLocalPlatformTypeOptions* options) const;

    // i_run_callback
    bool cb_run_frame();
    bool run_callbacks(frame_result& result);
    void free_callback(frame_result& result);

    // i_run_network
    bool on_network_message(const net_envelope& message);

private:
    // The account details we hold for one Epic account id. Only display_name is ever known for a
    // remote peer; the rest are ours alone, from settings.
    struct user_record {
        std::string display_name;
        std::string country;
        std::string preferred_language;
        std::string nickname;
    };

    bool is_local_user(EOS_EpicAccountId id) const;
    // Resolve a target Epic account id to its record. Returns false if we hold nothing for it.
    bool resolve(const std::string& epic_id, user_record& out) const;
    // The Epic account id that announced `display_name`, or empty if none has.
    std::string epic_for_display_name(const std::string& display_name) const;
    void deliver_query(void* client_data, completion_delegate delegate, EOS_EResult code,
                       const std::string& local, const std::string& target);

    sdk_settings& settings_;
    callback_manager& callbacks_;
    message_router& network_;
    sdk_connect& connect_;

    // Meshed peers: their key-derived Epic account id -> the product user id we reach them by. The
    // name itself lives on the Connect roster, keyed by that product user id, and we resolve through
    // it so a peer's name is always the authenticated one everyone else sees.
    std::map<std::string, std::string> epic_to_peer_;
    bool registered_;
};

// Copy-out stores. Each Copy* hands the game a struct whose strings we own; the matching Release
// erases it. A struct we did not mint, or already released, finds nothing and is a safe no-op.
void release_user_info(EOS_UserInfo* info);
void release_external_user_info(EOS_UserInfo_ExternalUserInfo* info);
void release_best_display_name(EOS_UserInfo_BestDisplayName* name);

} // namespace eosr

#endif
