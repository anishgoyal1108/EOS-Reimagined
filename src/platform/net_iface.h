#ifndef EOSR_PLATFORM_NET_IFACE_H
#define EOSR_PLATFORM_NET_IFACE_H

#include <vector>

#include "common/types.h"

namespace eosr {
namespace platform {

// The broadcast addresses of every usable local IPv4 interface, in host byte order. Discovery
// sends its advertisement to each of these, so a peer on any attached network sees us. An
// interface's broadcast address is its address with the host bits set (ip | ~netmask).
std::vector<u32> broadcast_addresses();

} // namespace platform
} // namespace eosr

#endif
