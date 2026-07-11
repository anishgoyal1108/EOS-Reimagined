#include "common/byte_buffer.h"

namespace eosr {

static const u32 byte_bits = 8;

// LEB128 constants.
static const u8 varint_more_bit = 0x80;
static const u8 varint_payload_mask = 0x7f;
static const u32 varint_group_bits = 7;
static const u32 varint_max_bits = 64;

void byte_writer::put_u8(u8 value) {
    buf_.push_back(value);
}

void byte_writer::put_u16(u16 value) {
    for (u32 i = 0; i < sizeof(u16); i++) {
        buf_.push_back(static_cast<u8>(value >> (i * byte_bits)));
    }
}

void byte_writer::put_u32(u32 value) {
    for (u32 i = 0; i < sizeof(u32); i++) {
        buf_.push_back(static_cast<u8>(value >> (i * byte_bits)));
    }
}

void byte_writer::put_u64(u64 value) {
    for (u32 i = 0; i < sizeof(u64); i++) {
        buf_.push_back(static_cast<u8>(value >> (i * byte_bits)));
    }
}

void byte_writer::put_var(u64 value) {
    while (value >= varint_more_bit) {
        buf_.push_back(static_cast<u8>(value) | varint_more_bit);
        value >>= varint_group_bits;
    }
    buf_.push_back(static_cast<u8>(value));
}

void byte_writer::put_svar(i64 value) {
    // Zig-zag without relying on implementation-defined signed right shift.
    const u64 magnitude = static_cast<u64>(value) << 1;
    const u64 sign_fill = (value < 0) ? ~static_cast<u64>(0) : static_cast<u64>(0);
    put_var(magnitude ^ sign_fill);
}

void byte_writer::put_bool(bool value) {
    buf_.push_back(value ? 1 : 0);
}

void byte_writer::put_bytes(const u8* data, std::size_t len) {
    put_var(static_cast<u64>(len));
    buf_.insert(buf_.end(), data, data + len);
}

void byte_writer::put_string(const std::string& value) {
    put_var(static_cast<u64>(value.size()));
    buf_.insert(buf_.end(), value.begin(), value.end());
}

byte_reader::byte_reader(const u8* data, std::size_t len)
    : data_(data)
    , len_(len)
    , pos_(0) {
}

bool byte_reader::get_u8(u8& out) {
    if (remaining() < sizeof(u8)) {
        return false;
    }
    out = data_[pos_];
    pos_ += sizeof(u8);
    return true;
}

bool byte_reader::get_u16(u16& out) {
    if (remaining() < sizeof(u16)) {
        return false;
    }
    u16 value = 0;
    for (u32 i = 0; i < sizeof(u16); i++) {
        value = static_cast<u16>(value | (static_cast<u16>(data_[pos_ + i]) << (i * byte_bits)));
    }
    pos_ += sizeof(u16);
    out = value;
    return true;
}

bool byte_reader::get_u32(u32& out) {
    if (remaining() < sizeof(u32)) {
        return false;
    }
    u32 value = 0;
    for (u32 i = 0; i < sizeof(u32); i++) {
        value |= static_cast<u32>(data_[pos_ + i]) << (i * byte_bits);
    }
    pos_ += sizeof(u32);
    out = value;
    return true;
}

bool byte_reader::get_u64(u64& out) {
    if (remaining() < sizeof(u64)) {
        return false;
    }
    u64 value = 0;
    for (u32 i = 0; i < sizeof(u64); i++) {
        value |= static_cast<u64>(data_[pos_ + i]) << (i * byte_bits);
    }
    pos_ += sizeof(u64);
    out = value;
    return true;
}

bool byte_reader::get_var(u64& out) {
    const std::size_t start = pos_;
    u64 value = 0;
    u32 shift = 0;
    while (shift < varint_max_bits) {
        if (pos_ >= len_) {
            pos_ = start;
            return false;
        }
        const u8 byte = data_[pos_];
        pos_++;
        value |= static_cast<u64>(byte & varint_payload_mask) << shift;
        if ((byte & varint_more_bit) == 0) {
            out = value;
            return true;
        }
        shift += varint_group_bits;
    }
    pos_ = start;
    return false;
}

bool byte_reader::get_svar(i64& out) {
    u64 zig = 0;
    if (!get_var(zig)) {
        return false;
    }
    const u64 sign_fill = ~(zig & 1) + 1;
    out = static_cast<i64>((zig >> 1) ^ sign_fill);
    return true;
}

bool byte_reader::get_bool(bool& out) {
    u8 value = 0;
    if (!get_u8(value)) {
        return false;
    }
    out = (value != 0);
    return true;
}

bool byte_reader::get_bytes(std::vector<u8>& out) {
    const std::size_t start = pos_;
    u64 len = 0;
    if (!get_var(len)) {
        return false;
    }
    if (len > remaining()) {
        pos_ = start;
        return false;
    }
    out.assign(data_ + pos_, data_ + pos_ + len);
    pos_ += static_cast<std::size_t>(len);
    return true;
}

bool byte_reader::get_string(std::string& out) {
    const std::size_t start = pos_;
    u64 len = 0;
    if (!get_var(len)) {
        return false;
    }
    if (len > remaining()) {
        pos_ = start;
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data_ + pos_), static_cast<std::size_t>(len));
    pos_ += static_cast<std::size_t>(len);
    return true;
}

} // namespace eosr
