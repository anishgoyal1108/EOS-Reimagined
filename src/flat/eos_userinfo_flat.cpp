// Flat C ABI trampolines for the UserInfo interface. Thin: resolve the platform, check the handle
// against its slot, forward. The Copy* calls hand back a struct the game frees through the matching
// EOS_UserInfo*_Release, which routes to the interface's copy-out store.
#include "eos_userinfo.h"

#include "core/platform.h"
#include "core/runtime.h"
#include "interfaces/userinfo.h"

namespace {

eosr::sdk_userinfo* checked_userinfo(EOS_HUserInfo handle) {
    eosr::sdk_platform* platform = eosr::platform_current();
    if (platform == 0 || !platform->is_created()) {
        return 0;
    }
    if (handle != reinterpret_cast<EOS_HUserInfo>(platform->interface_handle(eosr::if_userinfo))) {
        return 0;
    }
    return &platform->userinfo();
}

} // namespace

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfo(
    EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoOptions* Options, void* ClientData,
    const EOS_UserInfo_OnQueryUserInfoCallback CompletionDelegate) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo != 0) {
        userinfo->query_user_info(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfoByDisplayName(
    EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoByDisplayNameOptions* Options,
    void* ClientData, const EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback CompletionDelegate) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo != 0) {
        userinfo->query_user_info_by_display_name(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfoByExternalAccount(
    EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoByExternalAccountOptions* Options,
    void* ClientData, const EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback CompletionDelegate) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo != 0) {
        userinfo->query_user_info_by_external_account(Options, ClientData, CompletionDelegate);
    }
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyUserInfo(
    EOS_HUserInfo Handle, const EOS_UserInfo_CopyUserInfoOptions* Options,
    EOS_UserInfo** OutUserInfo) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo == 0) {
        if (OutUserInfo != 0) {
            *OutUserInfo = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return userinfo->copy_user_info(Options, OutUserInfo);
}

EOS_DECLARE_FUNC(uint32_t) EOS_UserInfo_GetExternalUserInfoCount(
    EOS_HUserInfo Handle, const EOS_UserInfo_GetExternalUserInfoCountOptions* Options) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    return (userinfo != 0) ? userinfo->get_external_user_info_count(Options) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByIndex(
    EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByIndexOptions* Options,
    EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo == 0) {
        if (OutExternalUserInfo != 0) {
            *OutExternalUserInfo = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return userinfo->copy_external_user_info_by_index(Options, OutExternalUserInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByAccountType(
    EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions* Options,
    EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo == 0) {
        if (OutExternalUserInfo != 0) {
            *OutExternalUserInfo = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return userinfo->copy_external_user_info_by_account_type(Options, OutExternalUserInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByAccountId(
    EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByAccountIdOptions* Options,
    EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo == 0) {
        if (OutExternalUserInfo != 0) {
            *OutExternalUserInfo = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return userinfo->copy_external_user_info_by_account_id(Options, OutExternalUserInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyBestDisplayName(
    EOS_HUserInfo Handle, const EOS_UserInfo_CopyBestDisplayNameOptions* Options,
    EOS_UserInfo_BestDisplayName** OutBestDisplayName) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo == 0) {
        if (OutBestDisplayName != 0) {
            *OutBestDisplayName = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return userinfo->copy_best_display_name(Options, OutBestDisplayName);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyBestDisplayNameWithPlatform(
    EOS_HUserInfo Handle, const EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions* Options,
    EOS_UserInfo_BestDisplayName** OutBestDisplayName) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    if (userinfo == 0) {
        if (OutBestDisplayName != 0) {
            *OutBestDisplayName = 0;
        }
        return EOS_EResult::EOS_InvalidParameters;
    }
    return userinfo->copy_best_display_name_with_platform(Options, OutBestDisplayName);
}

EOS_DECLARE_FUNC(EOS_OnlinePlatformType) EOS_UserInfo_GetLocalPlatformType(
    EOS_HUserInfo Handle, const EOS_UserInfo_GetLocalPlatformTypeOptions* Options) {
    eosr::sdk_userinfo* userinfo = checked_userinfo(Handle);
    return (userinfo != 0) ? userinfo->get_local_platform_type(Options) : EOS_OPT_Unknown;
}

// The three release entry points free a struct a Copy* handed the game. They take no handle, so they
// route straight to the interface's copy-out store, which recognizes only structs it minted.
EOS_DECLARE_FUNC(void) EOS_UserInfo_Release(EOS_UserInfo* UserInfo) {
    eosr::release_user_info(UserInfo);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_ExternalUserInfo_Release(
    EOS_UserInfo_ExternalUserInfo* ExternalUserInfo) {
    eosr::release_external_user_info(ExternalUserInfo);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_BestDisplayName_Release(
    EOS_UserInfo_BestDisplayName* BestDisplayName) {
    eosr::release_best_display_name(BestDisplayName);
}
