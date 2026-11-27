# MIT License
# Copyright (c) 2026 dbjwhs

"""
Python client against the Rust service runtime: the interop service of
sing/network/rust_interop/interop.song served by
rust/examples/interop_service.rs, with and without HMAC.

The proxy is songc's Python output for the same IDL.

The Rust binary is built by cargo (`cargo build --examples` in rust/). The
tests skip when it is absent; with SONG_REQUIRE_PEERS set (ctest sets it)
they fail instead.
"""

import os
import socket
import subprocess
import time
import unittest
from pathlib import Path

from song.buffer import Buffer
from song.connection import ConnectionError, SecurityError, ServiceConnection, ServiceError
from song.generated.interop import (
    METHOD_Interop_add,
    SERVICE_Interop,
    SERVICE_Interop_STREAM,
    SERVICE_Second,
    Access,
    Batch,
    InteropProxy,
    Sample,
    Status,
    SecondProxy,
)

KEY = b"song-interop-test-secret-32bytes"


def find_service_executable() -> Path:
    override = os.environ.get("SONG_RUST_INTEROP_SERVICE")
    path = Path(override) if override else (
        Path(__file__).parent.parent.parent / "rust" / "target" / "debug" / "examples" / "interop_service")
    if path.exists():
        return path
    raise FileNotFoundError(
        f"Rust interop service not found at {path}. Run `cargo build --examples` in rust/ first.")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class RustServiceCase(unittest.TestCase):
    """Starts the Rust service once per class, with KEY when the class has one."""

    key = None
    server_process = None
    port = 0

    @classmethod
    def setUpClass(cls):
        try:
            service_path = find_service_executable()
        except FileNotFoundError as e:
            if os.environ.get("SONG_REQUIRE_PEERS"):
                raise
            raise unittest.SkipTest(str(e))

        cls.port = free_port()
        command = [str(service_path), str(cls.port)]
        if cls.key:
            command.append(cls.key.decode("ascii"))
        cls.server_process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        for _ in range(50):
            time.sleep(0.1)
            try:
                probe = socket.create_connection(("127.0.0.1", cls.port), timeout=0.5)
                probe.close()
                return
            except OSError:
                pass

        cls.server_process.terminate()
        cls.server_process = None
        raise RuntimeError("Rust interop service did not start listening")

    @classmethod
    def tearDownClass(cls):
        if cls.server_process:
            cls.server_process.terminate()
            cls.server_process.wait()

    def connect(self, key=None, timeout=5.0) -> ServiceConnection:
        conn = ServiceConnection("127.0.0.1", self.port, timeout=timeout, key=key)
        self.addCleanup(conn.close)
        conn.connect()
        return conn

    def exercise(self, conn: ServiceConnection) -> None:
        """Unary calls, a failing call, a stream, and a stream that fails
        part way: what every transport must carry."""
        proxy = InteropProxy(conn)
        self.assertEqual(SecondProxy(conn).whoami("python"), "rust serving python")
        self.assertEqual(proxy.add(40, 2), 42)
        self.assertEqual(proxy.add(-7, 7), 0)
        proxy.ping()

        with self.assertRaises(ServiceError) as raised:
            proxy.fail("no such job")
        self.assertEqual(str(raised.exception), "no such job")
        self.assertEqual(raised.exception.code, 3)
        self.assertEqual(proxy.add(1, 1), 2)

        # Enums and flags as parameters, and inside a struct both ways.
        self.assertEqual(proxy.describe(Status.failed, Access.read | Access.exec, "job "), "job failed:5")
        self.assertEqual(proxy.describe(Status.ok, Access(0), ""), "ok:0")
        sample = Sample(id=7, label="caf\u00e9", value=-2.5, status=Status.retry,
                        access=Access.read | Access.write, note_id=-1, note="",
                        blob=bytes(range(256)), counts=[1, -2, 3], tags=["a", ""])
        echoed = proxy.echo_sample(sample)
        self.assertEqual(echoed, sample)
        self.assertIs(type(echoed.status), Status)
        batch = Batch(samples=[sample, sample], best=sample, grid=[[1, 2], [], [3]],
                      history=[Status.ok, Status.failed, Status.retry])
        self.assertEqual(proxy.echo_batch(batch), batch)
        streamed = list(proxy.samples(3, "s", chunk_timeout=5.0))
        self.assertEqual([s.id for s in streamed], [1, 2, 3])
        self.assertTrue(all(isinstance(s.status, Status) for s in streamed))

        # Optional parameters and an optional return, present and absent.
        self.assertEqual(proxy.bump(5, "abc"), 8)
        self.assertEqual(proxy.bump(-2, None), -2)
        self.assertIsNone(proxy.bump(None, "ignored"))

        self.assertEqual(list(proxy.count_to(5, chunk_timeout=5.0)), [1, 2, 3, 4, 5])
        self.assertEqual(list(proxy.count_to(0, chunk_timeout=5.0)), [])

        # A stream that fails part way: the chunks written first arrive,
        # then the error ends the stream. A clean end here would be the bug.
        got = []
        with self.assertRaises(ServiceError) as raised:
            for value in proxy.fail_after(3, "ran dry", chunk_timeout=5.0):
                got.append(value)
        self.assertEqual(got, [1, 2, 3])
        self.assertEqual(str(raised.exception), "ran dry")
        self.assertEqual(proxy.add(2, 3), 5)


