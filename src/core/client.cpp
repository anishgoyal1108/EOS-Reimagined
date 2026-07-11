#include "core/client.h"

#include "common/log.h"

namespace eosr {

sdk_client::sdk_client()
    : initialized_(false), allocate_(0), reallocate_(0), release_(0) {
}

EOS_EResult sdk_client::initialize(const EOS_InitializeOptions* options) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return EOS_EResult::EOS_AlreadyConfigured;
    }
    if (options == 0 || options->ProductName == 0 || options->ProductName[0] == '\0') {
        return EOS_EResult::EOS_InvalidParameters;
    }

    allocate_ = options->AllocateMemoryFunction;
    reallocate_ = options->ReallocateMemoryFunction;
    release_ = options->ReleaseMemoryFunction;
    product_name_ = options->ProductName;
    product_version_ = (options->ProductVersion != 0) ? options->ProductVersion : "";

    initialized_ = true;
    log_info("SDK initialized for product '" + product_name_ + "'");
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_client::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return EOS_EResult::EOS_NotConfigured;
    }
    allocate_ = 0;
    reallocate_ = 0;
    release_ = 0;
    product_name_.clear();
    product_version_.clear();
    initialized_ = false;
    log_info("SDK shut down");
    return EOS_EResult::EOS_Success;
}

bool sdk_client::is_initialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

} // namespace eosr
