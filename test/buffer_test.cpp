// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/buffer.hpp>
#include <cstring>

using namespace song;

// =============================================================================
// Buffer Basic Operations
// =============================================================================

TEST(BufferTest, DefaultConstruction) {
    Buffer buf;
    EXPECT_EQ(buf.size(), 0);
    EXPECT_TRUE(buf.is_inline());
    EXPECT_EQ(buf.read_pos(), 0);
    EXPECT_FALSE(buf.has_data());
}

TEST(BufferTest, WriteAndRead) {
    Buffer buf;
    const char* data = "Hello, Song!";
    buf.write(data, strlen(data));

    EXPECT_EQ(buf.size(), strlen(data));
    EXPECT_TRUE(buf.is_inline());

    char result[32] = {0};
    buf.read(result, strlen(data));
    EXPECT_STREQ(result, data);
}

TEST(BufferTest, Reset) {
    Buffer buf;
    encode_i32(buf, 42);
    EXPECT_EQ(buf.size(), 4);

    buf.reset();
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.read_pos(), 0);
}

TEST(BufferTest, ResetRead) {
    Buffer buf;
    encode_i32(buf, 42);
    encode_i32(buf, 100);

    i32 val1 = decode_i32(buf);
    EXPECT_EQ(val1, 42);
    EXPECT_EQ(buf.read_pos(), 4);

    buf.reset_read();
    EXPECT_EQ(buf.read_pos(), 0);

    i32 val2 = decode_i32(buf);
    EXPECT_EQ(val2, 42);
}

TEST(BufferTest, Remaining) {
    Buffer buf;
    encode_i32(buf, 1);
    encode_i32(buf, 2);
    encode_i32(buf, 3);

    EXPECT_EQ(buf.remaining(), 12);
    decode_i32(buf);
    EXPECT_EQ(buf.remaining(), 8);
    decode_i32(buf);
    EXPECT_EQ(buf.remaining(), 4);
    decode_i32(buf);
    EXPECT_EQ(buf.remaining(), 0);
    EXPECT_FALSE(buf.has_data());
}

// =============================================================================
// Small Buffer Optimization
// =============================================================================

TEST(BufferTest, SmallBufferOptimization) {
    Buffer buf;
    // Write less than 4KB - should stay inline
    std::vector<std::byte> small_data(1000);
    buf.write(small_data.data(), small_data.size());
    EXPECT_TRUE(buf.is_inline());
}

TEST(BufferTest, LargeBufferHeapAllocation) {
    Buffer buf;
    // Write more than 4KB - should move to heap
    std::vector<std::byte> large_data(5000);
    buf.write(large_data.data(), large_data.size());
    EXPECT_FALSE(buf.is_inline());
}

// =============================================================================
// Move Semantics
// =============================================================================

TEST(BufferTest, MoveConstructorInline) {
    Buffer buf1;
    encode_string(buf1, "test data");
    size_t original_size = buf1.size();

    Buffer buf2(std::move(buf1));

    EXPECT_EQ(buf2.size(), original_size);
    EXPECT_TRUE(buf2.is_inline());
    EXPECT_EQ(buf1.size(), 0);  // Moved-from buffer is empty
}

TEST(BufferTest, MoveConstructorHeap) {
    Buffer buf1;
    // Force heap allocation
    std::vector<std::byte> large_data(5000);
    buf1.write(large_data.data(), large_data.size());
    EXPECT_FALSE(buf1.is_inline());

    Buffer buf2(std::move(buf1));

    EXPECT_EQ(buf2.size(), 5000);
    EXPECT_FALSE(buf2.is_inline());
    EXPECT_EQ(buf1.size(), 0);
}

TEST(BufferTest, MoveAssignmentInline) {
    Buffer buf1;
    encode_i32(buf1, 42);

    Buffer buf2;
    encode_i32(buf2, 100);

    buf2 = std::move(buf1);

    buf2.reset_read();
    EXPECT_EQ(decode_i32(buf2), 42);
}

// =============================================================================
// Primitive Encoding/Decoding Round-trips
// =============================================================================

TEST(BufferTest, BoolRoundtrip) {
    Buffer buf;
    encode_bool(buf, true);
    encode_bool(buf, false);

    EXPECT_TRUE(decode_bool(buf));
    EXPECT_FALSE(decode_bool(buf));
}

TEST(BufferTest, I8Roundtrip) {
    Buffer buf;
    encode_i8(buf, -128);
    encode_i8(buf, 0);
    encode_i8(buf, 127);

    EXPECT_EQ(decode_i8(buf), -128);
    EXPECT_EQ(decode_i8(buf), 0);
    EXPECT_EQ(decode_i8(buf), 127);
}

TEST(BufferTest, I16Roundtrip) {
    Buffer buf;
    encode_i16(buf, -32768);
    encode_i16(buf, 0);
    encode_i16(buf, 32767);

    EXPECT_EQ(decode_i16(buf), -32768);
    EXPECT_EQ(decode_i16(buf), 0);
    EXPECT_EQ(decode_i16(buf), 32767);
}

