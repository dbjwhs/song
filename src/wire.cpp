// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/wire.hpp"
#include <cstring>
#include <span>

namespace song {
namespace wire {

void encode_header(Buffer& buf, const Header& hdr) {
    encode_u32(buf, hdr.magic);
    encode_u8(buf, static_cast<u8>(hdr.flags));
    encode_u8(buf, static_cast<u8>(hdr.type));
    encode_u16(buf, hdr.reserved);
    encode_u32(buf, hdr.payload_size);
    encode_u32(buf, hdr.sequence_id);
}

Header decode_header(Buffer& buf) {
    Header hdr;
    hdr.magic = decode_u32(buf);
    hdr.flags = static_cast<MsgFlags>(decode_u8(buf));
    hdr.type = static_cast<MsgType>(decode_u8(buf));
    hdr.reserved = decode_u16(buf);
    hdr.payload_size = decode_u32(buf);
    hdr.sequence_id = decode_u32(buf);
    return hdr;
}

Header decode_header_validated(Buffer& buf) {
    Header hdr = decode_header(buf);
    if (hdr.magic != kMagic) {
        throw ProtocolError("Invalid magic number in message header");
    }
    if (hdr.payload_size > kMaxPayloadSize) {
        throw ProtocolError("Payload size exceeds maximum");
    }
    return hdr;
}

void encode_init(Buffer& buf, const InitMessage& msg) {
    encode_u32(buf, msg.magic);
    encode_u16(buf, msg.first_version);
    encode_u16(buf, msg.current_version);
    encode_u32(buf, msg.capabilities);
    encode_u32(buf, msg.method_count);
}

InitMessage decode_init(Buffer& buf) {
    InitMessage msg;
    msg.magic = decode_u32(buf);
    if (msg.magic != kMagic) {
        throw ProtocolError("Invalid magic in init message");
    }
    msg.first_version = decode_u16(buf);
    msg.current_version = decode_u16(buf);
    msg.capabilities = decode_u32(buf);
    msg.method_count = decode_u32(buf);
    return msg;
}

void encode_method_descriptor(Buffer& buf, const MethodDescriptor& desc) {
    encode_u16(buf, desc.service_id);
    encode_u16(buf, desc.method_id);
    encode_u16(buf, static_cast<u16>(desc.flags));
    encode_u16(buf, desc.reserved);
}

MethodDescriptor decode_method_descriptor(Buffer& buf) {
    MethodDescriptor desc;
    desc.service_id = decode_u16(buf);
    desc.method_id = decode_u16(buf);
    desc.flags = static_cast<MethodFlags>(decode_u16(buf));
    desc.reserved = decode_u16(buf);
    return desc;
}

void encode_method_call(Buffer& buf, u16 service_id, u16 method_id, const Buffer& args) {
    encode_u16(buf, service_id);
    encode_u16(buf, method_id);
    // Append argument data
    buf.write(args.data(), args.size());
}

std::pair<u16, u16> decode_method_call_header(Buffer& buf) {
    u16 service_id = decode_u16(buf);
    u16 method_id = decode_u16(buf);
    return {service_id, method_id};
}

Buffer create_init_message(u16 first_version, u16 current_version, u32 capabilities) {
    Buffer payload;
    InitMessage msg{kMagic, first_version, current_version, capabilities, 0};
    encode_init(payload, msg);

    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::init,
        .reserved = 0,
        .payload_size = static_cast<u32>(payload.size()),
        .sequence_id = 0
    };
    encode_header(buf, hdr);
    buf.write(payload.data(), payload.size());
    return buf;
}

Buffer create_init_message(u16 first_version, u16 current_version, u32 capabilities,
                          std::span<const MethodDescriptor> methods) {
    Buffer payload;
    InitMessage msg{kMagic, first_version, current_version, capabilities,
                    static_cast<u32>(methods.size())};
    encode_init(payload, msg);

    // Encode method descriptors
    for (const auto& method : methods) {
        encode_method_descriptor(payload, method);
    }

    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::init,
        .reserved = 0,
        .payload_size = static_cast<u32>(payload.size()),
        .sequence_id = 0
    };
    encode_header(buf, hdr);
    buf.write(payload.data(), payload.size());
    return buf;
}

Buffer create_call_message(u32 sequence_id, u16 service_id, u16 method_id, const Buffer& args) {
    Buffer payload;
    encode_method_call(payload, service_id, method_id, args);

    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::call,
        .reserved = 0,
        .payload_size = static_cast<u32>(payload.size()),
        .sequence_id = sequence_id
    };
    encode_header(buf, hdr);
    buf.write(payload.data(), payload.size());
    return buf;
}

Buffer create_result_message(u32 sequence_id, const Buffer& result) {
    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::result,
        .reserved = 0,
        .payload_size = static_cast<u32>(result.size()),
        .sequence_id = sequence_id
    };
    encode_header(buf, hdr);
    buf.write(result.data(), result.size());
    return buf;
}

Buffer create_error_message(u32 sequence_id, ErrorCode code, const std::string& message) {
    Buffer payload;
    encode_u16(payload, static_cast<u16>(code));
    encode_string(payload, message);

    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::error,
        .reserved = 0,
        .payload_size = static_cast<u32>(payload.size()),
        .sequence_id = sequence_id
    };
    encode_header(buf, hdr);
    buf.write(payload.data(), payload.size());
    return buf;
}

Buffer create_shutdown_message() {
    Buffer buf;
    Header hdr{
        .magic = kMagic,
        .flags = MsgFlags::none,
        .type = MsgType::shutdown,
        .reserved = 0,
        .payload_size = 0,
        .sequence_id = 0
    };
    encode_header(buf, hdr);
    return buf;
}

} // namespace wire
} // namespace song
