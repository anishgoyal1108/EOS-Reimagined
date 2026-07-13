#include "platform/net_iface.h"

#include <cstring>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace eosr {
namespace platform {

std::vector<u32> broadcast_addresses() {
    std::vector<u32> out;
    ifaddrs* list = 0;
    if (getifaddrs(&list) != 0) {
        return out;
    }

    for (ifaddrs* iface = list; iface != 0; iface = iface->ifa_next) {
        if (iface->ifa_addr == 0 || iface->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((iface->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        // A point-to-point link has no broadcast domain to advertise into.
        if ((iface->ifa_flags & (IFF_BROADCAST | IFF_LOOPBACK)) == 0) {
            continue;
        }

        const sockaddr_in* addr = reinterpret_cast<const sockaddr_in*>(iface->ifa_addr);
        u32 broadcast = 0;
        if ((iface->ifa_flags & IFF_LOOPBACK) != 0) {
            // Loopback carries no broadcast, but other instances on this machine still have to
            // find us, so we address them directly.
            broadcast = ntohl(addr->sin_addr.s_addr);
        } else if (iface->ifa_netmask != 0) {
            const sockaddr_in* mask = reinterpret_cast<const sockaddr_in*>(iface->ifa_netmask);
            broadcast = ntohl(addr->sin_addr.s_addr) | ~ntohl(mask->sin_addr.s_addr);
        } else {
            continue;
        }

        bool known = false;
        for (std::size_t i = 0; i < out.size(); i++) {
            if (out[i] == broadcast) {
                known = true;
                break;
            }
        }
        if (!known) {
            out.push_back(broadcast);
        }
    }

    freeifaddrs(list);
    return out;
}

} // namespace platform
} // namespace eosr
