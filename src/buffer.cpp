// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/buffer.hpp"
#include <stdexcept>
#include <limits>
#include <bit>

// Song specifies a little-endian wire format. All numeric types are serialized
// via memcpy without byte swapping, so both endpoints must be little-endian.
// This covers x86, ARM (LE mode), and RISC-V — effectively all modern targets.
static_assert(std::endian::native == std::endian::little,
              "Song wire protocol requires a little-endian architecture");

namespace song {

Buffer::~Buffer() {
    if (!is_inline()) {
        delete[] data_;
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : size_(other.size_), capacity_(other.capacity_), read_pos_(other.read_pos_) {
    if (other.is_inline()) {
        // Only copy the bytes actually in use, not the full 4KB buffer
        std::memcpy(inline_, other.inline_, other.size_);
        data_ = inline_;
    } else {
        data_ = other.data_;
        other.data_ = other.inline_;
        other.capacity_ = kInlineSize;
    }
    other.size_ = 0;
    other.read_pos_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (!is_inline()) {
            delete[] data_;
        }

        size_ = other.size_;
        capacity_ = other.capacity_;
        read_pos_ = other.read_pos_;

        if (other.is_inline()) {
            // Only copy the bytes actually in use, not the full 4KB buffer
            std::memcpy(inline_, other.inline_, other.size_);
            data_ = inline_;
        } else {
            data_ = other.data_;
            other.data_ = other.inline_;
            other.capacity_ = kInlineSize;
        }
        other.size_ = 0;
        other.read_pos_ = 0;
    }
    return *this;
}

void Buffer::ensure_capacity(size_t required) {
    if (required <= capacity_) return;

    // Guard against overflow: capacity doubling must not wrap around SIZE_MAX
    constexpr size_t kMaxCapacity = std::numeric_limits<size_t>::max() / 2;
    if (required > kMaxCapacity) {
        throw std::runtime_error("Buffer capacity overflow: requested size too large");
    }

    size_t new_capacity = capacity_;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    auto* new_data = new std::byte[new_capacity];
    std::memcpy(new_data, data_, size_);

    if (!is_inline()) {
        delete[] data_;
    }

    data_ = new_data;
    capacity_ = new_capacity;
}

void Buffer::write(const void* data, size_t len) {
    if (len > 0 && size_ > std::numeric_limits<size_t>::max() - len) {
        throw std::runtime_error("Buffer write overflow: size + len exceeds maximum");
    }
    ensure_capacity(size_ + len);
    std::memcpy(data_ + size_, data, len);
    size_ += len;
}

void Buffer::read(void* data, size_t len) {
    if (len > size_ || read_pos_ > size_ - len) {
        throw std::runtime_error("Buffer underflow: attempted to read beyond buffer size");
    }
    std::memcpy(data, data_ + read_pos_, len);
    read_pos_ += len;
}

// Primitive encoders
void encode_bool(Buffer& buf, bool val) {
    u8 byte = val ? 1 : 0;
    buf.write(&byte, 1);
}

void encode_i8(Buffer& buf, i8 val) {
    buf.write(&val, sizeof(val));
}

void encode_i16(Buffer& buf, i16 val) {
    buf.write(&val, sizeof(val));
}

void encode_i32(Buffer& buf, i32 val) {
    buf.write(&val, sizeof(val));
}

void encode_i64(Buffer& buf, i64 val) {
    buf.write(&val, sizeof(val));
}

void encode_u8(Buffer& buf, u8 val) {
    buf.write(&val, sizeof(val));
}

void encode_u16(Buffer& buf, u16 val) {
    buf.write(&val, sizeof(val));
}

void encode_u32(Buffer& buf, u32 val) {
    buf.write(&val, sizeof(val));
}

void encode_u64(Buffer& buf, u64 val) {
    buf.write(&val, sizeof(val));
}

void encode_f32(Buffer& buf, f32 val) {
    buf.write(&val, sizeof(val));
}

void encode_f64(Buffer& buf, f64 val) {
    buf.write(&val, sizeof(val));
}

void encode_string(Buffer& buf, std::string_view val) {
    if (val.size() > kMaxStringSize) {
        throw std::runtime_error("String too large to encode");
    }
    u32 len = static_cast<u32>(val.size());
    encode_u32(buf, len);
    buf.write(val.data(), len);
}

void encode_bytes(Buffer& buf, std::span<const std::byte> val) {
    if (val.size() > kMaxBytesSize) {
        throw std::runtime_error("Byte array too large to encode");
    }
    u32 len = static_cast<u32>(val.size());
    encode_u32(buf, len);
    buf.write(val.data(), len);
}

// Primitive decoders
bool decode_bool(Buffer& buf) {
    u8 byte;
    buf.read(&byte, 1);
    return byte != 0;
}

i8 decode_i8(Buffer& buf) {
    i8 val;
    buf.read(&val, sizeof(val));
    return val;
}

i16 decode_i16(Buffer& buf) {
    i16 val;
    buf.read(&val, sizeof(val));
    return val;
}

i32 decode_i32(Buffer& buf) {
    i32 val;
    buf.read(&val, sizeof(val));
    return val;
}

i64 decode_i64(Buffer& buf) {
    i64 val;
    buf.read(&val, sizeof(val));
    return val;
}

u8 decode_u8(Buffer& buf) {
    u8 val;
    buf.read(&val, sizeof(val));
    return val;
}

u16 decode_u16(Buffer& buf) {
    u16 val;
    buf.read(&val, sizeof(val));
    return val;
}

u32 decode_u32(Buffer& buf) {
    u32 val;
    buf.read(&val, sizeof(val));
    return val;
}

u64 decode_u64(Buffer& buf) {
    u64 val;
    buf.read(&val, sizeof(val));
    return val;
}

f32 decode_f32(Buffer& buf) {
    f32 val;
    buf.read(&val, sizeof(val));
    return val;
}

f64 decode_f64(Buffer& buf) {
    f64 val;
    buf.read(&val, sizeof(val));
    return val;
}

std::string decode_string(Buffer& buf) {
    u32 len = decode_u32(buf);
    if (len > kMaxStringSize) {
        throw std::runtime_error("String too large");
    }
    std::string result(len, '\0');
    buf.read(result.data(), len);
    return result;
}

std::vector<std::byte> decode_bytes(Buffer& buf) {
    u32 len = decode_u32(buf);
    if (len > kMaxBytesSize) {
        throw std::runtime_error("Byte array too large");
    }
    std::vector<std::byte> result(len);
    buf.read(result.data(), len);
    return result;
}

} // namespace song
