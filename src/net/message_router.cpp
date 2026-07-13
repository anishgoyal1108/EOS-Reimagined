#include "net/message_router.h"

#include "common/byte_buffer.h"
#include "net/wire.h"
#include "platform/net_iface.h"

namespace eosr {

using namespace platform;

namespace {

const std::size_t stream_chunk = 4096;
const std::size_t udp_buffer_size = 4096;
const int self_pipe_accept_timeout_ms = 1000;
const int mesh_backlog = 16;

// Ten discovery slots, so up to ten instances on one machine can each hold one and still hear
// the others: this is what lets two copies of a game on the same PC find each other.
const u16 default_discovery_port_first = 55789;
const u16 default_discovery_port_last = 55798;

// How often we announce ourselves, and how long a silent peer stays in the mesh. The first
// advertisement goes out immediately, so discovery does not wait out an interval; the timeout is
// several intervals so a single lost datagram never drops a live peer.
const std::chrono::milliseconds advertise_interval(2000);
const std::chrono::milliseconds peer_timeout(10000);

} // namespace

net_config::net_config()
    : discovery_port_first(default_discovery_port_first),
      discovery_port_last(default_discovery_port_last) {
}

message_router::message_router() : mesh_port_(0), running_(false) {
}

message_router::~message_router() {
    stop();
}

void message_router::set_identity(const std::string& product_user_id, const std::string& game_id) {
    product_user_id_ = product_user_id;
    game_id_ = game_id;
}

void message_router::set_config(const net_config& config) {
    config_ = config;
}

bool message_router::open_discovery() {
    if (!udp_.open_udp()) {
        return false;
    }
    if (!udp_.set_broadcast(true)) {
        return false;
    }
    // Take the first free slot in the range; a slot already held by another local instance is not
    // an error, it just means that instance got there first, and we move to the next one. The
    // slot has to be exclusive for that to work, so we deliberately do not set SO_REUSEADDR here:
    // with it, a second instance would happily bind the same port and the two would then split
    // the datagrams sent to it instead of each holding its own slot.
    for (u32 port = config_.discovery_port_first; port <= config_.discovery_port_last; port++) {
        if (udp_.bind(endpoint(ip_any, static_cast<u16>(port)))) {
            return udp_.set_nonblocking(true);
        }
    }
    return false;
}

bool message_router::open_mesh() {
    if (!mesh_.open_tcp()) {
        return false;
    }
    if (!mesh_.set_reuseaddr(true)) {
        return false;
    }
    // The mesh port is ephemeral; peers learn it from our advertisement rather than guessing.
    if (!mesh_.bind(endpoint(ip_any, 0))) {
        return false;
    }
    endpoint local;
    if (!mesh_.local_endpoint(local)) {
        return false;
    }
    mesh_port_ = local.port;
    if (!mesh_.listen(mesh_backlog)) {
        return false;
    }
    return mesh_.set_nonblocking(true);
}

bool message_router::open_self_pipe() {
    socket listener;
    if (!listener.open_tcp() || !listener.set_reuseaddr(true)) {
        return false;
    }
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
    if (poll_readable(&listener_handle, 1, self_pipe_accept_timeout_ms, &ready) <= 0 || !ready) {
        return false;
    }
    endpoint peer_addr;
    if (!listener.accept(self_recv_, peer_addr)) {
        return false;
    }
    return self_recv_.set_nonblocking(true);
}

bool message_router::start() {
    if (running_) {
        return true;
    }
    // Recover cleanly if an earlier attempt only initialized some sockets.
    stop();

    if (!open_discovery() || !open_mesh() || !open_self_pipe()) {
        stop();
        return false;
    }

    // Announce ourselves on the first tick rather than waiting out an interval.
    last_advertise_ = std::chrono::steady_clock::time_point();
    running_ = true;
    return true;
}

void message_router::stop() {
    peers_.clear();
    self_send_.close();
    self_recv_.close();
    mesh_.close();
    udp_.close();
    self_buffer_.clear();
    mesh_port_ = 0;
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

std::vector<std::string> message_router::peer_ids() const {
    std::vector<std::string> out;
    std::map<std::string, peer>::const_iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        out.push_back(it->first);
    }
    return out;
}

bool message_router::send_framed(socket& sock, const std::vector<u8>& framed) {
    std::size_t sent = 0;
    while (sent < framed.size()) {
        const int count = sock.send(framed.data() + sent, framed.size() - sent);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool message_router::send(const net_envelope& msg) {
    if (!running_) {
        return false;
    }
    byte_writer writer;
    serialize(writer, msg);
    const std::vector<u8> framed = frame_message(writer.data());

    if (!msg.dest_id.empty()) {
        std::map<std::string, peer>::iterator it = peers_.find(msg.dest_id);
        if (it == peers_.end()) {
            return false;
        }
        if (!send_framed(it->second.connection, framed)) {
            // The peer's connection is gone; drop it so the interfaces learn it left.
            drop_peer(msg.dest_id, true);
            return false;
        }
        return true;
    }

    // A broadcast reaches every peer in the mesh. We collect the dead ones and drop them after
    // the loop so the traversal cannot be invalidated underneath us.
    std::vector<std::string> dead;
    std::map<std::string, peer>::iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        if (!send_framed(it->second.connection, framed)) {
            dead.push_back(it->first);
        }
    }
    for (std::size_t i = 0; i < dead.size(); i++) {
        drop_peer(dead[i], true);
    }
    return true;
}

bool message_router::send_to_self(const net_envelope& msg) {
    byte_writer writer;
    serialize(writer, msg);
    return send_framed(self_send_, frame_message(writer.data()));
}

void message_router::advertise() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - last_advertise_ < advertise_interval) {
        return;
    }
    last_advertise_ = now;

