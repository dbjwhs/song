# MIT License
# Copyright (c) 2026 dbjwhs

"""
Streaming calls in the Python client.

A scripted in-process server speaks the wire protocol over a real TCP
socket, so these tests need no built C++ service. Each test hands the
server the frames to send after it has read one call message.
"""

import socket
import struct
import threading
import unittest

from song.buffer import Buffer
from song.connection import ConnectionError, ServiceConnection, ServiceError
from song.wire import (
    CURRENT_VERSION,
    FIRST_VERSION,
    HEADER_SIZE,
    MAGIC,
    MsgType,
    ProtocolError,
    create_error_message,
    create_result_message,
    decode_header,
    encode_header,
    make_version,
    CAP_STREAMING,
    CLIENT_CAPABILITIES,
)


def init_frame(current_version: int = CURRENT_VERSION, capabilities: int = 0) -> bytes:
    payload = struct.pack('<IHHII', MAGIC, FIRST_VERSION, current_version, capabilities, 0)
    return encode_header(MsgType.init, 0, 0, len(payload)) + payload


def stream_frame(seq: int, value: int) -> bytes:
    buf = Buffer()
    buf.encode_i32(value)
    payload = buf.data()
    return encode_header(MsgType.stream, 0, seq, len(payload)) + payload


def result_frame(seq: int) -> bytes:
    return encode_header(MsgType.result, 0, seq, 0)


def stream_end_frame(seq: int) -> bytes:
    return encode_header(MsgType.stream_end, 0, seq, 0)


def recv_exact(sock: socket.socket, length: int) -> bytes:
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


class ScriptedServer:
    """Accepts one client; answers each call with script(call_index, seq)."""

    def __init__(self, script, greeting: bytes = None):
        self.acks = []
        self._greeting = init_frame() if greeting is None else greeting
        self._script = script
        self.calls = []
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self.port = self._listener.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self) -> None:
        try:
            client, _ = self._listener.accept()
        except OSError:
            return
        with client:
            try:
                client.sendall(self._greeting)
                while True:
                    header = decode_header(recv_exact(client, HEADER_SIZE))
                    payload = recv_exact(client, header.payload_size)
                    if header.msg_type == MsgType.init_ack:
                        # What a v1.1 service does: note it and carry on.
                        self.acks.append(payload)
                        continue
                    service_id, method_id = struct.unpack('<HH', payload[:4])
                    self.calls.append((service_id, method_id, payload[4:]))
                    for frame in self._script(len(self.calls) - 1, header.sequence_id):
                        client.sendall(frame)
            except (EOFError, OSError):
                pass

    def close(self) -> None:
        self._listener.close()
        self._thread.join(timeout=2)


class TestHandshake(unittest.TestCase):
    """The version and capability exchange (init, then init_ack to a v1.1+ service)."""

    def start(self, greeting: bytes):
        server = ScriptedServer(lambda ndx, seq: [result_frame(seq)], greeting)
        self.addCleanup(server.close)
        conn = ServiceConnection("127.0.0.1", server.port, timeout=2.0)
        conn.connect()
        self.addCleanup(conn.close)
        return server, conn

    def test_a_v11_service_is_sent_an_init_ack(self):
        server, conn = self.start(init_frame(make_version(1, 1), CAP_STREAMING | 0x08))
        conn.call(1, 1, Buffer())      # the ack is on the wire before the call
        self.assertEqual(len(server.acks), 1)
        # The init payload layout: magic, first and current version,
        # capabilities, no methods.
        self.assertEqual(server.acks[0], struct.pack(
            '<IHHII', MAGIC, make_version(1, 0), make_version(1, 1), CLIENT_CAPABILITIES, 0))
        self.assertEqual(conn.negotiated_version, make_version(1, 1))
        # Only what both sides announced: the service's properties bit is dropped.
        self.assertEqual(conn.negotiated_capabilities, CAP_STREAMING)

    def test_a_v10_service_is_not_sent_one(self):
        server, conn = self.start(init_frame(make_version(1, 0), 0))
        conn.call(1, 1, Buffer())
        self.assertEqual(server.acks, [])
        self.assertEqual(len(server.calls), 1)
        self.assertEqual(conn.negotiated_version, make_version(1, 0))
        self.assertEqual(conn.negotiated_capabilities, 0)

    def test_a_newer_service_negotiates_down_to_this_client(self):
        server, conn = self.start(init_frame(make_version(1, 7), CAP_STREAMING))
        conn.call(1, 1, Buffer())
        self.assertEqual(conn.negotiated_version, CURRENT_VERSION)
        self.assertEqual(len(server.acks), 1)


