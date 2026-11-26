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
//! handshake with capability exchange. Not supported, as in the Python
//! runtime: mDNS discovery, TLS, remote objects and properties.

#![deny(warnings)]

pub mod buffer;
pub mod error;
pub mod wire;

pub use buffer::Buffer;
pub use error::{Error, Result};
