// MIT License
// Copyright (c) 2026 dbjwhs

//! `ServiceRuntime`: the service side. Register dispatchers, then serve
//! clients over TCP (a thread per client) or over pipes (stdin and stdout,
//! the way a ServiceManager spawns a local service).
//!
//! Every serving loop goes through the one `handle_message`, as in the C++
//! runtime.

use crate::buffer::Buffer;
use crate::error::{code, Error, Result};
use crate::transport::{PipeTransport, TcpTransport, Transport};
use crate::wire::{self, capability, method_flags, MethodDescriptor, MsgType, HEADER_SIZE};
use std::collections::HashMap;
use std::net::{TcpListener, ToSocketAddrs};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Arc;

/// A unary dispatcher: (method id, request, response). An `Err` is sent to
/// the client as an `error` message.
pub type Dispatcher = Box<dyn Fn(u16, &mut Buffer, &mut Buffer) -> Result<()> + Send + Sync>;

/// A stream dispatcher: (method id, request, writer). Returning `Ok` ends
/// the stream; an `Err` ends it with that error instead of a clean
/// `stream_end`, after whatever chunks were already written.
pub type StreamDispatcher = Box<dyn Fn(u16, &mut Buffer, &mut StreamWriter) -> Result<()> + Send + Sync>;

/// Wraps each client's transport before any bytes are exchanged, for
/// example in a `SecureTransport`.
pub type TransportWrapper = Box<dyn Fn(Box<dyn Transport>) -> Box<dyn Transport> + Send + Sync>;

/// Writes the chunks of one streaming reply.
pub struct StreamWriter<'a> {
    transport: &'a mut dyn Transport,
    sequence_id: u32,
    chunks: u64,
}

impl StreamWriter<'_> {
    /// Send one chunk now. An `Err` means the client is gone: return it.
    pub fn write(&mut self, chunk: &Buffer) -> Result<()> {
        self.write_bytes(chunk.data())
    }

    pub fn write_bytes(&mut self, chunk: &[u8]) -> Result<()> {
        let msg = wire::create_stream_message(self.sequence_id, chunk)?;
        self.transport.send(msg.data())?;
        self.chunks += 1;
        Ok(())
    }

    /// Chunks written so far.
    pub fn chunks_written(&self) -> u64 {
        self.chunks
    }
}

/// A `StreamWriter` that encodes each chunk from a `T`: what a generated
/// service trait hands a streaming method.
pub struct TypedStreamWriter<'a, 'b, T> {
    writer: &'a mut StreamWriter<'b>,
    encode: fn(&mut Buffer, &T) -> Result<()>,
}

impl<'a, 'b, T> TypedStreamWriter<'a, 'b, T> {
    pub fn new(writer: &'a mut StreamWriter<'b>, encode: fn(&mut Buffer, &T) -> Result<()>) -> Self {
        TypedStreamWriter { writer, encode }
    }

    /// Encode `value` and send it as one chunk now. An `Err` means the
    /// value could not be encoded or the client is gone: return it.
    pub fn write(&mut self, value: &T) -> Result<()> {
        let mut chunk = Buffer::new();
        (self.encode)(&mut chunk, value)?;
        self.writer.write(&chunk)
    }

    /// Chunks written so far.
    pub fn chunks_written(&self) -> u64 {
        self.writer.chunks_written()
    }
}

/// Why `serve` returned.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServeEnd {
    /// The client closed the connection.
    Disconnected,
    /// The client sent a `shutdown` message.
    Shutdown,
}

#[derive(Default)]
pub struct ServiceRuntime {
    dispatchers: HashMap<u16, Dispatcher>,
    stream_dispatchers: HashMap<u16, StreamDispatcher>,
    methods: Vec<MethodDescriptor>,
    local_capabilities: u32,
    extension_names: Vec<String>,
    wrapper: Option<TransportWrapper>,
}

impl ServiceRuntime {
    pub fn new() -> ServiceRuntime {
        ServiceRuntime::default()
    }

    pub fn register_dispatcher<F>(&mut self, service_id: u16, dispatcher: F)
    where
        F: Fn(u16, &mut Buffer, &mut Buffer) -> Result<()> + Send + Sync + 'static,
    {
        self.dispatchers.insert(service_id, Box::new(dispatcher));
    }

    pub fn register_stream_dispatcher<F>(&mut self, service_id: u16, dispatcher: F)
    where
        F: Fn(u16, &mut Buffer, &mut StreamWriter) -> Result<()> + Send + Sync + 'static,
    {
        self.stream_dispatchers.insert(service_id, Box::new(dispatcher));
    }

