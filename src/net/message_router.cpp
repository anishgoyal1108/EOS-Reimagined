#include "net/message_router.h"

#include "common/byte_buffer.h"
#include "net/wire.h"

namespace eosr {

using namespace platform;

static const std::size_t stream_chunk = 4096;
static const std::size_t udp_buffer_size = 4096;
static const int self_pipe_accept_timeout_ms = 1000;

message_router::message_router() : running_(false) {
}

message_router::~message_router() {
    stop();
}

bool message_router::start(u16 discovery_port) {
    if (running_) {
        return true;
    }
    // Recover cleanly if an earlier start attempt only initialized some sockets.
    stop();

    // Discovery socket: broadcast-capable and shareable so several instances can co-exist.
    if (!udp_.open_udp()) {
        return false;
    }
    if (!udp_.set_reuseaddr(true) || !udp_.set_broadcast(true)) {
        stop();
        return false;
    }
    if (!udp_.bind(endpoint(ip_any, discovery_port))) {
        stop();
        return false;
    }
    if (!udp_.set_nonblocking(true)) {
        stop();
        return false;
    }

    // Self-pipe: a loopback TCP connection whose two ends we keep, so locally-originated
    // messages travel the same decode path as messages from peers.
    socket listener;
    if (!listener.open_tcp()) {
        stop();
        return false;
    }
    if (!listener.set_reuseaddr(true)) {
        stop();
        return false;
    }
    if (!listener.bind(endpoint(ip_loopback, 0))) {
        stop();
        return false;
    }
    endpoint local;
    if (!listener.local_endpoint(local)) {
        stop();
        return false;
    }
    if (!listener.listen(1)) {
        stop();
        return false;
    }
    if (!self_send_.open_tcp()) {
        stop();
        return false;
    }
    if (!self_send_.connect(endpoint(ip_loopback, local.port))) {
        stop();
        return false;
    }
    bool ready = false;
    native_socket listener_handle = listener.native();
    if (poll_readable(&listener_handle, 1, self_pipe_accept_timeout_ms, &ready) <= 0 || !ready) {
        stop();
        return false;
    }
    endpoint peer;
    if (!listener.accept(self_recv_, peer)) {
        stop();
        return false;
    }
    if (!self_recv_.set_nonblocking(true)) {
        stop();
        return false;
    }

    running_ = true;
    return true;
}

void message_router::stop() {
    self_send_.close();
    self_recv_.close();
    udp_.close();
    self_buffer_.clear();
    running_ = false;
}

void message_router::register_listener(message_type type, i_run_network* listener) {
    if (listener == 0) {
        return;
    }
    std::vector<i_run_network*>& bucket = listeners_[type];
    for (std::size_t i = 0; i < bucket.size(); i++) {
        if (bucket[i] == listener) {
            return;
        }
    }
    bucket.push_back(listener);
}

void message_router::unregister_listener(message_type type, i_run_network* listener) {
    std::map<message_type, std::vector<i_run_network*>>::iterator found = listeners_.find(type);
    if (found == listeners_.end()) {
        return;
    }
    std::vector<i_run_network*>& bucket = found->second;
    for (std::size_t i = 0; i < bucket.size(); i++) {
        if (bucket[i] == listener) {
            bucket.erase(bucket.begin() + i);
            if (bucket.empty()) {
                listeners_.erase(found);
            }
            return;
        }
    }
}

bool message_router::send_to_self(const net_envelope& msg) {
    byte_writer writer;
    serialize(writer, msg);
    const std::vector<u8> framed = frame_message(writer.data());
    std::size_t sent = 0;
    while (sent < framed.size()) {
        const int count = self_send_.send(framed.data() + sent, framed.size() - sent);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

void message_router::cb_run_frame() {
    if (!running_) {
        return;
    }
    drain_stream(self_recv_, self_buffer_);
    drain_datagrams(udp_);
}

void message_router::drain_stream(socket& sock, std::vector<u8>& buffer) {
    u8 temp[stream_chunk];
    while (true) {
        const int count = sock.recv(temp, sizeof(temp));
        if (count <= 0) {
            break;
        }
        buffer.insert(buffer.end(), temp, temp + count);
    }

    // Drop the connection's buffer if a peer declares an absurd frame length.
    if (buffer.size() >= 4) {
        const u32 declared = (static_cast<u32>(buffer[0]) << 24) |
                             (static_cast<u32>(buffer[1]) << 16) |
                             (static_cast<u32>(buffer[2]) << 8) |
                             static_cast<u32>(buffer[3]);
        if (declared > max_message_size) {
            buffer.clear();
            return;
        }
    }

    std::size_t offset = 0;
    std::vector<u8> body;
    std::size_t consumed = 0;
    while (try_deframe(buffer.data() + offset, buffer.size() - offset, body, consumed)) {
        byte_reader reader(body.data(), body.size());
        net_envelope msg;
        if (deserialize(reader, msg)) {
            dispatch(msg);
        }
        offset += consumed;
    }
    if (offset > 0) {
        buffer.erase(buffer.begin(), buffer.begin() + offset);
    }
}

void message_router::drain_datagrams(socket& sock) {
    u8 temp[udp_buffer_size];
    while (true) {
        endpoint from;
        const int count = sock.recv_from(temp, sizeof(temp), from);
        if (count <= 0) {
            break;
        }
        byte_reader reader(temp, static_cast<std::size_t>(count));
        net_envelope msg;
        if (deserialize(reader, msg)) {
            dispatch(msg);
        }
    }
}

void message_router::dispatch(const net_envelope& msg) {
    const message_type type = static_cast<message_type>(msg.type_tag);
    std::map<message_type, std::vector<i_run_network*>>::iterator it = listeners_.find(type);
    if (it == listeners_.end()) {
        return;
    }
    // Callbacks may register or unregister listeners. Iterate a stable snapshot so those
    // mutations take effect on the next message and cannot invalidate this traversal.
    const std::vector<i_run_network*> listeners = it->second;
    for (std::size_t i = 0; i < listeners.size(); i++) {
        if (listeners[i] != 0) {
            listeners[i]->on_network_message(msg);
        }
    }
}

} // namespace eosr