    net_advertise infos;
    infos.product_user_id = product_user_id_;
    infos.game_id = game_id_;
    infos.tcp_port = mesh_port_;
    byte_writer payload;
    serialize(payload, infos);

    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::net_advertise);
    envelope.source_id = product_user_id_;
    envelope.game_id = game_id_;
    envelope.payload = payload.data();
    byte_writer writer;
    serialize(writer, envelope);
    const std::vector<u8>& datagram = writer.data();

    std::vector<u32> targets = config_.broadcast_addresses;
    if (targets.empty()) {
        targets = broadcast_addresses();
    }
    // Reach every slot on every attached network, since we do not know which one a peer holds.
    for (std::size_t i = 0; i < targets.size(); i++) {
        for (u32 port = config_.discovery_port_first; port <= config_.discovery_port_last; port++) {
            udp_.send_to(datagram.data(), datagram.size(),
                         endpoint(targets[i], static_cast<u16>(port)));
        }
    }
}

void message_router::accept_peers() {
    // A peer dials our mesh listener after hearing our advertisement. We cannot name it yet, so
    // it waits in `pending_` until its first envelope tells us who it is.
    while (true) {
        socket incoming;
        endpoint from;
        if (!mesh_.accept(incoming, from)) {
            break;
        }
        if (!incoming.set_nonblocking(true)) {
            continue;
        }
        pending_peer entry;
        entry.connection = std::move(incoming);
        entry.accepted_at = std::chrono::steady_clock::now();
        pending_.push_back(std::move(entry));
    }
}

void message_router::drain_pending() {
    std::size_t i = 0;
    while (i < pending_.size()) {
        std::vector<net_envelope> messages;
        drain_stream(pending_[i].connection, pending_[i].buffer, messages);

        // The first envelope names the peer, which promotes the connection into the mesh.
        std::string id;
        for (std::size_t m = 0; m < messages.size(); m++) {
            if (!messages[m].source_id.empty()) {
                id = messages[m].source_id;
                break;
            }
        }
        const bool stale = std::chrono::steady_clock::now() - pending_[i].accepted_at > peer_timeout;
        if (id.empty()) {
            // Nothing identifying yet. Give up on a connection that never says who it is.
            if (stale) {
                pending_.erase(pending_.begin() + i);
            } else {
                i++;
            }
            continue;
        }

        socket connection = std::move(pending_[i].connection);
        std::vector<u8> leftover = pending_[i].buffer;
        pending_.erase(pending_.begin() + i);
        adopt_peer(id, std::move(connection));
        std::map<std::string, peer>::iterator it = peers_.find(id);
        if (it != peers_.end()) {
            it->second.buffer = leftover;
        }
        // Deliver what the peer already said, now that the interfaces know it exists.
        for (std::size_t m = 0; m < messages.size(); m++) {
            dispatch(messages[m]);
        }
    }
}

void message_router::adopt_peer(const std::string& id, socket connection) {
    if (id.empty() || id == product_user_id_) {
        return;
    }
    const bool known = peers_.find(id) != peers_.end();
    peer& entry = peers_[id];
    entry.connection = std::move(connection);
    entry.last_seen = std::chrono::steady_clock::now();
    if (!known) {
        dispatch_peer_event(message_type::peer_connected, id);
    }
}

