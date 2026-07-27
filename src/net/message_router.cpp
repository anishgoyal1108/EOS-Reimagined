#include "net/message_router.h"

#include "core/label_registry.h"
#include "core/peer_fp.h"
#include "core/runtime.h"
#include "core/trace_event.h"
#include "core/tracer.h"

#include <cstring>

#include "common/byte_buffer.h"
#include "common/crypto.h"
#include "common/log.h"
#include "net/wire.h"
#include "platform/net_iface.h"

namespace eosr {

namespace {

void net_record(const std::string& event, const std::vector<trace_field>& fields,
                bool failure = false) {
    global_tracer().record_net(event, fields, failure);
}

trace_field peer_field(const std::string& peer_id) {
    return make_field(field_id::peer,
                      tv_label(global_tracer().label(label_kind::puid, peer_id)));
}

trace_field peer_fp_field(const std::string& peer_id) {
    return make_field(field_id::peer_fp, tv_fingerprint(peer_fingerprint(peer_id)));
}

void net_reason(const std::string& event, const std::string& peer_id, const char* reason,
                bool proved, bool failure = false) {
    tracer& trace = global_tracer();
    if (!trace.enabled()) {
        return;
    }
    std::vector<trace_field> fields;
    if (!peer_id.empty()) {
        fields.push_back(peer_field(peer_id));
        if (proved) {
            fields.push_back(peer_fp_field(peer_id));
        }
    }
    fields.push_back(make_field(field_id::reason, tv_enum(reason)));
    net_record(event, fields, failure);
}

} // namespace

using namespace platform;

namespace {

const std::size_t stream_chunk = 4096;
const std::size_t udp_buffer_size = 4096;
const int self_pipe_accept_timeout_ms = 1000;
const int mesh_backlog = 16;
const std::size_t frame_prefix_len = 4;

// What a datagram is. Discovery is in the clear and believed about nothing; P2P data is sealed and
// is the only thing on this socket that carries any weight. The byte is what tells them apart, so
// neither has to be guessed at from its shape.
const u8 datagram_discovery = 0x01;
const u8 datagram_p2p = 0x02;
// [kind][u32 tag][u64 sequence], then the sealed body.
const std::size_t datagram_header_len = 1 + 4 + 8;

// Ten discovery slots, so up to ten instances on one machine can each hold one and still hear
// the others: this is what lets two copies of a game on the same PC find each other.
const u16 default_discovery_port_first = 55789;
const u16 default_discovery_port_last = 55798;

// How often we announce ourselves, and how long a silent peer stays in the mesh. The first
// advertisement goes out immediately, so discovery does not wait out an interval; the timeout is
// several intervals so a single lost datagram never drops a live peer.
const std::chrono::milliseconds advertise_interval(2000);
const std::chrono::milliseconds peer_timeout(10000);

// How long we keep trying to reach a peer that answered our game, and how long a peer has to finish
// proving who it is before we stop holding a socket open for it.
const std::chrono::milliseconds dial_timeout(5000);
const std::chrono::milliseconds handshake_timeout(5000);

// A peer that will not take its bytes eventually has to go: if it lets this much pile up it is not
// keeping up with us, and buffering more would only postpone the same conclusion while growing
// without bound.
const std::size_t max_outbox_bytes = 4 * 1024 * 1024;

void write_be32(u8 out[4], u32 value) {
    out[0] = static_cast<u8>((value >> 24) & 0xff);
    out[1] = static_cast<u8>((value >> 16) & 0xff);
    out[2] = static_cast<u8>((value >> 8) & 0xff);
    out[3] = static_cast<u8>(value & 0xff);
}

u32 read_be32(const u8* data) {
    return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
           (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
}

void write_be64(u8 out[8], u64 value) {
    for (int i = 0; i < 8; i++) {
        out[i] = static_cast<u8>((value >> (56 - 8 * i)) & 0xff);
    }
}

u64 read_be64(const u8* data) {
    u64 value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

} // namespace

net_config::net_config()
    : discovery_port_first(default_discovery_port_first),
      discovery_port_last(default_discovery_port_last) {
}

message_router::message_router()
    : have_profile_(false), discovery_port_(0), mesh_port_(0), datagrams_sent_(0),
      datagrams_received_(0), running_(false) {
    std::memset(static_priv_, 0, sizeof(static_priv_));
    std::memset(static_pub_, 0, sizeof(static_pub_));
}

message_router::~message_router() {
    stop();
    secure_wipe(static_priv_, sizeof(static_priv_));
}

void message_router::set_identity(const identity& profile, const std::string& product_id,
                                  const std::string& sandbox_id,
                                  const std::string& deployment_id) {
    game_id_ = product_id;
    sandbox_id_ = sandbox_id;
    deployment_id_ = deployment_id;
    have_profile_ = profile.has_key();
    if (!have_profile_) {
        product_user_id_.clear();
        return;
    }
    std::memcpy(static_priv_, profile.secret_key(), profile_key_len);
    std::memcpy(static_pub_, profile.public_key(), profile_key_len);
    // We derive our own id by the same formula every peer will apply to the key we prove to it, so
    // there is no other id we could answer to and none we could be talked into.
    product_user_id_ = id_of_key(static_pub_);
    prologue_ = mesh_prologue(product_id, sandbox_id, deployment_id);
}

std::string message_router::id_of_key(const u8* public_key) const {
    return derive_product_user_id(public_key, game_id_, sandbox_id_, deployment_id_);
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
            discovery_port_ = static_cast<u16>(port);
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

    // Without a key there is nothing to prove and nothing a peer could verify, so there is no mesh
    // to be had. Better to say so than to run a network nobody can trust.
    if (!have_profile_) {
        net_reason("listen", std::string(), "no_profile", false, true);
        log_error("net: no profile key, so no peer could be authenticated; the mesh stays down");
        return false;
    }
    if (!open_discovery()) {
        net_reason("listen", std::string(), "discovery_failed", false, true);
        stop();
        return false;
    }
    if (!open_mesh()) {
        net_reason("listen", std::string(), "mesh_failed", false, true);
        stop();
        return false;
    }
    if (!open_self_pipe()) {
        net_reason("listen", std::string(), "loopback_failed", false, true);
        stop();
        return false;
    }

    // Announce ourselves on the first tick rather than waiting out an interval.
    last_advertise_ = std::chrono::steady_clock::time_point();
    running_ = true;

    if (global_tracer().enabled()) {
        // Which discovery slot we took, out of the range we searched: two copies of one game on one
        // machine must land on different ones, and a trace that shows them on the same one explains
        // instantly why they never met.
        std::vector<trace_field> fields;
        fields.push_back(make_field(field_id::port, tv_uint(discovery_port_)));
        fields.push_back(make_field(field_id::port_first, tv_uint(config_.discovery_port_first)));
        fields.push_back(make_field(field_id::port_last, tv_uint(config_.discovery_port_last)));
        fields.push_back(make_field(field_id::count, tv_uint(config_.peer_seed_addresses.size())));
        net_record("listen", fields);
    }
    return true;
}

void message_router::stop() {
    // Every socket goes, including the connections still proving who they are and the ones we are
    // still dialing: leaving those open would let a stale frame arrive after a restart, and would
    // hold their descriptors for the life of the process.
    const std::vector<std::string> connected = peer_ids();
    for (std::size_t i = 0; i < connected.size(); i++) {
        drop_peer(connected[i], false, "local_shutdown");
    }
    handshaking_.clear();
    dialing_.clear();
    self_send_.close();
    self_recv_.close();
    mesh_.close();
    udp_.close();
    self_buffer_.clear();
    mesh_port_ = 0;
    discovery_port_ = 0;
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

std::string message_router::peer_epic_id(const std::string& peer_id) const {
    std::map<std::string, peer>::const_iterator it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second.epic_id : std::string();
}

std::size_t message_router::pending_output_bytes() const {
    std::size_t total = 0;
    std::map<std::string, peer>::const_iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        total += it->second.outbox.size();
    }
    return total;
}

bool message_router::flush_outbox(socket& connection, std::vector<u8>& outbox) {
    while (!outbox.empty()) {
        const int count = connection.send(outbox.data(), outbox.size());
        if (count > 0) {
            outbox.erase(outbox.begin(), outbox.begin() + count);
            continue;
        }
        // The kernel buffer is full. That is the peer reading slower than we are writing, not a
        // broken connection, so we keep what is left and try again as the socket drains.
        if (count < 0 && connection.last_error() == sock_error::would_block) {
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
    return flush_outbox(entry.connection, entry.outbox);
}

bool message_router::seal_frame(peer_channel& channel, const std::vector<u8>& plain,
                                std::vector<u8>& framed) const {
    const std::size_t sealed_len = plain.size() + aead_tag_len;
    if (sealed_len > max_message_size) {
        return false;
    }
    u8 prefix[frame_prefix_len];
    write_be32(prefix, static_cast<u32>(sealed_len));
    // The length rides along as associated data, so a frame cannot be re-cut to a different length
    // without failing its tag.
    std::vector<u8> sealed;
    if (!channel.seal(plain, prefix, sizeof(prefix), sealed)) {
        return false;
    }
    framed.clear();
    framed.reserve(sizeof(prefix) + sealed.size());
    framed.insert(framed.end(), prefix, prefix + sizeof(prefix));
    framed.insert(framed.end(), sealed.begin(), sealed.end());
    return true;
}

bool message_router::send(const net_envelope& msg) {
    if (!running_) {
        return false;
    }
    byte_writer writer;
    serialize(writer, msg);
    const std::vector<u8>& plain = writer.data();

    if (!msg.dest_id.empty()) {
        std::map<std::string, peer>::iterator it = peers_.find(msg.dest_id);
        if (it == peers_.end()) {
            return false;
        }
        std::vector<u8> framed;
        if (!seal_frame(*it->second.channel, plain, framed) ||
            !queue_and_flush(it->second, framed)) {
            // The connection is broken, or its counter is spent; either way it cannot carry another
            // frame. Drop it so the interfaces learn the peer left.
            drop_peer(msg.dest_id, true, "send_failed");
            return false;
        }
        return true;
    }

    // A broadcast reaches every peer in the mesh, but each gets its own sealed copy: they hold
    // different keys and their own counters, so one set of bytes could not open at two of them. We
    // collect the dead ones and drop them after the loop so the traversal cannot be invalidated
    // underneath us.
    std::vector<std::string> dead;
    std::map<std::string, peer>::iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        std::vector<u8> framed;
        if (!seal_frame(*it->second.channel, plain, framed) ||
            !queue_and_flush(it->second, framed)) {
            dead.push_back(it->first);
        }
    }
    for (std::size_t i = 0; i < dead.size(); i++) {
        drop_peer(dead[i], true, "send_failed");
    }
    return true;
}

bool message_router::send_to_self(const net_envelope& msg) {
    // The self-pipe is our own loopback, not a peer: there is nobody to authenticate to and nothing
    // to hide from, so it stays in the clear and simply reuses the dispatch path.
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

// The message that says who we are, where our mesh listens, and where our datagrams should be sent.
net_envelope message_router::self_advertisement() const {
    net_advertise infos;
    infos.product_user_id = product_user_id_;
    infos.game_id = game_id_;
    infos.tcp_port = mesh_port_;
    infos.udp_port = discovery_port_;
    byte_writer payload;
    serialize(payload, infos);

    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::net_advertise);
    envelope.source_id = product_user_id_;
    envelope.game_id = game_id_;
    envelope.payload = payload.data();
    return envelope;
}

void message_router::announce_to(const std::string& id) {
    net_envelope hello = self_advertisement();
    hello.dest_id = id;
    send(hello);
}

void message_router::advertise() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - last_advertise_ < advertise_interval) {
        return;
    }
    last_advertise_ = now;

    const net_envelope envelope = self_advertisement();
    byte_writer writer;
    serialize(writer, envelope);

    // Broadcast, this is in the clear, and it is a hint about where to look and nothing more.
    std::vector<u8> datagram;
    datagram.push_back(datagram_discovery);
    datagram.insert(datagram.end(), writer.data().begin(), writer.data().end());

    std::vector<u32> targets = config_.broadcast_addresses;
    if (targets.empty()) {
        targets = broadcast_addresses();
    }
    for (std::size_t i = 0; i < config_.peer_seed_addresses.size(); i++) {
        const u32 seed = config_.peer_seed_addresses[i];
        bool duplicate = false;
        for (std::size_t k = 0; k < targets.size(); k++) {
            if (targets[k] == seed) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            targets.push_back(seed);
        }
    }
    // Reach every slot on every attached network, since we do not know which one a peer holds.
    for (std::size_t i = 0; i < targets.size(); i++) {
        for (u32 port = config_.discovery_port_first; port <= config_.discovery_port_last; port++) {
            udp_.send_to(datagram.data(), datagram.size(),
                         endpoint(targets[i], static_cast<u16>(port)));
        }
    }

    // The same announcement goes to every established peer over the sealed mesh, where it means
    // something. That is what keeps a quiet peer alive: liveness rests on a frame the peer's key
    // sealed, never on a broadcast anyone could have sent, so nobody can keep a wedged connection
    // from timing out by shouting at us.
    send(envelope);
}

bool message_router::send_datagram(const std::string& peer_id, const std::vector<u8>& payload) {
    if (!running_) {
        return false;
    }
    std::map<std::string, peer>::iterator it = peers_.find(peer_id);
    if (it == peers_.end() || it->second.datagram_addr.port == 0 || !it->second.channel) {
        return false; // no peer, or it has not yet told us where its datagrams go
    }
    u64 seq = 0;
    std::vector<u8> sealed;
    if (!it->second.channel->seal_datagram(product_user_id_, peer_id, payload, seq, sealed)) {
        return false;
    }

    std::vector<u8> datagram;
    datagram.reserve(datagram_header_len + sealed.size());
    datagram.push_back(datagram_p2p);
    u8 header[12];
    write_be32(header, it->second.channel->udp_send_tag());
    write_be64(header + 4, seq);
    datagram.insert(datagram.end(), header, header + sizeof(header));
    datagram.insert(datagram.end(), sealed.begin(), sealed.end());

    // A datagram that the socket will not take is a datagram that was lost, which is exactly what
    // the caller signed up for. We do not queue it, because holding it would be the head-of-line
    // blocking they asked us to avoid.
    if (udp_.send_to(datagram.data(), datagram.size(), it->second.datagram_addr) <= 0) {
        return false;
    }
    datagrams_sent_++;
    return true;
}

void message_router::handle_datagram(const u8* data, std::size_t len) {
    if (len < datagram_header_len + aead_tag_len) {
        return;
    }
    const u32 tag = read_be32(data + 1);
    const u64 seq = read_be64(data + 5);

    // The tag says whose key this was sealed with. Only the two ends hold that key, so finding the
    // peer by it is not trusting anybody: a datagram we cannot then open is simply not from them.
    std::map<std::string, peer>::iterator it = peers_.begin();
    for (; it != peers_.end(); ++it) {
        if (it->second.channel && it->second.channel->udp_recv_tag() == tag) {
            break;
        }
    }
    if (it == peers_.end()) {
        return;
    }

    std::vector<u8> plain;
    if (!it->second.channel->open_datagram(it->first, product_user_id_, seq,
                                           data + datagram_header_len,
                                           len - datagram_header_len, plain)) {
        // It did not authenticate, or we have already had it. Either way it is dropped, and quietly:
        // unlike the mesh, an unreliable path has no session to tear down, and a peer that can send
        // us rubbish over UDP must not be able to end a working connection by doing so.
        return;
    }

    // From here it is the same message the mesh would have carried. Who it is from is the key that
    // opened it, never anything inside it.
    net_envelope msg;
    msg.type_tag = static_cast<u16>(message_type::p2p_data);
    msg.source_id = it->first;
    msg.dest_id = product_user_id_;
    msg.game_id = game_id_;
    msg.payload = plain;
    it->second.last_seen = std::chrono::steady_clock::now();
    datagrams_received_++;
    dispatch(msg);
}

// A peer's sealed advertisement is the only thing that tells us where to aim a datagram at it. The
// port comes from a frame its key sealed; the address comes from the connection that key
// authenticated. Neither is anyone else's to redirect.
void message_router::learn_datagram_port(const std::string& peer_id, const net_envelope& msg) {
    net_advertise infos;
    byte_reader reader(msg.payload.data(), msg.payload.size());
    if (!deserialize(reader, infos) || infos.udp_port == 0) {
        return;
    }
    std::map<std::string, peer>::iterator it = peers_.find(peer_id);
    if (it != peers_.end()) {
        it->second.datagram_addr.port = infos.udp_port;
    }
}

void message_router::accept_peers() {
    // A peer dials our mesh listener after hearing our advertisement. It is nobody yet: it has to
    // prove which key it holds before it is a peer, so it goes straight into a handshake.
    while (true) {
        socket incoming;
        endpoint from;
        if (!mesh_.accept(incoming, from)) {
            break;
        }
        if (!incoming.set_nonblocking(true)) {
            continue;
        }
        handshaking_peer entry;
        entry.connection = std::move(incoming);
        entry.channel.reset(new peer_channel(false, static_priv_, static_pub_, prologue_));
        entry.remote = from;
        entry.started_at = std::chrono::steady_clock::now();
        handshaking_.push_back(std::move(entry));
        net_reason("handshake", std::string(), "responder_started", false);
    }
}

void message_router::finish_dialing() {
    std::vector<std::string> done;
    std::vector<std::string> failed;
    std::vector<const char*> failure_reasons;
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
            failure_reasons.push_back("dial_failed");
        } else if (now - it->second.started_at > dial_timeout) {
            failed.push_back(it->first);
            failure_reasons.push_back("timeout");
        }
    }