class TestCallStreaming(unittest.TestCase):

    def start(self, script) -> ServiceConnection:
        self.server = ScriptedServer(script)
        self.addCleanup(self.server.close)
        conn = ServiceConnection("127.0.0.1", self.server.port, timeout=2.0)
        conn.connect()
        self.addCleanup(conn.close)
        return conn

    def test_chunks_arrive_in_order_then_end(self):
        conn = self.start(lambda ndx, seq: [
            stream_frame(seq, 10), stream_frame(seq, 20), stream_frame(seq, 30),
            stream_end_frame(seq),
        ])
        req = Buffer()
        req.encode_i32(3)
        values = [chunk.decode_i32() for chunk in conn.call_streaming(7, 2, req)]
        self.assertEqual(values, [10, 20, 30])
        self.assertEqual(self.server.calls[0][:2], (7, 2))

    def test_empty_stream(self):
        conn = self.start(lambda ndx, seq: [stream_end_frame(seq)])
        self.assertEqual(list(conn.call_streaming(7, 2, Buffer())), [])

    def test_error_terminates_stream_after_genuine_chunks(self):
        conn = self.start(lambda ndx, seq: [
            stream_frame(seq, 1),
            create_error_message(seq, 5, "exploded mid-stream"),
        ])
        seen = []
        with self.assertRaises(ServiceError) as raised:
            for chunk in conn.call_streaming(7, 2, Buffer()):
                seen.append(chunk.decode_i32())
        self.assertEqual(seen, [1])
        self.assertIn("exploded mid-stream", str(raised.exception))
        self.assertTrue(conn.is_connected())

    def test_unary_call_works_after_a_stream(self):
        def script(ndx, seq):
            if ndx == 0:
                return [stream_frame(seq, 1), stream_end_frame(seq)]
            reply = Buffer()
            reply.encode_i32(99)
            return [create_result_message(seq, reply.data())]

        conn = self.start(script)
        self.assertEqual(len(list(conn.call_streaming(7, 2, Buffer()))), 1)
        self.assertEqual(conn.call(7, 1, Buffer()).decode_i32(), 99)

    def test_chunk_timeout_closes_connection(self):
        conn = self.start(lambda ndx, seq: [stream_frame(seq, 1)])  # never ends
        stream = conn.call_streaming(7, 2, Buffer(), chunk_timeout=0.2)
        self.assertEqual(next(stream).decode_i32(), 1)
        with self.assertRaises(ConnectionError):
            next(stream)
        self.assertFalse(conn.is_connected())

    def test_abandoned_stream_closes_connection(self):
        conn = self.start(lambda ndx, seq: [
            stream_frame(seq, 1), stream_frame(seq, 2), stream_end_frame(seq),
        ])
        stream = conn.call_streaming(7, 2, Buffer())
        next(stream)
        stream.close()
        self.assertFalse(conn.is_connected())

    def test_wrong_sequence_id_is_a_protocol_error(self):
        conn = self.start(lambda ndx, seq: [stream_frame(seq + 1, 1)])
        with self.assertRaises(ProtocolError):
            list(conn.call_streaming(7, 2, Buffer()))


if __name__ == "__main__":
    unittest.main()
