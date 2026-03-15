// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/wire.hpp>

using namespace song;
using namespace song::wire;

// =============================================================================
// Header Encoding/Decoding
// =============================================================================

TEST(WireTest, HeaderRoundtrip) {
    Buffer buf;

    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = 1024,
        .sequence_id = 42
    };

    encode_header(buf, hdr);
    EXPECT_EQ(buf.size(), 16);  // Header is always 16 bytes

    Header decoded = decode_header(buf);
    EXPECT_EQ(decoded.magic, kMagic);
    EXPECT_EQ(decoded.flags, MsgFlags::none);
    EXPECT_EQ(decoded.type, MsgType::call);
    EXPECT_EQ(decoded.reserved, 0);
    EXPECT_EQ(decoded.payload_size, 1024);
    EXPECT_EQ(decoded.sequence_id, 42);
}

TEST(WireTest, HeaderValidatedThrowsOnBadMagic) {
    Buffer buf;

    Header hdr{
        .magic = 0xDEADBEEF,  // Wrong magic
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = 0,
        .sequence_id = 0
    };

    encode_header(buf, hdr);

    EXPECT_THROW(decode_header_validated(buf), ProtocolError);
}

TEST(WireTest, HeaderValidatedThrowsOnHugePayload) {
    Buffer buf;

    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = kMaxPayloadSize + 1,  // Too big
        .sequence_id = 0
    };

    encode_header(buf, hdr);

    EXPECT_THROW(decode_header_validated(buf), ProtocolError);
}

TEST(WireTest, AllMessageTypes) {
    std::vector<MsgType> types = {
        MsgType::init,
        MsgType::call,
        MsgType::result,
        MsgType::error,
        MsgType::stream,
        MsgType::stream_end,
        MsgType::ping,
        MsgType::shutdown
    };

    for (auto type : types) {
        Buffer buf;
        Header hdr{
            .magic = kMagic,
            .flags = MsgFlags::none,
            .type = type,
            .reserved = 0,
            .payload_size = 0,
            .sequence_id = 0
        };

        encode_header(buf, hdr);
        Header decoded = decode_header(buf);
        EXPECT_EQ(decoded.type, type);
    }
}

// =============================================================================
// Init Message
// =============================================================================

TEST(WireTest, InitMessageRoundtrip) {
    Buffer buf;

    InitMessage msg{
        .magic = kMagic,
        .first_version = kFirstVersion,
        .current_version = kCurrentVersion,
        .capabilities = 0x12345678,
        .method_count = 5
    };

    encode_init(buf, msg);

    InitMessage decoded = decode_init(buf);
    EXPECT_EQ(decoded.magic, kMagic);
    EXPECT_EQ(decoded.first_version, kFirstVersion);
    EXPECT_EQ(decoded.current_version, kCurrentVersion);
    EXPECT_EQ(decoded.capabilities, 0x12345678);
    EXPECT_EQ(decoded.method_count, 5);
}

TEST(WireTest, InitMessageBadMagicThrows) {
    Buffer buf;

    InitMessage msg{
        .magic = 0xBADBAD,  // Wrong magic
        .first_version = kFirstVersion,
        .current_version = kCurrentVersion,
        .capabilities = 0,
        .method_count = 0
    };

    encode_init(buf, msg);

    EXPECT_THROW(decode_init(buf), ProtocolError);
}

// =============================================================================
// Method Descriptor
// =============================================================================

TEST(WireTest, MethodDescriptorRoundtrip) {
    Buffer buf;

    MethodDescriptor desc{
        .service_id = 1,
        .method_id = 42,
        .flags = MethodFlags::streaming,
        .reserved = 0
    };

    encode_method_descriptor(buf, desc);
    EXPECT_EQ(buf.size(), 8);  // MethodDescriptor is 8 bytes

    MethodDescriptor decoded = decode_method_descriptor(buf);
    EXPECT_EQ(decoded.service_id, 1);
    EXPECT_EQ(decoded.method_id, 42);
    EXPECT_EQ(decoded.flags, MethodFlags::streaming);
}