    for (std::size_t i = 0; i < failed.size(); i++) {
        net_reason("handshake", failed[i], failure_reasons[i], false, true);
        dialing_.erase(failed[i]); // the peer's next advertisement starts a fresh attempt
    }
    for (std::size_t i = 0; i < done.size(); i++) {
        std::map<std::string, dialing_peer>::iterator entry = dialing_.find(done[i]);
        socket connection = std::move(entry->second.connection);
        const endpoint address = entry->second.address;
        dialing_.erase(entry);

        // The connection is up, and now the peer has to say who it is. We speak first: the dialer
        // is the Noise initiator.
        handshaking_peer shaking;
        shaking.connection = std::move(connection);
        shaking.channel.reset(new peer_channel(true, static_priv_, static_pub_, prologue_));
        shaking.expected_id = done[i];
        shaking.remote = address;
        shaking.started_at = now;
        net_reason("handshake", done[i], "initiator_started", false);

        std::vector<u8> opening;
        if (!shaking.channel->open(opening)) {
            net_reason("handshake", done[i], "random_failed", false, true);
            continue; // no secure randomness; we must not proceed with a guessable ephemeral
        }
        const std::vector<u8> framed = frame_message(opening);
        shaking.outbox.insert(shaking.outbox.end(), framed.begin(), framed.end());
        if (!flush_outbox(shaking.connection, shaking.outbox)) {
            net_reason("handshake", done[i], "send_failed", false, true);
            continue; // it died between connecting and being spoken to
        }
        handshaking_.push_back(std::move(shaking));
    }
}

