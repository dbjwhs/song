# MIT License
# Copyright (c) 2026 dbjwhs

"""Tests for the Song Python buffer implementation."""

import unittest
import struct
from song.buffer import Buffer, BufferError


class TestBuffer(unittest.TestCase):
    """Test cases for Buffer class."""

    def test_empty_buffer(self):
        """Test empty buffer creation."""
        buf = Buffer()
        self.assertEqual(buf.size(), 0)
        self.assertEqual(buf.remaining(), 0)
        self.assertFalse(buf.has_data())

    def test_write_and_read_raw(self):
        """Test raw write and read operations."""
        buf = Buffer()
        buf.write(b"hello")
        self.assertEqual(buf.size(), 5)

        buf.reset_read()
        self.assertEqual(buf.read(5), b"hello")

    def test_bool_roundtrip(self):
        """Test boolean encoding/decoding."""
        buf = Buffer()
        buf.encode_bool(True)
        buf.encode_bool(False)

        buf.reset_read()
        self.assertTrue(buf.decode_bool())
        self.assertFalse(buf.decode_bool())

    def test_i8_roundtrip(self):
        """Test i8 encoding/decoding."""
        buf = Buffer()
        buf.encode_i8(-128)
        buf.encode_i8(127)
        buf.encode_i8(0)

        buf.reset_read()
        self.assertEqual(buf.decode_i8(), -128)
        self.assertEqual(buf.decode_i8(), 127)
        self.assertEqual(buf.decode_i8(), 0)

    def test_i16_roundtrip(self):
        """Test i16 encoding/decoding."""
        buf = Buffer()
        buf.encode_i16(-32768)
        buf.encode_i16(32767)

        buf.reset_read()
        self.assertEqual(buf.decode_i16(), -32768)
        self.assertEqual(buf.decode_i16(), 32767)

    def test_i32_roundtrip(self):
        """Test i32 encoding/decoding."""
        buf = Buffer()
        buf.encode_i32(-2147483648)
        buf.encode_i32(2147483647)
        buf.encode_i32(42)

        buf.reset_read()
        self.assertEqual(buf.decode_i32(), -2147483648)
        self.assertEqual(buf.decode_i32(), 2147483647)
        self.assertEqual(buf.decode_i32(), 42)

    def test_i64_roundtrip(self):
        """Test i64 encoding/decoding."""
        buf = Buffer()
        buf.encode_i64(-9223372036854775808)
        buf.encode_i64(9223372036854775807)

        buf.reset_read()
        self.assertEqual(buf.decode_i64(), -9223372036854775808)
        self.assertEqual(buf.decode_i64(), 9223372036854775807)

    def test_u32_roundtrip(self):
        """Test u32 encoding/decoding."""
        buf = Buffer()
        buf.encode_u32(0)
        buf.encode_u32(4294967295)

        buf.reset_read()
        self.assertEqual(buf.decode_u32(), 0)
        self.assertEqual(buf.decode_u32(), 4294967295)

    def test_f32_roundtrip(self):
        """Test f32 encoding/decoding."""
        buf = Buffer()
        buf.encode_f32(3.14159)
        buf.encode_f32(-1.5)

        buf.reset_read()
        self.assertAlmostEqual(buf.decode_f32(), 3.14159, places=5)
        self.assertAlmostEqual(buf.decode_f32(), -1.5, places=5)

    def test_f64_roundtrip(self):
        """Test f64 encoding/decoding."""
        buf = Buffer()
        buf.encode_f64(3.141592653589793)
        buf.encode_f64(-2.718281828459045)

        buf.reset_read()
        self.assertAlmostEqual(buf.decode_f64(), 3.141592653589793, places=14)
        self.assertAlmostEqual(buf.decode_f64(), -2.718281828459045, places=14)

    def test_string_roundtrip(self):
        """Test string encoding/decoding."""
        buf = Buffer()
        buf.encode_string("hello")
        buf.encode_string("")
        buf.encode_string("unicode: 你好 🎉")

        buf.reset_read()
        self.assertEqual(buf.decode_string(), "hello")
        self.assertEqual(buf.decode_string(), "")
        self.assertEqual(buf.decode_string(), "unicode: 你好 🎉")

    def test_bytes_roundtrip(self):
        """Test bytes encoding/decoding."""
        buf = Buffer()
        buf.encode_bytes(b"\x00\x01\x02\xff")
        buf.encode_bytes(b"")

        buf.reset_read()
        self.assertEqual(buf.decode_bytes(), b"\x00\x01\x02\xff")
        self.assertEqual(buf.decode_bytes(), b"")

    def test_array_i32_roundtrip(self):
        """Test i32 array encoding/decoding."""
        buf = Buffer()
        buf.encode_array_i32([1, 2, 3, -4, 5])
        buf.encode_array_i32([])

        buf.reset_read()
        self.assertEqual(buf.decode_array_i32(), [1, 2, 3, -4, 5])
        self.assertEqual(buf.decode_array_i32(), [])

    def test_array_string_roundtrip(self):
        """Test string array encoding/decoding."""
        buf = Buffer()
        buf.encode_array_string(["hello", "world", ""])

        buf.reset_read()
        self.assertEqual(buf.decode_array_string(), ["hello", "world", ""])

    def test_optional_roundtrip(self):
        """Test optional encoding/decoding."""
        buf = Buffer()

        # Encode with value
        buf.encode_optional(42, lambda b, v: b.encode_i32(v))
        # Encode without value
        buf.encode_optional(None, lambda b, v: b.encode_i32(v))

        buf.reset_read()
        self.assertEqual(buf.decode_optional(lambda b: b.decode_i32()), 42)
        self.assertIsNone(buf.decode_optional(lambda b: b.decode_i32()))

    def test_buffer_underflow(self):
        """Test that reading past end raises error."""
        buf = Buffer()
        buf.encode_i32(42)

        buf.reset_read()
        buf.decode_i32()  # This should work

        with self.assertRaises(BufferError):
            buf.decode_i32()  # This should fail

    def test_little_endian_format(self):
        """Test that encoding is little-endian (matches C++)."""
        buf = Buffer()
        buf.encode_i32(0x12345678)

        # Little-endian: least significant byte first
        self.assertEqual(buf.data(), b"\x78\x56\x34\x12")

    def test_string_format(self):
        """Test string format (length-prefixed)."""
        buf = Buffer()
        buf.encode_string("hi")

        # Length (4 bytes LE) + content
        expected = struct.pack('<I', 2) + b"hi"
        self.assertEqual(buf.data(), expected)


