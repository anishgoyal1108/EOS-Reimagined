#ifndef EOSR_COMMON_IDS_H
#define EOSR_COMMON_IDS_H

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "eos_common.h"

// Concrete definitions of the opaque id handles the SDK only forward-declares. A handle
// is a pointer to one of these; the string is fixed once interned.
struct EOS_EpicAccountIdDetails {
    std::string id_str;
    bool valid = false;
};

struct EOS_ProductUserIdDetails {
    std::string id_str;
    bool valid = false;
};

namespace eosr {

// A well-formed id is this many hex characters; the all-zero id is the null sentinel.
constexpr std::size_t id_hex_length = 32;

// True when id_str is exactly id_hex_length hex digits and not the all-zero null id.
bool id_string_is_valid(const std::string& id_str);

// Interns id strings so a given string always resolves to the same handle pointer, which
// is what lets the flat API validate a handle by identity. The registry owns every handle.
// Spec: EOSSDK_Client id maps (wiki/internals/client.md §2)
class id_registry {
public:
    static id_registry& instance();

    id_registry(const id_registry&) = delete;
    id_registry& operator=(const id_registry&) = delete;

    EOS_EpicAccountId get_epic_account_id(const std::string& id_str);
    EOS_ProductUserId get_product_user_id(const std::string& id_str);

private:
    id_registry() = default;

    // Look up an interned id or create and store it, returning the stable handle pointer.
    template<class Details>
    Details* intern(std::map<std::string, std::unique_ptr<Details>>& table, const std::string& id_str) {
        std::lock_guard<std::mutex> lock(mutex_);
        typename std::map<std::string, std::unique_ptr<Details>>::iterator it = table.find(id_str);
        if (it != table.end()) {
            return it->second.get();
        }
        std::unique_ptr<Details> details(new Details());
        details->id_str = id_str;
        // EOS_*_FromString performs no format validation, and EOS_*_IsValid returns true for
        // any handle it produced. Only a null handle is invalid, so an interned id is valid.
        details->valid = true;
        Details* handle = details.get();
        table.emplace(id_str, std::move(details));
        return handle;
    }

    std::map<std::string, std::unique_ptr<EOS_EpicAccountIdDetails>> epic_ids_;
    std::map<std::string, std::unique_ptr<EOS_ProductUserIdDetails>> product_ids_;
    std::mutex mutex_;
};

} // namespace eosr

#endif