void message_router::drain_handshaking() {
    std::size_t i = 0;
    while (i < handshaking_.size()) {
        handshaking_peer& entry = handshaking_[i];

        bool failed = !flush_outbox(entry.connection, entry.outbox);
        const char* failure_reason = failed ? "send_failed" : 0;
        const stream_health health =
            failed ? stream_closed : read_stream(entry.connection, entry.buffer);

        // One frame at a time, and we stop the moment the handshake completes: a peer may well
        // pipeline its first sealed frame behind its last handshake message, and that frame is not
        // a handshake message. It stays in the buffer for the established path to open.
        std::string proved;
        while (!failed && !entry.channel->established()) {
            std::vector<u8> message;
            const frame_state state = take_frame(entry.buffer, message);
            if (state == frame_refused) {
                failed = true;
                failure_reason = "frame_too_large";
                break;
            }
            if (state == frame_none) {
                break;
            }
            std::vector<u8> reply;
            const peer_channel::step result =
                entry.channel->read_handshake(message.data(), message.size(), reply);
            if (result == peer_channel::step_failed) {
                failed = true;
                failure_reason = "authentication_failed";
                break;
            }
            if (!reply.empty()) {
                const std::vector<u8> framed = frame_message(reply);
                entry.outbox.insert(entry.outbox.end(), framed.begin(), framed.end());
                if (!flush_outbox(entry.connection, entry.outbox)) {
                    failed = true;
                    failure_reason = "send_failed";
                    break;
                }
            }
            if (result == peer_channel::step_done) {
                proved = id_of_key(entry.channel->remote_static());
            }
        }

        // The advertisement said who would answer here. The key says who actually did. If they
        // disagree, the advertisement was not telling the truth about who lives at this address,
        // and we want no part of the connection -- we do not quietly adopt whoever turned up.
        if (!failed && !proved.empty() && !entry.expected_id.empty() &&
            entry.expected_id != proved) {
            log_warn("net: the peer that answered is not the one that was advertised");
            failed = true;
            failure_reason = "identity_mismatch";
        }

        // A peer that hung up is gone, and a valid proof arriving in the same read does not bring it
        // back: a closed socket cannot carry a frame. Adopting one would announce a peer that is
        // already unreachable -- every interface would take it onto a roster, and then take it off
        // again on the very next tick when the dead stream reads closed. So the EOF decides,
        // whether or not the handshake finished first.
        if (failed || health == stream_closed) {
            net_reason("handshake", entry.expected_id,
                       failure_reason != 0 ? failure_reason : "stream_closed", false, true);
            handshaking_.erase(handshaking_.begin() + i);
            continue;
        }
        if (proved.empty()) {
            i++; // still shaking hands
            continue;
        }

        // Take everything out of the entry before it is erased, so the peer inherits the bytes that
        // arrived behind the handshake and any reply the socket would not take yet.
        socket connection = std::move(entry.connection);
        std::unique_ptr<peer_channel> channel = std::move(entry.channel);
        const endpoint remote = entry.remote;
        const std::vector<u8> leftover = entry.buffer;
        const std::vector<u8> unsent = entry.outbox;
        handshaking_.erase(handshaking_.begin() + i);
        net_reason("handshake", proved, "complete", true);
        adopt_peer(proved, std::move(connection), std::move(channel), remote, leftover, unsent);
    }
}

