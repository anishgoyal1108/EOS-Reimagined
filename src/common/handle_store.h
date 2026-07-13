#ifndef EOSR_COMMON_HANDLE_STORE_H
#define EOSR_COMMON_HANDLE_STORE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

namespace eosr {

// A handle is a small integer minted from a single process-wide counter, handed back to the game
// as an opaque pointer. The counter never repeats and never reuses a value, which is what makes a
// handle safe to hand out: because no value is ever issued twice, a released handle, a
// double-release, and a handle minted by a different store all fail to find anything and are
// ignored rather than dereferenced. Sharing one counter across every store is what lets a handle
// from one store be rejected by another instead of colliding with an unrelated id.
inline std::uintptr_t next_handle_id() {
    // Starts at 1 so a null handle (0) is never a value we issued.
    static std::atomic<std::uintptr_t> counter(1);
    return counter.fetch_add(1);
}

// Hands the game an opaque handle to an object it later releases, and survives the ways a game
// gets that wrong. Releasing removes the object outright, so memory tracks the handles a game is
// actually holding -- a game that browses sessions all afternoon does not grow without bound.
template <class object_type>
class handle_store {
public:
    handle_store() {}

    handle_store(const handle_store&) = delete;
    handle_store& operator=(const handle_store&) = delete;

    // Take ownership of `object` and return the handle the game will hold.
    void* add(std::unique_ptr<object_type> object) {
        const std::uintptr_t id = next_handle_id();
        objects_[id] = std::move(object);
        return reinterpret_cast<void*>(id);
    }

    // The object behind `handle`, or null if we never issued it or it has been released.
    object_type* find(void* handle) const {
        typename map_type::const_iterator it =
            objects_.find(reinterpret_cast<std::uintptr_t>(handle));
        return (it != objects_.end()) ? it->second.get() : 0;
    }

    // Free the object behind `handle`. A handle we never issued, or one already released, finds
    // nothing to erase and is a no-op rather than a second free.
    void release(void* handle) {
        objects_.erase(reinterpret_cast<std::uintptr_t>(handle));
    }

    void clear() {
        objects_.clear();
    }

    std::size_t live_count() const {
        return objects_.size();
    }

private:
    typedef std::map<std::uintptr_t, std::unique_ptr<object_type> > map_type;
    map_type objects_;
};

} // namespace eosr

#endif
