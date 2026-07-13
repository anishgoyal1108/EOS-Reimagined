#include "platform/net_iface.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

namespace eosr {
namespace platform {

std::vector<u32> broadcast_addresses() {
    std::vector<u32> out;

    // GetAdaptersInfo tells us how much room it needs, and the table can grow between the two
    // calls, so we retry with the size it reports.
    ULONG size = 0;
    if (GetAdaptersInfo(0, &size) != ERROR_BUFFER_OVERFLOW || size == 0) {
        return out;
    }
    std::vector<u8> storage(size);
    IP_ADAPTER_INFO* adapters = reinterpret_cast<IP_ADAPTER_INFO*>(storage.data());
    if (GetAdaptersInfo(adapters, &size) != NO_ERROR) {
        return out;
    }

    for (IP_ADAPTER_INFO* adapter = adapters; adapter != 0; adapter = adapter->Next) {
        for (IP_ADDR_STRING* address = &adapter->IpAddressList; address != 0;
             address = address->Next) {
            const u32 ip = ntohl(inet_addr(address->IpAddress.String));
            const u32 mask = ntohl(inet_addr(address->IpMask.String));
            if (ip == 0 || ip == INADDR_NONE || mask == 0) {
                continue;
            }
            const u32 broadcast = ip | ~mask;

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
    }

    // Other instances on this machine reach us over loopback, which has no broadcast address.
    const u32 loopback = 0x7f000001u;
    bool has_loopback = false;
    for (std::size_t i = 0; i < out.size(); i++) {
        if (out[i] == loopback) {
            has_loopback = true;
            break;
        }
    }
    if (!has_loopback) {
        out.push_back(loopback);
    }
    return out;
}

} // namespace platform
} // namespace eosr
