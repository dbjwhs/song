// MIT License
// Copyright (c) 2026 dbjwhs

//! `ServiceConnection`: the client side of a Song service. Connects over
//! TCP (or any `Transport`), performs the init handshake, and makes unary
//! and streaming calls.

use crate::buffer::Buffer;
use crate::error::{Error, Result};
use crate::security::SecureTransport;
use crate::transport::{TcpTransport, Transport};
use crate::wire::{self, MethodDescriptor, MsgType, HEADER_SIZE};
use std::time::Duration;

/// How a connection is made.
#[derive(Debug, Clone)]
pub struct ConnectOptions {
    /// How long to wait for the TCP connection, for the service's init
    /// message, and for the reply to each unary call.
    pub timeout: Duration,
    /// Shared secret for HMAC-SHA256 message authentication, for a service
    /// that wraps its clients in SecureTransport. `None` talks to an
    /// unauthenticated service.
    pub key: Option<Vec<u8>>,
    /// The capability bits this client announces in its init_ack; the
    /// negotiated set is these AND the service's.
    pub capabilities: u32,
}

impl Default for ConnectOptions {
    fn default() -> ConnectOptions {
        ConnectOptions { timeout: Duration::from_secs(5), key: None, capabilities: wire::capability::STREAMING }
    }
}

/// A connection to one Song service. One call at a time: a call holds the
/// connection until its reply (or the end of its stream) has been read.
pub struct ServiceConnection {
    transport: Box<dyn Transport>,
    timeout: Duration,
    sequence_id: u32,
    local_capabilities: u32,
    peer_capabilities: u32,
    negotiated_version: u16,
    methods: Vec<MethodDescriptor>,
}

impl ServiceConnection {
    /// Connect to a service on `host:port` with the default options.
    pub fn connect(host: &str, port: u16) -> Result<ServiceConnection> {
        ServiceConnection::connect_with(host, port, ConnectOptions::default())
    }

    pub fn connect_with(host: &str, port: u16, options: ConnectOptions) -> Result<ServiceConnection> {
        let tcp = TcpTransport::connect(host, port, options.timeout)?;
        let transport: Box<dyn Transport> = match &options.key {
            Some(key) => Box::new(SecureTransport::new(tcp, key.clone())),
            None => Box::new(tcp),
        };
        ServiceConnection::from_transport(transport, options)
    }

    /// Take over a connected transport and perform the handshake on it.
    /// `options.key` is not applied here: wrap the transport yourself.
    pub fn from_transport(transport: Box<dyn Transport>, options: ConnectOptions) -> Result<ServiceConnection> {
        let mut conn = ServiceConnection {
            transport,
            timeout: options.timeout,
            sequence_id: 0,
            local_capabilities: options.capabilities,
            peer_capabilities: 0,
            negotiated_version: 0,
            methods: Vec::new(),
        };
        if let Err(e) = conn.init_handshake() {
            conn.close();
            return Err(e);
        }
        Ok(conn)
    }

