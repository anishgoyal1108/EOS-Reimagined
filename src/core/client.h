#ifndef EOSR_CORE_CLIENT_H
#define EOSR_CORE_CLIENT_H

#include <mutex>
#include <string>

#include "eos_common.h"
#include "eos_init.h"

namespace eosr {

// The process-global SDK state established by EOS_Initialize and torn down by EOS_Shutdown:
// the memory hooks the game supplied and the product identity, guarded by the init flag every
// other entry point checks. It creates no interfaces; those belong to the platform. The flat
// layer owns the single instance, so the class itself stays free of singleton machinery and
// can be constructed in isolation by tests.
// Spec: EOSSDK_Client global state (docs/client.md), EOS_Initialize (docs/architecture.md)
class sdk_client {
public:
    sdk_client();

    // EOS_Success on the first call; EOS_AlreadyConfigured if already initialized;
    // EOS_InvalidParameters for null options or an empty product name.
    EOS_EResult initialize(const EOS_InitializeOptions* options);

    // EOS_Success when it tears down a live SDK; EOS_NotConfigured if never initialized.
    EOS_EResult shutdown();

    bool is_initialized() const;

    const std::string& product_name() const { return product_name_; }
    const std::string& product_version() const { return product_version_; }

private:
    mutable std::mutex mutex_;
    bool initialized_;

    EOS_AllocateMemoryFunc allocate_;
    EOS_ReallocateMemoryFunc reallocate_;
    EOS_ReleaseMemoryFunc release_;
    std::string product_name_;
    std::string product_version_;
};

} // namespace eosr

#endif
