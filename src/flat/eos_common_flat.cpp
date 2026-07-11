// Flat C ABI trampolines for the handle-free common helpers: result stringification and the
// id conversion/validation family. Ids are interned by the shared id_registry, so a given
// string always resolves to the same handle pointer.
#include <cstring>
#include <string>

#include "eos_common.h"

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
    switch (Result) {
#define EOS_RESULT_VALUE(Name, Value) case EOS_EResult::Name: return #Name;
#define EOS_RESULT_VALUE_LAST(Name, Value) case EOS_EResult::Name: return #Name;
#include "eos_result.h"
#undef EOS_RESULT_VALUE
#undef EOS_RESULT_VALUE_LAST
    }
    // Reached only for an out-of-range value cast into the enum.
    return "EOS_UnexpectedError";
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
