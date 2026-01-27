# MIT License
# Copyright (c) 2026 dbjwhs

"""
Service connection for Song RPC clients.

Handles TCP communication with Song services.
"""

import socket
import threading
from typing import Optional

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


class ServiceConnection:
    """
    Connection to a Song RPC service.

    Thread-safe for concurrent calls (uses sequence IDs for correlation).
    """

    def __init__(self, host: str, port: int, timeout: float = 5.0):
        """
        Create a connection to a Song service.

        Args:
            host: Service hostname or IP address
            port: Service port number
            timeout: Connection and read timeout in seconds
        """
        self._host = host
        self._port = port
        self._timeout = timeout
        self._socket: Optional[socket.socket] = None
        self._sequence_id = 0
        self._lock = threading.Lock()
        self._connected = False

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

        # Read header
        header_data = self._recv_exact(HEADER_SIZE)
        header = decode_header(header_data)

        if header.msg_type != MsgType.init:
            raise ProtocolError(f"Expected init message, got {header.msg_type}")

        # Read payload
        if header.payload_size > 0:
            payload_data = self._recv_exact(header.payload_size)
            buf = Buffer(payload_data)

            # Parse init message:
            # magic (u32), first_version (u16), current_version (u16),
            # capabilities (u32), method_count (u32)
            init_magic = buf.decode_u32()
            if init_magic != MAGIC:
                raise ProtocolError(f"Invalid magic in init payload: 0x{init_magic:08X}")

            first_version = buf.decode_u16()
            current_version = buf.decode_u16()
            _ = buf.decode_u32()  # capabilities (unused for now)
            _ = buf.decode_u32()  # method_count (unused for now)

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
            self._send_all(message)

            # Receive response
            header_data = self._recv_exact(HEADER_SIZE)
            header = decode_header(header_data)

            # Validate sequence ID
            if header.sequence_id != seq_id:
                raise ProtocolError(
                    f"Sequence ID mismatch: expected {seq_id}, got {header.sequence_id}"
                )

            # Read payload
            payload = b""
            if header.payload_size > 0:
                payload = self._recv_exact(header.payload_size)

            # Handle response type
            if header.msg_type == MsgType.result:
                return Buffer(payload)

            elif header.msg_type == MsgType.error:
                buf = Buffer(payload)
                code, message = decode_error_message(buf)
                raise ServiceError(message, code)

            else:
                raise ProtocolError(f"Unexpected response type: {header.msg_type}")

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