bool message_router::adopt_peer(const std::string& id, socket connection,
                                std::unique_ptr<peer_channel> channel, const endpoint& remote,
                                const std::vector<u8>& leftover, const std::vector<u8>& unsent) {
    if (id.empty() || id == product_user_id_ || !channel) {
        net_reason("handshake", id, "invalid_identity", !id.empty(), true);
        return false;
    }
    if (peers_.find(id) != peers_.end()) {
        // Already connected. Refuse a second connection under this id rather than replacing the live
        // one: even a peer that does hold the key must not be able to displace its own established
        // session, or a reconnect race would tear down a working one. If the existing connection is
        // actually dead, it is dropped when its stream next reads closed, and the peer's next
        // advertisement dials a clean one. The refused socket closes here.
        net_reason("handshake", id, "duplicate_peer", true, true);
        return false;
    }
    peer& entry = peers_[id];
    entry.connection = std::move(connection);
    entry.channel = std::move(channel);
    // The epic account id comes from the same key as the product user id, so it is as unforgeable as
    // the one we just adopted this peer under. An interface that keys on it never has to take a
    // peer's word for it.
    entry.epic_id = derive_epic_account_id(entry.channel->remote_static());
    entry.buffer = leftover;
    entry.outbox = unsent;
    // Datagrams go to the address whose handshake we just authenticated -- nobody without the key
    // could have been at the other end of that connection. The port waits for the peer to tell us,
    // over the sealed mesh, which it does as soon as it hears from us.
    entry.datagram_addr = endpoint(remote.ip, 0);
    entry.last_seen = std::chrono::steady_clock::now();

    if (global_tracer().enabled()) {
        // The id is recomputed from the key the handshake proved, so this one is real -- and it is the
        // first point at which a fingerprint means anything, which is what lets two traces be joined.
        std::vector<trace_field> fields;
        fields.push_back(peer_field(id));
        fields.push_back(peer_fp_field(id));
        net_record("adopt", fields);
    }

    announce_to(id);
    dispatch_peer_event(message_type::peer_connected, id);
    return true;
}

