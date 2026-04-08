// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <random>
#include <cstring>

using namespace song;

// =============================================================================
// Random data generator
// =============================================================================

class FuzzGen {
    std::mt19937 rng_;

public:
    explicit FuzzGen(uint32_t seed) : rng_(seed) {}

    // Random bytes
    std::vector<std::byte> bytes(size_t len) {
        std::vector<std::byte> data(len);
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : data) {
            b = static_cast<std::byte>(dist(rng_));
        }
        return data;
    }

    // Random Buffer with N random bytes
    Buffer buffer(size_t len) {
        auto data = bytes(len);
        Buffer buf;
        buf.write(data.data(), data.size());
        buf.reset_read();
        return buf;
    }

    // Valid header with random payload
    Buffer valid_header_random_payload(size_t payload_len) {
        auto payload = bytes(payload_len);
        Buffer buf;
        wire::Header hdr{
            .magic = wire::kMagic,
            .flags = wire::MsgFlags::none,
            .type = wire::MsgType::call,
            .reserved = 0,
            .payload_size = static_cast<u32>(payload_len),
            .sequence_id = 1
        };
        wire::encode_header(buf, hdr);
        buf.write(payload.data(), payload.size());
        buf.reset_read();
        return buf;
    }

    // Bit-flip a valid message
    Buffer bit_flip(const Buffer& original, int num_flips) {
        Buffer copy;
        copy.write(original.data(), original.size());
        std::uniform_int_distribution<size_t> pos_dist(0, copy.size() - 1);
        std::uniform_int_distribution<int> bit_dist(0, 7);
        for (int ndx = 0; ndx < num_flips; ++ndx) {
            size_t pos = pos_dist(rng_);
            int bit = bit_dist(rng_);
            auto* ptr = reinterpret_cast<uint8_t*>(copy.data() + pos);
            *ptr ^= (1 << bit);
        }
        copy.reset_read();
        return copy;
    }

    uint32_t next_u32() {
        return rng_();
    }

    size_t next_size(size_t min, size_t max) {
        std::uniform_int_distribution<size_t> dist(min, max);
        return dist(rng_);
    }
};

// =============================================================================
// Wire Protocol Decoder Fuzz Tests
// =============================================================================

TEST(WireFuzzTest, RandomBytesDecodeHeader) {
    FuzzGen gen(42);
    int exceptions = 0;
    int successes = 0;

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 32));
        try {
            wire::decode_header(buf);
            successes++;
        } catch (...) {
            exceptions++;
        }
    }

    // Should not crash -- some will decode (garbage values), some will throw
    EXPECT_GT(exceptions + successes, 0);
}

TEST(WireFuzzTest, RandomBytesDecodeHeaderValidated) {
    FuzzGen gen(123);

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 32));
        try {
            wire::decode_header_validated(buf);
            // If it succeeds, the magic was accidentally valid
        } catch (const ProtocolError&) {
            // Expected: invalid magic or payload too large
        } catch (const std::runtime_error&) {
            // Buffer underflow
        }
    }
}

TEST(WireFuzzTest, TruncatedHeaders) {
    FuzzGen gen(999);

    // Headers less than 16 bytes should always fail cleanly
    for (int len = 0; len < 16; ++len) {
        Buffer buf = gen.buffer(static_cast<size_t>(len));
        EXPECT_THROW(wire::decode_header(buf), std::runtime_error)
            << "Length " << len << " should throw";
    }
}

TEST(WireFuzzTest, RandomBytesDecodeInit) {
    FuzzGen gen(456);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 64));
        try {
            wire::decode_init(buf);
        } catch (...) {
            // Expected for most random input
        }
    }
}

TEST(WireFuzzTest, RandomBytesDecodeMethodCall) {
    FuzzGen gen(789);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 16));
        try {
            wire::decode_method_call_header(buf);
        } catch (...) {
            // Expected
        }
    }
}

TEST(WireFuzzTest, RandomBytesDecodePropertyHeader) {
    FuzzGen gen(101);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 24));
        try {
            wire::decode_property_header(buf);
        } catch (...) {
            // Expected
        }
    }
}

TEST(WireFuzzTest, RandomBytesDecodeObjectRef) {
    FuzzGen gen(202);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 16));
        try {
            wire::decode_object_ref(buf);
        } catch (...) {
            // Expected
        }
    }
}

TEST(WireFuzzTest, RandomBytesDecodeObjectCreate) {
    FuzzGen gen(303);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 24));
        try {
            wire::decode_object_create_header(buf);
        } catch (...) {
            // Expected
        }
    }
}