    /// Announce a method in the init message, with its `method_flags`.
    /// Also what routes a call when a unary and a stream dispatcher share a
    /// service id (see `routes_to_stream`).
    pub fn register_method(&mut self, service_id: u16, method_id: u16, flags: u16) {
        self.methods.push(MethodDescriptor { service_id, method_id, flags, reserved: 0 });
    }

    pub fn methods(&self) -> &[MethodDescriptor] {
        &self.methods
    }

    pub fn has_service(&self, service_id: u16) -> bool {
        self.dispatchers.contains_key(&service_id)
    }

    pub fn set_capability(&mut self, cap: u32) {
        self.local_capabilities |= cap;
    }

    pub fn clear_capability(&mut self, cap: u32) {
        self.local_capabilities &= !cap;
    }

    /// The capability bits announced: those set by hand, plus `STREAMING`
    /// when any stream dispatcher is registered.
    pub fn capabilities(&self) -> u32 {
        let mut caps = self.local_capabilities;
        if !self.stream_dispatchers.is_empty() {
            caps |= capability::STREAMING;
        }
        caps
    }

    /// Claim an extension capability slot (bits 16-23) for `name` and
    /// return its bit. The same name returns the same bit; a ninth name is
    /// an error.
    pub fn register_extension(&mut self, name: &str) -> Result<u32> {
        if let Some(slot) = self.extension_names.iter().position(|n| n == name) {
            return Ok(capability::EXT_0 << slot);
        }
        if self.extension_names.len() >= 8 {
            return Err(Error::protocol("all 8 extension capability slots are in use"));
        }
        let bit = capability::EXT_0 << self.extension_names.len();
        self.extension_names.push(name.to_string());
        self.set_capability(bit);
        Ok(bit)
    }

    pub fn has_extension(&self, name: &str) -> bool {
        self.extension_names.iter().any(|n| n == name)
    }

    /// Wrap every client's transport, before the init message is sent.
    pub fn set_transport_wrapper<F>(&mut self, wrapper: F)
    where
        F: Fn(Box<dyn Transport>) -> Box<dyn Transport> + Send + Sync + 'static,
    {
        self.wrapper = Some(Box::new(wrapper));
    }

    fn wrap(&self, transport: Box<dyn Transport>) -> Box<dyn Transport> {
        match &self.wrapper {
            Some(wrapper) => wrapper(transport),
            None => transport,
        }
    }

    /// Whether a call to (service, method) goes to the stream dispatcher.
    /// With only a stream dispatcher on the id it does. When a unary
    /// dispatcher shares the id, the method's registered flags decide, and
    /// an undeclared method goes to the stream dispatcher.
    pub fn routes_to_stream(&self, service_id: u16, method_id: u16) -> bool {
        if !self.stream_dispatchers.contains_key(&service_id) {
            return false;
        }
        if !self.dispatchers.contains_key(&service_id) {
            return true;
        }
        for m in &self.methods {
            if m.service_id == service_id && m.method_id == method_id {
                return m.flags & method_flags::STREAMING != 0;
            }
        }
        true
    }