    /// Read the service's init message, check the versions, record its
    /// capabilities and methods, and acknowledge when both sides speak 1.1.
    fn init_handshake(&mut self) -> Result<()> {
        let mut msg = match self.transport.receive(Some(self.timeout))? {
            Some(msg) => msg,
            None => return Err(Error::protocol("service closed the connection before its init message")),
        };
        let hdr = wire::decode_header_validated(&mut msg)?;
        if hdr.msg_type() != Some(MsgType::Init) {
            return Err(Error::protocol("expected init message from service"));
        }
        let init = wire::decode_init(&mut msg)?;
        if init.current_version < wire::FIRST_VERSION {
            return Err(Error::VersionMismatch(format!(
                "service version too old: service={}, minimum required={}",
                version_text(init.current_version),
                version_text(wire::FIRST_VERSION)
            )));
        }
        if init.first_version > wire::CURRENT_VERSION {
            return Err(Error::VersionMismatch(format!(
                "client version too old: client={}, service requires={}",
                version_text(wire::CURRENT_VERSION),
                version_text(init.first_version)
            )));
        }
        self.negotiated_version = wire::CURRENT_VERSION.min(init.current_version);
        self.peer_capabilities = init.capabilities;

        // Each descriptor is 8 bytes: a count the payload cannot hold is a
        // forged count, refused before the list is allocated.
        let count = init.method_count as usize;
        if count > msg.remaining() / 8 {
            return Err(Error::protocol("init message method count exceeds its payload"));
        }
        self.methods = Vec::with_capacity(count);
        for _ in 0..count {
            self.methods.push(wire::decode_method_descriptor(&mut msg)?);
        }

        if self.negotiated_version >= wire::make_version(1, 1) {
            let ack =
                wire::create_init_ack_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, self.local_capabilities)?;
            self.transport.send(ack.data())?;
        }
        Ok(())
    }

    pub fn close(&mut self) {
        self.transport.close();
    }

    pub fn is_connected(&self) -> bool {
        self.transport.is_connected()
    }

    /// The reply timeout of unary calls.
    pub fn timeout(&self) -> Duration {
        self.timeout
    }

    pub fn set_timeout(&mut self, timeout: Duration) {
        self.timeout = timeout;
    }

    /// The protocol version both sides speak.
    pub fn negotiated_version(&self) -> u16 {
        self.negotiated_version
    }

    /// The capability bits the service announced.
    pub fn peer_capabilities(&self) -> u32 {
        self.peer_capabilities
    }

    /// The capability bits both sides have.
    pub fn negotiated_capabilities(&self) -> u32 {
        self.local_capabilities & self.peer_capabilities
    }

    /// Whether both sides have the capability bit(s) `cap`.
    pub fn has_capability(&self, cap: u32) -> bool {
        self.negotiated_capabilities() & cap != 0
    }

    /// The methods the service registered, with their flags.
    pub fn methods(&self) -> &[MethodDescriptor] {
        &self.methods
    }

    fn next_sequence_id(&mut self) -> u32 {
        self.sequence_id = self.sequence_id.wrapping_add(1);
        self.sequence_id
    }

    /// The next message that belongs to a call: property notifications a
    /// service may push at any time are not replies and are dropped (this
    /// runtime does not subscribe to properties).
    fn receive_reply(&mut self, timeout: Option<Duration>) -> Result<(wire::Header, Buffer)> {
        loop {
            let mut msg = match self.transport.receive(timeout)? {
                Some(msg) => msg,
                None => return Err(Error::Closed),
            };
            let hdr = wire::decode_header_validated(&mut msg)?;
            if hdr.msg_type() == Some(MsgType::PropNotify) {
                continue;
            }
            let payload = Buffer::from_slice(&msg.data()[HEADER_SIZE..]);
            return Ok((hdr, payload));
        }
    }

    /// Make a unary call: send the request, return the reply payload.
    ///
    /// An `error` reply is `Err(Error::Service)`. Any other failure (a
    /// timeout, a mismatched sequence id, an unexpected message) leaves the
    /// position in the byte stream unknown, so the connection is closed.
    pub fn call(&mut self, service_id: u16, method_id: u16, request: &Buffer) -> Result<Buffer> {
        if !self.is_connected() {
            return Err(Error::Closed);
        }
        let seq = self.next_sequence_id();
        let outcome = self.call_inner(seq, service_id, method_id, request);
        if let Err(e) = &outcome {
            if !matches!(e, Error::Service { .. }) {
                self.close();
            }
        }
        outcome
    }

    fn call_inner(&mut self, seq: u32, service_id: u16, method_id: u16, request: &Buffer) -> Result<Buffer> {
        let msg = wire::create_call_message(seq, service_id, method_id, request.data())?;
        self.transport.send(msg.data())?;
        let (hdr, mut payload) = self.receive_reply(Some(self.timeout))?;
        if hdr.sequence_id != seq {
            return Err(Error::protocol(format!("sequence id mismatch: expected {seq}, got {}", hdr.sequence_id)));
        }
        match hdr.msg_type() {
            Some(MsgType::Result) => Ok(payload),
            Some(MsgType::Error) => {
                let (code, message) = wire::decode_error(&mut payload)?;
                Err(Error::Service { code, message })
            }
            _ => Err(Error::protocol(format!("unexpected response type 0x{:02X}", hdr.type_byte))),
        }
    }

    /// Make a streaming call. The returned iterator yields each chunk's
    /// payload as it arrives, and ends at the service's `stream_end`. An
    /// `error` message ends the stream with `Err(Error::Service)`: the
    /// chunks yielded before it were genuine.
    ///
    /// `chunk_timeout` is how long to wait for EACH message; size it to the
    /// remote operation, which may be silent for a long time between
    /// chunks. `None` waits for ever.
    ///
    /// The connection is held for the life of the stream. Dropping the
    /// iterator before it is exhausted leaves unread chunks on the socket,
    /// so the connection is closed.
    pub fn call_streaming(
        &mut self,
        service_id: u16,
        method_id: u16,
        request: &Buffer,
        chunk_timeout: Option<Duration>,
    ) -> Result<StreamCall<'_>> {
        if !self.is_connected() {
            return Err(Error::Closed);
        }
        let seq = self.next_sequence_id();
        let msg = wire::create_call_message(seq, service_id, method_id, request.data())?;
        if let Err(e) = self.transport.send(msg.data()) {
            self.close();
            return Err(e);
        }
        Ok(StreamCall { conn: self, seq, chunk_timeout, finished: false })
    }
}

