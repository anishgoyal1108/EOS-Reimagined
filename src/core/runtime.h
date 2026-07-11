#ifndef EOSR_CORE_RUNTIME_H
#define EOSR_CORE_RUNTIME_H

namespace eosr {

class sdk_client;
class sdk_platform;

// The single process-wide SDK client and platform that back the flat C ABI: one of each per
// loaded library, exactly the lifetime EOS_Initialize and EOS_Platform_Create expect. Two
// game instances that need independent state load the library twice. Tests construct their
// own sdk_client / sdk_platform in isolation instead of reaching for these.
sdk_client& global_client();
sdk_platform& global_platform();

} // namespace eosr

#endif
