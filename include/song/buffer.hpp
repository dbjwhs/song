// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>

namespace song {

/// Fixed-size buffer with small-buffer optimization
/// Uses inline storage for small allocations (< 4KB)
class Buffer {
    static constexpr size_t kInlineSize = 4096;
    alignas(8) std::byte inline_[kInlineSize];
    std::byte* data_ = inline_;
    size_t size_ = 0;
    size_t capacity_ = kInlineSize;
    size_t read_pos_ = 0;

public:
    Buffer() = default;
    ~Buffer();

    // Non-copyable, movable
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    /// Write raw bytes to buffer
    void write(const void* data, size_t len);

    /// Read raw bytes from buffer
    void read(void* data, size_t len);

    /// Get view of buffer contents
    std::span<const std::byte> view() const { return {data_, size_}; }

    /// Get mutable data pointer
    std::byte* data() { return data_; }
    const std::byte* data() const { return data_; }

    /// Get current size
    size_t size() const { return size_; }

    /// Check if using inline storage
    bool is_inline() const { return data_ == inline_; }

    /// Reset write position
    void reset() { size_ = 0; read_pos_ = 0; }

    /// Reset read position to beginning
    void reset_read() { read_pos_ = 0; }

    /// Get current read position
    size_t read_pos() const { return read_pos_; }

    /// Check if more data available to read
    bool has_data() const { return read_pos_ < size_; }

    /// Get remaining bytes to read
    size_t remaining() const { return size_ - read_pos_; }

private:
    void ensure_capacity(size_t required);
};

// Primitive type encoders
void encode_bool(Buffer& buf, bool val);
void encode_i8(Buffer& buf, i8 val);
void encode_i16(Buffer& buf, i16 val);
void encode_i32(Buffer& buf, i32 val);
void encode_i64(Buffer& buf, i64 val);
void encode_u8(Buffer& buf, u8 val);
void encode_u16(Buffer& buf, u16 val);
void encode_u32(Buffer& buf, u32 val);
void encode_u64(Buffer& buf, u64 val);
void encode_f32(Buffer& buf, f32 val);
void encode_f64(Buffer& buf, f64 val);
void encode_string(Buffer& buf, std::string_view val);
void encode_bytes(Buffer& buf, std::span<const std::byte> val);

// Primitive type decoders
bool decode_bool(Buffer& buf);
i8 decode_i8(Buffer& buf);
i16 decode_i16(Buffer& buf);
i32 decode_i32(Buffer& buf);
i64 decode_i64(Buffer& buf);
u8 decode_u8(Buffer& buf);
u16 decode_u16(Buffer& buf);
u32 decode_u32(Buffer& buf);
u64 decode_u64(Buffer& buf);
f32 decode_f32(Buffer& buf);
f64 decode_f64(Buffer& buf);
std::string decode_string(Buffer& buf);
std::vector<std::byte> decode_bytes(Buffer& buf);

// Array encoders/decoders
template<typename T>
void encode_array(Buffer& buf, std::span<const T> vals);

template<typename T>
std::vector<T> decode_array(Buffer& buf);

} // namespace song
