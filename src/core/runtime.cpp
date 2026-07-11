#include "core/runtime.h"

#include "core/client.h"
#include "core/platform.h"

namespace eosr {

sdk_client& global_client() {
    static sdk_client client;
    return client;
}

sdk_platform& global_platform() {
    static sdk_platform platform;
    return platform;
}

} // namespace eosr
