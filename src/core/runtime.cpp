#include "core/runtime.h"

#include <mutex>

#include "core/client.h"
#include "core/platform.h"

namespace eosr {

namespace {

std::mutex platform_mutex;
sdk_platform* live_platform = 0;

} // namespace

sdk_client& global_client() {
    static sdk_client client;
    return client;
}

sdk_platform* platform_create() {
    std::lock_guard<std::mutex> lock(platform_mutex);
    if (live_platform == 0) {
        live_platform = new sdk_platform();
    }
    return live_platform;
}

sdk_platform* platform_current() {
    std::lock_guard<std::mutex> lock(platform_mutex);
    return live_platform;
}

void platform_destroy() {
    sdk_platform* doomed = 0;
    {
        std::lock_guard<std::mutex> lock(platform_mutex);
        doomed = live_platform;
        live_platform = 0;
    }
    // Delete outside the lock: ~sdk_platform runs release(), which we keep off the registry
    // lock so a teardown path can never contend with it.
    delete doomed;
}

} // namespace eosr
