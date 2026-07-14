#include "interfaces/userinfo.h"

#include <cstring>
#include <map>
#include <memory>
#include <mutex>

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/connect.h"
#include "net/message_router.h"
#include "net/messages.h"

namespace eosr {

namespace {

const callback_type_id cb_query = 1;
const callback_type_id cb_query_by_name = 2;
const callback_type_id cb_query_by_external = 3;

// Our identity is an Epic account, so the local player's platform is Epic. This is what
// CopyBestDisplayNameWithPlatform is asked about, and the only platform we have an answer for.
const EOS_OnlinePlatformType local_platform_type = EOS_OPT_Epic;

bool version_ok(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

EOS_EpicAccountId epic_from(const std::string& id_str) {
    return id_str.empty() ? 0 : id_registry::instance().get_epic_account_id(id_str);
}

char* duplicate(const std::string& text) {
    char* copy = new char[text.size() + 1];
    std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}

const char* or_null(const std::string& text) {
    return text.empty() ? 0 : text.c_str();
}

// A copied EOS_UserInfo owns its own strings; the game frees it through EOS_UserInfo_Release. We key
// the store by the struct pointer we hand out, exactly as Presence does for EOS_Presence_Info.
struct user_info_holder {
    EOS_UserInfo info;
    std::string country;
    std::string display_name;
    std::string preferred_language;
    std::string nickname;
    std::string display_name_sanitized;
};

struct best_name_holder {
    EOS_UserInfo_BestDisplayName name;
    std::string display_name;
    std::string display_name_sanitized;
    std::string nickname;
};

std::mutex g_userinfo_mutex;
std::map<void*, std::unique_ptr<user_info_holder> > g_user_infos;
std::map<void*, std::unique_ptr<best_name_holder> > g_best_names;

EOS_UserInfo* build_user_info(const std::string& epic, const std::string& display_name,
                              const std::string& country, const std::string& language,
                              const std::string& nickname) {
    std::unique_ptr<user_info_holder> holder(new user_info_holder());
    holder->country = country;
    holder->display_name = display_name;
    holder->preferred_language = language;
    holder->nickname = nickname;
    // We do not sanitize display names, so the sanitized form is the name as it was announced.
    holder->display_name_sanitized = display_name;
    holder->info.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    holder->info.UserId = epic_from(epic);
    holder->info.Country = or_null(holder->country);
    holder->info.DisplayName = or_null(holder->display_name);
    holder->info.PreferredLanguage = or_null(holder->preferred_language);
    holder->info.Nickname = or_null(holder->nickname);
    holder->info.DisplayNameSanitized = or_null(holder->display_name_sanitized);
    EOS_UserInfo* info = &holder->info;
    {
        std::lock_guard<std::mutex> lock(g_userinfo_mutex);
        g_user_infos[info] = std::move(holder);
    }
    return info;
}

EOS_UserInfo_BestDisplayName* build_best_name(const std::string& epic,
                                              const std::string& display_name,
                                              const std::string& nickname,
                                              EOS_OnlinePlatformType platform) {
    std::unique_ptr<best_name_holder> holder(new best_name_holder());
    holder->display_name = display_name;
    holder->display_name_sanitized = display_name;
    holder->nickname = nickname;
    holder->name.ApiVersion = EOS_USERINFO_BESTDISPLAYNAME_API_LATEST;
    holder->name.UserId = epic_from(epic);
    holder->name.DisplayName = or_null(holder->display_name);
    holder->name.DisplayNameSanitized = or_null(holder->display_name_sanitized);
    holder->name.Nickname = or_null(holder->nickname);
    holder->name.PlatformType = platform;
    EOS_UserInfo_BestDisplayName* name = &holder->name;
    {
        std::lock_guard<std::mutex> lock(g_userinfo_mutex);
        g_best_names[name] = std::move(holder);
    }
    return name;
}

} // namespace

void release_user_info(EOS_UserInfo* info) {
    if (info == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_userinfo_mutex);
    g_user_infos.erase(info);
}

// We never mint an external user info -- the list is always empty -- so there is nothing to free.
void release_external_user_info(EOS_UserInfo_ExternalUserInfo* info) {
    (void)info;
}

void release_best_display_name(EOS_UserInfo_BestDisplayName* name) {
    if (name == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_userinfo_mutex);
    g_best_names.erase(name);
}

sdk_userinfo::sdk_userinfo(sdk_settings& settings, callback_manager& callbacks,
                           message_router& network, sdk_connect& connect)
    : settings_(settings), callbacks_(callbacks), network_(network), connect_(connect),
      registered_(false) {
}

sdk_userinfo::~sdk_userinfo() {
    emu_deinit();
}

void sdk_userinfo::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::peer_connected, this);
    network_.register_listener(message_type::peer_disconnected, this);
    registered_ = true;
}

