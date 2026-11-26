# MIT License
# Copyright (c) 2026 dbjwhs

"""
The Python encoders against test/golden/wire_vectors.txt, the byte vectors
the C++ and Rust tests check too (test/golden_test.cpp, rust/tests/golden.rs).

The Python runtime is a client: it has no builder for init, init_ack,
stream, stream_end or shutdown messages, so those vectors are checked on the
decode side only (their headers) and are listed in NOT_BUILT.
"""

import unittest
from pathlib import Path

from song.buffer import Buffer
from song.connection import ServiceConnection
from song.wire import (
    HEADER_SIZE,
    MsgType,
    create_call_message,
    create_error_message,
    create_result_message,
    decode_error_message,
    decode_header,
    encode_header,
)

VECTOR_FILE = Path(__file__).resolve().parents[2] / "test" / "golden" / "wire_vectors.txt"

# Vectors the Python client has no encoder for.
NOT_BUILT = {"msg_init", "msg_init_ack", "msg_stream", "msg_stream_end", "msg_shutdown", "hmac_key"}


def load_vectors() -> dict:
    out = {}
    for line in VECTOR_FILE.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, value = line.split("=", 1)
        out[name.strip()] = bytes.fromhex(value.strip())
    return out


def buf(build) -> bytes:
    b = Buffer()
    build(b)
    return b.data()


def i32_args(*values) -> bytes:
    return buf(lambda b: [b.encode_i32(v) for v in values])


class GoldenVectorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vectors = load_vectors()
        cls.key = cls.vectors["hmac_key"]

    def tagged(self, message: bytes) -> bytes:
        """What a keyed connection puts on the wire for message."""
        sent = []
        conn = ServiceConnection("127.0.0.1", 0, key=self.key)
        conn._send_all = sent.append
        conn._send_message(message)
        return b"".join(sent)

    def built(self) -> dict:
        call = create_call_message(7, 1, 2, i32_args(5, 9))
        return {
            "bool_true": buf(lambda b: b.encode_bool(True)),
            "bool_false": buf(lambda b: b.encode_bool(False)),
            "i8_minus_2": buf(lambda b: b.encode_i8(-2)),
            "i16_minus_2": buf(lambda b: b.encode_i16(-2)),
            "i32_0x12345678": buf(lambda b: b.encode_i32(0x12345678)),
            "i64_minus_2": buf(lambda b: b.encode_i64(-2)),
            "u8_0xab": buf(lambda b: b.encode_u8(0xAB)),
            "u16_0xbeef": buf(lambda b: b.encode_u16(0xBEEF)),
            "u32_0xdeadbeef": buf(lambda b: b.encode_u32(0xDEADBEEF)),
            "u64_0x0102030405060708": buf(lambda b: b.encode_u64(0x0102030405060708)),
            "f32_1_5": buf(lambda b: b.encode_f32(1.5)),
            "f64_minus_2_25": buf(lambda b: b.encode_f64(-2.25)),
            "string_hello": buf(lambda b: b.encode_string("hello, song")),
            "string_empty": buf(lambda b: b.encode_string("")),
            "string_utf8": buf(lambda b: b.encode_string("héllo ♫")),
            "bytes_00_ff_10": buf(lambda b: b.encode_bytes(b"\x00\xff\x10")),
            "array_i32": buf(lambda b: b.encode_array_i32([1, -1, 65536])),
            "array_string": buf(lambda b: b.encode_array_string(["a", "bc"])),
            "array_nested_i32": buf(lambda b: b.encode_array(
                [[1, 2], [], [3]], lambda bb, inner: bb.encode_array_i32(inner))),
            "optional_i32_some_7": buf(lambda b: b.encode_optional(7, lambda bb, v: bb.encode_i32(v))),
            "optional_none": buf(lambda b: b.encode_optional(None, lambda bb, v: bb.encode_i32(v))),
            "header_call_seq7_size4": encode_header(MsgType.call, 0, 7, 4),
            "msg_call": call,
            "msg_result": create_result_message(7, i32_args(14)),
            "msg_error": create_error_message(7, 3, "boom"),
            "msg_call_tagged": self.tagged(call),
            "msg_stream_end_tagged": self.tagged(encode_header(MsgType.stream_end, 0, 9, 0)),
        }

    def test_encoders_match_the_shared_vectors(self):
        ours = self.built()
        for name, want in self.vectors.items():
            if name in NOT_BUILT:
                continue
            self.assertIn(name, ours, f"no Python encoder for golden vector {name}")
            self.assertEqual(ours[name].hex(), want.hex(), f"golden vector {name}")
        for name in list(ours) + sorted(NOT_BUILT):
            self.assertIn(name, self.vectors, f"{name} is not in the golden vector file")

    def test_decoders_read_the_shared_vectors(self):
        v = self.vectors
        self.assertEqual(Buffer(v["string_utf8"]).decode_string(), "héllo ♫")
        self.assertEqual(Buffer(v["bytes_00_ff_10"]).decode_bytes(), b"\x00\xff\x10")
        self.assertEqual(Buffer(v["array_i32"]).decode_array_i32(), [1, -1, 65536])
        self.assertEqual(Buffer(v["array_nested_i32"]).decode_array(lambda b: b.decode_array_i32()),
                         [[1, 2], [], [3]])
        self.assertEqual(Buffer(v["optional_i32_some_7"]).decode_optional(lambda b: b.decode_i32()), 7)
        self.assertIsNone(Buffer(v["optional_none"]).decode_optional(lambda b: b.decode_i32()))
        for name, msg_type, seq in (("msg_init", MsgType.init, 0), ("msg_init_ack", MsgType.init_ack, 0),
                                    ("msg_stream", MsgType.stream, 9), ("msg_stream_end", MsgType.stream_end, 9),
                                    ("msg_shutdown", MsgType.shutdown, 0), ("msg_error", MsgType.error, 7)):
            header = decode_header(v[name][:HEADER_SIZE])
            self.assertEqual((header.msg_type, header.sequence_id, header.payload_size),
                             (msg_type, seq, len(v[name]) - HEADER_SIZE), name)
        self.assertEqual(decode_error_message(Buffer(v["msg_error"][HEADER_SIZE:])), (3, "boom"))


if __name__ == "__main__":
    unittest.main()
