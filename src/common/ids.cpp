#include "common/ids.h"

namespace eosr {

static bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool id_string_is_valid(const std::string& id_str) {
    if (id_str.size() != id_hex_length) {
        return false;
    }
    bool all_zero = true;
    for (std::size_t i = 0; i < id_str.size(); i++) {
        if (!is_hex_char(id_str[i])) {
            return false;
        }
        if (id_str[i] != '0') {
            all_zero = false;
        }
    }
    return !all_zero;
}

id_registry& id_registry::instance() {
    static id_registry inst;
    return inst;
}

EOS_EpicAccountId id_registry::get_epic_account_id(const std::string& id_str) {
    return intern(epic_ids_, id_str);
}

EOS_ProductUserId id_registry::get_product_user_id(const std::string& id_str) {
    return intern(product_ids_, id_str);
}

} // namespace eosr
