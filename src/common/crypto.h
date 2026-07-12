#ifndef EOSR_COMMON_CRYPTO_H
#define EOSR_COMMON_CRYPTO_H

#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// SHA-256 of `data`, returned as 32 raw bytes. A from-scratch implementation of FIPS 180-4.
std::vector<u8> sha256(const u8* data, std::size_t len);

// HMAC-SHA256 of `message` under `key`, returned as 32 raw bytes (RFC 2104).
std::vector<u8> hmac_sha256(const u8* key, std::size_t key_len, const u8* message, std::size_t message_len);

// base64url without padding (RFC 7515), used for JWT segments.
std::string base64url_encode(const u8* data, std::size_t len);
std::string base64url_encode(const std::string& text);

} // namespace eosr

#endif
