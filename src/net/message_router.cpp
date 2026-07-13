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

// How long we keep trying to reach a peer that answered our game before giving up on the dial.
const std::chrono::milliseconds dial_timeout(5000);

// A peer that will not take its bytes eventually has to go: if it lets this much pile up it is not
// keeping up with us, and buffering more would only postpone the same conclusion while growing
// without bound.
const std::size_t max_outbox_bytes = 4 * 1024 * 1024;

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
    // Every socket goes, including the connections we accepted but have not named yet and the
    // ones we are still dialing: leaving those open would let a stale frame arrive after a
    // restart, and would hold their descriptors for the life of the process.
    peers_.clear();
    pending_.clear();
    dialing_.clear();
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

std::size_t message_router::pending_output_bytes() const {
    std::size_t total = 0;
    std::map<std::string, peer>::const_iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        total += it->second.outbox.size();
    }
    return total;
}

bool message_router::flush_outbox(peer& entry) {
    while (!entry.outbox.empty()) {
        const int count = entry.connection.send(entry.outbox.data(), entry.outbox.size());
        if (count > 0) {
            entry.outbox.erase(entry.outbox.begin(), entry.outbox.begin() + count);
            continue;
        }
        // The kernel buffer is full. That is the peer reading slower than we are writing, not a
        // broken connection, so we keep what is left and try again as the socket drains.
        if (count < 0 && entry.connection.last_error() == sock_error::would_block) {
            return true;
        }
        return false;
    }
    return true;
}

bool message_router::queue_and_flush(peer& entry, const std::vector<u8>& framed) {
    if (entry.outbox.size() + framed.size() > max_outbox_bytes) {
        return false;
    }
    entry.outbox.insert(entry.outbox.end(), framed.begin(), framed.end());
    return flush_outbox(entry);
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
        if (!queue_and_flush(it->second, framed)) {
            // The connection is genuinely broken; drop it so the interfaces learn the peer left.
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
        if (!queue_and_flush(it->second, framed)) {
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
    const std::vector<u8> framed = frame_message(writer.data());
    std::size_t sent = 0;
    while (sent < framed.size()) {
        const int count = self_send_.send(framed.data() + sent, framed.size() - sent);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && self_send_.last_error() == sock_error::would_block) {
            continue; // the loopback pair drains immediately; keep offering the rest
        }
        return false;
    }
    return true;
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
        const stream_health health =
            drain_stream(pending_[i].connection, pending_[i].buffer, messages);

        // A peer joins the mesh only once it identifies itself with a proper handshake.
        const std::string id = identify_peer(messages);
        const bool stale = std::chrono::steady_clock::now() - pending_[i].accepted_at > peer_timeout;
        if (id.empty()) {
            // Not identified yet. Give up on one that hung up or never says who it is.
            if (health == stream_closed || stale) {
                pending_.erase(pending_.begin() + i);
            } else {
                i++;
            }
            continue;
        }

        socket connection = std::move(pending_[i].connection);
        std::vector<u8> leftover = pending_[i].buffer;
        pending_.erase(pending_.begin() + i);
        if (!adopt_peer(id, std::move(connection))) {
            continue; // a duplicate of a peer we already hold; the connection is dropped
        }
        std::map<std::string, peer>::iterator it = peers_.find(id);
        if (it != peers_.end()) {
            it->second.buffer = leftover;
        }
        // Deliver what the peer already said, now that the interfaces know it exists. The socket it
        // arrived on decides who it is from, not the field the sender wrote.
        for (std::size_t m = 0; m < messages.size(); m++) {
            messages[m].source_id = id;
            if (accept_inbound(messages[m])) {
                dispatch(messages[m]);
            }
        }
    }
}

bool message_router::adopt_peer(const std::string& id, socket connection) {
    if (id.empty() || id == product_user_id_) {
        return false;
    }
    if (peers_.find(id) != peers_.end()) {
        // Already connected. Refuse a second connection claiming this id rather than replacing the
        // live one -- replacing would let a fresh socket seize an established peer's identity. If
        // the existing connection is actually dead, it is dropped when its stream next reads closed,
        // and the peer's next advertisement dials a clean one. The refused socket closes here.
        return false;
    }
    peer& entry = peers_[id];
    entry.connection = std::move(connection);
    entry.last_seen = std::chrono::steady_clock::now();
    dispatch_peer_event(message_type::peer_connected, id);
    return true;
}

// A peer must identify itself before it joins the mesh, and it does so the one way we can check: a
// net_advertise naming itself, whose nested id matches the envelope it rode in and whose game is
// ours. Anything else as a first frame is not a handshake, and the connection is not adopted. This
// does not authenticate first contact -- identity on the mesh is self-asserted -- but it keeps a
// peer from being adopted under an id it never actually announced.
std::string message_router::identify_peer(const std::vector<net_envelope>& frames) const {
    for (std::size_t i = 0; i < frames.size(); i++) {
        if (frames[i].type_tag != static_cast<u16>(message_type::net_advertise)) {
            continue;
        }
        net_advertise infos;
        byte_reader reader(frames[i].payload.data(), frames[i].payload.size());
        if (!deserialize(reader, infos)) {
            return std::string();
        }
        if (infos.product_user_id.empty() || infos.product_user_id != frames[i].source_id) {
            return std::string(); // the envelope and the advertisement disagree on who this is
        }
        if (infos.game_id != game_id_ ||
            (!frames[i].game_id.empty() && frames[i].game_id != game_id_)) {
            return std::string(); // a different game, or an inconsistent one
        }
        return infos.product_user_id;
    }
    return std::string();
}

