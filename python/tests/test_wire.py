# MIT License
# Copyright (c) 2026 dbjwhs

"""Tests for the Song Python wire protocol: header decoding and error messages."""

import socket
import struct
import unittest

from song.buffer import Buffer
from song.wire import (
    MAGIC,
    MsgType,
    Header,
    ProtocolError,
    decode_header,
    create_error_message,
    decode_error_message,
)
from song.connection import ServiceConnection, ServiceError


class TestDecodeHeader(unittest.TestCase):
    """decode_header must report malformed headers as ProtocolError."""

    def _header_bytes(self, msg_type: int) -> bytes:
        # <IBBHII: magic, flags, msg_type, reserved, payload_size, sequence_id
        return struct.pack('<IBBHII', MAGIC, 0, msg_type, 0, 0, 1)

    def test_unknown_message_type_raises_protocol_error(self):
        # An unrecognized message-type byte must raise ProtocolError, not the raw
        # ValueError that MsgType(...) would raise for an invalid enum value.
        for bad_type in (0x00, 0x99, 0xFF):
            with self.assertRaises(ProtocolError):
                decode_header(self._header_bytes(bad_type))

    def test_valid_message_type_decodes(self):
        header = decode_header(self._header_bytes(int(MsgType.call)))
        self.assertEqual(header.msg_type, MsgType.call)
        self.assertEqual(header.sequence_id, 1)


class TestErrorMessage(unittest.TestCase):
    """The error-message encode/decode path."""

    def test_error_message_roundtrip(self):
        msg = create_error_message(7, 42, "boom")
        header = decode_header(msg[:16])
        self.assertEqual(header.msg_type, MsgType.error)
        self.assertEqual(header.sequence_id, 7)

        code, message = decode_error_message(Buffer(msg[16:]))
        self.assertEqual(code, 42)
        self.assertEqual(message, "boom")

    def test_call_raises_service_error_on_error_reply(self):
        # Drive ServiceConnection.call() against a socketpair whose peer replies
        # with an error message, and confirm it raises ServiceError with the code
        # and message.
        near, far = socket.socketpair()
        try:
            conn = ServiceConnection("127.0.0.1", 0)
            conn._socket = near
            conn._connected = True

            # The first call uses sequence id 1; pre-load the matching error reply.
            far.sendall(create_error_message(1, 42, "boom"))

            with self.assertRaises(ServiceError) as ctx:
                conn.call(1, 1, Buffer())
            self.assertEqual(ctx.exception.code, 42)
            self.assertIn("boom", str(ctx.exception))
        finally:
            near.close()
            far.close()


if __name__ == '__main__':
    unittest.main()
