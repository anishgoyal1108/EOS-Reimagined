#include "core/peer_fp.h"

#include <vector>

#include "common/crypto.h"
#include "common/types.h"

namespace eosr {

namespace {

const char* const peer_domain = "eosr-trace-peer-v1"; // 18 bytes, the domain separator
const std::size_t product_user_id_len = 32;           // 32 lowercase-hex characters
const std::size_t fingerprint_bytes = 8;              // first 8 digest bytes -> 16 hex characters

bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

} // namespace

std::string peer_fingerprint(const std::string& product_user_id) {
    if (product_user_id.size() != product_user_id_len) {
        return std::string();
    }
    for (std::size_t i = 0; i < product_user_id.size(); i++) {
        if (!is_lower_hex(product_user_id[i])) {
            return std::string();
        }
    }
    // The domain bytes immediately followed by the id bytes: both fixed length, so the concatenation
    // is unambiguous with no separator.
    std::string input(peer_domain);
    input += product_user_id;

    const std::vector<u8> digest = sha256(reinterpret_cast<const u8*>(input.data()), input.size());
    static const char* const hex = "0123456789abcdef";
    std::string out;
    out.reserve(fingerprint_bytes * 2);
    for (std::size_t i = 0; i < fingerprint_bytes; i++) {
        const u8 byte = digest[i];
        out += hex[byte >> 4];
        out += hex[byte & 0x0f];
    }
    return out;
}

} // namespace eosr
