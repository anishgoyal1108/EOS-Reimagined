#include "platform/socket.h"

#include <cstring>
#include <limits>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace eosr {
namespace platform {

static SOCKET sock_of(native_socket handle) {
    return static_cast<SOCKET>(handle);
}

static sock_error map_wsa(int code) {
    if (code == WSAEWOULDBLOCK) {
        return sock_error::would_block;
    }
    if (code == WSAEINPROGRESS || code == WSAEALREADY) {
        return sock_error::in_progress;
    }
    if (code == WSAEISCONN) {
        return sock_error::is_connected;
    }
    return sock_error::other;
}

static void fill_sockaddr(sockaddr_in& out, const endpoint& addr) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = host_to_net_16(addr.port);
    out.sin_addr.s_addr = host_to_net_32(addr.ip);
}

static endpoint endpoint_of(const sockaddr_in& addr) {
    return endpoint(net_to_host_32(addr.sin_addr.s_addr), net_to_host_16(addr.sin_port));
}

bool net_init() {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void net_shutdown() {
    WSACleanup();
}

socket::socket() : handle_(invalid_socket), last_error_(sock_error::none) {
}

socket::~socket() noexcept {
    close();
}

socket::socket(socket&& other) noexcept : handle_(other.handle_), last_error_(other.last_error_) {
    other.handle_ = invalid_socket;
}

socket& socket::operator=(socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        last_error_ = other.last_error_;
        other.handle_ = invalid_socket;
    }
    return *this;
}

bool socket::open_udp() {
    handle_ = static_cast<native_socket>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (!is_open()) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

bool socket::open_tcp() {
    handle_ = static_cast<native_socket>(::socket(AF_INET, SOCK_STREAM, 0));
    if (!is_open()) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

void socket::close() {
    if (is_open()) {
        ::closesocket(sock_of(handle_));
        handle_ = invalid_socket;
    }
}

bool socket::bind(const endpoint& addr) {
    sockaddr_in sa;
    fill_sockaddr(sa, addr);
    if (::bind(sock_of(handle_), reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

bool socket::listen(int backlog) {
    if (::listen(sock_of(handle_), backlog) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

bool socket::accept(socket& out, endpoint& peer) {
    sockaddr_in sa;
    int len = sizeof(sa);
    const SOCKET accepted = ::accept(sock_of(handle_), reinterpret_cast<sockaddr*>(&sa), &len);
    if (accepted == INVALID_SOCKET) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    out.close();
    out.handle_ = static_cast<native_socket>(accepted);
    out.last_error_ = sock_error::none;
    peer = endpoint_of(sa);
    last_error_ = sock_error::none;
    return true;
}

bool socket::connect(const endpoint& addr) {
    sockaddr_in sa;
    fill_sockaddr(sa, addr);
    if (::connect(sock_of(handle_), reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        last_error_ = sock_error::none;
        return true;
    }
    last_error_ = map_wsa(WSAGetLastError());
    return last_error_ == sock_error::in_progress
        || last_error_ == sock_error::would_block
        || last_error_ == sock_error::is_connected;
}

int socket::send(const u8* data, std::size_t len) {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        last_error_ = sock_error::other;
        return -1;
    }
    const int count = ::send(sock_of(handle_), reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
    if (count < 0) {
        last_error_ = map_wsa(WSAGetLastError());
    } else {
        last_error_ = sock_error::none;
    }
    return count;
}

int socket::recv(u8* data, std::size_t len) {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        last_error_ = sock_error::other;
        return -1;
    }
    const int count = ::recv(sock_of(handle_), reinterpret_cast<char*>(data), static_cast<int>(len), 0);
    if (count < 0) {
        last_error_ = map_wsa(WSAGetLastError());
    } else {
        last_error_ = sock_error::none;
    }
    return count;
}

int socket::send_to(const u8* data, std::size_t len, const endpoint& to) {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        last_error_ = sock_error::other;
        return -1;
    }
    sockaddr_in sa;
    fill_sockaddr(sa, to);
    const int count = ::sendto(sock_of(handle_), reinterpret_cast<const char*>(data),
                               static_cast<int>(len), 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (count < 0) {
        last_error_ = map_wsa(WSAGetLastError());
    } else {
        last_error_ = sock_error::none;
    }
    return count;
}

int socket::recv_from(u8* data, std::size_t len, endpoint& from) {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        last_error_ = sock_error::other;
        return -1;
    }
    sockaddr_in sa;
    int slen = sizeof(sa);
    const int count = ::recvfrom(sock_of(handle_), reinterpret_cast<char*>(data),
                                 static_cast<int>(len), 0, reinterpret_cast<sockaddr*>(&sa), &slen);
    if (count < 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return count;
    }
    from = endpoint_of(sa);
    last_error_ = sock_error::none;
    return count;
}

bool socket::set_nonblocking(bool enabled) {
    u_long mode = enabled ? 1 : 0;
    if (::ioctlsocket(sock_of(handle_), FIONBIO, &mode) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

bool socket::set_broadcast(bool enabled) {
    const int value = enabled ? 1 : 0;
    if (::setsockopt(sock_of(handle_), SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

bool socket::set_reuseaddr(bool enabled) {
    const int value = enabled ? 1 : 0;
    if (::setsockopt(sock_of(handle_), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    last_error_ = sock_error::none;
    return true;
}

std::size_t socket::bytes_available() {
    u_long count = 0;
    if (::ioctlsocket(sock_of(handle_), FIONREAD, &count) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return 0;
    }
    last_error_ = sock_error::none;
    return static_cast<std::size_t>(count);
}

bool socket::local_endpoint(endpoint& out) {
    sockaddr_in sa;
    int len = sizeof(sa);
    if (::getsockname(sock_of(handle_), reinterpret_cast<sockaddr*>(&sa), &len) != 0) {
        last_error_ = map_wsa(WSAGetLastError());
        return false;
    }
    out = endpoint_of(sa);
    last_error_ = sock_error::none;
    return true;
}

int poll_readable(const native_socket* handles, std::size_t count, int timeout_ms, bool* readable_out) {
    if (count > static_cast<std::size_t>(std::numeric_limits<ULONG>::max())) {
        return -1;
    }
    std::vector<WSAPOLLFD> fds(count);
    for (std::size_t i = 0; i < count; i++) {
        fds[i].fd = sock_of(handles[i]);
        fds[i].events = POLLRDNORM;
        fds[i].revents = 0;
        readable_out[i] = false;
    }
    const int ready = ::WSAPoll(fds.data(), static_cast<ULONG>(count), timeout_ms);
    if (ready <= 0) {
        return ready;
    }
    for (std::size_t i = 0; i < count; i++) {
        if ((fds[i].revents & (POLLRDNORM | POLLHUP | POLLERR)) != 0) {
            readable_out[i] = true;
        }
    }
    return ready;
}

} // namespace platform
} // namespace eosr