class TestWireProtocol(unittest.TestCase):
    """Test wire protocol encoding."""

    def test_header_encoding(self):
        """Test header encoding matches C++ format."""
        from song.wire import encode_header, decode_header, MsgType, MAGIC

        header = encode_header(MsgType.call, 0, 123, 456)
        self.assertEqual(len(header), 16)

        decoded = decode_header(header)
        self.assertEqual(decoded.magic, MAGIC)
        self.assertEqual(decoded.msg_type, MsgType.call)
        self.assertEqual(decoded.sequence_id, 123)
        self.assertEqual(decoded.payload_size, 456)

    def test_invalid_magic_rejected(self):
        """Test that invalid magic number is rejected."""
        from song.wire import decode_header, ProtocolError

        # Create header with wrong magic
        bad_header = b"\x00\x00\x00\x00" + b"\x00" * 12

        with self.assertRaises(ProtocolError):
            decode_header(bad_header)


class TestBufferErrorHandling(unittest.TestCase):
    """Buffer errors surface as BufferError, matching the C++ contract."""

    def test_read_negative_length_raises(self):
        buf = Buffer(b"abc")
        with self.assertRaises(BufferError):
            buf.read(-1)
        # The read position must be intact, so remaining never exceeds size.
        self.assertLessEqual(buf.remaining(), buf.size())
        self.assertEqual(buf.remaining(), 3)

    def test_decode_string_invalid_utf8_raises_buffer_error(self):
        buf = Buffer()
        buf.encode_u32(2)
        buf.write(b"\xff\xfe")
        buf.reset_read()
        with self.assertRaises(BufferError):
            buf.decode_string()

    def test_decode_string_length_exceeds_data_raises(self):
        buf = Buffer()
        buf.encode_u32(1000)  # claim 1000 bytes
        buf.write(b"abc")     # but only 3 present
        buf.reset_read()
        pos_before = buf.remaining()
        with self.assertRaises(BufferError):
            buf.decode_string()
        # The failed read leaves the position after the length prefix; the field
        # itself was not consumed and remaining stays within bounds.
        self.assertLessEqual(buf.remaining(), buf.size())
        self.assertEqual(buf.remaining(), pos_before - 4)  # only the u32 prefix read

    def test_encode_out_of_range_raises_buffer_error(self):
        cases = [
            ("encode_i8", 128), ("encode_i8", -129),
            ("encode_u8", 256), ("encode_u8", -1),
            ("encode_u16", 65536), ("encode_u16", -1),
            ("encode_u32", 2 ** 32), ("encode_u32", -1),
            ("encode_i32", 2 ** 31), ("encode_i32", -(2 ** 31) - 1),
            ("encode_u64", 2 ** 64), ("encode_i64", 2 ** 63),
        ]
        for method_name, value in cases:
            with self.subTest(method=method_name, value=value):
                buf = Buffer()
                with self.assertRaises(BufferError):
                    getattr(buf, method_name)(value)

    def test_encode_in_range_still_works(self):
        buf = Buffer()
        buf.encode_i8(-128)
        buf.encode_u8(255)
        buf.encode_u32(2 ** 32 - 1)
        buf.reset_read()
        self.assertEqual(buf.decode_i8(), -128)
        self.assertEqual(buf.decode_u8(), 255)
        self.assertEqual(buf.decode_u32(), 2 ** 32 - 1)


if __name__ == "__main__":
    unittest.main()
