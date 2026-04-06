// MIT License
// Copyright (c) 2026 dbjwhs
//
// libFuzzer harness for Song buffer decode operations.
// Build with: clang++ -fsanitize=fuzzer,address,undefined -std=c++20 -I../include
//             fuzz_buffer_decoder.cpp ../src/buffer.cpp -o fuzz_buffer

#include <song/buffer.hpp>
#include <cstdint>

using namespace song;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Buffer buf;
    buf.write(reinterpret_cast<const std::byte*>(data), size);

    // Try each decode type on the same input
    buf.reset_read();
    try { decode_bool(buf); } catch (...) {}

    buf.reset_read();
    try { decode_i8(buf); } catch (...) {}

    buf.reset_read();
    try { decode_i16(buf); } catch (...) {}

    buf.reset_read();
    try { decode_i32(buf); } catch (...) {}

    buf.reset_read();
    try { decode_i64(buf); } catch (...) {}

    buf.reset_read();
    try { decode_u8(buf); } catch (...) {}

    buf.reset_read();
    try { decode_u16(buf); } catch (...) {}

    buf.reset_read();
    try { decode_u32(buf); } catch (...) {}

    buf.reset_read();
    try { decode_u64(buf); } catch (...) {}

    buf.reset_read();
    try { decode_f32(buf); } catch (...) {}

    buf.reset_read();
    try { decode_f64(buf); } catch (...) {}

    buf.reset_read();
    try { decode_string(buf); } catch (...) {}

    buf.reset_read();
    try { decode_bytes(buf); } catch (...) {}

    buf.reset_read();
    try { decode_array<i32>(buf); } catch (...) {}

    buf.reset_read();
    try { decode_array<std::string>(buf); } catch (...) {}

    buf.reset_read();
    try { decode_array<f64>(buf); } catch (...) {}

    return 0;
}
