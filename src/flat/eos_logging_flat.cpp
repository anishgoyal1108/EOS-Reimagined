// Flat C ABI trampolines for the handle-free Logging interface. Configuration lives in the
// process-global logger; per the header contract these are rejected before EOS_Initialize.
#include "eos_logging.h"

#include "common/log.h"
#include "core/client.h"
#include "core/runtime.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Logging_SetCallback(EOS_LogMessageFunc Callback) {
    if (!eosr::global_client().is_initialized()) {
        return EOS_EResult::EOS_NotConfigured;
    }
    eosr::logger::instance().set_callback(Callback);
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Logging_SetLogLevel(EOS_ELogCategory LogCategory, EOS_ELogLevel LogLevel) {
    if (!eosr::global_client().is_initialized()) {
        return EOS_EResult::EOS_NotConfigured;
    }
    eosr::logger::instance().set_level(LogCategory, LogLevel);
    return EOS_EResult::EOS_Success;
}
