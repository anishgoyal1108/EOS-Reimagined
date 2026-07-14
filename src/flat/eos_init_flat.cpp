// Flat C ABI trampolines for the SDK lifecycle. Each exported EOS_* function forwards to the
// process-global sdk_client; the header's EOS_DECLARE_FUNC carries the export attribute.
#include <string>
#include <vector>

#include "eos_init.h"
#include "eos_logging.h"

#include "common/log.h"
#include "common/eos_names.h"
#include "core/client.h"
#include "core/config.h"
#include "core/runtime.h"
#include "core/system_config_source.h"
#include "core/tracer.h"
#include "net/message_router.h"
#include "platform/paths.h"

namespace eosr {
namespace {

// The emulator's log levels map onto EOS's, which is the only threshold the logger understands. This
// is the seam where the two meet: core/config.h knows nothing of EOS, and common/log.h nothing of the
// config.
EOS_ELogLevel to_eos_log_level(log_level level) {
    switch (level) {
        case log_level::off: return EOS_ELogLevel::EOS_LOG_Off;
        case log_level::fatal: return EOS_ELogLevel::EOS_LOG_Fatal;
        case log_level::error: return EOS_ELogLevel::EOS_LOG_Error;
        case log_level::warn: return EOS_ELogLevel::EOS_LOG_Warning;
        case log_level::info: return EOS_ELogLevel::EOS_LOG_Info;
        case log_level::debug: return EOS_ELogLevel::EOS_LOG_Verbose;
        case log_level::trace: return EOS_ELogLevel::EOS_LOG_VeryVerbose;
    }
    return EOS_ELogLevel::EOS_LOG_Off;
}

} // namespace
} // namespace eosr

EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options) {
    const EOS_EResult result = eosr::global_client().initialize(Options);
    if (result == EOS_EResult::EOS_Success) {
        // Resolve the emulator's own configuration once, here, off the client lock: a diagnostic routes
        // through the log callback, which the game may use to re-enter the SDK. The config's discovery
        // range default matches the network layer's, so an unset range is the one a run would use.
        const eosr::net_config defaults;
        const eosr::discovery_range default_ports = {defaults.discovery_port_first,
                                                     defaults.discovery_port_last};
        const eosr::resolved_config config =
            eosr::load_resolved_config(eosr::platform::user_data_directory(), default_ports);

        // The logger's threshold is the player's to set: a game that never calls EOS_Logging_SetLogLevel
        // otherwise leaves it at Warning, and our own diagnostics with it.
        eosr::logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                           eosr::to_eos_log_level(config.logging));

        // The platform reads this at EOS_Platform_Create for what the game does not supply: the
        // player's display name and language, the discovery ports, whether to run a peer network.
        eosr::set_global_run_config(config);
        eosr::global_tracer().start(config);
    }
    eosr::tracer& trace = eosr::global_tracer();
    if (trace.enabled()) {
        const i32 api = Options != 0 ? Options->ApiVersion : 0;
        trace.record_call("EOS_Initialize", api, std::string(), std::vector<eosr::trace_field>());
        trace.record_return(
            "EOS_Initialize", std::string(),
            eosr::return_result(static_cast<i32>(result), eosr::result_name(result)));
    }
    return result;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Shutdown() {
    eosr::tracer& trace = eosr::global_tracer();
    if (trace.enabled()) {
        trace.record_call("EOS_Shutdown", 0, std::string(), std::vector<eosr::trace_field>());
    }
    const EOS_EResult result = eosr::global_client().shutdown();
    if (trace.enabled()) {
        trace.record_return(
            "EOS_Shutdown", std::string(),
            eosr::return_result(static_cast<i32>(result), eosr::result_name(result)));
    }
    if (result == EOS_EResult::EOS_Success) {
        trace.stop();
    }
    return result;
}
