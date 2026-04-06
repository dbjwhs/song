// MIT License
// Copyright (c) 2026 dbjwhs
//
// libFuzzer harness for Song wire protocol decoders.
// Build with: clang++ -fsanitize=fuzzer,address,undefined -std=c++20 -I../include
//             fuzz_wire_decoder.cpp ../src/wire.cpp ../src/buffer.cpp -o fuzz_wire

#include <song/wire.hpp>
#include <song/buffer.hpp>
#include <cstdint>
#include <cstring>

using namespace song;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Buffer buf;
    buf.write(reinterpret_cast<const std::byte*>(data), size);
    buf.reset_read();

    // Try decoding as header
    try {
        auto hdr = wire::decode_header_validated(buf);

        // If header decoded, try payload-specific decoders
        buf.reset_read();
        wire::decode_header(buf);  // Skip header

        switch (hdr.type) {
            case wire::MsgType::init:
                try { wire::decode_init(buf); } catch (...) {}
                break;
            case wire::MsgType::call:
                try { wire::decode_method_call_header(buf); } catch (...) {}
                break;
            case wire::MsgType::create:
                try { wire::decode_object_create_header(buf); } catch (...) {}
                break;
            case wire::MsgType::release:
                try { wire::decode_object_release_header(buf); } catch (...) {}
                break;
            case wire::MsgType::prop_get:
            case wire::MsgType::prop_set:
            case wire::MsgType::prop_subscribe:
            case wire::MsgType::prop_unsubscribe:
            case wire::MsgType::prop_notify:
                try { wire::decode_property_header(buf); } catch (...) {}
                break;
            default:
                break;
        }
    } catch (...) {
        // Expected for most random input
    }

    // Also try standalone decoders on raw input
    buf.reset_read();
    try { wire::decode_init(buf); } catch (...) {}

    buf.reset_read();
    try { wire::decode_method_call_header(buf); } catch (...) {}

    buf.reset_read();
    try { wire::decode_object_ref(buf); } catch (...) {}

    buf.reset_read();
    try { wire::decode_property_header(buf); } catch (...) {}

    return 0;
}
