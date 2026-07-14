#ifndef EOSR_COMMON_EOS_NAMES_H
#define EOSR_COMMON_EOS_NAMES_H

#include "eos_common.h"
#include "eos_p2p_types.h"
#include "eos_types.h"

namespace eosr {

// The symbolic name of an EOS_EResult ("EOS_Success", "EOS_NotFound", ...). The flat layer's
// EOS_EResult_ToString is this, and so is the `name` a trace record carries beside the numeric code,
// so the two can never drift apart. An out-of-range value reads as EOS_UnexpectedError.
const char* result_name(EOS_EResult result);

// The symbolic name of an EOS_EExternalCredentialType ("EOS_ECT_EPIC", ...), for the `cred_type` a
// login's trace record carries. The credential itself is never traced -- only which kind it was.
const char* credential_type_name(EOS_EExternalCredentialType type);

// The symbolic name of an EOS_EConnectionClosedReason, for the `reason` a net/p2p_close record
// carries -- the same reason the game is handed, so the trace and the game agree on why it closed.
const char* connection_closed_reason_name(EOS_EConnectionClosedReason reason);
const char* application_status_name(EOS_EApplicationStatus status);
const char* network_status_name(EOS_ENetworkStatus status);

} // namespace eosr

#endif
