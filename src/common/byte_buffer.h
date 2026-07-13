#ifndef EOSR_COMMON_BYTE_BUFFER_H
#define EOSR_COMMON_BYTE_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"

namespace eosr {

// Appends values to a growing buffer in our little-endian wire format. Integers wider than
// a byte are written explicitly byte by byte, so the encoding is the same on any host.
class byte_writer {
public:
    void put_u8(u8 value);
    void put_u16(u16 value);
    void put_u32(u32 value);
    void put_u64(u64 value);

    // Unsigned LEB128; put_svar zig-zags a signed value first so small magnitudes stay small.
    void put_var(u64 value);
    void put_svar(i64 value);

    void put_bool(bool value);

    // Doubles travel as their IEEE-754 bit pattern in a fixed u64, so no precision is lost.
    void put_f64(f64 value);

    // Length-prefixed (put_var length, then the raw bytes).
    void put_bytes(const u8* data, std::size_t len);
    void put_string(const std::string& value);

    const std::vector<u8>& data() const { return buf_; }
    std::size_t size() const { return buf_.size(); }

private:
    std::vector<u8> buf_;
};

// Reads values back with bounds checking. Every getter returns false and leaves the cursor
// untouched if the buffer is too short, so a truncated or malformed message can never read
// out of bounds.
class byte_reader {
public:
    byte_reader(const u8* data, std::size_t len);

    bool get_u8(u8& out);
    bool get_u16(u16& out);
    bool get_u32(u32& out);
    bool get_u64(u64& out);

    bool get_var(u64& out);
    bool get_svar(i64& out);

    bool get_bool(bool& out);
    bool get_f64(f64& out);

    bool get_bytes(std::vector<u8>& out);
    bool get_string(std::string& out);

    std::size_t remaining() const { return len_ - pos_; }
    bool at_end() const { return pos_ == len_; }

private:
    const u8* data_;
    std::size_t len_;
    std::size_t pos_;
};

} // namespace eosr

#endif
