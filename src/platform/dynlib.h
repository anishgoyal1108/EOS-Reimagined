#ifndef EOSR_PLATFORM_DYNLIB_H
#define EOSR_PLATFORM_DYNLIB_H

namespace eosr {
namespace platform {

// A minimal cross-platform dynamic-library loader (dlopen / LoadLibrary behind one interface).
// The shipped SDK never loads other libraries; this exists so the integration test can load the
// real built .so/.dll and drive it through the C ABI exactly as a game would.
class dynamic_library {
public:
    dynamic_library();
    ~dynamic_library();

    dynamic_library(const dynamic_library&) = delete;
    dynamic_library& operator=(const dynamic_library&) = delete;

    // Load the library at `path`. Returns false if it cannot be found or loaded.
    bool open(const char* path);
    void close();
    bool is_open() const { return handle_ != 0; }

    // Resolve an exported symbol, or null if the library is closed or the symbol is absent.
    void* symbol(const char* name);

private:
    void* handle_;
};

} // namespace platform
} // namespace eosr

#endif
