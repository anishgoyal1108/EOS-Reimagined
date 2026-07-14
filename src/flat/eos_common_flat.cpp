// Flat C ABI trampolines for the handle-free common helpers: result stringification and the
// id conversion/validation family. Ids are interned by the shared id_registry, so a given
// string always resolves to the same handle pointer.
#include <cstring>
#include <string>

#include "eos_common.h"

#include "common/eos_names.h"
#include "eos_types.h"
#include "eos_version.h"

#include "common/ids.h"

namespace {

// Copy an id string into the caller's buffer following the EOS out-parameter contract.
EOS_EResult id_to_string(const std::string& id_str, bool valid, char* out_buffer,
                         int32_t* in_out_length) {
    if (out_buffer == 0 || in_out_length == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!valid) {
        return EOS_EResult::EOS_InvalidUser;
    }
    const int32_t needed = static_cast<int32_t>(id_str.size()) + 1; // room for the null
    if (*in_out_length < needed) {
        *in_out_length = needed;
        return EOS_EResult::EOS_LimitExceeded;
    }
    std::memcpy(out_buffer, id_str.c_str(), static_cast<std::size_t>(needed));
    *in_out_length = needed;
    return EOS_EResult::EOS_Success;
}

} // namespace

EOS_DECLARE_FUNC(const char*) EOS_EResult_ToString(EOS_EResult Result) {
    // The same mapping the trace records use, so the exported name and the traced name cannot drift.
    return eosr::result_name(Result);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_EpicAccountId_IsValid(EOS_EpicAccountId AccountId) {
    return (AccountId != 0 && AccountId->valid) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_EpicAccountId_ToString(EOS_EpicAccountId AccountId,
                                                         char* OutBuffer, int32_t* InOutBufferLength) {
    const bool valid = (AccountId != 0 && AccountId->valid);
    return id_to_string(valid ? AccountId->id_str : std::string(), valid, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_EpicAccountId_FromString(const char* AccountIdString) {
    if (AccountIdString == 0) {
        return 0;
    }
    return eosr::id_registry::instance().get_epic_account_id(AccountIdString);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_ProductUserId_IsValid(EOS_ProductUserId AccountId) {
    return (AccountId != 0 && AccountId->valid) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_ProductUserId_ToString(EOS_ProductUserId AccountId,
                                                         char* OutBuffer, int32_t* InOutBufferLength) {
    const bool valid = (AccountId != 0 && AccountId->valid);
    return id_to_string(valid ? AccountId->id_str : std::string(), valid, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_ProductUserId_FromString(const char* ProductUserIdString) {
    if (ProductUserIdString == 0) {
        return 0;
    }
    return eosr::id_registry::instance().get_product_user_id(ProductUserIdString);
}

// A result says the operation is finished unless the callback that carried it is going to be called
// again -- which is only ever the retry and the two interactive-login continuations.
EOS_DECLARE_FUNC(EOS_Bool) EOS_EResult_IsOperationComplete(EOS_EResult Result) {
    switch (Result) {
        case EOS_EResult::EOS_OperationWillRetry:
        case EOS_EResult::EOS_Auth_PinGrantCode:
        case EOS_EResult::EOS_Auth_MFARequired:
            return EOS_FALSE;
        default:
            return EOS_TRUE;
    }
}

// Hex, uppercase, null-terminated -- the encoding the header's own example spells out ("FA87097A..").
// A buffer too small is LimitExceeded with the length it would have needed, not a truncation.
EOS_DECLARE_FUNC(EOS_EResult) EOS_ByteArray_ToString(const uint8_t* ByteArray, const uint32_t Length,
                                                     char* OutBuffer,
                                                     uint32_t* InOutBufferLength) {
    // A zero length is not an empty success: the header lists InvalidParameters for "a null pointer
    // or invalid length", and the reference SDK refuses a zero length outright. There is nothing to
    // encode, and a caller asking us to encode nothing has made a mistake it would rather hear about.
    if (OutBuffer == 0 || InOutBufferLength == 0 || ByteArray == 0 || Length == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // Two characters a byte, plus room for the null. We have to know that fits in the length we
    // report *before* computing it: past this, the multiply wraps, and the wrapped value looks like
    // a buffer the caller could easily satisfy -- so a caller that trusted it and handed us one byte
    // would send the loop below walking gigabytes off the end of both buffers.
    const uint32_t max_encodable_length = (0xffffffffu - 1) / 2;
    if (Length > max_encodable_length) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const uint32_t needed = (Length * 2) + 1;
    if (*InOutBufferLength < needed) {
        *InOutBufferLength = needed;
        return EOS_EResult::EOS_LimitExceeded;
    }
    static const char digits[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < Length; i++) {
        OutBuffer[i * 2] = digits[(ByteArray[i] >> 4) & 0xf];
        OutBuffer[(i * 2) + 1] = digits[ByteArray[i] & 0xf];
    }
    OutBuffer[Length * 2] = '\0';
    *InOutBufferLength = needed;
    return EOS_EResult::EOS_Success;
}

// A continuance token is minted by an interactive login continuation, and there is nothing here to
// continue: an emulator has no account portal to send anyone to. So no token we are handed is one of
// ours, and the header has a word for that which is not the word for a malformed call -- a caller
// telling those apart is choosing between fixing its arguments and abandoning a login.
EOS_DECLARE_FUNC(EOS_EResult) EOS_ContinuanceToken_ToString(EOS_ContinuanceToken ContinuanceToken,
                                                            char* OutBuffer,
                                                            int32_t* InOutBufferLength) {
    if (OutBuffer == 0 || InOutBufferLength == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    (void)ContinuanceToken;
    return EOS_EResult::EOS_InvalidUser;
}

EOS_DECLARE_FUNC(const char*) EOS_EApplicationStatus_ToString(EOS_EApplicationStatus Status) {
    switch (Status) {
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

EOS_DECLARE_FUNC(const char*) EOS_ENetworkStatus_ToString(EOS_ENetworkStatus Status) {
    switch (Status) {
        case EOS_ENetworkStatus::EOS_NS_Disabled:
            return "EOS_NS_Disabled";
        case EOS_ENetworkStatus::EOS_NS_Offline:
            return "EOS_NS_Offline";
        case EOS_ENetworkStatus::EOS_NS_Online:
            return "EOS_NS_Online";
    }
    return "EOS_NS_Online";
}

// The version of the SDK we answer as. A game asking this is asking what API it may expect, so we
// name the headers we are built against -- the version string the real SDK would have produced from
// the very same header.
EOS_DECLARE_FUNC(const char*) EOS_GetVersion(void) {
    return EOS_VERSION_STRING;
}
