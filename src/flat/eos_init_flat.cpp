// Flat C ABI trampolines for the SDK lifecycle. Each exported EOS_* function forwards to the
// process-global sdk_client; the header's EOS_DECLARE_FUNC carries the export attribute.
#include "eos_init.h"

#include "core/client.h"
#include "core/config.h"
#include "core/runtime.h"
#include "core/system_config_source.h"
#include "core/tracer.h"
#include "net/message_router.h"
#include "platform/paths.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options) {
    const EOS_EResult result = eosr::global_client().initialize(Options);
    if (result == EOS_EResult::EOS_Success) {
        // Resolve config and open the trace run now, off the client lock: a sink diagnostic routes
        // through the log callback, which the game may use to re-enter the SDK. The config's discovery
        // range default matches the network layer's, so runtime.json reports the ports a run would use.
        const eosr::net_config defaults;
        const eosr::discovery_range default_ports = {defaults.discovery_port_first,
                                                     defaults.discovery_port_last};
        eosr::global_tracer().start(
            eosr::load_resolved_config(eosr::platform::user_data_directory(), default_ports));
    }
    return result;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Shutdown() {
    const EOS_EResult result = eosr::global_client().shutdown();
    if (result == EOS_EResult::EOS_Success) {
        eosr::global_tracer().stop();
    }
    return result;
}
