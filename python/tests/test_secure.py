# MIT License
# Copyright (c) 2026 dbjwhs

"""
HMAC-SHA256 message authentication in the Python client.

The frame tests check the exact bytes against an independent computation
(header with payload_size grown by 8, payload, then the first 8 bytes of
HMAC-SHA256 over the untagged message). The last class talks to the real
C++ TCP calculator service serving through SecureTransport.
"""

import hashlib
import hmac
import socket
import struct
import subprocess
import threading
import time
import unittest
from pathlib import Path

from song.buffer import Buffer
from song.connection import ConnectionError, SecurityError, ServiceConnection
from song.generated.calculator import CalculatorProxy
from song.wire import (
    CURRENT_VERSION,
    FIRST_VERSION,
    HEADER_SIZE,
    MAGIC,
    MsgType,
    create_result_message,
    encode_header,
)

KEY = b"song-python-test-secret-32-bytes"


def tagged(message: bytes, key: bytes = KEY) -> bytes:
    size = struct.unpack_from('<I', message, 8)[0]
    tag = hmac.new(key, message, hashlib.sha256).digest()[:8]
    return message[:8] + struct.pack('<I', size + 8) + message[12:] + tag


def init_frame() -> bytes:
    payload = struct.pack('<IHHII', MAGIC, FIRST_VERSION, CURRENT_VERSION, 0, 0)
    return encode_header(MsgType.init, 0, 0, len(payload)) + payload


def recv_exact(sock: socket.socket, length: int) -> bytes:
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


class OneShotServer:
    """Sends `greeting`, reads one raw message, answers with reply(raw)."""

    def __init__(self, greeting: bytes, reply=None):
        self.received = None
        self._greeting = greeting
        self._reply = reply
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
                    header = recv_exact(client, HEADER_SIZE)
                    size = struct.unpack_from('<I', header, 8)[0]
                    self.received = header + recv_exact(client, size)
                    if header[5] != MsgType.init_ack:
                        break       # the client's init_ack comes before its call
                if self._reply:
                    client.sendall(self._reply(self.received))
                    recv_exact(client, 1)  # hold open until the client closes
            except (EOFError, OSError):
                pass

    def close(self) -> None:
        self._listener.close()
        self._thread.join(timeout=2)


class TestHmacFrames(unittest.TestCase):

    def serve(self, greeting: bytes, reply=None) -> OneShotServer:
        server = OneShotServer(greeting, reply)
        self.addCleanup(server.close)
        return server

    def connect(self, server: OneShotServer, key=KEY) -> ServiceConnection:
        conn = ServiceConnection("127.0.0.1", server.port, timeout=2.0, key=key)
        self.addCleanup(conn.close)
        conn.connect()
        return conn

    def test_call_is_tagged_and_tagged_reply_is_accepted(self):
        def reply(raw: bytes) -> bytes:
            seq = struct.unpack_from('<I', raw, 12)[0]
            answer = Buffer()
            answer.encode_i32(42)
            return tagged(create_result_message(seq, answer.data()))

        server = self.serve(tagged(init_frame()), reply)
        conn = self.connect(server)

        req = Buffer()
        req.encode_i32(5)
        self.assertEqual(conn.call(1, 2, req).decode_i32(), 42)

        # What went on the wire is exactly the tagged form of the plain call.
        plain = (encode_header(MsgType.call, 0, 1, 8)
                 + struct.pack('<HH', 1, 2) + struct.pack('<i', 5))
        self.assertEqual(server.received, tagged(plain))

    def test_string_key_is_utf8(self):
        server = self.serve(tagged(init_frame()))
        self.connect(server, key=KEY.decode("ascii"))

    def test_wrong_key_is_rejected_at_handshake(self):
        server = self.serve(tagged(init_frame(), key=b"x" * 32))
        with self.assertRaises(SecurityError):
            self.connect(server)

    def test_untagged_service_is_rejected_by_keyed_client(self):
        # An init without a tag: its last 8 payload bytes are not a valid tag.
        server = self.serve(init_frame())
        with self.assertRaises(SecurityError):
            self.connect(server)

    def test_tampered_reply_is_rejected_and_connection_closed(self):
        def reply(raw: bytes) -> bytes:
            seq = struct.unpack_from('<I', raw, 12)[0]
            answer = Buffer()
            answer.encode_i32(42)
            good = bytearray(tagged(create_result_message(seq, answer.data())))
            good[HEADER_SIZE] ^= 0x01  # flip one payload bit
            return bytes(good)

        server = self.serve(tagged(init_frame()), reply)
        conn = self.connect(server)
        with self.assertRaises(SecurityError):
            conn.call(1, 2, Buffer())
        self.assertFalse(conn.is_connected())


def find_service_executable() -> Path:
    path = (Path(__file__).parent.parent.parent / "build" / "sing" / "network"
            / "tcp_calculator" / "sing_network_tcp_calculator_service")
    if path.exists():
        return path
    raise FileNotFoundError("TCP calculator service not found. Build the project first.")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class TestSecureCppService(unittest.TestCase):
    """Python client against the C++ service wrapped in SecureTransport."""

    server_process = None
    port = 0

    @classmethod
    def setUpClass(cls):
        try:
            service_path = find_service_executable()
        except FileNotFoundError as e:
            raise unittest.SkipTest(str(e))

        cls.port = free_port()
        cls.server_process = subprocess.Popen(
            [str(service_path), str(cls.port), KEY.decode("ascii")],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        for _ in range(30):
            time.sleep(0.1)
            try:
                probe = socket.create_connection(("127.0.0.1", cls.port), timeout=0.5)
                probe.close()
                return
            except OSError:
                pass

        cls.server_process.terminate()
        cls.server_process = None
        raise unittest.SkipTest("Failed to start TCP calculator service")

    @classmethod
    def tearDownClass(cls):
        if cls.server_process:
            cls.server_process.terminate()
            cls.server_process.wait()

    def test_keyed_client_calls_through(self):
        with ServiceConnection("127.0.0.1", self.port, key=KEY) as conn:
            calc = CalculatorProxy(conn)
            self.assertEqual(calc.add(5, 3), 8)
            self.assertEqual(calc.sum([1, 2, 3, 4]), 10)
            self.assertEqual(calc.divide(17, 5).remainder, 2)

    def test_wrong_key_is_rejected(self):
        conn = ServiceConnection("127.0.0.1", self.port, timeout=2.0, key=b"y" * 32)
        self.addCleanup(conn.close)
        with self.assertRaises(SecurityError):
            conn.connect()

    def test_keyless_client_cannot_call(self):
        # The init parses (the tag just looks like trailing payload), but the
        # service refuses the untagged call and no answer comes back.
        conn = ServiceConnection("127.0.0.1", self.port, timeout=1.0)
        self.addCleanup(conn.close)
        conn.connect()
        with self.assertRaises(ConnectionError):
            CalculatorProxy(conn).add(1, 2)


if __name__ == "__main__":
    unittest.main()