TEST(WireFuzzTest, BitFlippedValidMessages) {
    FuzzGen gen(404);

    // Create valid messages of each type, then bit-flip them
    std::vector<Buffer> valid_messages;

    // Call message
    Buffer args;
    encode_i32(args, 42);
    valid_messages.push_back(wire::create_call_message(1, 1, 1, args));

    // Result message
    Buffer result;
    encode_string(result, "ok");
    valid_messages.push_back(wire::create_result_message(1, result));

    // Error message
    valid_messages.push_back(wire::create_error_message(1, ErrorCode::unknown_method, "err"));

    // Init message
    valid_messages.push_back(wire::create_init_message(
        wire::kFirstVersion, wire::kCurrentVersion, 0));

    // Stream messages
    Buffer chunk;
    encode_i32(chunk, 99);
    valid_messages.push_back(wire::create_stream_message(1, chunk));
    valid_messages.push_back(wire::create_stream_end_message(1));

    // Shutdown
    valid_messages.push_back(wire::create_shutdown_message());

    for (const auto& msg : valid_messages) {
        for (int ndx = 0; ndx < 1000; ++ndx) {
            Buffer flipped = gen.bit_flip(msg, static_cast<int>(gen.next_size(1, 5)));
            try {
                auto hdr = wire::decode_header_validated(flipped);
                // If header decoded, try decoding payload based on type
                if (hdr.type == wire::MsgType::call && hdr.payload_size >= 4) {
                    wire::decode_method_call_header(flipped);
                } else if (hdr.type == wire::MsgType::init) {
                    wire::decode_init(flipped);
                }
            } catch (...) {
                // Expected for corrupted data
            }
        }
    }
}

TEST(WireFuzzTest, OversizedPayloadClaim) {
    // Header claims huge payload but buffer is small
    for (u32 fake_size : {0xFFFFFFFFu, 0x7FFFFFFFu, 1000000u, 16u * 1024u * 1024u + 1u}) {
        Buffer buf;
        wire::Header hdr{
            .magic = wire::kMagic,
            .flags = wire::MsgFlags::none,
            .type = wire::MsgType::call,
            .reserved = 0,
            .payload_size = fake_size,
            .sequence_id = 1
        };
        wire::encode_header(buf, hdr);
        buf.reset_read();

        try {
            [[maybe_unused]] auto decoded = wire::decode_header_validated(buf);
            // Should reject if > kMaxPayloadSize
            if (fake_size > static_cast<u32>(wire::kMaxPayloadSize)) {
                FAIL() << "Should have thrown for payload_size=" << fake_size;
            }
        } catch (const ProtocolError&) {
            // Expected
        }
    }
}

// =============================================================================
// Buffer Decode Fuzz Tests
// =============================================================================

TEST(BufferFuzzTest, RandomBytesDecodeI32) {
    FuzzGen gen(500);

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 8));
        try {
            decode_i32(buf);
        } catch (const std::runtime_error&) {
            // Buffer underflow
        }
    }
}

TEST(BufferFuzzTest, RandomBytesDecodeI64) {
    FuzzGen gen(501);

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 12));
        try {
            decode_i64(buf);
        } catch (const std::runtime_error&) {
            // Buffer underflow
        }
    }
}

TEST(BufferFuzzTest, RandomBytesDecodeF64) {
    FuzzGen gen(502);

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 12));
        try {
            decode_f64(buf);
        } catch (const std::runtime_error&) {
            // Buffer underflow
        }
    }
}

TEST(BufferFuzzTest, RandomBytesDecodeString) {
    FuzzGen gen(503);

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 64));
        try {
            decode_string(buf);
        } catch (const std::runtime_error&) {
            // Expected: bad length prefix, buffer underflow, oversized
        }
    }
}

TEST(BufferFuzzTest, RandomBytesDecodeBytes) {
    FuzzGen gen(504);

    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 64));
        try {
            decode_bytes(buf);
        } catch (const std::runtime_error&) {
            // Expected
        }
    }
}

TEST(BufferFuzzTest, RandomBytesDecodeArrayI32) {
    FuzzGen gen(505);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 128));
        try {
            decode_array<i32>(buf);
        } catch (const std::runtime_error&) {
            // Expected: bad count, underflow
        }
    }
}

TEST(BufferFuzzTest, RandomBytesDecodeArrayString) {
    FuzzGen gen(506);

    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer buf = gen.buffer(gen.next_size(0, 128));
        try {
            decode_array<std::string>(buf);
        } catch (const std::runtime_error&) {
            // Expected
        }
    }
}