void sdk_userinfo::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::peer_connected, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    epic_to_peer_.clear();
    registered_ = false;
}

bool sdk_userinfo::is_local_user(EOS_EpicAccountId id) const {
    return id != 0 && id->valid && id->id_str == settings_.epic_account_id();
}

bool sdk_userinfo::resolve(const std::string& epic_id, user_record& out) const {
    if (epic_id.empty()) {
        return false;
    }
    if (epic_id == settings_.epic_account_id()) {
        out.display_name = settings_.username();
        out.country = settings_.override_country();
        out.preferred_language = settings_.override_locale();
        out.nickname.clear();
        return true;
    }
    std::map<std::string, std::string>::const_iterator it = epic_to_peer_.find(epic_id);
    if (it == epic_to_peer_.end()) {
        return false;
    }
    out.display_name = connect_.peer_display_name(it->second);
    out.country.clear();
    out.preferred_language.clear();
    out.nickname.clear();
    return true;
}

std::string sdk_userinfo::epic_for_display_name(const std::string& display_name) const {
    if (display_name.empty()) {
        return std::string();
    }
    if (display_name == settings_.username()) {
        return settings_.epic_account_id();
    }
    std::map<std::string, std::string>::const_iterator it = epic_to_peer_.begin();
    for (; it != epic_to_peer_.end(); ++it) {
        if (connect_.peer_display_name(it->second) == display_name) {
            return it->first;
        }
    }
    return std::string();
}

