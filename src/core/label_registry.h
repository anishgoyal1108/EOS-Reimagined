#ifndef EOSR_CORE_LABEL_REGISTRY_H
#define EOSR_CORE_LABEL_REGISTRY_H

#include <map>
#include <string>

#include "common/types.h"

namespace eosr {

// What a label names. The prefix is the enum's own name, so a label reads as `puid#3` / `session#7`.
enum class label_kind {
    puid,
    eaid,
    account,
    session,
    lobby,
    socket,
    handle,
    notif
};

// Opaque, stable labels for the handles and ids the trace must never carry raw. Each distinct token
// of a kind gets `<kind>#<n>` from a per-kind counter on first sight and keeps it, so one object is
// followable through its whole life without a pointer or a real id ever reaching the file.
//
// The registry is bounded: an all-day session that browses thousands of lobbies cannot grow it without
// end, so at the cap the least-recently-seen entry of that kind is evicted. The counter never rewinds,
// so an evicted token seen again simply gets a fresh label -- two live objects can never share one.
// Spec: wiki/internals/alpha-tracing.md §4 (labels), §6 (bounded registries).
class label_registry {
public:
    explicit label_registry(std::size_t max_per_kind = 4096);

    // The label for `token`, minting one on first sight. An empty token has no label (the empty
    // string), so a missing id yields no field rather than a made-up one.
    std::string label(label_kind kind, const std::string& token);

    // Drop everything and rewind the counters: a new run starts with fresh labels.
    void clear();

    // How many tokens of `kind` are currently held -- the bound, observable.
    std::size_t size(label_kind kind) const;

private:
    struct entry {
        std::string text;   // the label itself, e.g. "puid#3"
        u64 last_seen;      // for eviction: the smallest is the coldest
    };
    struct bucket {
        std::map<std::string, entry> tokens;
        u32 next_index;     // never rewinds within a run, so a label is never reused
        bucket() : next_index(0) {}
    };

    bucket& bucket_for(label_kind kind);
    const bucket* find_bucket(label_kind kind) const;

    std::map<int, bucket> buckets_;
    std::size_t max_per_kind_;
    u64 clock_;             // a monotonic tick stamped on every touch, for recency
};

} // namespace eosr

#endif
