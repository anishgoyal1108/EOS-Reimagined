#include "platform/rng.h"

#include <cstdio>

namespace eosr {
namespace platform {

bool random_bytes(u8* out, std::size_t len) {
    if (out == 0 || len == 0) {
        return false;
    }
    // /dev/urandom is the portable POSIX source. getrandom(2) would avoid the descriptor, but it
    // is Linux-only and needs a glibc new enough to expose it, so we read the device instead.
    std::FILE* source = std::fopen("/dev/urandom", "rb");
    if (source == 0) {
        return false;
    }
    const std::size_t read = std::fread(out, 1, len, source);
    std::fclose(source);
    return read == len;
}

} // namespace platform
} // namespace eosr