void message_router::handle_advertise(const net_envelope& msg, const endpoint& from) {
    net_advertise infos;
    byte_reader reader(msg.payload.data(), msg.payload.size());
    if (!deserialize(reader, infos)) {
        return;
    }
    // Ignore ourselves, a malformed advertisement, and instances of a different game.
    if (infos.product_user_id.empty() || infos.product_user_id == product_user_id_) {
        return;
    }
    if (infos.game_id != game_id_ || infos.tcp_port == 0) {
        return;
    }

    std::map<std::string, peer>::iterator it = peers_.find(infos.product_user_id);
    if (it != peers_.end()) {
        it->second.last_seen = std::chrono::steady_clock::now();
        return;
    }

    // Both peers hear each other, so both would dial. We let the lower id dial and the higher id
    // accept, which leaves exactly one connection between any two peers.
    if (product_user_id_ > infos.product_user_id) {
        return;
    }

    socket connection;
    if (!connection.open_tcp()) {
        return;
    }
    // The peer advertised a moment ago, so this connect resolves promptly; we switch the socket
    // to non-blocking only once it is established.
    if (!connection.connect(endpoint(from.ip, infos.tcp_port))) {
        return;
    }
    if (!connection.set_nonblocking(true)) {
        return;
    }

    adopt_peer(infos.product_user_id, std::move(connection));

    // Name ourselves to the peer that just accepted us, so it can key the connection.
    net_advertise self;
    self.product_user_id = product_user_id_;
    self.game_id = game_id_;
    self.tcp_port = mesh_port_;
    byte_writer payload;
    serialize(payload, self);
    net_envelope hello;
    hello.type_tag = static_cast<u16>(message_type::net_advertise);
    hello.source_id = product_user_id_;
    hello.dest_id = infos.product_user_id;
    hello.game_id = game_id_;
    hello.payload = payload.data();
    send(hello);
}

void message_router::drop_peer(const std::string& id, bool notify) {
    std::map<std::string, peer>::iterator it = peers_.find(id);
    if (it == peers_.end()) {
        return;
    }
    peers_.erase(it);
    if (notify) {
        dispatch_peer_event(message_type::peer_disconnected, id);
    }
}

void message_router::expire_peers() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::vector<std::string> dead;
    std::map<std::string, peer>::iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        if (now - it->second.last_seen > peer_timeout) {
            dead.push_back(it->first);
        }
    }
    for (std::size_t i = 0; i < dead.size(); i++) {
        drop_peer(dead[i], true);
    }
}

void message_router::cb_run_frame() {
    if (!running_) {
        return;
    }
    advertise();
    accept_peers();
    drain_datagrams();
    drain_pending();

    // Drain each peer in turn. A peer whose connection died is dropped after the pass so the
    // traversal is never invalidated by an erase, and its envelopes are dispatched afterwards so
    // a listener cannot invalidate the peer table underneath us.
    const std::vector<std::string> ids = peer_ids();
    std::vector<net_envelope> messages;
    for (std::size_t i = 0; i < ids.size(); i++) {
        std::map<std::string, peer>::iterator it = peers_.find(ids[i]);
        if (it == peers_.end()) {
            continue;
        }
        const std::size_t before = messages.size();
        drain_stream(it->second.connection, it->second.buffer, messages);
        if (messages.size() > before) {
            it->second.last_seen = std::chrono::steady_clock::now();
        }
    }

    drain_stream(self_recv_, self_buffer_, messages);
    for (std::size_t i = 0; i < messages.size(); i++) {
        dispatch(messages[i]);
    }
    expire_peers();
}

bool message_router::decode_frames(std::vector<u8>& buffer, std::vector<net_envelope>& out) {
    // Refuse a connection that declares an absurd frame length rather than buffering it.
    if (buffer.size() >= 4) {
        const u32 declared = (static_cast<u32>(buffer[0]) << 24) |
                             (static_cast<u32>(buffer[1]) << 16) |
                             (static_cast<u32>(buffer[2]) << 8) |
                             static_cast<u32>(buffer[3]);
        if (declared > max_message_size) {
            buffer.clear();
            return false;
        }
    }

    std::size_t offset = 0;
    std::vector<u8> body;
    std::size_t consumed = 0;
    while (try_deframe(buffer.data() + offset, buffer.size() - offset, body, consumed)) {
        byte_reader reader(body.data(), body.size());
        net_envelope msg;
        if (deserialize(reader, msg)) {
            out.push_back(msg);
        }
        offset += consumed;
    }
    if (offset > 0) {
        buffer.erase(buffer.begin(), buffer.begin() + offset);
    }
    return true;
}

void message_router::drain_stream(socket& sock, std::vector<u8>& buffer,
                                  std::vector<net_envelope>& out) {
    u8 temp[stream_chunk];
    while (true) {
        const int count = sock.recv(temp, sizeof(temp));
        if (count <= 0) {
            break;
        }
        buffer.insert(buffer.end(), temp, temp + count);
    }
    decode_frames(buffer, out);
}

void message_router::drain_datagrams() {
    u8 temp[udp_buffer_size];
    while (true) {
        endpoint from;
        const int count = udp_.recv_from(temp, sizeof(temp), from);
        if (count <= 0) {
            break;
        }
        byte_reader reader(temp, static_cast<std::size_t>(count));
        net_envelope msg;
        if (!deserialize(reader, msg)) {
            continue;
        }
        // Discovery is the router's own business; everything else goes to the interfaces.
        if (msg.type_tag == static_cast<u16>(message_type::net_advertise)) {
            handle_advertise(msg, from);
            continue;
        }
        dispatch(msg);
    }
}

void message_router::dispatch_peer_event(message_type type, const std::string& peer_id) {
    net_envelope event;
    event.type_tag = static_cast<u16>(type);
    event.source_id = peer_id;
    event.game_id = game_id_;
    dispatch(event);
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
