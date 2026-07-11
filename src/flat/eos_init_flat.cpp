// Flat C ABI trampolines for the SDK lifecycle. Each exported EOS_* function forwards to the
// process-global sdk_client; the header's EOS_DECLARE_FUNC carries the export attribute.
#include "eos_init.h"

#include "core/client.h"
#include "core/runtime.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options) {
    return eosr::global_client().initialize(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Shutdown() {
    return eosr::global_client().shutdown();
}
