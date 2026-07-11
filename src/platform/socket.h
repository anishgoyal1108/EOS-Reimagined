#ifndef EOSR_PLATFORM_SOCKET_H
#define EOSR_PLATFORM_SOCKET_H

#include <cstddef>
#include <cstdint>

#include "common/types.h"

namespace eosr {
namespace platform {

// Holds an OS socket handle without dragging OS headers into this file: a POSIX fd or a
// Windows SOCKET and a POSIX fd both fit. The invalid value is represented as all bits set.
using native_socket = std::uintptr_t;
constexpr native_socket invalid_socket = static_cast<native_socket>(-1);

// An IPv4 endpoint in host byte order.
struct endpoint {
    u32 ip;
    u16 port;

    endpoint() : ip(0), port(0) {}
    endpoint(u32 ip_value, u16 port_value) : ip(ip_value), port(port_value) {}
};

constexpr u32 ip_any = 0x00000000u;
constexpr u32 ip_loopback = 0x7f000001u; // 127.0.0.1

// The states a non-blocking operation can leave behind, unified across Winsock and POSIX.
enum class sock_error {
    none,
    would_block,
    in_progress,
    is_connected,
    other
};

// Call once before using any socket (WSAStartup on Windows; a no-op elsewhere).
bool net_init();
void net_shutdown();

// A non-copyable socket that closes itself when destroyed.
class socket {
public:
    socket();
    ~socket() noexcept;
    socket(socket&& other) noexcept;
    socket& operator=(socket&& other) noexcept;
    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

    bool open_udp();
    bool open_tcp();
    bool is_open() const { return handle_ != invalid_socket; }
    void close();

    bool bind(const endpoint& addr);
    bool listen(int backlog);
    // Accept a pending connection into `out`, filling `peer`. Returns false if none is ready.
    bool accept(socket& out, endpoint& peer);
    // Start a (non-blocking) connect. Returns true if it completed or is in progress; use
    // last_error() to tell which.
    bool connect(const endpoint& addr);

    int send(const u8* data, std::size_t len);
    int recv(u8* data, std::size_t len);
    int send_to(const u8* data, std::size_t len, const endpoint& to);
    int recv_from(u8* data, std::size_t len, endpoint& from);

    bool set_nonblocking(bool enabled);
    bool set_broadcast(bool enabled);
    bool set_reuseaddr(bool enabled);

    std::size_t bytes_available();
    bool local_endpoint(endpoint& out);

    sock_error last_error() const { return last_error_; }
    native_socket native() const { return handle_; }

private:
    native_socket handle_;
    sock_error last_error_;
};

// Wait up to timeout_ms for readability across `handles`. Writes true into readable_out[i]
// where handles[i] has data (or a pending connection). Returns the number ready, or -1.
int poll_readable(const native_socket* handles, std::size_t count, int timeout_ms, bool* readable_out);

// Host/network byte-order conversion without <winsock>; a no-op on big-endian hosts.
inline u16 host_to_net_16(u16 value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return value;
#else
    return static_cast<u16>((value << 8) | (value >> 8));
#endif
}

inline u32 host_to_net_32(u32 value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return value;
#else
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
#endif
}

inline u16 net_to_host_16(u16 value) { return host_to_net_16(value); }
inline u32 net_to_host_32(u32 value) { return host_to_net_32(value); }

} // namespace platform
} // namespace eosr

#endif