bool message_router::is_connecting_to(const std::string& id) const {
    if (dialing_.find(id) != dialing_.end()) {
        return true;
    }
    for (std::size_t i = 0; i < handshaking_.size(); i++) {
        if (handshaking_[i].expected_id == id) {
            return true;
        }
    }
    return false;
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

    // An advertisement is a hint about where to look and nothing more. It cannot refresh a peer's
    // liveness -- that rests on frames the peer's key sealed -- and it cannot introduce anyone: all
    // it does is start a handshake that will decide for itself who is there.
    if (peers_.find(infos.product_user_id) != peers_.end()) {
        return;
    }

    // Both peers hear each other, so both would dial. We let the lower id dial and the higher id
    // accept, which leaves exactly one connection between any two peers.
    if (product_user_id_ > infos.product_user_id) {
        return;
    }
    if (is_connecting_to(infos.product_user_id)) {
        return; // already on our way there
    }

    if (global_tracer().enabled()) {
        std::vector<trace_field> fields;
        fields.push_back(peer_field(infos.product_user_id));
        fields.push_back(make_field(field_id::port, tv_uint(infos.tcp_port)));
        net_record("discover", fields);
    }

    socket connection;
    if (!connection.open_tcp()) {
        net_reason("handshake", infos.product_user_id, "dial_failed", false, true);
        return;
    }
    // Non-blocking before we dial, so a peer whose advertised port is stale or filtered cannot
    // stall the game inside connect() for however long the OS takes to give up. The connect is
    // finished on a later tick instead.
    if (!connection.set_nonblocking(true)) {
        net_reason("handshake", infos.product_user_id, "dial_failed", false, true);
        return;
    }

    dialing_peer dial;
    dial.address = endpoint(from.ip, infos.tcp_port);
    dial.started_at = std::chrono::steady_clock::now();
    dial.connection = std::move(connection);
    if (!dial.connection.connect(dial.address)) {
        net_reason("handshake", infos.product_user_id, "dial_failed", false, true);
        return;
    }
    dialing_[infos.product_user_id] = std::move(dial);
}