TEST(BufferTest, I32Roundtrip) {
    Buffer buf;
    encode_i32(buf, -2147483648);
    encode_i32(buf, 0);
    encode_i32(buf, 2147483647);

    EXPECT_EQ(decode_i32(buf), -2147483648);
    EXPECT_EQ(decode_i32(buf), 0);
    EXPECT_EQ(decode_i32(buf), 2147483647);
}

TEST(BufferTest, I64Roundtrip) {
    Buffer buf;
    encode_i64(buf, INT64_MIN);
    encode_i64(buf, 0);
    encode_i64(buf, INT64_MAX);

    EXPECT_EQ(decode_i64(buf), INT64_MIN);
    EXPECT_EQ(decode_i64(buf), 0);
    EXPECT_EQ(decode_i64(buf), INT64_MAX);
}

TEST(BufferTest, U8Roundtrip) {
    Buffer buf;
    encode_u8(buf, 0);
    encode_u8(buf, 255);

    EXPECT_EQ(decode_u8(buf), 0);
    EXPECT_EQ(decode_u8(buf), 255);
}

TEST(BufferTest, U16Roundtrip) {
    Buffer buf;
    encode_u16(buf, 0);
    encode_u16(buf, 65535);

    EXPECT_EQ(decode_u16(buf), 0);
    EXPECT_EQ(decode_u16(buf), 65535);
}

TEST(BufferTest, U32Roundtrip) {
    Buffer buf;
    encode_u32(buf, 0);
    encode_u32(buf, UINT32_MAX);

    EXPECT_EQ(decode_u32(buf), 0);
    EXPECT_EQ(decode_u32(buf), UINT32_MAX);
}

TEST(BufferTest, U64Roundtrip) {
    Buffer buf;
    encode_u64(buf, 0);
    encode_u64(buf, UINT64_MAX);

    EXPECT_EQ(decode_u64(buf), 0);
    EXPECT_EQ(decode_u64(buf), UINT64_MAX);
}

TEST(BufferTest, F32Roundtrip) {
    Buffer buf;
    encode_f32(buf, 0.0f);
    encode_f32(buf, 3.14159f);
    encode_f32(buf, -1.0e10f);

    EXPECT_FLOAT_EQ(decode_f32(buf), 0.0f);
    EXPECT_FLOAT_EQ(decode_f32(buf), 3.14159f);
    EXPECT_FLOAT_EQ(decode_f32(buf), -1.0e10f);
}

TEST(BufferTest, F64Roundtrip) {
    Buffer buf;
    encode_f64(buf, 0.0);
    encode_f64(buf, 3.14159265358979);
    encode_f64(buf, -1.0e100);

    EXPECT_DOUBLE_EQ(decode_f64(buf), 0.0);
    EXPECT_DOUBLE_EQ(decode_f64(buf), 3.14159265358979);
    EXPECT_DOUBLE_EQ(decode_f64(buf), -1.0e100);
}

TEST(BufferTest, StringRoundtrip) {
    Buffer buf;
    encode_string(buf, "");
    encode_string(buf, "Hello, World!");
    encode_string(buf, std::string(1000, 'x'));

    EXPECT_EQ(decode_string(buf), "");
    EXPECT_EQ(decode_string(buf), "Hello, World!");
    EXPECT_EQ(decode_string(buf), std::string(1000, 'x'));
}

TEST(BufferTest, BytesRoundtrip) {
    Buffer buf;
    std::vector<std::byte> data = {std::byte{0x00}, std::byte{0xFF}, std::byte{0x42}};
    encode_bytes(buf, data);

    auto result = decode_bytes(buf);
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], std::byte{0x00});
    EXPECT_EQ(result[1], std::byte{0xFF});
    EXPECT_EQ(result[2], std::byte{0x42});
}

// =============================================================================
// Array Encoding/Decoding
// =============================================================================

TEST(BufferTest, EmptyArrayRoundtrip) {
    Buffer buf;
    std::vector<i32> empty;
    encode_array<i32>(buf, empty);

    auto result = decode_array<i32>(buf);
    EXPECT_TRUE(result.empty());
}

TEST(BufferTest, I32ArrayRoundtrip) {
    Buffer buf;
    std::vector<i32> data = {1, 2, 3, 4, 5, -100, 0, 100};
    encode_array<i32>(buf, data);

    auto result = decode_array<i32>(buf);
    EXPECT_EQ(result, data);
}

TEST(BufferTest, StringArrayRoundtrip) {
    Buffer buf;
    std::vector<std::string> data = {"hello", "world", "", "test"};
    encode_array<std::string>(buf, data);

    auto result = decode_array<std::string>(buf);
    EXPECT_EQ(result, data);
}

TEST(BufferTest, F64ArrayRoundtrip) {
    Buffer buf;
    std::vector<f64> data = {1.1, 2.2, 3.3, -4.4};
    encode_array<f64>(buf, data);

    auto result = decode_array<f64>(buf);
    ASSERT_EQ(result.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_DOUBLE_EQ(result[i], data[i]);
    }
}

