#ifndef EOSR_COMMON_HANDLE_STORE_H
#define EOSR_COMMON_HANDLE_STORE_H

#include <cstddef>
#include <deque>
#include <map>
#include <memory>

namespace eosr {

// Hands the game an opaque handle to an object it later releases, and survives the ways a game
// gets that wrong.
//
// The handle is a pointer to a slot, not to the object. Releasing frees the object but keeps the
// slot, so a slot's address is never handed out twice. That is what makes a stale handle safe: a
// release of one we already freed, or of a pointer we never issued, finds no live object and does
// nothing, instead of freeing whatever happens to live at that address by then. Using a released
// handle is likewise rejected rather than dereferenced.
//
// What accumulates is only the slots, and the objects behind them — a search's results, a
// session's attributes — are freed on release, so a game that browses sessions all afternoon does
// not grow without bound.
template <class object_type>
class handle_store {
public:
    handle_store() {}

    handle_store(const handle_store&) = delete;
    handle_store& operator=(const handle_store&) = delete;

    // Take ownership of `object` and return the handle the game will hold.
    void* add(std::unique_ptr<object_type> object) {
        // A deque never moves an element already in it, so the slot's address is stable for as
        // long as this store lives. That stability is the whole point.
        slots_.push_back(slot());
        slot* entry = &slots_.back();
        entry->object = std::move(object);
        index_[entry] = entry;
        return entry;
    }

    // The object behind `handle`, or null if we never issued it or it has been released.
    object_type* find(void* handle) const {
        const slot* entry = lookup(handle);
        return (entry != 0) ? entry->object.get() : 0;
    }

    // Free the object behind `handle`. A handle we never issued, or one already released, is a
    // no-op rather than a second free.
    void release(void* handle) {
        typename std::map<const void*, slot*>::iterator found = index_.find(handle);
        if (found != index_.end()) {
            found->second->object.reset();
        }
    }

    // Free every live object, keeping the slots so their addresses stay spent.
    void clear() {
        typename std::map<const void*, slot*>::iterator it = index_.begin();
        for (; it != index_.end(); ++it) {
            it->second->object.reset();
        }
    }

    std::size_t live_count() const {
        std::size_t live = 0;
        typename std::map<const void*, slot*>::const_iterator it = index_.begin();
        for (; it != index_.end(); ++it) {
            if (it->second->object) {
                live++;
            }
        }
        return live;
    }

private:
    struct slot {
        std::unique_ptr<object_type> object;
    };

    const slot* lookup(const void* handle) const {
        typename std::map<const void*, slot*>::const_iterator found = index_.find(handle);
        return (found != index_.end()) ? found->second : 0;
    }

    std::deque<slot> slots_;
    // Only a pointer we issued may be dereferenced, so a handle is looked up here before we ever
    // treat it as a slot.
    std::map<const void*, slot*> index_;
};

} // namespace eosr

#endif