void message_router::drop_peer(const std::string& id, bool notify, const char* reason) {
    std::map<std::string, peer>::iterator it = peers_.find(id);
    if (it == peers_.end()) {
        return;
    }
    peers_.erase(it);

    if (global_tracer().enabled()) {
        std::vector<trace_field> fields;
        fields.push_back(peer_field(id));
        fields.push_back(peer_fp_field(id));
        fields.push_back(make_field(field_id::reason, tv_enum(reason)));
        net_record("drop", fields, std::strcmp(reason, "local_shutdown") != 0);
    }

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
        drop_peer(dead[i], true, "timeout");
    }
}

void message_router::expire_handshaking() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::size_t i = 0;
    while (i < handshaking_.size()) {
        if (now - handshaking_[i].started_at > handshake_timeout) {
            net_reason("handshake", handshaking_[i].expected_id, "timeout", false, true);
            handshaking_.erase(handshaking_.begin() + i);
            continue;
        }
        i++;
    }
}

void message_router::drain_peers() {
    // Drain each peer in turn, and push out anything a full send buffer made us hold back. A peer
    // whose connection died is dropped after the pass so the traversal is never invalidated by an
    // erase, and its envelopes are dispatched afterwards so a listener cannot invalidate the peer
    // table underneath us.
    const std::vector<std::string> ids = peer_ids();
    std::vector<net_envelope> messages;
    std::vector<std::string> dead;
    std::vector<const char*> drop_reasons;
    for (std::size_t i = 0; i < ids.size(); i++) {
        std::map<std::string, peer>::iterator it = peers_.find(ids[i]);
        if (it == peers_.end()) {
            continue;
        }
        peer& entry = it->second;
        const stream_health health = read_stream(entry.connection, entry.buffer);

        bool broken = false;
        while (true) {
            std::vector<u8> sealed;
            const frame_state state = take_frame(entry.buffer, sealed);
            if (state == frame_refused) {
                broken = true;
                drop_reasons.push_back("frame_too_large");
                break;
            }
            if (state == frame_none) {
                break;
            }
            u8 prefix[frame_prefix_len];
            write_be32(prefix, static_cast<u32>(sealed.size()));
            std::vector<u8> plain;
            if (!entry.channel->unseal(sealed.data(), sealed.size(), prefix, sizeof(prefix),
                                       plain)) {
                // The tag did not verify. That is a forged, tampered, replayed, or reordered frame,
                // and there is no way to resynchronize a counter -- the connection ends here.
                log_warn("net: a frame from a peer failed to authenticate; dropping the connection");
                broken = true;
                drop_reasons.push_back("authentication_failed");
                break;
            }
            byte_reader reader(plain.data(), plain.size());
            net_envelope msg;
            if (!deserialize(reader, msg)) {
                continue; // it opened, so the peer sent it; we simply cannot read it
            }
            // The key that opened this frame is who it is from -- not the source_id the sender
            // wrote. Every ownership check downstream rests on that, so a peer cannot speak as
            // another even if it says it is.
            msg.source_id = ids[i];
            entry.last_seen = std::chrono::steady_clock::now();
            if (msg.type_tag == static_cast<u16>(message_type::net_advertise)) {
                // Sealed, this is the peer telling us where to aim its datagrams. It is also the
                // keepalive, and it is not for the interfaces.
                learn_datagram_port(ids[i], msg);
                continue;
            }
            if (accept_inbound(msg)) {
                messages.push_back(msg);
            }
        }

        if (broken) {
            dead.push_back(ids[i]);
        } else if (health == stream_closed) {
            dead.push_back(ids[i]);
            drop_reasons.push_back("stream_closed");
        } else if (!flush_outbox(entry.connection, entry.outbox)) {
            dead.push_back(ids[i]);
            drop_reasons.push_back("send_failed");
        }
    }
    for (std::size_t i = 0; i < dead.size(); i++) {
        drop_peer(dead[i], true, drop_reasons[i]);
    }

    // The self-pipe is our own trusted loopback; its frames are in the clear, already carry our id,
    // and pass straight through.
    read_stream(self_recv_, self_buffer_);
    while (true) {
        std::vector<u8> body;
        const frame_state state = take_frame(self_buffer_, body);
        if (state != frame_ready) {
            break;
        }
        byte_reader reader(body.data(), body.size());
        net_envelope msg;
        if (deserialize(reader, msg)) {
            messages.push_back(msg);
        }
    }

    for (std::size_t i = 0; i < messages.size(); i++) {
        dispatch(messages[i]);
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
    drain_handshaking();
    drain_peers();
    expire_handshaking();
    expire_peers();
}

message_router::frame_state message_router::take_frame(std::vector<u8>& buffer,
                                                       std::vector<u8>& body) const {
    // Refuse a connection that declares an absurd frame length rather than buffering it.
    if (buffer.size() >= frame_prefix_len) {
        const u32 declared = (static_cast<u32>(buffer[0]) << 24) |
                             (static_cast<u32>(buffer[1]) << 16) |
                             (static_cast<u32>(buffer[2]) << 8) |
                             static_cast<u32>(buffer[3]);
        if (declared > max_message_size) {
            buffer.clear();
            return frame_refused;
        }
    }
    std::size_t consumed = 0;
    if (!try_deframe(buffer.data(), buffer.size(), body, consumed)) {
        return frame_none;
    }
    buffer.erase(buffer.begin(), buffer.begin() + consumed);
    return frame_ready;
}

message_router::stream_health message_router::read_stream(socket& sock, std::vector<u8>& buffer) {
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
            return stream_closed;
        }
        return stream_alive;
    }
}