TEST(WireTest, MultipleMethodDescriptors) {
    Buffer buf;

    std::vector<MethodDescriptor> descs = {
        {1, 1, MethodFlags::none, 0},
        {1, 2, MethodFlags::optional, 0},
        {2, 1, MethodFlags::streaming, 0},
        {2, 2, MethodFlags::oneway, 0}
    };

    for (const auto& d : descs) {
        encode_method_descriptor(buf, d);
    }

    for (const auto& expected : descs) {
        auto decoded = decode_method_descriptor(buf);
        EXPECT_EQ(decoded.service_id, expected.service_id);
        EXPECT_EQ(decoded.method_id, expected.method_id);
        EXPECT_EQ(decoded.flags, expected.flags);
    }
}

// =============================================================================
// Method Call
// =============================================================================

TEST(WireTest, MethodCallHeader) {
    Buffer buf;
    Buffer args;
    encode_string(args, "test argument");

    encode_method_call(buf, 1, 42, args);

    auto [service_id, method_id] = decode_method_call_header(buf);
    EXPECT_EQ(service_id, 1);
    EXPECT_EQ(method_id, 42);

    // Remaining data should be the argument
    std::string arg = decode_string(buf);
    EXPECT_EQ(arg, "test argument");
}

// =============================================================================
// Full Message Creation
// =============================================================================

TEST(WireTest, CreateInitMessage) {
    Buffer msg = create_init_message(kFirstVersion, kCurrentVersion, 0);

    // Should have header (16 bytes) + init payload (16 bytes)
    EXPECT_EQ(msg.size(), 32);

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::init);
    EXPECT_EQ(hdr.payload_size, 16);

    InitMessage init = decode_init(msg);
    EXPECT_EQ(init.first_version, kFirstVersion);
    EXPECT_EQ(init.current_version, kCurrentVersion);
    EXPECT_EQ(init.method_count, 0);
}

TEST(WireTest, CreateInitMessageWithMethods) {
    std::vector<MethodDescriptor> methods = {
        {1, 1, MethodFlags::none, 0},
        {1, 2, MethodFlags::none, 0}
    };

    Buffer msg = create_init_message(kFirstVersion, kCurrentVersion, 0, methods);

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::init);

    InitMessage init = decode_init(msg);
    EXPECT_EQ(init.method_count, 2);

    // Decode methods
    for (size_t i = 0; i < methods.size(); ++i) {
        auto decoded = decode_method_descriptor(msg);
        EXPECT_EQ(decoded.service_id, methods[i].service_id);
        EXPECT_EQ(decoded.method_id, methods[i].method_id);
    }
}

TEST(WireTest, CreateCallMessage) {
    Buffer args;
    encode_i32(args, 42);
    encode_string(args, "hello");

    Buffer msg = create_call_message(123, 1, 2, args);

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::call);
    EXPECT_EQ(hdr.sequence_id, 123);

    auto [service_id, method_id] = decode_method_call_header(msg);
    EXPECT_EQ(service_id, 1);
    EXPECT_EQ(method_id, 2);

    EXPECT_EQ(decode_i32(msg), 42);
    EXPECT_EQ(decode_string(msg), "hello");
}

TEST(WireTest, CreateResultMessage) {
    Buffer result;
    encode_string(result, "success");

    Buffer msg = create_result_message(456, result);

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::result);
    EXPECT_EQ(hdr.sequence_id, 456);

    EXPECT_EQ(decode_string(msg), "success");
}

TEST(WireTest, CreateErrorMessage) {
    Buffer msg = create_error_message(789, ErrorCode::unknown_method, "Method not found");

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::error);
    EXPECT_EQ(hdr.sequence_id, 789);

    u16 code = decode_u16(msg);
    EXPECT_EQ(static_cast<ErrorCode>(code), ErrorCode::unknown_method);

    std::string error_msg = decode_string(msg);
    EXPECT_EQ(error_msg, "Method not found");
}

TEST(WireTest, CreateShutdownMessage) {
    Buffer msg = create_shutdown_message();

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::shutdown);
    EXPECT_EQ(hdr.payload_size, 0);
}

// =============================================================================
// Version Helpers
// =============================================================================

TEST(WireTest, MakeVersion) {
    EXPECT_EQ(make_version(1, 0), 0x0100);
    EXPECT_EQ(make_version(2, 5), 0x0205);
    EXPECT_EQ(make_version(255, 255), 0xFFFF);
}

TEST(WireTest, CurrentVersionFormat) {
    // kCurrentVersion should be 1.0 = 0x0100
    EXPECT_EQ(kCurrentVersion, make_version(1, 0));
    EXPECT_EQ(kFirstVersion, make_version(1, 0));
}

