#ifndef EOSR_COMMON_EOS_NAMES_H
#define EOSR_COMMON_EOS_NAMES_H

#include "eos_common.h"

namespace eosr {

// The symbolic name of an EOS_EResult ("EOS_Success", "EOS_NotFound", ...). The flat layer's
// EOS_EResult_ToString is this, and so is the `name` a trace record carries beside the numeric code,
// so the two can never drift apart. An out-of-range value reads as EOS_UnexpectedError.
const char* result_name(EOS_EResult result);

// The symbolic name of an EOS_EExternalCredentialType ("EOS_ECT_EPIC", ...), for the `cred_type` a
// login's trace record carries. The credential itself is never traced -- only which kind it was.
const char* credential_type_name(EOS_EExternalCredentialType type);

} // namespace eosr

#endif
