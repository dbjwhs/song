// MIT License
// Copyright (c) 2026 dbjwhs

//! The Song wire protocol: the 16-byte header, the message types, the init
//! handshake and the message builders. Mirrors `include/song/wire.hpp`.
//!
//! Header layout (16 bytes): magic u32, flags u8, type u8, reserved u16,
//! payload_size u32, sequence_id u32.

use crate::buffer::Buffer;
use crate::error::{Error, Result};

/// "SONG" in ASCII.
pub const MAGIC: u32 = 0x534F_4E47;
pub const HEADER_SIZE: usize = 16;
/// Largest payload of one message, enforced when a message is built and on
/// every receive.
pub const MAX_PAYLOAD_SIZE: usize = 16 * 1024 * 1024;
/// Byte offset of `payload_size` in the header (the HMAC transport patches it).
pub const PAYLOAD_SIZE_OFFSET: usize = 8;

/// A version: major in the high byte, minor in the low byte.
pub const fn make_version(major: u8, minor: u8) -> u16 {
    ((major as u16) << 8) | minor as u16
}

/// The version this runtime speaks (1.1: capability negotiation, init_ack).
pub const CURRENT_VERSION: u16 = make_version(1, 1);
/// The oldest version it can talk to.
pub const FIRST_VERSION: u16 = make_version(1, 0);

/// Message types. Only the first eight and `InitAck` are used by this
/// runtime; the object and property types are listed so a header naming one
/// decodes to a name.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MsgType {
    Init = 0x01,
    Call = 0x02,
    Result = 0x03,
    Error = 0x04,
    Stream = 0x05,
    StreamEnd = 0x06,
    Ping = 0x07,
    Shutdown = 0x08,
    Create = 0x09,
    Release = 0x0A,
    PropGet = 0x0B,
    PropSet = 0x0C,
    InitAck = 0x0D,
    PropSubscribe = 0x0E,
    PropUnsubscribe = 0x0F,
    PropNotify = 0x10,
    ObjectCall = 0x11,
}

impl MsgType {
    pub fn from_u8(value: u8) -> Option<MsgType> {
        Some(match value {
            0x01 => MsgType::Init,
            0x02 => MsgType::Call,
            0x03 => MsgType::Result,
            0x04 => MsgType::Error,
            0x05 => MsgType::Stream,
            0x06 => MsgType::StreamEnd,
            0x07 => MsgType::Ping,
            0x08 => MsgType::Shutdown,
            0x09 => MsgType::Create,
            0x0A => MsgType::Release,
            0x0B => MsgType::PropGet,
            0x0C => MsgType::PropSet,
            0x0D => MsgType::InitAck,
            0x0E => MsgType::PropSubscribe,
            0x0F => MsgType::PropUnsubscribe,
            0x10 => MsgType::PropNotify,
            0x11 => MsgType::ObjectCall,
            _ => return None,
        })
    }
}

/// Message flags (u8).
pub mod flags {
    pub const NONE: u8 = 0x00;
    pub const COMPRESSED: u8 = 0x01;
    pub const ENCRYPTED: u8 = 0x02;
}

/// Capability bits exchanged in the init handshake; the negotiated set is
/// the AND of both peers'. Bits 0-15 are protocol features, 16-23 runtime
/// extensions, 24-31 vendor slots.
pub mod capability {
    pub const NONE: u32 = 0x0000_0000;
    pub const STREAMING: u32 = 0x0000_0001;
    pub const COMPRESSION: u32 = 0x0000_0002;
    pub const TLS: u32 = 0x0000_0004;
    pub const PROPERTIES: u32 = 0x0000_0008;
    pub const OBJECTS: u32 = 0x0000_0010;
    pub const ONEWAY: u32 = 0x0000_0020;
    pub const HMAC: u32 = 0x0000_0040;
    /// The first extension slot; slot n is `EXT_0 << n`, n in 0..8.
    pub const EXT_0: u32 = 0x0001_0000;
    /// The first vendor slot; slot n is `VENDOR_0 << n`, n in 0..8.
    pub const VENDOR_0: u32 = 0x0100_0000;
}

/// Method flags in a method descriptor (u16).
pub mod method_flags {
    pub const NONE: u16 = 0x0000;
    pub const OPTIONAL: u16 = 0x0001;
    pub const STREAMING: u16 = 0x0002;
    pub const ONEWAY: u16 = 0x0004;
}

/// The fixed header of every message.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Header {
    pub magic: u32,
    pub flags: u8,
    /// The raw type byte; `msg_type()` names it.
    pub type_byte: u8,
    pub reserved: u16,
    pub payload_size: u32,
    pub sequence_id: u32,
}

