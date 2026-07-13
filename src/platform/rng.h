#ifndef EOSR_PLATFORM_RNG_H
#define EOSR_PLATFORM_RNG_H

#include <cstddef>

#include "common/types.h"

namespace eosr {
namespace platform {

// Fill `out` with `len` bytes from the operating system's random source. Returns false if the
// source is unavailable, in which case `out` is left untouched. The ids we mint from this are
// identifiers, not secrets, but they must never collide between two instances, so we take them
// from the OS rather than from a seeded generator that two copies of a game would both replay.
bool random_bytes(u8* out, std::size_t len);

} // namespace platform
} // namespace eosr

#endif
