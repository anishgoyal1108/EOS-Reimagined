#include "core/label_registry.h"

namespace eosr {

namespace {

const char* kind_prefix(label_kind kind) {
    switch (kind) {
        case label_kind::puid: return "puid";
        case label_kind::eaid: return "eaid";
        case label_kind::account: return "account";
        case label_kind::session: return "session";
        case label_kind::lobby: return "lobby";
        case label_kind::socket: return "socket";
        case label_kind::handle: return "handle";
        case label_kind::notif: return "notif";
    }
    return "handle";
}

} // namespace

label_registry::label_registry(std::size_t max_per_kind)
    : max_per_kind_(max_per_kind > 0 ? max_per_kind : 1), clock_(0) {}

label_registry::bucket& label_registry::bucket_for(label_kind kind) {
    return buckets_[static_cast<int>(kind)];
}

const label_registry::bucket* label_registry::find_bucket(label_kind kind) const {
    std::map<int, bucket>::const_iterator it = buckets_.find(static_cast<int>(kind));
    return (it == buckets_.end()) ? 0 : &it->second;
}

std::string label_registry::label(label_kind kind, const std::string& token) {
    if (token.empty()) {
        return std::string(); // no id, no label -- never a fabricated one
    }
    bucket& b = bucket_for(kind);
    clock_++;

    std::map<std::string, entry>::iterator it = b.tokens.find(token);
    if (it != b.tokens.end()) {
        it->second.last_seen = clock_;
        return it->second.text;
    }

    if (b.tokens.size() >= max_per_kind_) {
        // At the cap: drop the coldest token of this kind. The counter does not rewind, so the label
        // it held is retired with it and can never name a different object later.
        std::map<std::string, entry>::iterator coldest = b.tokens.begin();
        for (std::map<std::string, entry>::iterator i = b.tokens.begin(); i != b.tokens.end(); ++i) {
            if (i->second.last_seen < coldest->second.last_seen) {
                coldest = i;
            }
        }
        b.tokens.erase(coldest);
    }

    entry fresh;
    fresh.text = std::string(kind_prefix(kind)) + "#" + std::to_string(b.next_index++);
    fresh.last_seen = clock_;
    b.tokens[token] = fresh;
    return fresh.text;
}

void label_registry::clear() {
    buckets_.clear();
    clock_ = 0;
}

std::size_t label_registry::size(label_kind kind) const {
    const bucket* b = find_bucket(kind);
    return (b == 0) ? 0 : b->tokens.size();
}

} // namespace eosr
