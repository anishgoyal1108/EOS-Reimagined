#include "core/client.h"

#include <cstring>

#include "common/log.h"

namespace eosr {

namespace {

// EOS requires product name and version to be non-empty, within a length bound, and made of
// readable ANSI characters (32-127).
bool is_readable_ansi(const char* value, std::size_t max_length) {
    if (value == 0 || value[0] == '\0') {
        return false;
    }
    const std::size_t length = std::strlen(value);
    if (length > max_length) {
        return false;
    }
    for (std::size_t i = 0; i < length; i++) {
        const unsigned char character = static_cast<unsigned char>(value[i]);
        if (character < 32 || character > 127) {
            return false;
        }
    }
    return true;
}

// The custom allocator hooks are all-or-nothing: either the whole triple is supplied or none.
bool memory_callbacks_are_valid(const EOS_InitializeOptions& options) {
    const bool has_allocate = options.AllocateMemoryFunction != 0;
    const bool has_reallocate = options.ReallocateMemoryFunction != 0;
    const bool has_release = options.ReleaseMemoryFunction != 0;
    return (has_allocate == has_reallocate) && (has_allocate == has_release);
}

} // namespace

sdk_client::sdk_client()
    : state_(client_never_initialized), allocate_(0), reallocate_(0), release_(0) {
}

EOS_EResult sdk_client::initialize(const EOS_InitializeOptions* options) {
    std::string product_name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != client_never_initialized) {
            return EOS_EResult::EOS_AlreadyConfigured;
        }
        if (
            options == 0 ||
            options->ApiVersion <= 0 ||
            options->ApiVersion > EOS_INITIALIZE_API_LATEST ||
            !is_readable_ansi(options->ProductName, EOS_INITIALIZEOPTIONS_PRODUCTNAME_MAX_LENGTH) ||
            !is_readable_ansi(options->ProductVersion, EOS_INITIALIZEOPTIONS_PRODUCTVERSION_MAX_LENGTH) ||
            !memory_callbacks_are_valid(*options)
        ) {
            return EOS_EResult::EOS_InvalidParameters;
        }

        allocate_ = options->AllocateMemoryFunction;
        reallocate_ = options->ReallocateMemoryFunction;
        release_ = options->ReleaseMemoryFunction;
        product_name_ = options->ProductName;
        product_version_ = options->ProductVersion;

        state_ = client_initialized;
        product_name = product_name_;
    }
    // We log after releasing the lock: the game's log callback may re-enter the SDK (for
    // example to set the log level), and holding the lock here would deadlock that call.
    log_info("SDK initialized for product '" + product_name + "'");
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_client::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == client_never_initialized) {
            return EOS_EResult::EOS_NotConfigured;
        }
        if (state_ == client_shutdown) {
            return EOS_EResult::EOS_UnexpectedError;
        }
        allocate_ = 0;
        reallocate_ = 0;
        release_ = 0;
        product_name_.clear();
        product_version_.clear();
        state_ = client_shutdown;
    }
    log_info("SDK shut down");
    return EOS_EResult::EOS_Success;
}

bool sdk_client::is_initialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == client_initialized;
}

} // namespace eosr
