#ifndef EOSR_COMMON_LOG_H
#define EOSR_COMMON_LOG_H

#include <map>
#include <mutex>
#include <string>

#include "eos_logging.h"

#include "common/types.h"

namespace eosr {

// The SDK log sink. EOS_Logging_SetCallback and EOS_Logging_SetLogLevel are handle-free,
// process-global functions, so the state they configure lives in one shared instance used
// by the flat layer and by internal callers that emit diagnostics. A message reaches the
// game only when its level passes the threshold configured for its category.
// Spec: Logging Interface (wiki/developers/internals/architecture.qmd), EOSSDK_Client::SetupLogs (wiki/developers/internals/client.qmd)
class logger {
public:
    static logger& instance();

    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;

    void set_callback(EOS_LogMessageFunc callback);
    void set_level(EOS_ELogCategory category, EOS_ELogLevel level);

    // Deliver one message to the game callback if one is set and the level passes the
    // category threshold. `category_name` is the UTF-8 label handed to the game verbatim.
    void log(EOS_ELogCategory category, EOS_ELogLevel level, const char* category_name,
             const std::string& message);

private:
    logger();

    EOS_ELogLevel threshold_for(EOS_ELogCategory category) const;

    EOS_LogMessageFunc callback_;
    // Default applies to every category; per-category entries override it.
    EOS_ELogLevel default_level_;
    std::map<i32, EOS_ELogLevel> category_levels_;
    mutable std::mutex mutex_;
};

// Convenience for internal diagnostics, all under the core category.
void log_info(const std::string& message);
void log_warn(const std::string& message);
void log_error(const std::string& message);

} // namespace eosr

#endif
