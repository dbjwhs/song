// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "error.hpp"

namespace song {
namespace wire {

// Magic number "SONG" in ASCII
constexpr u32 kMagic = 0x534F4E47;

// Protocol version
constexpr u16 kVersionMajor = 1;
constexpr u16 kVersionMinor = 0;

// Version encoding helper: major in high byte, minor in low byte
constexpr u16 make_version(u8 major, u8 minor) {
    return (static_cast<u16>(major) << 8) | minor;
}

// Current protocol version (what we speak)
constexpr u16 kCurrentVersion = make_version(kVersionMajor, kVersionMinor);

// First supported version (oldest we can talk to)
// For now, same as current since we only have v1.0
constexpr u16 kFirstVersion = make_version(1, 0);

// Message types
enum class MsgType : u8 {
    init         = 0x01,
    call         = 0x02,
    result       = 0x03,
    error        = 0x04,
    stream       = 0x05,
    stream_end   = 0x06,
    ping         = 0x07,
    shutdown     = 0x08,
};

// Message flags
enum class MsgFlags : u8 {
    none         = 0x00,
    compressed   = 0x01,  // Reserved for future
    encrypted    = 0x02,  // Reserved for future
};

// Message header structure (16 bytes fixed)
struct Header {
    u32 magic;
    MsgFlags flags;
    MsgType type;
    u16 reserved;
    u32 payload_size;
    u32 sequence_id;
};
static_assert(sizeof(Header) == 16, "Header must be 16 bytes");

// Maximum payload size (larger than individual field limits)
constexpr size_t kMaxPayloadSize = 16 * 1024 * 1024;  // 16 MB
// Note: kMaxStringSize, kMaxBytesSize, kMaxArrayCount are defined in buffer.hpp

// Method flags for capability exchange
enum class MethodFlags : u16 {
    none      = 0x0000,
    optional  = 0x0001,  // Method may not be implemented
    streaming = 0x0002,  // Method returns a stream
    oneway    = 0x0004,  // No response expected
};

// Method descriptor for capability exchange
struct MethodDescriptor {
    u16 service_id;      // Service this method belongs to
    u16 method_id;       // Method ID within the service
    MethodFlags flags;   // Method flags
    u16 reserved;        // Alignment padding
};
static_assert(sizeof(MethodDescriptor) == 8, "MethodDescriptor must be 8 bytes");

// Init message (fixed part - followed by method list)
struct InitMessage {
    u32 magic;           // kMagic
    u16 first_version;   // Oldest version we support
    u16 current_version; // Our current version
    u32 capabilities;    // Feature flags
    u32 method_count;    // Number of MethodDescriptors following
};

// Method call header
struct MethodCallHeader {
    u16 service_id;
    u16 method_id;
};

// Header encoding/decoding
void encode_header(Buffer& buf, const Header& hdr);
Header decode_header(Buffer& buf);
Header decode_header_validated(Buffer& buf);  // Throws on invalid magic

// Init message encoding/decoding
void encode_init(Buffer& buf, const InitMessage& msg);
InitMessage decode_init(Buffer& buf);

// Method descriptor encoding/decoding
void encode_method_descriptor(Buffer& buf, const MethodDescriptor& desc);
MethodDescriptor decode_method_descriptor(Buffer& buf);

// Method call encoding/decoding
void encode_method_call(Buffer& buf, u16 service_id, u16 method_id, const Buffer& args);
std::pair<u16, u16> decode_method_call_header(Buffer& buf);

// Full message creation helpers
Buffer create_init_message(u16 first_version, u16 current_version, u32 capabilities = 0);
Buffer create_init_message(u16 first_version, u16 current_version, u32 capabilities,
                          std::span<const MethodDescriptor> methods);
Buffer create_call_message(u32 sequence_id, u16 service_id, u16 method_id, const Buffer& args);
Buffer create_result_message(u32 sequence_id, const Buffer& result);
Buffer create_error_message(u32 sequence_id, ErrorCode code, const std::string& message);
Buffer create_shutdown_message();

} // namespace wire
} // namespace song
