// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace song {

// Type trait to detect std::vector for nested array support
template<typename T> struct is_std_vector : std::false_type {};
template<typename T, typename A> struct is_std_vector<std::vector<T, A>> : std::true_type {};
template<typename T> constexpr bool is_std_vector_v = is_std_vector<T>::value;

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

// Maximum sizes for serialization
// These match the corresponding wire:: constants
constexpr size_t kMaxStringSize = 1 * 1024 * 1024;   // 1 MB
constexpr size_t kMaxBytesSize = 1 * 1024 * 1024;    // 1 MB
constexpr size_t kMaxArrayCount = 1 * 1024 * 1024;   // 1M elements

template<typename T>
void encode_array(Buffer& buf, std::span<const T> vals) {
    if (vals.size() > kMaxArrayCount) {
        throw std::runtime_error("Array too large");
    }

    u32 count = static_cast<u32>(vals.size());
    encode_u32(buf, count);

    for (const auto& val : vals) {
        if constexpr (std::is_same_v<T, bool>) {
            encode_bool(buf, val);
        } else if constexpr (std::is_same_v<T, i8>) {
            encode_i8(buf, val);
        } else if constexpr (std::is_same_v<T, i16>) {
            encode_i16(buf, val);
        } else if constexpr (std::is_same_v<T, i32>) {
            encode_i32(buf, val);
        } else if constexpr (std::is_same_v<T, i64>) {
            encode_i64(buf, val);
        } else if constexpr (std::is_same_v<T, u8>) {
            encode_u8(buf, val);
        } else if constexpr (std::is_same_v<T, u16>) {
            encode_u16(buf, val);
        } else if constexpr (std::is_same_v<T, u32>) {
            encode_u32(buf, val);
        } else if constexpr (std::is_same_v<T, u64>) {
            encode_u64(buf, val);
        } else if constexpr (std::is_same_v<T, f32>) {
            encode_f32(buf, val);
        } else if constexpr (std::is_same_v<T, f64>) {
            encode_f64(buf, val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            encode_string(buf, val);
        } else if constexpr (is_std_vector_v<T>) {
            // Recursive case: T is std::vector<U>, encode as nested array
            encode_array<typename T::value_type>(buf, val);
        } else {
            static_assert(sizeof(T) == 0, "Unsupported array element type");
        }
    }
}

template<typename T>
std::vector<T> decode_array(Buffer& buf) {
    u32 count = decode_u32(buf);
    if (count > kMaxArrayCount) {
        throw std::runtime_error("Array too large");
    }

    std::vector<T> result;
    result.reserve(count);

    for (u32 i = 0; i < count; ++i) {
        if constexpr (std::is_same_v<T, bool>) {
            result.push_back(decode_bool(buf));
        } else if constexpr (std::is_same_v<T, i8>) {
            result.push_back(decode_i8(buf));
        } else if constexpr (std::is_same_v<T, i16>) {
            result.push_back(decode_i16(buf));
        } else if constexpr (std::is_same_v<T, i32>) {
            result.push_back(decode_i32(buf));
        } else if constexpr (std::is_same_v<T, i64>) {
            result.push_back(decode_i64(buf));
        } else if constexpr (std::is_same_v<T, u8>) {
            result.push_back(decode_u8(buf));
        } else if constexpr (std::is_same_v<T, u16>) {
            result.push_back(decode_u16(buf));
        } else if constexpr (std::is_same_v<T, u32>) {
            result.push_back(decode_u32(buf));
        } else if constexpr (std::is_same_v<T, u64>) {
            result.push_back(decode_u64(buf));
        } else if constexpr (std::is_same_v<T, f32>) {
            result.push_back(decode_f32(buf));
        } else if constexpr (std::is_same_v<T, f64>) {
            result.push_back(decode_f64(buf));
        } else if constexpr (std::is_same_v<T, std::string>) {
            result.push_back(decode_string(buf));
        } else if constexpr (is_std_vector_v<T>) {
            // Recursive case: T is std::vector<U>, decode as nested array
            result.push_back(decode_array<typename T::value_type>(buf));
        } else {
            static_assert(sizeof(T) == 0, "Unsupported array element type");
        }
    }

    return result;
}

} // namespace song