impl Header {
    pub fn new(msg_type: MsgType, sequence_id: u32, payload_size: u32) -> Header {
        Header {
            magic: MAGIC,
            flags: flags::NONE,
            type_byte: msg_type as u8,
            reserved: 0,
            payload_size,
            sequence_id,
        }
    }

    /// The message type, or None for a type byte this runtime does not know.
    pub fn msg_type(&self) -> Option<MsgType> {
        MsgType::from_u8(self.type_byte)
    }
}

pub fn encode_header(buf: &mut Buffer, hdr: &Header) {
    buf.encode_u32(hdr.magic);
    buf.encode_u8(hdr.flags);
    buf.encode_u8(hdr.type_byte);
    buf.encode_u16(hdr.reserved);
    buf.encode_u32(hdr.payload_size);
    buf.encode_u32(hdr.sequence_id);
}

/// A header as it is on the wire, unchecked.
pub fn decode_header(buf: &mut Buffer) -> Result<Header> {
    Ok(Header {
        magic: buf.decode_u32()?,
        flags: buf.decode_u8()?,
        type_byte: buf.decode_u8()?,
        reserved: buf.decode_u16()?,
        payload_size: buf.decode_u32()?,
        sequence_id: buf.decode_u32()?,
    })
}

/// A header, refused when the magic is wrong or the payload is over the cap.
pub fn decode_header_validated(buf: &mut Buffer) -> Result<Header> {
    let hdr = decode_header(buf)?;
    if hdr.magic != MAGIC {
        return Err(Error::protocol(format!("invalid magic number 0x{:08X}", hdr.magic)));
    }
    if hdr.payload_size as usize > MAX_PAYLOAD_SIZE {
        return Err(Error::protocol("payload size exceeds maximum"));
    }
    Ok(hdr)
}

/// One method a service announces in its init message (8 bytes).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MethodDescriptor {
    pub service_id: u16,
    pub method_id: u16,
    pub flags: u16,
    pub reserved: u16,
}

pub fn encode_method_descriptor(buf: &mut Buffer, desc: &MethodDescriptor) {
    buf.encode_u16(desc.service_id);
    buf.encode_u16(desc.method_id);
    buf.encode_u16(desc.flags);
    buf.encode_u16(desc.reserved);
}

pub fn decode_method_descriptor(buf: &mut Buffer) -> Result<MethodDescriptor> {
    Ok(MethodDescriptor {
        service_id: buf.decode_u16()?,
        method_id: buf.decode_u16()?,
        flags: buf.decode_u16()?,
        reserved: buf.decode_u16()?,
    })
}

/// The fixed part of an init or init_ack payload; method descriptors follow.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InitMessage {
    pub magic: u32,
    pub first_version: u16,
    pub current_version: u16,
    pub capabilities: u32,
    pub method_count: u32,
}

pub fn encode_init(buf: &mut Buffer, msg: &InitMessage) {
    buf.encode_u32(msg.magic);
    buf.encode_u16(msg.first_version);
    buf.encode_u16(msg.current_version);
    buf.encode_u32(msg.capabilities);
    buf.encode_u32(msg.method_count);
}

pub fn decode_init(buf: &mut Buffer) -> Result<InitMessage> {
    let magic = buf.decode_u32()?;
    if magic != MAGIC {
        return Err(Error::protocol("invalid magic in init message"));
    }
    Ok(InitMessage {
        magic,
        first_version: buf.decode_u16()?,
        current_version: buf.decode_u16()?,
        capabilities: buf.decode_u32()?,
        method_count: buf.decode_u32()?,
    })
}

/// The (service id, method id) that open a call payload.
pub fn decode_method_call_header(buf: &mut Buffer) -> Result<(u16, u16)> {
    Ok((buf.decode_u16()?, buf.decode_u16()?))
}

/// The (code, text) of an error payload.
pub fn decode_error(buf: &mut Buffer) -> Result<(u16, String)> {
    Ok((buf.decode_u16()?, buf.decode_string()?))
}

/// A whole message: the header, then the payload. Refuses a payload the
/// peer is bound to reject.
fn message(msg_type: MsgType, sequence_id: u32, payload: &[u8]) -> Result<Buffer> {
    if payload.len() > MAX_PAYLOAD_SIZE {
        return Err(Error::protocol("message payload exceeds maximum size (16 MB)"));
    }
    let mut buf = Buffer::new();
    encode_header(&mut buf, &Header::new(msg_type, sequence_id, payload.len() as u32));
    buf.write(payload);
    Ok(buf)
}