void sdk_userinfo::deliver_query(void* client_data, completion_delegate delegate, EOS_EResult code,
                                 const std::string& local, const std::string& target) {
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_UserInfo_QueryUserInfoCallbackInfo* info =
        static_cast<EOS_UserInfo_QueryUserInfoCallbackInfo*>(result->create_callback(
            cb_query, sizeof(EOS_UserInfo_QueryUserInfoCallbackInfo), delegate));
    info->ResultCode = code;
    info->ClientData = client_data;
    info->LocalUserId = epic_from(local);
    info->TargetUserId = epic_from(target);
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

// A query has nothing to fetch -- what we know, we already know -- so it settles on the next tick:
// a name we hold succeeds, one we do not is NotFound.
void sdk_userinfo::query_user_info(const EOS_UserInfo_QueryUserInfoOptions* options,
                                   void* client_data,
                                   EOS_UserInfo_OnQueryUserInfoCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const completion_delegate cb = reinterpret_cast<completion_delegate>(delegate);
    if (options == 0 || !version_ok(options->ApiVersion, EOS_USERINFO_QUERYUSERINFO_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0 || !options->LocalUserId->valid ||
        !options->TargetUserId->valid) {
        deliver_query(client_data, cb, EOS_EResult::EOS_InvalidParameters, std::string(),
                      std::string());
        return;
    }
    if (!is_local_user(options->LocalUserId)) {
        deliver_query(client_data, cb, EOS_EResult::EOS_InvalidUser, options->LocalUserId->id_str,
                      options->TargetUserId->id_str);
        return;
    }
    user_record record;
    const bool known = resolve(options->TargetUserId->id_str, record);
    deliver_query(client_data, cb, known ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NotFound,
                  options->LocalUserId->id_str, options->TargetUserId->id_str);
}

void sdk_userinfo::query_user_info_by_display_name(
    const EOS_UserInfo_QueryUserInfoByDisplayNameOptions* options, void* client_data,
    EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const std::string name = (options != 0 && options->DisplayName != 0) ? options->DisplayName
                                                                         : std::string();
    EOS_EResult code = EOS_EResult::EOS_InvalidParameters;
    std::string local;
    std::string target;
    if (options != 0 &&
        version_ok(options->ApiVersion, EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST) &&
        options->LocalUserId != 0 && options->LocalUserId->valid && options->DisplayName != 0) {
        local = options->LocalUserId->id_str;
        if (!is_local_user(options->LocalUserId)) {
            code = EOS_EResult::EOS_InvalidUser;
        } else {
            target = epic_for_display_name(name);
            code = target.empty() ? EOS_EResult::EOS_NotFound : EOS_EResult::EOS_Success;
        }
    }

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo* info =
        static_cast<EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo*>(result->create_callback(
            cb_query_by_name, sizeof(EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = code;
    info->ClientData = client_data;
    info->LocalUserId = epic_from(local);
    info->TargetUserId = epic_from(target);
    // The header says this pointer is valid only during the callback, so we own the copy and free it
    // in free_callback once the delegate has run.
    info->DisplayName = duplicate(name);
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

// There are no external accounts on the mesh, so there is no account to resolve to an Epic id.
void sdk_userinfo::query_user_info_by_external_account(
    const EOS_UserInfo_QueryUserInfoByExternalAccountOptions* options, void* client_data,
    EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback delegate) {
    if (delegate == 0) {
        return;
    }
    const std::string external =
        (options != 0 && options->ExternalAccountId != 0) ? options->ExternalAccountId
                                                          : std::string();
    EOS_EResult code = EOS_EResult::EOS_InvalidParameters;
    std::string local;
    EOS_EExternalAccountType account_type = EOS_EExternalAccountType::EOS_EAT_EPIC;
    if (options != 0 &&
        version_ok(options->ApiVersion, EOS_USERINFO_QUERYUSERINFOBYEXTERNALACCOUNT_API_LATEST) &&
        options->LocalUserId != 0 && options->LocalUserId->valid && options->ExternalAccountId != 0) {
        local = options->LocalUserId->id_str;
        account_type = options->AccountType;
        code = is_local_user(options->LocalUserId) ? EOS_EResult::EOS_NotFound
                                                   : EOS_EResult::EOS_InvalidUser;
    }

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo* info =
        static_cast<EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo*>(result->create_callback(
            cb_query_by_external, sizeof(EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = code;
    info->ClientData = client_data;
    info->LocalUserId = epic_from(local);
    info->TargetUserId = 0;
    info->AccountType = account_type;
    info->ExternalAccountId = duplicate(external);
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

EOS_EResult sdk_userinfo::copy_user_info(const EOS_UserInfo_CopyUserInfoOptions* options,
                                         EOS_UserInfo** out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // A rejected ApiVersion is an ABI mismatch, not a malformed request: the header names
    // IncompatibleVersion for it, and a game uses that to pick an older call shape.
    if (!version_ok(options->ApiVersion, EOS_USERINFO_COPYUSERINFO_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    if (options->LocalUserId == 0 || options->TargetUserId == 0 || !options->TargetUserId->valid) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    user_record record;
    if (!resolve(options->TargetUserId->id_str, record)) {
        return EOS_EResult::EOS_NotFound;
    }
    *out = build_user_info(options->TargetUserId->id_str, record.display_name, record.country,
                           record.preferred_language, record.nickname);
    return EOS_EResult::EOS_Success;
}

u32 sdk_userinfo::get_external_user_info_count(
    const EOS_UserInfo_GetExternalUserInfoCountOptions* options) const {
    (void)options;
    return 0;
}

EOS_EResult sdk_userinfo::copy_external_user_info_by_index(
    const EOS_UserInfo_CopyExternalUserInfoByIndexOptions* options,
    EOS_UserInfo_ExternalUserInfo** out) const {
    (void)options;
    // A null out-pointer is a malformed call, not an empty-cache miss.
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_userinfo::copy_external_user_info_by_account_type(
    const EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions* options,
    EOS_UserInfo_ExternalUserInfo** out) const {
    (void)options;
    // A null out-pointer is a malformed call, not an empty-cache miss.
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_userinfo::copy_external_user_info_by_account_id(
    const EOS_UserInfo_CopyExternalUserInfoByAccountIdOptions* options,
    EOS_UserInfo_ExternalUserInfo** out) const {
    (void)options;
    // A null out-pointer is a malformed call, not an empty-cache miss.
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_userinfo::copy_best_display_name(
    const EOS_UserInfo_CopyBestDisplayNameOptions* options,
    EOS_UserInfo_BestDisplayName** out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_USERINFO_COPYBESTDISPLAYNAME_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    if (options->TargetUserId == 0 || !options->TargetUserId->valid) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    user_record record;
    if (!resolve(options->TargetUserId->id_str, record) || record.display_name.empty()) {
        return EOS_EResult::EOS_NotFound;
    }
    *out = build_best_name(options->TargetUserId->id_str, record.display_name, record.nickname,
                           local_platform_type);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_userinfo::copy_best_display_name_with_platform(
    const EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions* options,
    EOS_UserInfo_BestDisplayName** out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_USERINFO_COPYBESTDISPLAYNAMEWITHPLATFORM_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    if (options->TargetUserId == 0 || !options->TargetUserId->valid) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // The only display name we hold is the Epic one. Asked for a specific other platform, we have no
    // linked account to answer from, and returning the Epic bytes relabelled as that platform would
    // assert an identity we do not have -- so the best name is indeterminate, as the header allows.
    if (options->TargetPlatformType != local_platform_type) {
        return EOS_EResult::EOS_UserInfo_BestDisplayNameIndeterminate;
    }
    user_record record;
    if (!resolve(options->TargetUserId->id_str, record) || record.display_name.empty()) {
        return EOS_EResult::EOS_NotFound;
    }
    *out = build_best_name(options->TargetUserId->id_str, record.display_name, record.nickname,
                           local_platform_type);
    return EOS_EResult::EOS_Success;
}

EOS_OnlinePlatformType sdk_userinfo::get_local_platform_type(
    const EOS_UserInfo_GetLocalPlatformTypeOptions* options) const {
    (void)options;
    return local_platform_type;
}

bool sdk_userinfo::cb_run_frame() {
    return false;
}

bool sdk_userinfo::run_callbacks(frame_result&) {
    return false;
}

// The by-name and by-external completions own an echoed string the header says is valid only during
// the callback. Once the delegate has run, we free it.
void sdk_userinfo::free_callback(frame_result& result) {
    if (result.type_id() == cb_query_by_name) {
        EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo* info =
            result.get_callback<EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo>();
        delete[] const_cast<char*>(info->DisplayName);
        info->DisplayName = 0;
    } else if (result.type_id() == cb_query_by_external) {
        EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo* info =
            result.get_callback<EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo>();
        delete[] const_cast<char*>(info->ExternalAccountId);
        info->ExternalAccountId = 0;
    }
}

bool sdk_userinfo::on_network_message(const net_envelope& message) {
    if (message.source_id.empty() || message.source_id == settings_.product_user_id()) {
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::peer_connected)) {
        const std::string epic(message.payload.begin(), message.payload.end());
        if (!epic.empty() && epic != settings_.epic_account_id()) {
            epic_to_peer_[epic] = message.source_id;
        }
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        // The disconnect carries no epic id, so we drop the entry that pointed at this peer.
        std::map<std::string, std::string>::iterator it = epic_to_peer_.begin();
        while (it != epic_to_peer_.end()) {
            if (it->second == message.source_id) {
                epic_to_peer_.erase(it++);
            } else {
                ++it;
            }
        }
        return true;
    }
    return false;
}

} // namespace eosr