TEST(BufferFuzzTest, MaliciousStringLength) {
    // String with absurdly large length prefix
    for (u32 bad_len : {0xFFFFFFFFu, 0x7FFFFFFFu, 10000000u, 1048577u}) {
        Buffer buf;
        encode_u32(buf, bad_len);
        // Only a few bytes of actual data
        encode_u8(buf, 'A');
        encode_u8(buf, 'B');
        buf.reset_read();

        EXPECT_THROW(decode_string(buf), std::runtime_error)
            << "Should reject string length=" << bad_len;
    }
}

TEST(BufferFuzzTest, MaliciousArrayCount) {
    // Array with absurdly large element count
    for (u32 bad_count : {0xFFFFFFFFu, 0x7FFFFFFFu, 1048577u}) {
        Buffer buf;
        encode_u32(buf, bad_count);
        buf.reset_read();

        EXPECT_THROW(decode_array<i32>(buf), std::runtime_error)
            << "Should reject array count=" << bad_count;
    }
}

TEST(BufferFuzzTest, ZeroLengthEdgeCases) {
    // Empty string
    {
        Buffer buf;
        encode_string(buf, "");
        buf.reset_read();
        EXPECT_EQ(decode_string(buf), "");
    }

    // Empty bytes
    {
        Buffer buf;
        encode_bytes(buf, std::span<const std::byte>{});
        buf.reset_read();
        auto result = decode_bytes(buf);
        EXPECT_TRUE(result.empty());
    }

    // Empty array
    {
        Buffer buf;
        encode_array<i32>(buf, std::vector<i32>{});
        buf.reset_read();
        auto result = decode_array<i32>(buf);
        EXPECT_TRUE(result.empty());
    }
}

TEST(BufferFuzzTest, SequentialDecodeExhaustion) {
    // Encode a few values, then keep decoding past the end
    Buffer buf;
    encode_i32(buf, 1);
    encode_i32(buf, 2);
    buf.reset_read();

    EXPECT_EQ(decode_i32(buf), 1);
    EXPECT_EQ(decode_i32(buf), 2);
    EXPECT_THROW(decode_i32(buf), std::runtime_error);  // Past end
    EXPECT_THROW(decode_i32(buf), std::runtime_error);  // Still past end
}

// =============================================================================
// Mixed Encode/Decode Fuzz: Round-trip with random valid data
// =============================================================================

TEST(RoundTripFuzzTest, RandomI32RoundTrip) {
    FuzzGen gen(600);
    for (int ndx = 0; ndx < 10000; ++ndx) {
        i32 val = static_cast<i32>(gen.next_u32());
        Buffer buf;
        encode_i32(buf, val);
        buf.reset_read();
        EXPECT_EQ(decode_i32(buf), val);
    }
}

TEST(RoundTripFuzzTest, RandomStringRoundTrip) {
    FuzzGen gen(601);
    for (int ndx = 0; ndx < 5000; ++ndx) {
        size_t len = gen.next_size(0, 1000);
        auto data = gen.bytes(len);
        // Convert to string (may contain nulls, that's fine)
        std::string s(reinterpret_cast<const char*>(data.data()), len);
        Buffer buf;
        encode_string(buf, s);
        buf.reset_read();
        EXPECT_EQ(decode_string(buf), s);
    }
}

TEST(RoundTripFuzzTest, RandomHeaderRoundTrip) {
    FuzzGen gen(602);
    for (int ndx = 0; ndx < 10000; ++ndx) {
        wire::Header hdr{
            .magic = wire::kMagic,
            .flags = static_cast<wire::MsgFlags>(gen.next_u32() & 0xFF),
            .type = static_cast<wire::MsgType>((gen.next_u32() % 16) + 1),
            .reserved = 0,
            .payload_size = gen.next_u32() % static_cast<u32>(wire::kMaxPayloadSize + 1),
            .sequence_id = gen.next_u32()
        };

        Buffer buf;
        wire::encode_header(buf, hdr);
        buf.reset_read();
        auto decoded = wire::decode_header(buf);

        EXPECT_EQ(decoded.magic, hdr.magic);
        EXPECT_EQ(decoded.flags, hdr.flags);
        EXPECT_EQ(decoded.type, hdr.type);
        EXPECT_EQ(decoded.payload_size, hdr.payload_size);
        EXPECT_EQ(decoded.sequence_id, hdr.sequence_id);
    }
}
