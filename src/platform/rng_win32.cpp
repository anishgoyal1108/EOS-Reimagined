#include "platform/rng.h"

#include <windows.h>
#include <bcrypt.h>

namespace eosr {
namespace platform {

bool random_bytes(u8* out, std::size_t len) {
    if (out == 0 || len == 0) {
        return false;
    }
    const NTSTATUS status = BCryptGenRandom(0, reinterpret_cast<PUCHAR>(out),
                                            static_cast<ULONG>(len),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return status == 0; // STATUS_SUCCESS
}

} // namespace platform
} // namespace eosr
