#include "platform/dynlib.h"

#include <dlfcn.h>

namespace eosr {
namespace platform {

dynamic_library::dynamic_library() : handle_(0) {
}

dynamic_library::~dynamic_library() {
    close();
}

bool dynamic_library::open(const char* path) {
    close();
    handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    return handle_ != 0;
}

void dynamic_library::close() {
    if (handle_ != 0) {
        dlclose(handle_);
        handle_ = 0;
    }
}

void* dynamic_library::symbol(const char* name) {
    if (handle_ == 0) {
        return 0;
    }
    return dlsym(handle_, name);
}

} // namespace platform
} // namespace eosr
