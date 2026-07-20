# MIT License
# Copyright (c) 2026 dbjwhs

"""Tests for ServiceConnection framing, handshake, and response validation."""

import socket
import unittest

from song.buffer import Buffer
from song.connection import ServiceConnection, ConnectionError
from song.wire import (
    MAX_PAYLOAD_SIZE,
    MsgType,
    ProtocolError,
    encode_header,
    decode_header,
    create_result_message,
)


class _ScriptedSocket:
    """Minimal socket stand-in. recv() yields queued bytes in fixed-size chunks;
    send()/sendall() discard; recv() can raise socket.timeout when exhausted."""

    def __init__(self, data=b"", chunk=4096, timeout_after=False):
        self._data = data
        self._chunk = chunk
        self._pos = 0
        self._timeout_after = timeout_after

    def recv(self, n):
        if self._pos >= len(self._data):
            if self._timeout_after:
                raise socket.timeout()
            return b""  # EOF
        end = min(self._pos + min(n, self._chunk), len(self._data))
        out = self._data[self._pos:end]
        self._pos = end
        return out

    def send(self, data):
        return len(data)

    def sendall(self, data):
        return None

    def settimeout(self, timeout):
        pass

    def close(self):
        pass


def _make_conn(sock):
    conn = ServiceConnection("127.0.0.1", 0)
    conn._socket = sock
    conn._connected = True
    return conn


class TestRecvExact(unittest.TestCase):
    def test_reassembles_bytes_delivered_one_at_a_time(self):
        payload = bytes(range(16))
        conn = _make_conn(_ScriptedSocket(payload, chunk=1))
        self.assertEqual(conn._recv_exact(16), payload)
        self.assertTrue(conn._connected)

    def test_eof_mid_read_raises_and_disconnects(self):
        conn = _make_conn(_ScriptedSocket(b"abc", chunk=1))  # only 3 of 16
        with self.assertRaises(ConnectionError):
            conn._recv_exact(16)
        self.assertFalse(conn._connected)

    def test_timeout_raises_and_disconnects(self):
        # Regression: a mid-read timeout must mark the connection unusable so a
        # desynchronized socket is not reused.
        conn = _make_conn(_ScriptedSocket(b"ab", chunk=1, timeout_after=True))
        with self.assertRaises(ConnectionError):
            conn._recv_exact(16)
        self.assertFalse(conn._connected)


class TestHandshake(unittest.TestCase):
    def _init_payload(self, magic):
        buf = Buffer()
        buf.encode_u32(magic)      # magic
        buf.encode_u16(0x0100)     # first_version
        buf.encode_u16(0x0101)     # current_version
        buf.encode_u32(0)          # capabilities
        buf.encode_u32(0)          # method_count
        return buf.data()

    def test_non_init_message_rejected(self):
        msg = create_result_message(0, b"")  # a result where init is expected
        conn = _make_conn(_ScriptedSocket(msg))
        with self.assertRaises(ProtocolError):
            conn._init_handshake()

    def test_wrong_magic_in_init_payload_rejected(self):
        payload = self._init_payload(0xDEADBEEF)
        header = encode_header(MsgType.init, 0, 0, len(payload))
        conn = _make_conn(_ScriptedSocket(header + payload))
        with self.assertRaises(ProtocolError):
            conn._init_handshake()


class TestCallResponseValidation(unittest.TestCase):
    def _conn_with_reply(self, reply):
        near, far = socket.socketpair()
        conn = ServiceConnection("127.0.0.1", 0)
        conn._socket = near
        conn._connected = True
        far.sendall(reply)
        return conn, near, far

    def test_sequence_id_mismatch_raises(self):
        # The first call expects seq id 1; reply with 2.
        conn, near, far = self._conn_with_reply(create_result_message(2, b""))
        try:
            with self.assertRaises(ProtocolError):
                conn.call(1, 1, Buffer())
        finally:
            near.close()
            far.close()

    def test_unexpected_response_type_raises(self):
        # Correct seq id but a type that is not a valid call reply.
        header = encode_header(MsgType.ping, 0, 1, 0)
        conn, near, far = self._conn_with_reply(header)
        try:
            with self.assertRaises(ProtocolError):
                conn.call(1, 1, Buffer())
        finally:
            near.close()
            far.close()


class TestDecodeHeaderPayloadCap(unittest.TestCase):
    def _header(self, payload_size):
        return encode_header(MsgType.call, 0, 1, payload_size)

    def test_payload_at_cap_ok(self):
        hdr = decode_header(self._header(MAX_PAYLOAD_SIZE))
        self.assertEqual(hdr.payload_size, MAX_PAYLOAD_SIZE)

    def test_payload_over_cap_raises(self):
        with self.assertRaises(ProtocolError):
            decode_header(self._header(MAX_PAYLOAD_SIZE + 1))


if __name__ == "__main__":
    unittest.main()
