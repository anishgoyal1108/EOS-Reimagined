#include "manager/sha256.h"

#include <cstring>

namespace eosr {
namespace manager {

namespace {

unsigned int rotate_right(unsigned int value, unsigned int bits) {
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

sha256_hasher::sha256_hasher()
    : buffer_size_(0), byte_count_(0), finalized_(false) {
    const unsigned int initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::memcpy(state_, initial, sizeof(state_));
    std::memset(buffer_, 0, sizeof(buffer_));
}

void sha256_hasher::compress(const unsigned char block[64]) {
    static const unsigned int constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    unsigned int words[64];
    for (unsigned int i = 0; i < 16; i++) {
        words[i] = (static_cast<unsigned int>(block[i * 4]) << 24) |
                   (static_cast<unsigned int>(block[i * 4 + 1]) << 16) |
                   (static_cast<unsigned int>(block[i * 4 + 2]) << 8) |
                   static_cast<unsigned int>(block[i * 4 + 3]);
    }
    for (unsigned int i = 16; i < 64; i++) {
        const unsigned int s0 = rotate_right(words[i - 15], 7) ^
                                rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const unsigned int s1 = rotate_right(words[i - 2], 17) ^
                                rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    unsigned int a = state_[0];
    unsigned int b = state_[1];
    unsigned int c = state_[2];
    unsigned int d = state_[3];
    unsigned int e = state_[4];
    unsigned int f = state_[5];
    unsigned int g = state_[6];
    unsigned int h = state_[7];
    for (unsigned int i = 0; i < 64; i++) {
        const unsigned int sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const unsigned int choose = (e & f) ^ (~e & g);
        const unsigned int first = h + sum1 + choose + constants[i] + words[i];
        const unsigned int sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const unsigned int majority = (a & b) ^ (a & c) ^ (b & c);
        const unsigned int second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void sha256_hasher::update(const unsigned char* data, std::size_t size) {
    if (finalized_ || (data == 0 && size != 0)) {
        return;
    }
    byte_count_ += static_cast<unsigned long long>(size);
    while (size != 0) {
        const std::size_t space = sizeof(buffer_) - buffer_size_;
        const std::size_t count = size < space ? size : space;
        std::memcpy(buffer_ + buffer_size_, data, count);
        buffer_size_ += count;
        data += count;
        size -= count;
        if (buffer_size_ == sizeof(buffer_)) {
            compress(buffer_);
            buffer_size_ = 0;
        }
    }
}

void sha256_hasher::update(const std::string& data) {
    update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

std::string sha256_hasher::final_hex() {
    if (finalized_) {
        return std::string();
    }
    const unsigned long long bit_count = byte_count_ * 8;
    buffer_[buffer_size_++] = 0x80;
    if (buffer_size_ > 56) {
        std::memset(buffer_ + buffer_size_, 0, sizeof(buffer_) - buffer_size_);
        compress(buffer_);
        buffer_size_ = 0;
    }
    std::memset(buffer_ + buffer_size_, 0, 56 - buffer_size_);
    for (int i = 0; i < 8; i++) {
        buffer_[56 + i] = static_cast<unsigned char>((bit_count >> (56 - i * 8)) & 0xff);
    }
    compress(buffer_);
    finalized_ = true;

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::size_t i = 0; i < 8; i++) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            const unsigned char byte = static_cast<unsigned char>((state_[i] >> shift) & 0xff);
            out += hex[byte >> 4];
            out += hex[byte & 0x0f];
        }
    }
    return out;
}

std::string sha256_hex(const unsigned char* data, std::size_t size) {
    sha256_hasher hasher;
    hasher.update(data, size);
    return hasher.final_hex();
}

std::string sha256_hex(const std::string& data) {
    return sha256_hex(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

} // namespace manager
} // namespace eosr