// =============================================================================
// Multi-dimensional Arrays
// =============================================================================

TEST(BufferTest, TwoDimensionalI32ArrayRoundtrip) {
    Buffer buf;
    std::vector<std::vector<i32>> data = {
        {1, 2, 3},
        {4, 5},
        {6, 7, 8, 9}
    };
    encode_array<std::vector<i32>>(buf, data);

    auto result = decode_array<std::vector<i32>>(buf);
    ASSERT_EQ(result.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(result[i], data[i]);
    }
}

TEST(BufferTest, TwoDimensionalF64ArrayRoundtrip) {
    Buffer buf;
    std::vector<std::vector<f64>> data = {
        {1.1, 2.2},
        {3.3, 4.4, 5.5}
    };
    encode_array<std::vector<f64>>(buf, data);

    auto result = decode_array<std::vector<f64>>(buf);
    ASSERT_EQ(result.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        ASSERT_EQ(result[i].size(), data[i].size());
        for (size_t j = 0; j < data[i].size(); ++j) {
            EXPECT_DOUBLE_EQ(result[i][j], data[i][j]);
        }
    }
}

TEST(BufferTest, TwoDimensionalStringArrayRoundtrip) {
    Buffer buf;
    std::vector<std::vector<std::string>> data = {
        {"hello", "world"},
        {"foo"},
        {"bar", "baz", "qux"}
    };
    encode_array<std::vector<std::string>>(buf, data);

    auto result = decode_array<std::vector<std::string>>(buf);
    ASSERT_EQ(result.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(result[i], data[i]);
    }
}

TEST(BufferTest, ThreeDimensionalI32ArrayRoundtrip) {
    Buffer buf;
    std::vector<std::vector<std::vector<i32>>> data = {
        {{1, 2}, {3}},
        {{4, 5, 6}},
        {{7}, {8, 9}, {10, 11, 12}}
    };
    encode_array<std::vector<std::vector<i32>>>(buf, data);

    auto result = decode_array<std::vector<std::vector<i32>>>(buf);
    ASSERT_EQ(result.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        ASSERT_EQ(result[i].size(), data[i].size());
        for (size_t j = 0; j < data[i].size(); ++j) {
            EXPECT_EQ(result[i][j], data[i][j]);
        }
    }
}

TEST(BufferTest, EmptyNestedArrayRoundtrip) {
    Buffer buf;
    std::vector<std::vector<i32>> data = {
        {},
        {1, 2},
        {}
    };
    encode_array<std::vector<i32>>(buf, data);

    auto result = decode_array<std::vector<i32>>(buf);
    ASSERT_EQ(result.size(), data.size());
    EXPECT_TRUE(result[0].empty());
    EXPECT_EQ(result[1], (std::vector<i32>{1, 2}));
    EXPECT_TRUE(result[2].empty());
}

TEST(BufferTest, SparseJaggedArrayRoundtrip) {
    // Simulates sparse matrix: only some rows have data
    Buffer buf;
    std::vector<std::vector<i32>> data = {
        {100},           // row 0: one element
        {},              // row 1: empty (sparse)
        {},              // row 2: empty (sparse)
        {200, 300, 400}, // row 3: three elements
        {}               // row 4: empty (sparse)
    };
    encode_array<std::vector<i32>>(buf, data);

    auto result = decode_array<std::vector<i32>>(buf);
    EXPECT_EQ(result, data);
}

// =============================================================================
// Error Handling
// =============================================================================

TEST(BufferTest, ReadUnderflowThrows) {
    Buffer buf;
    encode_i32(buf, 42);

    decode_i32(buf);  // Read the value

    // Try to read more than available
    EXPECT_THROW(decode_i32(buf), std::runtime_error);
}

TEST(BufferTest, StringTooLargeThrows) {
    Buffer buf;
    // Manually encode a huge length
    encode_u32(buf, 2 * 1024 * 1024);  // 2MB - exceeds limit

    EXPECT_THROW(decode_string(buf), std::runtime_error);
}

TEST(BufferTest, ArrayTooLargeThrows) {
    Buffer buf;
    // Manually encode a huge count
    encode_u32(buf, 2 * 1024 * 1024);  // 2M elements - exceeds limit

    EXPECT_THROW((decode_array<i32>(buf)), std::runtime_error);
}

// =============================================================================
// Mixed Data
// =============================================================================

TEST(BufferTest, MixedDataRoundtrip) {
    Buffer buf;

    encode_bool(buf, true);
    encode_i32(buf, 42);
    encode_string(buf, "test");
    encode_f64(buf, 3.14);
    std::vector<u8> bytes = {1, 2, 3};
    encode_array<u8>(buf, bytes);

    EXPECT_TRUE(decode_bool(buf));
    EXPECT_EQ(decode_i32(buf), 42);
    EXPECT_EQ(decode_string(buf), "test");
    EXPECT_DOUBLE_EQ(decode_f64(buf), 3.14);
    auto result_bytes = decode_array<u8>(buf);
    EXPECT_EQ(result_bytes, bytes);
}