pub fn create_init_message(
    first_version: u16,
    current_version: u16,
    capabilities: u32,
    methods: &[MethodDescriptor],
) -> Result<Buffer> {
    let mut payload = Buffer::new();
    encode_init(
        &mut payload,
        &InitMessage {
            magic: MAGIC,
            first_version,
            current_version,
            capabilities,
            method_count: methods.len() as u32,
        },
    );
    for method in methods {
        encode_method_descriptor(&mut payload, method);
    }
    message(MsgType::Init, 0, payload.data())
}

pub fn create_init_ack_message(first_version: u16, current_version: u16, capabilities: u32) -> Result<Buffer> {
    let mut payload = Buffer::new();
    encode_init(
        &mut payload,
        &InitMessage { magic: MAGIC, first_version, current_version, capabilities, method_count: 0 },
    );
    message(MsgType::InitAck, 0, payload.data())
}

pub fn create_call_message(sequence_id: u32, service_id: u16, method_id: u16, args: &[u8]) -> Result<Buffer> {
    let mut payload = Buffer::new();
    payload.encode_u16(service_id);
    payload.encode_u16(method_id);
    payload.write(args);
    message(MsgType::Call, sequence_id, payload.data())
}

pub fn create_result_message(sequence_id: u32, result: &[u8]) -> Result<Buffer> {
    message(MsgType::Result, sequence_id, result)
}

pub fn create_error_message(sequence_id: u32, code: u16, text: &str) -> Result<Buffer> {
    let mut payload = Buffer::new();
    payload.encode_u16(code);
    payload.encode_string(text)?;
    message(MsgType::Error, sequence_id, payload.data())
}

pub fn create_stream_message(sequence_id: u32, chunk: &[u8]) -> Result<Buffer> {
    message(MsgType::Stream, sequence_id, chunk)
}

pub fn create_stream_end_message(sequence_id: u32) -> Result<Buffer> {
    message(MsgType::StreamEnd, sequence_id, &[])
}

pub fn create_shutdown_message() -> Result<Buffer> {
    message(MsgType::Shutdown, 0, &[])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_and_sizes() {
        assert_eq!(make_version(1, 1), 0x0101);
        assert_eq!(FIRST_VERSION, 0x0100);
        assert_eq!(CURRENT_VERSION, 0x0101);
        let mut b = Buffer::new();
        encode_header(&mut b, &Header::new(MsgType::Ping, 1, 0));
        assert_eq!(b.size(), HEADER_SIZE);
    }

    #[test]
    fn header_round_trips_and_names_its_type() {
        let mut b = Buffer::new();
        let hdr = Header { magic: MAGIC, flags: flags::ENCRYPTED, type_byte: 0x05, reserved: 0, payload_size: 9, sequence_id: 77 };
        encode_header(&mut b, &hdr);
        let back = decode_header_validated(&mut b).unwrap();
        assert_eq!(back, hdr);
        assert_eq!(back.msg_type(), Some(MsgType::Stream));
        for value in 0x01..=0x11u8 {
            assert_eq!(MsgType::from_u8(value).map(|t| t as u8), Some(value));
        }
        assert_eq!(MsgType::from_u8(0), None);
        assert_eq!(MsgType::from_u8(0x12), None);
    }

    #[test]
    fn a_bad_magic_or_an_oversized_payload_is_refused() {
        let mut b = Buffer::new();
        encode_header(&mut b, &Header { magic: 0x1234_5678, ..Header::new(MsgType::Call, 1, 0) });
        assert!(matches!(decode_header_validated(&mut b), Err(Error::Protocol(_))));

        let mut b = Buffer::new();
        encode_header(&mut b, &Header::new(MsgType::Call, 1, MAX_PAYLOAD_SIZE as u32 + 1));
        assert!(matches!(decode_header_validated(&mut b.clone()), Err(Error::Protocol(_))));
        assert!(decode_header(&mut b).is_ok(), "the unchecked decoder reports what is there");

        let mut short = Buffer::from_slice(&[0u8; 15]);
        assert!(matches!(decode_header(&mut short), Err(Error::Decode(_))));
    }

    #[test]
    fn a_message_over_the_payload_cap_is_not_built() {
        let big = vec![0u8; MAX_PAYLOAD_SIZE + 1];
        assert!(matches!(create_result_message(1, &big), Err(Error::Protocol(_))));
        assert!(create_result_message(1, &big[..MAX_PAYLOAD_SIZE]).is_ok());
    }

    #[test]
    fn an_init_payload_with_the_wrong_magic_is_refused() {
        let mut b = Buffer::new();
        encode_init(&mut b, &InitMessage { magic: 1, first_version: 0, current_version: 0, capabilities: 0, method_count: 0 });
        assert!(matches!(decode_init(&mut b), Err(Error::Protocol(_))));
    }
}
