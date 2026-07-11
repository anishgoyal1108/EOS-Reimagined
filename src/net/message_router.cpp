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

    // Discovery socket: broadcast-capable and shareable so several instances can co-exist.
    if (!udp_.open_udp()) {
        return false;
    }
    udp_.set_reuseaddr(true);
    udp_.set_broadcast(true);
    if (!udp_.bind(endpoint(ip_any, discovery_port))) {
        return false;
    }
    udp_.set_nonblocking(true);

    // Self-pipe: a loopback TCP connection whose two ends we keep, so locally-originated
    // messages travel the same decode path as messages from peers.
    socket listener;
    if (!listener.open_tcp()) {
        return false;
    }
    listener.set_reuseaddr(true);
    if (!listener.bind(endpoint(ip_loopback, 0))) {
        return false;
    }
    endpoint local;
    if (!listener.local_endpoint(local)) {
        return false;
    }
    if (!listener.listen(1)) {
        return false;
    }
    if (!self_send_.open_tcp()) {
        return false;
    }
    if (!self_send_.connect(endpoint(ip_loopback, local.port))) {
        return false;
    }
    bool ready = false;
    native_socket listener_handle = listener.native();
    poll_readable(&listener_handle, 1, self_pipe_accept_timeout_ms, &ready);
    endpoint peer;
    if (!listener.accept(self_recv_, peer)) {
        return false;
    }
    self_recv_.set_nonblocking(true);

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
    listeners_[static_cast<u16>(type)].push_back(listener);
}

void message_router::unregister_listener(message_type type, i_run_network* listener) {
    std::vector<i_run_network*>& bucket = listeners_[static_cast<u16>(type)];
    for (std::size_t i = 0; i < bucket.size(); i++) {
        if (bucket[i] == listener) {
            bucket.erase(bucket.begin() + i);
            return;
        }
    }
}

bool message_router::send_to_self(const net_envelope& msg) {
    byte_writer writer;
    serialize(writer, msg);
    const std::vector<u8> framed = frame_message(writer.data());
    return self_send_.send(framed.data(), framed.size()) == static_cast<int>(framed.size());
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
    std::map<u16, std::vector<i_run_network*>>::iterator it = listeners_.find(msg.type_tag);
    if (it == listeners_.end()) {
        return;
    }
    for (std::size_t i = 0; i < it->second.size(); i++) {
        it->second[i]->on_network_message(msg);
    }
}

} // namespace eosr
