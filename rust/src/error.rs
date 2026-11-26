// MIT License
// Copyright (c) 2026 dbjwhs

//! The one error type of the runtime, and the error codes that travel in an
//! `error` message (the values of `ErrorCode` in the C++ `error.hpp`).

use std::fmt;

/// Error codes carried in an `error` message payload (u16 on the wire).
pub mod code {
    pub const OK: u16 = 0;
    pub const INVALID_MESSAGE: u16 = 1;
    pub const UNKNOWN_SERVICE: u16 = 2;
    pub const UNKNOWN_METHOD: u16 = 3;
    pub const VERSION_MISMATCH: u16 = 4;
    pub const MAGIC_MISMATCH: u16 = 5;
    pub const DECODE_ERROR: u16 = 100;
    pub const BUFFER_OVERFLOW: u16 = 101;
    pub const BUFFER_UNDERFLOW: u16 = 102;
}

/// Everything that can go wrong in the runtime.
#[derive(Debug)]
pub enum Error {
    /// The socket or pipe failed.
    Io(std::io::Error),
    /// The peer closed the connection.
    Closed,
    /// No message arrived within the timeout given.
    Timeout,
    /// A buffer could not be decoded or a value could not be encoded: read
    /// past the end, a length over its cap, invalid UTF-8, an unknown enum
    /// value.
    Decode(String),
    /// A message that breaks the protocol: bad magic, an oversized payload,
    /// an unexpected message type, a sequence id that does not match.
    Protocol(String),
    /// The peer speaks a version this runtime cannot talk to.
    VersionMismatch(String),
    /// HMAC verification failed, or a message is too short to carry a tag.
    Security(String),
    /// The service answered with an `error` message.
    Service { code: u16, message: String },
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    /// A service-side failure with the code C++ dispatchers report for any
    /// exception (`unknown_method`).
    pub fn service(message: impl Into<String>) -> Error {
        Error::Service { code: code::UNKNOWN_METHOD, message: message.into() }
    }

    pub fn decode(message: impl Into<String>) -> Error {
        Error::Decode(message.into())
    }

    pub fn protocol(message: impl Into<String>) -> Error {
        Error::Protocol(message.into())
    }

    /// The text sent to a client in an `error` message for this failure.
    pub fn wire_message(&self) -> String {
        match self {
            Error::Service { message, .. } => message.clone(),
            other => other.to_string(),
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => write!(f, "I/O error: {e}"),
            Error::Closed => write!(f, "connection closed by peer"),
            Error::Timeout => write!(f, "receive timeout"),
            Error::Decode(m) => write!(f, "decode error: {m}"),
            Error::Protocol(m) => write!(f, "protocol error: {m}"),
            Error::VersionMismatch(m) => write!(f, "version mismatch: {m}"),
            Error::Security(m) => write!(f, "security error: {m}"),
            Error::Service { code, message } => write!(f, "service error {code}: {message}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Error {
        match e.kind() {
            std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut => Error::Timeout,
            std::io::ErrorKind::UnexpectedEof => Error::Closed,
            _ => Error::Io(e),
        }
    }
}
