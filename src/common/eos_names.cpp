#include "common/eos_names.h"

namespace eosr {

const char* result_name(EOS_EResult result) {
    switch (result) {
#define EOS_RESULT_VALUE(Name, Value) case EOS_EResult::Name: return #Name;
#define EOS_RESULT_VALUE_LAST(Name, Value) case EOS_EResult::Name: return #Name;
#include "eos_result.h"
#undef EOS_RESULT_VALUE
#undef EOS_RESULT_VALUE_LAST
    }
    // Reached only for an out-of-range value cast into the enum.
    return "EOS_UnexpectedError";
}

const char* credential_type_name(EOS_EExternalCredentialType type) {
    switch (type) {
        case EOS_EExternalCredentialType::EOS_ECT_EPIC: return "EOS_ECT_EPIC";
        case EOS_EExternalCredentialType::EOS_ECT_STEAM_APP_TICKET: return "EOS_ECT_STEAM_APP_TICKET";
        case EOS_EExternalCredentialType::EOS_ECT_PSN_ID_TOKEN: return "EOS_ECT_PSN_ID_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_XBL_XSTS_TOKEN: return "EOS_ECT_XBL_XSTS_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_DISCORD_ACCESS_TOKEN: return "EOS_ECT_DISCORD_ACCESS_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_GOG_SESSION_TICKET: return "EOS_ECT_GOG_SESSION_TICKET";
        case EOS_EExternalCredentialType::EOS_ECT_NINTENDO_ID_TOKEN: return "EOS_ECT_NINTENDO_ID_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_NINTENDO_NSA_ID_TOKEN: return "EOS_ECT_NINTENDO_NSA_ID_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_UPLAY_ACCESS_TOKEN: return "EOS_ECT_UPLAY_ACCESS_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN: return "EOS_ECT_OPENID_ACCESS_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN: return "EOS_ECT_DEVICEID_ACCESS_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_APPLE_ID_TOKEN: return "EOS_ECT_APPLE_ID_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_GOOGLE_ID_TOKEN: return "EOS_ECT_GOOGLE_ID_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_OCULUS_USERID_NONCE: return "EOS_ECT_OCULUS_USERID_NONCE";
        case EOS_EExternalCredentialType::EOS_ECT_ITCHIO_JWT: return "EOS_ECT_ITCHIO_JWT";
        case EOS_EExternalCredentialType::EOS_ECT_ITCHIO_KEY: return "EOS_ECT_ITCHIO_KEY";
        case EOS_EExternalCredentialType::EOS_ECT_EPIC_ID_TOKEN: return "EOS_ECT_EPIC_ID_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_AMAZON_ACCESS_TOKEN: return "EOS_ECT_AMAZON_ACCESS_TOKEN";
        case EOS_EExternalCredentialType::EOS_ECT_STEAM_SESSION_TICKET: return "EOS_ECT_STEAM_SESSION_TICKET";
        case EOS_EExternalCredentialType::EOS_ECT_VIVEPORT_USER_TOKEN: return "EOS_ECT_VIVEPORT_USER_TOKEN";
    }
    // An out-of-range value cast into the enum: name nothing rather than invent a credential kind.
    return "";
}

const char* connection_closed_reason_name(EOS_EConnectionClosedReason reason) {
    switch (reason) {
        case EOS_EConnectionClosedReason::EOS_CCR_ClosedByLocalUser: return "EOS_CCR_ClosedByLocalUser";
        case EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer: return "EOS_CCR_ClosedByPeer";
        case EOS_EConnectionClosedReason::EOS_CCR_ConnectionClosed: return "EOS_CCR_ConnectionClosed";
        case EOS_EConnectionClosedReason::EOS_CCR_ConnectionFailed: return "EOS_CCR_ConnectionFailed";
        case EOS_EConnectionClosedReason::EOS_CCR_ConnectionIgnored: return "EOS_CCR_ConnectionIgnored";
        case EOS_EConnectionClosedReason::EOS_CCR_InvalidData: return "EOS_CCR_InvalidData";
        case EOS_EConnectionClosedReason::EOS_CCR_InvalidMessage: return "EOS_CCR_InvalidMessage";
        case EOS_EConnectionClosedReason::EOS_CCR_NegotiationFailed: return "EOS_CCR_NegotiationFailed";
        case EOS_EConnectionClosedReason::EOS_CCR_TimedOut: return "EOS_CCR_TimedOut";
        case EOS_EConnectionClosedReason::EOS_CCR_TooManyConnections: return "EOS_CCR_TooManyConnections";
        case EOS_EConnectionClosedReason::EOS_CCR_UnexpectedError: return "EOS_CCR_UnexpectedError";
        case EOS_EConnectionClosedReason::EOS_CCR_Unknown: return "EOS_CCR_Unknown";
    }
    // An out-of-range value cast into the enum: say Unknown rather than invent a cause.
    return "EOS_CCR_Unknown";
}

const char* application_status_name(EOS_EApplicationStatus status) {
    switch (status) {
        case EOS_EApplicationStatus::EOS_AS_BackgroundConstrained:
            return "EOS_AS_BackgroundConstrained";
        case EOS_EApplicationStatus::EOS_AS_BackgroundUnconstrained:
            return "EOS_AS_BackgroundUnconstrained";
        case EOS_EApplicationStatus::EOS_AS_BackgroundSuspended:
            return "EOS_AS_BackgroundSuspended";
        case EOS_EApplicationStatus::EOS_AS_Foreground:
            return "EOS_AS_Foreground";
    }
    return "EOS_AS_Foreground";
}

const char* network_status_name(EOS_ENetworkStatus status) {
    switch (status) {
        case EOS_ENetworkStatus::EOS_NS_Disabled: return "EOS_NS_Disabled";
        case EOS_ENetworkStatus::EOS_NS_Offline: return "EOS_NS_Offline";
        case EOS_ENetworkStatus::EOS_NS_Online: return "EOS_NS_Online";
    }
    return "EOS_NS_Online";
}

} // namespace eosr
