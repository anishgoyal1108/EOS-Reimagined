#include "platform/dynlib.h"

#include <windows.h>

namespace eosr {
namespace platform {

dynamic_library::dynamic_library() : handle_(0) {
}

dynamic_library::~dynamic_library() {
    close();
}

bool dynamic_library::open(const char* path) {
    close();
    if (path == 0) {
        return false;
    }
    handle_ = LoadLibraryA(path);
    if (handle_ == 0) {
        // Fall back to the bare file name, which searches the loading executable's directory.
        const char* base = path;
        for (const char* p = path; *p != '\0'; p++) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        if (base != path) {
            handle_ = LoadLibraryA(base);
        }
    }
    return handle_ != 0;
}

void dynamic_library::close() {
    if (handle_ != 0) {
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
        handle_ = 0;
    }
}

void* dynamic_library::symbol(const char* name) {
    if (handle_ == 0) {
        return 0;
    }
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
}

} // namespace platform
} // namespace eosr
