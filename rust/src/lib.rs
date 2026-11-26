// MIT License
// Copyright (c) 2026 dbjwhs

//! Song RPC runtime for Rust.
//!
//! Byte-compatible with the C++ runtime (`include/song`, `src`) and the
//! Python client (`python/song`): the wire format is the contract, and the
//! tests check it against byte vectors all three languages share
//! (`test/golden/wire_vectors.txt`).
//!
//! Supported: every IDL value type, the message framing, the init
//! handshake with capability exchange, TCP and pipe transports, HMAC
//! message authentication, a client with unary and streaming calls, and a
//! service runtime that serves TCP (a thread per client) and pipes. Not
//! supported, as in the Python runtime: mDNS discovery, TLS, remote objects and properties.

#![deny(warnings)]

pub mod buffer;
pub mod connection;
pub mod error;
pub mod runtime;
pub mod security;
pub mod sha256;
pub mod transport;
pub mod wire;

pub use buffer::Buffer;
pub use connection::{ConnectOptions, ServiceConnection, StreamCall, TypedStream};
pub use error::{Error, Result};
pub use runtime::{ServeEnd, ServiceRuntime, StreamWriter, TypedStreamWriter};
pub use security::SecureTransport;
pub use transport::{PipeTransport, TcpTransport, Transport};