    fn init_message(&self) -> Result<Buffer> {
        wire::create_init_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, self.capabilities(), &self.methods)
    }

    /// Serve one client on `transport` until it disconnects or asks to shut
    /// down: send the init message, then answer each message in turn. An
    /// `Err` is a transport or protocol failure (including a failed HMAC
    /// check); the connection is then unusable.
    pub fn serve(&self, transport: &mut dyn Transport) -> Result<ServeEnd> {
        transport.send(self.init_message()?.data())?;
        loop {
            let mut msg = match transport.receive(None) {
                Ok(Some(msg)) => msg,
                Ok(None) | Err(Error::Closed) => return Ok(ServeEnd::Disconnected),
                Err(e) => return Err(e),
            };
            let hdr = wire::decode_header(&mut msg)?;
            if hdr.magic != wire::MAGIC {
                return Err(Error::protocol("invalid magic number from client"));
            }
            match hdr.msg_type() {
                Some(MsgType::Shutdown) => return Ok(ServeEnd::Shutdown),
                // The acknowledgment of a v1.1 client: read and dropped.
                Some(MsgType::InitAck) => continue,
                _ => {}
            }
            let mut payload = Buffer::from_slice(&msg.data()[HEADER_SIZE..]);
            match self.handle_message(&hdr, &mut payload, transport) {
                Ok(()) => {}
                // The client went away while a reply was being written.
                Err(Error::Closed) | Err(Error::Io(_)) => return Ok(ServeEnd::Disconnected),
                Err(e) => return Err(e),
            }
        }
    }

    /// Answer one message. The `Err` of a dispatcher becomes an `error`
    /// message; the `Err` returned here is a failure to send.
    fn handle_message(&self, hdr: &wire::Header, payload: &mut Buffer, transport: &mut dyn Transport) -> Result<()> {
        let seq = hdr.sequence_id;
        match hdr.msg_type() {
            Some(MsgType::Call) => {}
            // Requests of features this runtime does not have, which a
            // client waits on: answered, so it does not wait for ever.
            Some(MsgType::Create) | Some(MsgType::PropGet) | Some(MsgType::PropSet) | Some(MsgType::ObjectCall) => {
                return send_error(
                    transport,
                    seq,
                    code::INVALID_MESSAGE,
                    "objects and properties are not supported by the Rust runtime",
                );
            }
            // Everything else expects no reply (release, subscribe,
            // unsubscribe, ping, types this runtime does not know).
            _ => return Ok(()),
        }

        let (service_id, method_id) = match wire::decode_method_call_header(payload) {
            Ok(ids) => ids,
            Err(e) => return send_error(transport, seq, code::INVALID_MESSAGE, &e.to_string()),
        };

        if self.routes_to_stream(service_id, method_id) {
            let dispatcher = &self.stream_dispatchers[&service_id];
            let outcome = {
                let mut writer = StreamWriter { transport: &mut *transport, sequence_id: seq, chunks: 0 };
                guarded(|| dispatcher(method_id, payload, &mut writer))
            };
            return match outcome {
                Ok(()) => transport.send(wire::create_stream_end_message(seq)?.data()),
                // The failure ends the stream: the client reads the error,
                // never a clean-looking stream_end.
                Err(e) => send_error(transport, seq, code::UNKNOWN_METHOD, &e.wire_message()),
            };
        }

        let dispatcher = match self.dispatchers.get(&service_id) {
            Some(dispatcher) => dispatcher,
            None => return send_error(transport, seq, code::UNKNOWN_SERVICE, "Unknown service ID"),
        };
        let mut response = Buffer::new();
        match guarded(|| dispatcher(method_id, payload, &mut response)) {
            Ok(()) => match wire::create_result_message(seq, response.data()) {
                Ok(msg) => transport.send(msg.data()),
                Err(e) => send_error(transport, seq, code::UNKNOWN_METHOD, &e.to_string()),
            },
            Err(e) => send_error(transport, seq, code::UNKNOWN_METHOD, &e.wire_message()),
        }
    }

    /// Serve the one client on stdin and stdout: a service spawned over
    /// pipes. Returns when the parent closes the pipe or sends `shutdown`.
    pub fn run(&self) -> Result<ServeEnd> {
        let mut transport = self.wrap(Box::new(PipeTransport::stdio()));
        self.serve(transport.as_mut())
    }

    /// Serve TCP clients on `listener`, each on a thread of its own, for
    /// ever. Returns only when accepting fails.
    pub fn serve_listener(self: &Arc<Self>, listener: TcpListener) -> Result<()> {
        loop {
            let (stream, _) = match listener.accept() {
                Ok(pair) => pair,
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(e) if e.kind() == std::io::ErrorKind::ConnectionAborted => continue,
                Err(e) => return Err(e.into()),
            };
            let runtime = Arc::clone(self);
            std::thread::spawn(move || {
                let tcp = match TcpTransport::from_stream(stream) {
                    Ok(tcp) => tcp,
                    Err(_) => return,
                };
                let mut transport = runtime.wrap(Box::new(tcp));
                // A failure ends this client only; dropping the transport
                // closes its socket.
                let _ = runtime.serve(transport.as_mut());
                transport.close();
            });
        }
    }

    /// Bind `addr` (for example `("0.0.0.0", 9000)`) and serve TCP clients,
    /// a thread per client, for ever.
    pub fn run_tcp_multi<A: ToSocketAddrs>(self: &Arc<Self>, addr: A) -> Result<()> {
        self.serve_listener(TcpListener::bind(addr)?)
    }
}

fn send_error(transport: &mut dyn Transport, seq: u32, code: u16, text: &str) -> Result<()> {
    let msg = match wire::create_error_message(seq, code, text) {
        Ok(msg) => msg,
        // The text itself cannot be sent (over a size cap): say so instead.
        Err(_) => wire::create_error_message(seq, code, "error message too large to send")?,
    };
    transport.send(msg.data())
}

/// Run a dispatcher; a panic inside it is reported to the client as an
/// error instead of taking the serving thread (or a pipe service) down
/// with no reply.
fn guarded<F: FnOnce() -> Result<()>>(f: F) -> Result<()> {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(outcome) => outcome,
        Err(panic) => {
            let text = panic
                .downcast_ref::<&str>()
                .map(|s| s.to_string())
                .or_else(|| panic.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "unknown panic".to_string());
            Err(Error::service(format!("dispatcher panicked: {text}")))
        }
    }
}
