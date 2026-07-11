#include "common/log.h"

namespace eosr {

logger& logger::instance() {
    static logger the_logger;
    return the_logger;
}

// EOS delivers Warnings, Errors, and Fatals by default until a game raises the level.
logger::logger() : callback_(0), default_level_(EOS_ELogLevel::EOS_LOG_Warning) {
}

void logger::set_callback(EOS_LogMessageFunc callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
}

void logger::set_level(EOS_ELogCategory category, EOS_ELogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (category == EOS_ELogCategory::EOS_LC_ALL_CATEGORIES) {
        default_level_ = level;
        category_levels_.clear();
        return;
    }
    category_levels_[static_cast<i32>(category)] = level;
}

EOS_ELogLevel logger::threshold_for(EOS_ELogCategory category) const {
    std::map<i32, EOS_ELogLevel>::const_iterator it =
        category_levels_.find(static_cast<i32>(category));
    if (it != category_levels_.end()) {
        return it->second;
    }
    return default_level_;
}

void logger::log(EOS_ELogCategory category, EOS_ELogLevel level, const char* category_name,
                 const std::string& message) {
    EOS_LogMessageFunc callback = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // A higher level value is more verbose; deliver only what the threshold admits.
        if (callback_ != 0 && level <= threshold_for(category)) {
            callback = callback_;
        }
    }
    // Fire outside the lock so a game callback that re-enters the SDK cannot deadlock us.
    if (callback != 0) {
        EOS_LogMessage msg;
        msg.Category = category_name;
        msg.Message = message.c_str();
        msg.Level = level;
        callback(&msg);
    }
}

void log_info(const std::string& message) {
    logger::instance().log(EOS_ELogCategory::EOS_LC_Core, EOS_ELogLevel::EOS_LOG_Info,
                           "LogEOS", message);
}

void log_warn(const std::string& message) {
    logger::instance().log(EOS_ELogCategory::EOS_LC_Core, EOS_ELogLevel::EOS_LOG_Warning,
                           "LogEOS", message);
}

void log_error(const std::string& message) {
    logger::instance().log(EOS_ELogCategory::EOS_LC_Core, EOS_ELogLevel::EOS_LOG_Error,
                           "LogEOS", message);
}

} // namespace eosr
