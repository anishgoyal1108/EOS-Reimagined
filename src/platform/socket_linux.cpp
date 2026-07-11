#include "platform/socket.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace eosr {
namespace platform {

static int fd_of(native_socket handle) {
    return static_cast<int>(handle);
}

static sock_error map_errno(int code) {
    if (code == EWOULDBLOCK || code == EAGAIN) {
        return sock_error::would_block;
    }
    if (code == EINPROGRESS) {
        return sock_error::in_progress;
    }
    if (code == EISCONN) {
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
    return true;
}

void net_shutdown() {
}

socket::socket() : handle_(invalid_socket), last_error_(sock_error::none) {
}

socket::~socket() {
    close();
}

socket::socket(socket&& other) : handle_(other.handle_), last_error_(other.last_error_) {
    other.handle_ = invalid_socket;
}

socket& socket::operator=(socket&& other) {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        last_error_ = other.last_error_;
        other.handle_ = invalid_socket;
    }
    return *this;
}

bool socket::open_udp() {
    handle_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    return is_open();
}

bool socket::open_tcp() {
    handle_ = ::socket(AF_INET, SOCK_STREAM, 0);
    return is_open();
}

void socket::close() {
    if (is_open()) {
        ::close(fd_of(handle_));
        handle_ = invalid_socket;
    }
}

bool socket::bind(const endpoint& addr) {
    sockaddr_in sa;
    fill_sockaddr(sa, addr);
    if (::bind(fd_of(handle_), reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        last_error_ = map_errno(errno);
        return false;
    }
    return true;
}

bool socket::listen(int backlog) {
    return ::listen(fd_of(handle_), backlog) == 0;
}

bool socket::accept(socket& out, endpoint& peer) {
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    const int fd = ::accept(fd_of(handle_), reinterpret_cast<sockaddr*>(&sa), &len);
    if (fd < 0) {
        last_error_ = map_errno(errno);
        return false;
    }
    out.close();
    out.handle_ = fd;
    peer = endpoint_of(sa);
    return true;
}

bool socket::connect(const endpoint& addr) {
    sockaddr_in sa;
    fill_sockaddr(sa, addr);
    if (::connect(fd_of(handle_), reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        last_error_ = sock_error::is_connected;
        return true;
    }
    last_error_ = map_errno(errno);
    return last_error_ == sock_error::in_progress
        || last_error_ == sock_error::would_block
        || last_error_ == sock_error::is_connected;
}

int socket::send(const u8* data, std::size_t len) {
    const ssize_t count = ::send(fd_of(handle_), data, len, 0);
    if (count < 0) {
        last_error_ = map_errno(errno);
    }
    return static_cast<int>(count);
}

int socket::recv(u8* data, std::size_t len) {
    const ssize_t count = ::recv(fd_of(handle_), data, len, 0);
    if (count < 0) {
        last_error_ = map_errno(errno);
    }
    return static_cast<int>(count);
}

int socket::send_to(const u8* data, std::size_t len, const endpoint& to) {
    sockaddr_in sa;
    fill_sockaddr(sa, to);
    const ssize_t count =
        ::sendto(fd_of(handle_), data, len, 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (count < 0) {
        last_error_ = map_errno(errno);
    }
    return static_cast<int>(count);
}

int socket::recv_from(u8* data, std::size_t len, endpoint& from) {
    sockaddr_in sa;
    socklen_t slen = sizeof(sa);
    const ssize_t count =
        ::recvfrom(fd_of(handle_), data, len, 0, reinterpret_cast<sockaddr*>(&sa), &slen);
    if (count < 0) {
        last_error_ = map_errno(errno);
        return static_cast<int>(count);
    }
    from = endpoint_of(sa);
    return static_cast<int>(count);
}

bool socket::set_nonblocking(bool enabled) {
    int flags = fcntl(fd_of(handle_), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd_of(handle_), F_SETFL, flags) == 0;
}

bool socket::set_broadcast(bool enabled) {
    const int value = enabled ? 1 : 0;
    return setsockopt(fd_of(handle_), SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) == 0;
}

bool socket::set_reuseaddr(bool enabled) {
    const int value = enabled ? 1 : 0;
    return setsockopt(fd_of(handle_), SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == 0;
}

std::size_t socket::bytes_available() {
    int count = 0;
    if (ioctl(fd_of(handle_), FIONREAD, &count) != 0 || count < 0) {
        return 0;
    }
    return static_cast<std::size_t>(count);
}

bool socket::local_endpoint(endpoint& out) {
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (getsockname(fd_of(handle_), reinterpret_cast<sockaddr*>(&sa), &len) != 0) {
        return false;
    }
    out = endpoint_of(sa);
    return true;
}

int poll_readable(const native_socket* handles, std::size_t count, int timeout_ms, bool* readable_out) {
    std::vector<pollfd> fds(count);
    for (std::size_t i = 0; i < count; i++) {
        fds[i].fd = fd_of(handles[i]);
        fds[i].events = POLLIN;
        fds[i].revents = 0;
        readable_out[i] = false;
    }
    const int ready = ::poll(fds.data(), static_cast<nfds_t>(count), timeout_ms);
    if (ready <= 0) {
        return ready;
    }
    for (std::size_t i = 0; i < count; i++) {
        if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            readable_out[i] = true;
        }
    }
    return ready;
}

} // namespace platform
} // namespace eosr
