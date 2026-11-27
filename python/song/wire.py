# MIT License
# Copyright (c) 2026 dbjwhs

"""
Wire protocol constants and helpers for Song RPC.

Matches the C++ wire.hpp implementation exactly.
"""

import struct
from enum import IntEnum
from dataclasses import dataclass
from typing import Tuple

# Protocol constants
MAGIC = 0x534F4E47  # "SONG" in ASCII
HEADER_SIZE = 16
MAX_PAYLOAD_SIZE = 16 * 1024 * 1024  # 16 MB, matches wire::kMaxPayloadSize in C++

# Version constants
# Version is encoded as (major << 8) | minor, so v1.0 = 0x0100 = 256
def make_version(major: int, minor: int) -> int:
    return (major << 8) | minor

FIRST_VERSION = make_version(1, 0)   # 0x0100 = 256
CURRENT_VERSION = make_version(1, 1)  # 0x0101 = 257

# Capability bits exchanged in init / init_ack (wire.hpp Capability). The
# negotiated set is the AND of both peers'.
CAP_STREAMING = 0x00000001
CAP_HMAC = 0x00000040

# What this client supports: streaming calls. (HMAC is a property of the
# connection, not announced here, as in the C++ client.)
CLIENT_CAPABILITIES = CAP_STREAMING


class MsgType(IntEnum):
    """Message types in the Song protocol."""
    init = 0x01       # Service -> Client: initialization/capabilities
    call = 0x02       # Client -> Service: method call
    result = 0x03     # Service -> Client: successful result
    error = 0x04      # Service -> Client: error response
    stream = 0x05     # Service -> Client: stream data
    stream_end = 0x06 # Service -> Client: stream end
    ping = 0x07       # Keep-alive ping
    shutdown = 0x08   # Client -> Service: graceful shutdown request
    create = 0x09     # Client -> Service: create object
    release = 0x0A    # Client -> Service: release object
    prop_get = 0x0B   # Client -> Service: get property
    prop_set = 0x0C   # Client -> Service: set property
    init_ack = 0x0D   # Client -> Service: version/capability acknowledgment
    prop_subscribe = 0x0E    # Client -> Service: subscribe to property changes
    prop_unsubscribe = 0x0F  # Client -> Service: unsubscribe
    prop_notify = 0x10       # Service -> Client: property value changed
    object_call = 0x11       # Client -> Service: object method call


@dataclass
class Header:
    """Wire protocol header (16 bytes)."""
    magic: int
    msg_type: MsgType
    flags: int
    sequence_id: int
    payload_size: int

    def is_valid(self) -> bool:
        """Check if header has valid magic number."""
        return self.magic == MAGIC


class ProtocolError(Exception):
    """Raised when protocol violations occur."""
    pass


def encode_header(msg_type: MsgType, flags: int, sequence_id: int, payload_size: int) -> bytes:
    """
    Encode a wire protocol header.

    Header format (16 bytes, native byte order, as the C++ runtime writes it):
    - magic: u32 (4 bytes) - 0x534F4E47 "SONG"
    - flags: u8 (1 byte) - message flags
    - type: u8 (1 byte) - message type
    - reserved: u16 (2 bytes) - reserved for future use
    - payload_size: u32 (4 bytes) - size of payload following header
    - sequence_id: u32 (4 bytes) - request/response correlation
    """
    if payload_size > MAX_PAYLOAD_SIZE:
        raise ProtocolError(
            f"Message payload {payload_size} exceeds maximum {MAX_PAYLOAD_SIZE}"
        )
    return struct.pack(
        '=IBBHII',
        MAGIC,
        flags,
        msg_type,
        0,  # reserved
        payload_size,
        sequence_id
    )


def decode_header(data: bytes) -> Header:
    """
    Decode a wire protocol header.

    Args:
        data: 16 bytes of header data

    Returns:
        Header object

    Raises:
        ProtocolError: If data is wrong size or has invalid magic
    """
    if len(data) != HEADER_SIZE:
        raise ProtocolError(f"Header must be {HEADER_SIZE} bytes, got {len(data)}")

    magic, flags, msg_type, _, payload_size, sequence_id = struct.unpack(
        '=IBBHII', data
    )

    if magic != MAGIC:
        raise ProtocolError(f"Invalid magic number: 0x{magic:08X}")

    if payload_size > MAX_PAYLOAD_SIZE:
        raise ProtocolError(f"Payload size {payload_size} exceeds maximum {MAX_PAYLOAD_SIZE}")

    return Header(
        magic=magic,
        msg_type=MsgType(msg_type),
        flags=flags,
        sequence_id=sequence_id,
        payload_size=payload_size
    )


def create_init_ack_message(first_version: int, current_version: int,
                            capabilities: int) -> bytes:
    """
    The client's answer to a v1.1+ service's init message: the same payload
    layout as init (magic, first version, current version, capabilities,
    method count), with no methods.
    """
    payload = struct.pack('=IHHII', MAGIC, first_version, current_version,
                          capabilities, 0)
    return encode_header(MsgType.init_ack, 0, 0, len(payload)) + payload


def create_call_message(sequence_id: int, service_id: int, method_id: int,
                        payload: bytes) -> bytes:
    """
    Create a method call message.

    The payload should already contain the encoded method arguments.
    This prepends the service_id and method_id to the payload.
    """
    from .buffer import Buffer

    # Build full payload: service_id (u16) + method_id (u16) + args
    buf = Buffer()
    buf.encode_u16(service_id)
    buf.encode_u16(method_id)
    buf.write(payload)

    full_payload = buf.data()
    header = encode_header(MsgType.call, 0, sequence_id, len(full_payload))

    return header + full_payload


def decode_method_call_header(buf: 'Buffer') -> Tuple[int, int]:
    """
    Decode service_id and method_id from a call message payload.

    Returns:
        (service_id, method_id) tuple
    """
    service_id = buf.decode_u16()
    method_id = buf.decode_u16()
    return service_id, method_id


def create_result_message(sequence_id: int, payload: bytes) -> bytes:
    """Create a result message."""
    header = encode_header(MsgType.result, 0, sequence_id, len(payload))
    return header + payload


def create_error_message(sequence_id: int, error_code: int, message: str) -> bytes:
    """Create an error message."""
    from .buffer import Buffer

    buf = Buffer()
    buf.encode_u16(error_code)
    buf.encode_string(message)

    payload = buf.data()
    header = encode_header(MsgType.error, 0, sequence_id, len(payload))

    return header + payload


def decode_error_message(buf: 'Buffer') -> Tuple[int, str]:
    """
    Decode an error message payload.

    Returns:
        (error_code, error_message) tuple
    """
    error_code = buf.decode_u16()
    error_message = buf.decode_string()
    return error_code, error_message
