# MIT License
# Copyright (c) 2026 dbjwhs

"""
Song Python Client Library

A Python client library for connecting to Song RPC services.
"""

from .buffer import Buffer
from .wire import (
    MsgType,
    MAGIC,
    HEADER_SIZE,
    MAX_PAYLOAD_SIZE,
    encode_header,
    decode_header,
)
from .connection import ServiceConnection, ConnectionError

__version__ = "0.1.0"
__all__ = [
    "Buffer",
    "MsgType",
    "MAGIC",
    "HEADER_SIZE",
    "MAX_PAYLOAD_SIZE",
    "encode_header",
    "decode_header",
    "ServiceConnection",
    "ConnectionError",
]
