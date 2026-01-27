# MIT License
# Copyright (c) 2026 dbjwhs

"""
Buffer class for Song wire protocol serialization.

All integers are little-endian to match the C++ implementation.
"""

import struct
from typing import List, Optional, Callable, TypeVar

T = TypeVar('T')


class BufferError(Exception):
    """Raised when buffer operations fail."""
    pass


class Buffer:
    """
    A byte buffer for serializing and deserializing Song wire protocol data.

    Matches the C++ Buffer class wire format exactly.
    """

    def __init__(self, data: bytes = b""):
        self._data = bytearray(data)
        self._read_pos = 0

    # =========================================================================
    # Basic Operations
    # =========================================================================

    def write(self, data: bytes) -> None:
        """Write raw bytes to the buffer."""
        self._data.extend(data)

    def read(self, length: int) -> bytes:
        """Read raw bytes from the buffer."""
        if self._read_pos + length > len(self._data):
            raise BufferError(
                f"Buffer underflow: attempted to read {length} bytes "
                f"but only {len(self._data) - self._read_pos} available"
            )
        result = bytes(self._data[self._read_pos:self._read_pos + length])
        self._read_pos += length
        return result

    def data(self) -> bytes:
        """Get all data in the buffer."""
        return bytes(self._data)

    def size(self) -> int:
        """Get current buffer size."""
        return len(self._data)

    def remaining(self) -> int:
        """Get remaining bytes to read."""
        return len(self._data) - self._read_pos

    def has_data(self) -> bool:
        """Check if more data available to read."""
        return self._read_pos < len(self._data)

    def reset(self) -> None:
        """Reset buffer (clear all data)."""
        self._data = bytearray()
        self._read_pos = 0

    def reset_read(self) -> None:
        """Reset read position to beginning."""
        self._read_pos = 0

    # =========================================================================
    # Primitive Encoding (Little Endian)
    # =========================================================================

    def encode_bool(self, val: bool) -> None:
        """Encode a boolean (1 byte)."""
        self._data.append(1 if val else 0)

    def encode_i8(self, val: int) -> None:
        """Encode a signed 8-bit integer."""
        self._data.extend(struct.pack('<b', val))

    def encode_i16(self, val: int) -> None:
        """Encode a signed 16-bit integer."""
        self._data.extend(struct.pack('<h', val))

    def encode_i32(self, val: int) -> None:
        """Encode a signed 32-bit integer."""
        self._data.extend(struct.pack('<i', val))

    def encode_i64(self, val: int) -> None:
        """Encode a signed 64-bit integer."""
        self._data.extend(struct.pack('<q', val))

    def encode_u8(self, val: int) -> None:
        """Encode an unsigned 8-bit integer."""
        self._data.extend(struct.pack('<B', val))

    def encode_u16(self, val: int) -> None:
        """Encode an unsigned 16-bit integer."""
        self._data.extend(struct.pack('<H', val))

    def encode_u32(self, val: int) -> None:
        """Encode an unsigned 32-bit integer."""
        self._data.extend(struct.pack('<I', val))

    def encode_u64(self, val: int) -> None:
        """Encode an unsigned 64-bit integer."""
        self._data.extend(struct.pack('<Q', val))

    def encode_f32(self, val: float) -> None:
        """Encode a 32-bit float."""
        self._data.extend(struct.pack('<f', val))

    def encode_f64(self, val: float) -> None:
        """Encode a 64-bit float."""
        self._data.extend(struct.pack('<d', val))

    def encode_string(self, val: str) -> None:
        """Encode a string (length-prefixed UTF-8)."""
        encoded = val.encode('utf-8')
        if len(encoded) > 0xFFFFFFFF:
            raise BufferError("String too large to encode")
        self.encode_u32(len(encoded))
        self._data.extend(encoded)

    def encode_bytes(self, val: bytes) -> None:
        """Encode raw bytes (length-prefixed)."""
        if len(val) > 0xFFFFFFFF:
            raise BufferError("Bytes too large to encode")
        self.encode_u32(len(val))
        self._data.extend(val)

    # =========================================================================
    # Primitive Decoding (Little Endian)
    # =========================================================================

    def decode_bool(self) -> bool:
        """Decode a boolean."""
        return self.read(1)[0] != 0

    def decode_i8(self) -> int:
        """Decode a signed 8-bit integer."""
        return struct.unpack('<b', self.read(1))[0]

    def decode_i16(self) -> int:
        """Decode a signed 16-bit integer."""
        return struct.unpack('<h', self.read(2))[0]

    def decode_i32(self) -> int:
        """Decode a signed 32-bit integer."""
        return struct.unpack('<i', self.read(4))[0]

    def decode_i64(self) -> int:
        """Decode a signed 64-bit integer."""
        return struct.unpack('<q', self.read(8))[0]

    def decode_u8(self) -> int:
        """Decode an unsigned 8-bit integer."""
        return struct.unpack('<B', self.read(1))[0]

    def decode_u16(self) -> int:
        """Decode an unsigned 16-bit integer."""
        return struct.unpack('<H', self.read(2))[0]

    def decode_u32(self) -> int:
        """Decode an unsigned 32-bit integer."""
        return struct.unpack('<I', self.read(4))[0]

    def decode_u64(self) -> int:
        """Decode an unsigned 64-bit integer."""
        return struct.unpack('<Q', self.read(8))[0]

    def decode_f32(self) -> float:
        """Decode a 32-bit float."""
        return struct.unpack('<f', self.read(4))[0]

    def decode_f64(self) -> float:
        """Decode a 64-bit float."""
        return struct.unpack('<d', self.read(8))[0]

    def decode_string(self) -> str:
        """Decode a string."""
        length = self.decode_u32()
        return self.read(length).decode('utf-8')

    def decode_bytes(self) -> bytes:
        """Decode raw bytes."""
        length = self.decode_u32()
        return self.read(length)

    # =========================================================================
    # Array Encoding/Decoding
    # =========================================================================

    def encode_array_i32(self, arr: List[int]) -> None:
        """Encode an array of i32."""
        self.encode_u32(len(arr))
        for val in arr:
            self.encode_i32(val)

    def decode_array_i32(self) -> List[int]:
        """Decode an array of i32."""
        count = self.decode_u32()
        return [self.decode_i32() for _ in range(count)]

    def encode_array_i64(self, arr: List[int]) -> None:
        """Encode an array of i64."""
        self.encode_u32(len(arr))
        for val in arr:
            self.encode_i64(val)

    def decode_array_i64(self) -> List[int]:
        """Decode an array of i64."""
        count = self.decode_u32()
        return [self.decode_i64() for _ in range(count)]

    def encode_array_f32(self, arr: List[float]) -> None:
        """Encode an array of f32."""
        self.encode_u32(len(arr))
        for val in arr:
            self.encode_f32(val)

    def decode_array_f32(self) -> List[float]:
        """Decode an array of f32."""
        count = self.decode_u32()
        return [self.decode_f32() for _ in range(count)]

    def encode_array_f64(self, arr: List[float]) -> None:
        """Encode an array of f64."""
        self.encode_u32(len(arr))
        for val in arr:
            self.encode_f64(val)

    def decode_array_f64(self) -> List[float]:
        """Decode an array of f64."""
        count = self.decode_u32()
        return [self.decode_f64() for _ in range(count)]

    def encode_array_string(self, arr: List[str]) -> None:
        """Encode an array of strings."""
        self.encode_u32(len(arr))
        for val in arr:
            self.encode_string(val)

    def decode_array_string(self) -> List[str]:
        """Decode an array of strings."""
        count = self.decode_u32()
        return [self.decode_string() for _ in range(count)]

    def encode_array(self, arr: List[T], encoder: Callable[['Buffer', T], None]) -> None:
        """Encode an array using a custom encoder function."""
        self.encode_u32(len(arr))
        for val in arr:
            encoder(self, val)

    def decode_array(self, decoder: Callable[['Buffer'], T]) -> List[T]:
        """Decode an array using a custom decoder function."""
        count = self.decode_u32()
        return [decoder(self) for _ in range(count)]

    # =========================================================================
    # Optional Encoding/Decoding
    # =========================================================================

    def encode_optional(self, val: Optional[T], encoder: Callable[['Buffer', T], None]) -> None:
        """Encode an optional value."""
        if val is None:
            self.encode_bool(False)
        else:
            self.encode_bool(True)
            encoder(self, val)

    def decode_optional(self, decoder: Callable[['Buffer'], T]) -> Optional[T]:
        """Decode an optional value."""
        has_value = self.decode_bool()
        if has_value:
            return decoder(self)
        return None