fn version_text(version: u16) -> String {
    format!("{}.{}", version >> 8, version & 0xFF)
}

/// The chunks of one streaming call, in arrival order.
pub struct StreamCall<'a> {
    conn: &'a mut ServiceConnection,
    seq: u32,
    chunk_timeout: Option<Duration>,
    finished: bool,
}

impl StreamCall<'_> {
    fn read_one(&mut self) -> Result<Option<Buffer>> {
        let (hdr, mut payload) = self.conn.receive_reply(self.chunk_timeout)?;
        if hdr.sequence_id != self.seq {
            return Err(Error::protocol(format!(
                "sequence id mismatch in stream: expected {}, got {}",
                self.seq, hdr.sequence_id
            )));
        }
        match hdr.msg_type() {
            Some(MsgType::Stream) => Ok(Some(payload)),
            Some(MsgType::StreamEnd) => {
                self.finished = true;
                Ok(None)
            }
            Some(MsgType::Error) => {
                // The stream is over and the socket is at a message
                // boundary: the connection stays usable.
                self.finished = true;
                let (code, message) = wire::decode_error(&mut payload)?;
                Err(Error::Service { code, message })
            }
            _ => Err(Error::protocol(format!("unexpected message in stream: 0x{:02X}", hdr.type_byte))),
        }
    }
}

impl<'a> StreamCall<'a> {
    /// The same stream with every chunk decoded by `decode`: what a
    /// generated streaming proxy returns.
    pub fn decoded<T>(self, decode: fn(&mut Buffer) -> Result<T>) -> TypedStream<'a, T> {
        TypedStream { inner: self, decode }
    }
}

/// The chunks of one streaming call, each decoded to a `T`. A chunk that
/// does not decode is yielded as its error; the stream itself goes on.
/// Dropping the stream before its end closes the connection, as for
/// `StreamCall`.
pub struct TypedStream<'a, T> {
    inner: StreamCall<'a>,
    decode: fn(&mut Buffer) -> Result<T>,
}

impl<T> Iterator for TypedStream<'_, T> {
    type Item = Result<T>;

    fn next(&mut self) -> Option<Result<T>> {
        match self.inner.next()? {
            Ok(mut chunk) => Some((self.decode)(&mut chunk)),
            Err(e) => Some(Err(e)),
        }
    }
}

impl Iterator for StreamCall<'_> {
    type Item = Result<Buffer>;

    fn next(&mut self) -> Option<Result<Buffer>> {
        if self.finished {
            return None;
        }
        match self.read_one() {
            Ok(Some(chunk)) => Some(Ok(chunk)),
            Ok(None) => None,
            Err(e) => {
                if !self.finished {
                    // Failed mid-stream: nothing more can be read safely.
                    self.finished = true;
                    self.conn.close();
                }
                Some(Err(e))
            }
        }
    }
}

impl Drop for StreamCall<'_> {
    fn drop(&mut self) {
        if !self.finished {
            self.conn.close();
        }
    }
}
