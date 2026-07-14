#include "core/runtime.h"

#include <mutex>
#include <vector>

#include "core/client.h"
#include "core/platform.h"
#include "core/tracer.h"

namespace eosr {

namespace {

std::mutex platform_mutex;
sdk_platform* live_platform = 0;

// Every platform we ever create is kept here and never freed for the life of the process. A
// released platform is torn down (its sockets and callbacks are freed) but its object memory is
// retained, so the allocator can never hand its address to a later platform. That is what keeps
// a handle from a released platform from ever matching a new one. Games create and release a
// platform once, so this holds a single object in practice. The container is itself immortal so
// the retained platforms stay reachable at exit and read as intentional, not as leaks.
std::vector<sdk_platform*>& retained_platforms() {
    static std::vector<sdk_platform*>* platforms = new std::vector<sdk_platform*>();
    return *platforms;
}

} // namespace

sdk_client& global_client() {
    static sdk_client client;
    return client;
}

tracer& global_tracer() {
    static tracer the_tracer;
    return the_tracer;
}

sdk_platform* platform_create() {
    std::lock_guard<std::mutex> lock(platform_mutex);
    if (live_platform == 0) {
        live_platform = new sdk_platform();
        retained_platforms().push_back(live_platform);
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
    // Tear down the platform but keep the object: releasing (not freeing) means a concurrent or
    // subsequent call that still holds this handle sees a not-created platform, never freed
    // memory. release() runs off the registry lock so a teardown can never contend with it.
    if (doomed != 0) {
        doomed->release();
    }
}

} // namespace eosr