class TestRustService(RustServiceCase):
    """Python client against the Rust service, no authentication."""

    def test_ids_match_the_rust_and_cpp_generators(self):
        self.assertEqual((SERVICE_Interop, SERVICE_Interop_STREAM, SERVICE_Second), (1, 2, 3))

    def test_unary_and_streaming_calls(self):
        self.exercise(self.connect())

    def test_unknown_service_and_method(self):
        conn = self.connect()
        with self.assertRaises(ServiceError) as raised:
            conn.call(77, 1, Buffer())
        self.assertEqual((str(raised.exception), raised.exception.code), ("Unknown service ID", 2))
        with self.assertRaises(ServiceError) as raised:
            conn.call(SERVICE_Interop, 99, Buffer())
        self.assertEqual(str(raised.exception), "Unknown method ID: 99")
        request = Buffer()
        request.encode_i32(1)
        request.encode_i32(2)
        self.assertEqual(conn.call(SERVICE_Interop, METHOD_Interop_add, request).decode_i32(), 3)

    def test_two_clients_at_once(self):
        first = InteropProxy(self.connect())
        second = InteropProxy(self.connect())
        self.assertEqual(first.add(1, 2), 3)
        self.assertEqual(second.add(3, 4), 7)
        self.assertEqual(first.add(5, 6), 11)

    def test_keyed_client_refuses_the_unkeyed_service(self):
        conn = ServiceConnection("127.0.0.1", self.port, timeout=2.0, key=KEY)
        self.addCleanup(conn.close)
        with self.assertRaises(SecurityError):
            conn.connect()


class TestSecureRustService(RustServiceCase):
    """Python client against the Rust service wrapped in SecureTransport."""

    key = KEY

    def test_keyed_client_calls_through(self):
        self.exercise(self.connect(key=KEY))

    def test_wrong_key_is_rejected(self):
        conn = ServiceConnection("127.0.0.1", self.port, timeout=2.0, key=b"y" * 32)
        self.addCleanup(conn.close)
        with self.assertRaises(SecurityError):
            conn.connect()
        # The service still serves a client that has the key.
        self.assertEqual(InteropProxy(self.connect(key=KEY)).add(1, 2), 3)

    def test_keyless_client_cannot_call(self):
        # The init parses (the tag just looks like trailing payload), but the
        # service refuses the untagged call and no answer comes back.
        conn = self.connect(timeout=1.0)
        with self.assertRaises(ConnectionError):
            InteropProxy(conn).add(1, 2)


if __name__ == "__main__":
    unittest.main()