void message_router::drain_datagrams() {
    u8 temp[udp_buffer_size];
    while (true) {
        endpoint from;
        const int count = udp_.recv_from(temp, sizeof(temp), from);
        if (count <= 0) {
            break;
        }
        const std::size_t len = static_cast<std::size_t>(count);
        if (len < 1) {
            continue;
        }

        // A sealed datagram is P2P gameplay data, and the key that opens it is the only thing that
        // says who sent it.
        if (temp[0] == datagram_p2p) {
            handle_datagram(temp, len);
            continue;
        }
        if (temp[0] != datagram_discovery) {
            continue;
        }

        // Discovery is the only thing that arrives here in the clear, and it is believed about
        // nothing: all it can do is start a handshake that decides for itself who is there.
        byte_reader reader(temp + 1, len - 1);
        net_envelope msg;
        if (!deserialize(reader, msg)) {
            continue;
        }
        if (msg.type_tag == static_cast<u16>(message_type::net_advertise)) {
            handle_advertise(msg, from);
        }
    }
}

void message_router::dispatch_peer_event(message_type type, const std::string& peer_id) {
    net_envelope event;
    event.type_tag = static_cast<u16>(type);
    event.source_id = peer_id;
    event.game_id = game_id_;
    // A peer arrives carrying the epic account id its key derives. An interface that keys on one --
    // Presence does, and so do Friends and UserInfo -- learns it here, from the key we authenticated,
    // rather than from a payload the peer wrote later and could have written anything into.
    const std::string epic_id = peer_epic_id(peer_id);
    event.payload.assign(epic_id.begin(), epic_id.end());
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
