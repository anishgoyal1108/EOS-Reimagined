// Flat C ABI trampolines for the handle-free Logging interface. Configuration lives in the
// process-global logger; per the header contract these are rejected before EOS_Initialize.
#include "eos_logging.h"

#include "common/log.h"
#include "core/client.h"
#include "core/runtime.h"
#include "core/tracer.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Logging_SetCallback(EOS_LogMessageFunc Callback) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Logging_SetCallback", 0,
                                 eosr::call_mode::sync);
    if (!eosr::global_client().is_initialized()) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_NotConfigured);
    }
    eosr::logger::instance().set_callback(Callback);
    return eosr::traced_result(eosr_trace, EOS_EResult::EOS_Success);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Logging_SetLogLevel(EOS_ELogCategory LogCategory, EOS_ELogLevel LogLevel) {
    eosr::trace_scope eosr_trace(eosr::global_tracer(), "EOS_Logging_SetLogLevel", 0,
                                 eosr::call_mode::sync);
    if (!eosr::global_client().is_initialized()) {
        return eosr::traced_result(eosr_trace, EOS_EResult::EOS_NotConfigured);
    }
    eosr::logger::instance().set_level(LogCategory, LogLevel);
    return eosr::traced_result(eosr_trace, EOS_EResult::EOS_Success);
}