bool message_router::accept_inbound(const net_envelope& msg) const {
    // A frame for a different game, or addressed to a peer other than us, is not ours to deliver.
    if (!msg.game_id.empty() && msg.game_id != game_id_) {
        return false;
    }
    if (!msg.dest_id.empty() && msg.dest_id != product_user_id_) {
        return false;
    }
    return true;
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

    if (dialing_.find(infos.product_user_id) != dialing_.end()) {
        return; // already on our way there
    }

    socket connection;
    if (!connection.open_tcp()) {
        return;
    }
    // Non-blocking before we dial, so a peer whose advertised port is stale or filtered cannot
    // stall the game inside connect() for however long the OS takes to give up. The connect is
    // finished on a later tick instead.
    if (!connection.set_nonblocking(true)) {
        return;
    }

    dialing_peer dial;
    dial.address = endpoint(from.ip, infos.tcp_port);
    dial.started_at = std::chrono::steady_clock::now();
    dial.connection = std::move(connection);
    const bool done = dial.connection.connect(dial.address) &&
                      dial.connection.last_error() != sock_error::would_block &&
                      dial.connection.last_error() != sock_error::in_progress;
    if (done) {
        adopt_peer(infos.product_user_id, std::move(dial.connection));
        announce_to(infos.product_user_id);
        return;
    }
    dialing_[infos.product_user_id] = std::move(dial);
}

void message_router::finish_dialing() {
    std::vector<std::string> done;
    std::vector<std::string> failed;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    std::map<std::string, dialing_peer>::iterator it = dialing_.begin();
    for (; it != dialing_.end(); ++it) {
        // Offering the address again is how a non-blocking connect reports itself finished: once
        // it lands, the socket answers that it is already connected.
        const bool ok = it->second.connection.connect(it->second.address);
        const sock_error error = it->second.connection.last_error();
        if (ok && (error == sock_error::none || error == sock_error::is_connected)) {
            done.push_back(it->first);
        } else if (!ok && error != sock_error::would_block && error != sock_error::in_progress) {
            failed.push_back(it->first);
        } else if (now - it->second.started_at > dial_timeout) {
            failed.push_back(it->first);
        }
    }

    for (std::size_t i = 0; i < failed.size(); i++) {
        dialing_.erase(failed[i]); // the peer's next advertisement starts a fresh attempt
    }
    for (std::size_t i = 0; i < done.size(); i++) {
        std::map<std::string, dialing_peer>::iterator entry = dialing_.find(done[i]);
        socket connection = std::move(entry->second.connection);
        dialing_.erase(entry);
        // Only name ourselves to a peer we actually adopted; a connection we refused as a duplicate
        // is already closed.
        if (adopt_peer(done[i], std::move(connection))) {
            announce_to(done[i]);
        }
    }
}

// Name ourselves to a peer that just accepted our connection, so it can key it to us.
void message_router::announce_to(const std::string& id) {
    net_advertise self;
    self.product_user_id = product_user_id_;
    self.game_id = game_id_;
    self.tcp_port = mesh_port_;
    byte_writer payload;
    serialize(payload, self);

    net_envelope hello;
    hello.type_tag = static_cast<u16>(message_type::net_advertise);
    hello.source_id = product_user_id_;
    hello.dest_id = id;
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
    finish_dialing();
    drain_datagrams();
    drain_pending();

    // Drain each peer in turn, and push out anything a full send buffer made us hold back. A peer
    // whose connection died is dropped after the pass so the traversal is never invalidated by an
    // erase, and its envelopes are dispatched afterwards so a listener cannot invalidate the peer
    // table underneath us.
    const std::vector<std::string> ids = peer_ids();
    std::vector<net_envelope> messages;
    std::vector<std::string> dead;
    for (std::size_t i = 0; i < ids.size(); i++) {
        std::map<std::string, peer>::iterator it = peers_.find(ids[i]);
        if (it == peers_.end()) {
            continue;
        }
        std::vector<net_envelope> from_peer;
        const stream_health health =
            drain_stream(it->second.connection, it->second.buffer, from_peer);
        if (!from_peer.empty()) {
            it->second.last_seen = std::chrono::steady_clock::now();
        }
        // The socket a frame arrived on is who it is from -- not the source_id the sender wrote.
        // Stamping the peer's own id here is what lets every ownership check downstream rest on the
        // connection instead of an unauthenticated claim, so a peer can no longer speak as another.
        for (std::size_t m = 0; m < from_peer.size(); m++) {
            from_peer[m].source_id = ids[i];
            if (accept_inbound(from_peer[m])) {
                messages.push_back(from_peer[m]);
            }
        }
        if (health == stream_closed || !flush_outbox(it->second)) {
            dead.push_back(ids[i]);
        }
    }
    for (std::size_t i = 0; i < dead.size(); i++) {
        drop_peer(dead[i], true);
    }

    // The self-pipe is our own trusted loopback; its frames already carry our id and pass straight
    // through.
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

message_router::stream_health message_router::drain_stream(socket& sock, std::vector<u8>& buffer,
                                                           std::vector<net_envelope>& out) {
    stream_health health = stream_alive;
    u8 temp[stream_chunk];
    while (true) {
        const int count = sock.recv(temp, sizeof(temp));
        if (count > 0) {
            buffer.insert(buffer.end(), temp, temp + count);
            continue;
        }
        // Zero is an orderly hangup, and any error other than "nothing to read yet" means the
        // connection is finished. Either way the peer is gone and we should not wait out its
        // advertisement timeout to notice.
        if (count == 0 || sock.last_error() != sock_error::would_block) {
            health = stream_closed;
        }
        break;
    }
    if (!decode_frames(buffer, out)) {
        health = stream_closed; // it sent a frame we refuse to buffer
    }
    return health;
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