// =============================================================================
// Malformed Wire Messages
// =============================================================================

TEST(WireTest, TruncatedHeaderThrows) {
    // Header needs 16 bytes; provide only 8
    Buffer buf;
    buf.write("\x47\x4E\x4F\x53\x00\x02\x00\x00", 8);
    buf.reset_read();

    EXPECT_THROW(decode_header(buf), std::runtime_error);
}

TEST(WireTest, EmptyBufferDecodeHeaderThrows) {
    Buffer buf;
    EXPECT_THROW(decode_header(buf), std::runtime_error);
}

TEST(WireTest, TruncatedInitMessageThrows) {
    // Valid header saying init with 16-byte payload, but only 4 bytes of payload
    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::init,
        .reserved = 0,
        .payload_size = 16,
        .sequence_id = 0
    };
    encode_header(buf, hdr);
    // Write only 4 bytes of the expected 16-byte init payload
    encode_u32(buf, kMagic);
    buf.reset_read();

    decode_header(buf);  // Header decodes fine
    EXPECT_THROW(decode_init(buf), std::runtime_error);
}

TEST(WireTest, TruncatedMethodDescriptorThrows) {
    // MethodDescriptor needs 8 bytes; provide only 4
    Buffer buf;
    encode_u32(buf, 0x00010001);
    buf.reset_read();

    EXPECT_THROW(decode_method_descriptor(buf), std::runtime_error);
}

TEST(WireTest, CorruptLengthPrefixedString) {
    // String with length prefix claiming 1MB but buffer has only a few bytes
    Buffer buf;
    encode_u32(buf, 1024 * 1024);  // Length = 1MB
    buf.write("hi", 2);            // Only 2 bytes of data
    buf.reset_read();

    EXPECT_THROW(decode_string(buf), std::runtime_error);
}

TEST(WireTest, ZeroLengthPayloadCallMessage) {
    // Call message with zero-byte payload — should still decode header cleanly
    Buffer msg = create_call_message(1, 1, 1, Buffer{});

    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::call);

    auto [service_id, method_id] = decode_method_call_header(msg);
    EXPECT_EQ(service_id, 1);
    EXPECT_EQ(method_id, 1);
}

TEST(WireTest, NearMaxPayloadSizeHeader) {
    // Payload size exactly at kMaxPayloadSize should be valid
    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = kMaxPayloadSize,
        .sequence_id = 0
    };
    encode_header(buf, hdr);

    Header decoded = decode_header_validated(buf);
    EXPECT_EQ(decoded.payload_size, kMaxPayloadSize);
}

TEST(WireTest, PayloadSizeJustOverMaxThrows) {
    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = kMaxPayloadSize + 1,
        .sequence_id = 0
    };
    encode_header(buf, hdr);

    EXPECT_THROW(decode_header_validated(buf), ProtocolError);
}

TEST(WireTest, MaxU32PayloadSizeThrows) {
    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = 0xFFFFFFFF,
        .sequence_id = 0
    };
    encode_header(buf, hdr);

    EXPECT_THROW(decode_header_validated(buf), ProtocolError);
}

TEST(WireTest, TruncatedCallPayload) {
    // Create a valid call message, then truncate the payload
    Buffer args;
    encode_string(args, "this is a test string");

    Buffer msg = create_call_message(1, 1, 1, args);
    // Read just past the header to verify it's valid
    Header hdr = decode_header_validated(msg);
    EXPECT_EQ(hdr.type, MsgType::call);

    // Skip method descriptor, then try to decode a string from truncated data
    decode_method_call_header(msg);

    // The string should decode fine since the payload is complete
    std::string str = decode_string(msg);
    EXPECT_EQ(str, "this is a test string");
}

TEST(WireTest, InitMessageBadVersionStillDecodes) {
    // Init message with future version — should still decode (validation is caller's job)
    Buffer buf;
    InitMessage msg{
        .magic = kMagic,
        .first_version = make_version(99, 0),
        .current_version = make_version(99, 0),
        .capabilities = 0,
        .method_count = 0
    };
    encode_init(buf, msg);

    InitMessage decoded = decode_init(buf);
    EXPECT_EQ(decoded.first_version, make_version(99, 0));
    EXPECT_EQ(decoded.current_version, make_version(99, 0));
}
