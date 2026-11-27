# MIT License
# Copyright (c) 2026 dbjwhs

"""
Service connection for Song RPC clients.

Handles TCP communication with Song services.
"""

import hashlib
import hmac
import socket
import struct
import threading
from typing import Iterator, Optional, Tuple, Union

from .buffer import Buffer
from .wire import (
    HEADER_SIZE,
    MsgType,
    Header,
    ProtocolError,
    decode_header,
    create_call_message,
    decode_error_message,
    FIRST_VERSION,
    CURRENT_VERSION,
    CLIENT_CAPABILITIES,
    make_version,
    create_init_ack_message,
)


class ConnectionError(Exception):
    """Raised when connection operations fail."""
    pass


class ServiceError(Exception):
    """Raised when the service returns an error."""
    def __init__(self, message: str, code: int = 0):
        super().__init__(message)
        self.code = code


class VersionMismatchError(Exception):
    """Raised when protocol versions are incompatible."""
    pass


class SecurityError(Exception):
    """Raised when HMAC verification of a received message fails."""
    pass


# HMAC-SHA256 truncated to 8 bytes, appended to the payload (see
# SecureTransport in the C++ runtime).
HMAC_TAG_SIZE = 8


class ServiceConnection:
    """
    Connection to a Song RPC service.

    Thread-safe for concurrent calls (uses sequence IDs for correlation).
    """

    def __init__(self, host: str, port: int, timeout: float = 5.0,
                 key: Optional[Union[bytes, str]] = None):
        """
        Create a connection to a Song service.

        Args:
            host: Service hostname or IP address
            port: Service port number
            timeout: Connection and read timeout in seconds
            key: Shared secret for HMAC-SHA256 message authentication, for a
                service that wraps its clients in SecureTransport. Every
                message in both directions is then tagged and verified.
                None talks to an unauthenticated service.
        """
        self._host = host
        self._port = port
        self._timeout = timeout
        if isinstance(key, str):
            key = key.encode("utf-8")
        self._key: Optional[bytes] = key if key else None
        self._socket: Optional[socket.socket] = None
        self._sequence_id = 0
        self._lock = threading.Lock()
        self._connected = False
        self._negotiated_version = FIRST_VERSION
        self._negotiated_capabilities = 0

    def connect(self) -> None:
        """
        Connect to the service and perform handshake.

        Raises:
            ConnectionError: If connection fails
            VersionMismatchError: If protocol versions are incompatible
        """
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(self._timeout)
            self._socket.connect((self._host, self._port))
            self._connected = True

            # Perform handshake - receive init message from service
            self._init_handshake()

        except socket.error as e:
            self._connected = False
            raise ConnectionError(f"Failed to connect to {self._host}:{self._port}: {e}")

    def _init_handshake(self) -> None:
        """Receive and validate init message from service."""
        from .wire import MAGIC

        header, payload_data = self._recv_message()

        if header.msg_type != MsgType.init:
            raise ProtocolError(f"Expected init message, got {header.msg_type}")

        self._negotiated_version = FIRST_VERSION
        self._negotiated_capabilities = 0

        if payload_data:
            buf = Buffer(payload_data)

            # Parse init message:
            # magic (u32), first_version (u16), current_version (u16),
            # capabilities (u32), method_count (u32)
            init_magic = buf.decode_u32()
            if init_magic != MAGIC:
                raise ProtocolError(f"Invalid magic in init payload: 0x{init_magic:08X}")

            first_version = buf.decode_u16()
            current_version = buf.decode_u16()
            service_capabilities = buf.decode_u32()
            _ = buf.decode_u32()  # method_count (the descriptors are not used)

            # Version check
            if CURRENT_VERSION < first_version:
                raise VersionMismatchError(
                    f"Client version {CURRENT_VERSION} too old, "
                    f"service requires at least {first_version}"
                )
            if FIRST_VERSION > current_version:
                raise VersionMismatchError(
                    f"Service version {current_version} too old, "
                    f"client requires at least {FIRST_VERSION}"
                )

            # Both sides speak the lower of the two current versions and the
            # capabilities both announced.
            self._negotiated_version = min(CURRENT_VERSION, current_version)
            self._negotiated_capabilities = service_capabilities & CLIENT_CAPABILITIES

            # A v1.1+ service is told what this client speaks. A 1.0 service
            # does not know the message, so it is not sent one.
            if self._negotiated_version >= make_version(1, 1):
                self._send_message(create_init_ack_message(
                    FIRST_VERSION, CURRENT_VERSION, CLIENT_CAPABILITIES))

    @property
    def negotiated_version(self) -> int:
        """The protocol version in use: the lower of the two peers' current versions."""
        return self._negotiated_version

    @property
    def negotiated_capabilities(self) -> int:
        """The capability bits both peers announced."""
        return self._negotiated_capabilities

    def close(self) -> None:
        """Close the connection."""
        if self._socket:
            try:
                self._socket.close()
            except socket.error:
                pass
            self._socket = None
        self._connected = False

    def is_connected(self) -> bool:
        """Check if connected to service."""
        return self._connected and self._socket is not None

    def call(self, service_id: int, method_id: int, request: Buffer) -> Buffer:
        """
        Make an RPC call to the service.

        Args:
            service_id: Target service ID
            method_id: Target method ID
            request: Buffer containing encoded method arguments

        Returns:
            Buffer containing the response payload

        Raises:
            ConnectionError: If not connected or communication fails
            ServiceError: If service returns an error
            ProtocolError: If protocol violation occurs
        """
        if not self.is_connected():
            raise ConnectionError("Not connected to service")

        with self._lock:
            # Assign sequence ID
            self._sequence_id += 1
            seq_id = self._sequence_id

            # Build and send message
            message = create_call_message(seq_id, service_id, method_id, request.data())
            self._send_message(message)

            # Receive response
            header, payload = self._recv_message()

            # Validate sequence ID
            if header.sequence_id != seq_id:
                raise ProtocolError(
                    f"Sequence ID mismatch: expected {seq_id}, got {header.sequence_id}"
                )

            # Handle response type
            if header.msg_type == MsgType.result:
                return Buffer(payload)

            elif header.msg_type == MsgType.error:
                buf = Buffer(payload)
                code, message = decode_error_message(buf)
                raise ServiceError(message, code)

            else:
                raise ProtocolError(f"Unexpected response type: {header.msg_type}")

    def call_streaming(self, service_id: int, method_id: int, request: Buffer,
                       chunk_timeout: Optional[float] = None) -> Iterator[Buffer]:
        """
        Make a streaming RPC call, yielding each chunk as it arrives.

        The service answers with zero or more stream messages followed by a
        stream_end. An error reply terminates the stream instead: chunks
        yielded before it were genuine, then ServiceError is raised.

        The connection is held for the life of the stream. Abandoning the
        iterator before it is exhausted leaves unread chunks on the socket,
        so the connection is closed.

        Args:
            service_id: Target service ID (the service's stream ID)
            method_id: Target method ID
            request: Buffer containing encoded method arguments
            chunk_timeout: Max seconds to wait for EACH message. Size it to
                the remote operation, which may be silent for a long time
                between chunks. None uses the connection timeout.

        Yields:
            Buffer holding one chunk's payload

        Raises:
            ConnectionError: If not connected, on timeout, or on disconnect
            ServiceError: If the service ends the stream with an error
            ProtocolError: If protocol violation occurs
        """
        if not self.is_connected():
            raise ConnectionError("Not connected to service")

        with self._lock:
            self._sequence_id += 1
            seq_id = self._sequence_id

            message = create_call_message(seq_id, service_id, method_id, request.data())
            self._send_message(message)

            finished = False
            if chunk_timeout is not None:
                self._socket.settimeout(chunk_timeout)
            try:
                while True:
                    header, payload = self._recv_message()

                    if header.sequence_id != seq_id:
                        raise ProtocolError(
                            f"Sequence ID mismatch in stream: expected {seq_id}, "
                            f"got {header.sequence_id}"
                        )

                    if header.msg_type == MsgType.stream:
                        yield Buffer(payload)

                    elif header.msg_type == MsgType.stream_end:
                        finished = True
                        return

                    elif header.msg_type == MsgType.error:
                        finished = True
                        code, text = decode_error_message(Buffer(payload))
                        raise ServiceError(text, code)

                    else:
                        raise ProtocolError(
                            f"Unexpected message in stream: {header.msg_type}"
                        )
            finally:
                if not finished:
                    # Abandoned or failed mid-stream: the socket position is
                    # unknown, so the connection cannot be reused.
                    self.close()
                elif chunk_timeout is not None and self._socket:
                    self._socket.settimeout(self._timeout)

    def _tag(self, message: bytes) -> bytes:
        """HMAC tag over the raw wire bytes of an untagged message."""
        return hmac.new(self._key, message, hashlib.sha256).digest()[:HMAC_TAG_SIZE]

    def _send_message(self, message: bytes) -> None:
        """Send one wire message (header + payload), tagging it when keyed."""
        if self._key is None:
            self._send_all(message)
            return

        # The tag rides at the end of the payload: payload_size (offset 8)
        # grows by the tag size, and the tag covers the original bytes.
        payload_size = struct.unpack_from('=I', message, 8)[0]
        patched = (message[:8] + struct.pack('=I', payload_size + HMAC_TAG_SIZE)
                   + message[12:])
        self._send_all(patched + self._tag(message))

    def _recv_message(self) -> Tuple[Header, bytes]:
        """Receive one wire message, verifying and stripping its tag when keyed."""
        header_data = self._recv_exact(HEADER_SIZE)
        header = decode_header(header_data)

        body = b""
        if header.payload_size > 0:
            body = self._recv_exact(header.payload_size)

        if self._key is None:
            return header, body

        if len(body) < HMAC_TAG_SIZE:
            self.close()
            raise SecurityError("message payload too short for HMAC verification")

        payload, received_tag = body[:-HMAC_TAG_SIZE], body[-HMAC_TAG_SIZE:]
        original = (header_data[:8] + struct.pack('=I', len(payload))
                    + header_data[12:] + payload)
        if not hmac.compare_digest(self._tag(original), received_tag):
            self.close()
            raise SecurityError("HMAC verification failed - message may be tampered")

        header.payload_size = len(payload)
        return header, payload

    def _send_all(self, data: bytes) -> None:
        """Send all data to socket."""
        total_sent = 0
        while total_sent < len(data):
            try:
                sent = self._socket.send(data[total_sent:])
                if sent == 0:
                    raise ConnectionError("Socket connection broken")
                total_sent += sent
            except socket.error as e:
                self._connected = False
                raise ConnectionError(f"Send failed: {e}")

    def _recv_exact(self, length: int) -> bytes:
        """Receive exactly length bytes from socket."""
        chunks = []
        received = 0
        while received < length:
            try:
                chunk = self._socket.recv(length - received)
                if not chunk:
                    self._connected = False
                    raise ConnectionError("Connection closed by service")
                chunks.append(chunk)
                received += len(chunk)
            except socket.timeout:
                raise ConnectionError("Receive timeout")
            except socket.error as e:
                self._connected = False
                raise ConnectionError(f"Receive failed: {e}")
        return b"".join(chunks)

    def __enter__(self) -> 'ServiceConnection':
        """Context manager entry - connect."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit - close."""
        self.close()
