// MIT License
// Copyright (c) 2026 dbjwhs

//! `Transport`: how whole messages move. Everything above the byte layer
//! (the client, the server, the HMAC wrapper) talks to this trait.
//!
//! A message is the 16-byte header followed by its payload. `receive`
//! hands back the whole message with the read position at the header.

use crate::buffer::Buffer;
use crate::error::{Error, Result};
use crate::wire::{self, HEADER_SIZE, MAX_PAYLOAD_SIZE};
use std::io::{Read, Write};
use std::net::{Shutdown, TcpStream, ToSocketAddrs};
use std::time::Duration;

pub trait Transport: Send {
    /// Send one whole message (header and payload).
    fn send(&mut self, msg: &[u8]) -> Result<()>;

    /// Receive one whole message. `Ok(None)` is a clean close between
    /// messages. `timeout` is how long to wait for the message to begin;
    /// `None` waits for ever. A timeout is `Err(Error::Timeout)`, and a
    /// close in the middle of a message is `Err(Error::Closed)`.
    fn receive(&mut self, timeout: Option<Duration>) -> Result<Option<Buffer>>;

    /// Close the transport. Never fails; closing twice is harmless.
    fn close(&mut self);

    fn is_connected(&self) -> bool;
}

/// Read one message from `reader`: the header, a check of the magic and of
/// the payload size against the cap, then exactly that many payload bytes.
/// `Ok(None)` when the stream ends before the first byte of a header.
pub fn read_message<R: Read>(reader: &mut R) -> Result<Option<Buffer>> {
    let mut header = [0u8; HEADER_SIZE];
    let mut filled = 0;
    while filled < HEADER_SIZE {
        match reader.read(&mut header[filled..]) {
            Ok(0) if filled == 0 => return Ok(None),
            Ok(0) => return Err(Error::Closed),
            Ok(n) => filled += n,
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
            Err(e) => return Err(e.into()),
        }
    }
    let mut probe = Buffer::from_slice(&header);
    let hdr = wire::decode_header(&mut probe)?;
    if hdr.magic != wire::MAGIC {
        return Err(Error::protocol(format!("invalid magic number 0x{:08X}", hdr.magic)));
    }
    let size = hdr.payload_size as usize;
    if size > MAX_PAYLOAD_SIZE {
        return Err(Error::protocol("payload size exceeds maximum"));
    }
    let mut msg = Vec::with_capacity(HEADER_SIZE + size);
    msg.extend_from_slice(&header);
    msg.resize(HEADER_SIZE + size, 0);
    reader.read_exact(&mut msg[HEADER_SIZE..])?;
    Ok(Some(Buffer::from_vec(msg)))
}

/// Messages over a TCP socket.
pub struct TcpTransport {
    stream: Option<TcpStream>,
}

impl TcpTransport {
    /// Connect to `host:port`, waiting at most `timeout` for the connection.
    pub fn connect(host: &str, port: u16, timeout: Duration) -> Result<TcpTransport> {
        let mut last = None;
        for addr in (host, port).to_socket_addrs()? {
            match TcpStream::connect_timeout(&addr, timeout) {
                Ok(stream) => return TcpTransport::from_stream(stream),
                Err(e) => last = Some(e),
            }
        }
        Err(match last {
            Some(e) => Error::Io(e),
            None => Error::protocol(format!("no address for {host}:{port}")),
        })
    }

    /// Wrap a connected socket (a client's, or one a listener accepted).
    pub fn from_stream(stream: TcpStream) -> Result<TcpTransport> {
        stream.set_nodelay(true)?;
        Ok(TcpTransport { stream: Some(stream) })
    }
}

impl Transport for TcpTransport {
    fn send(&mut self, msg: &[u8]) -> Result<()> {
        let stream = self.stream.as_mut().ok_or(Error::Closed)?;
        stream.write_all(msg)?;
        Ok(())
    }

    fn receive(&mut self, timeout: Option<Duration>) -> Result<Option<Buffer>> {
        let stream = self.stream.as_mut().ok_or(Error::Closed)?;
        // A zero timeout is "do not block" to the socket API and an error
        // to set_read_timeout; the shortest wait stands in for it.
        let wait = timeout.map(|t| t.max(Duration::from_millis(1)));
        stream.set_read_timeout(wait)?;
        let got = read_message(stream);
        if matches!(got, Ok(None) | Err(Error::Closed)) {
            self.close();
        }
        got
    }

    fn close(&mut self) {
        if let Some(stream) = self.stream.take() {
            let _ = stream.shutdown(Shutdown::Both);
        }
    }

    fn is_connected(&self) -> bool {
        self.stream.is_some()
    }
}

/// Messages over any reader and writer: the pipes of a service a
/// ServiceManager spawned (its stdin and stdout), or a test's in-memory
/// pair. There is no timeout on a pipe: `receive` waits for the message
/// whatever `timeout` says.
pub struct PipeTransport<R: Read + Send, W: Write + Send> {
    reader: R,
    writer: W,
    open: bool,
}

impl<R: Read + Send, W: Write + Send> PipeTransport<R, W> {
    pub fn new(reader: R, writer: W) -> PipeTransport<R, W> {
        PipeTransport { reader, writer, open: true }
    }
}

impl PipeTransport<std::io::Stdin, std::io::Stdout> {
    /// The transport of a service run over pipes: requests on stdin,
    /// replies on stdout.
    pub fn stdio() -> PipeTransport<std::io::Stdin, std::io::Stdout> {
        PipeTransport::new(std::io::stdin(), std::io::stdout())
    }
}

impl<R: Read + Send, W: Write + Send> Transport for PipeTransport<R, W> {
    fn send(&mut self, msg: &[u8]) -> Result<()> {
        if !self.open {
            return Err(Error::Closed);
        }
        self.writer.write_all(msg)?;
        self.writer.flush()?;
        Ok(())
    }

    fn receive(&mut self, _timeout: Option<Duration>) -> Result<Option<Buffer>> {
        if !self.open {
            return Err(Error::Closed);
        }
        let got = read_message(&mut self.reader);
        if matches!(got, Ok(None) | Err(Error::Closed)) {
            self.open = false;
        }
        got
    }

    fn close(&mut self) {
        self.open = false;
    }

    fn is_connected(&self) -> bool {
        self.open
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn frame(payload: &[u8]) -> Vec<u8> {
        wire::create_result_message(3, payload).unwrap().into_vec()
    }

    #[test]
    fn read_message_returns_whole_messages_then_a_clean_end() {
        let mut bytes = frame(b"abc");
        bytes.extend(frame(b""));
        let mut cursor = Cursor::new(bytes);
        let mut first = read_message(&mut cursor).unwrap().unwrap();
        let hdr = wire::decode_header_validated(&mut first).unwrap();
        assert_eq!((hdr.payload_size, first.unread()), (3, &b"abc"[..]));
        let second = read_message(&mut cursor).unwrap().unwrap();
        assert_eq!(second.size(), HEADER_SIZE);
        assert!(read_message(&mut cursor).unwrap().is_none());
    }

    #[test]
    fn a_stream_that_ends_inside_a_message_is_closed_not_clean() {
        let bytes = frame(b"abcdef");
        for cut in [5, HEADER_SIZE, HEADER_SIZE + 2] {
            let mut cursor = Cursor::new(bytes[..cut].to_vec());
            assert!(matches!(read_message(&mut cursor), Err(Error::Closed)), "cut at {cut}");
        }
    }

    /// The payload size is checked against the cap before the payload is
    /// allocated, and a wrong magic is refused before anything else.
    #[test]
    fn a_bad_header_is_refused_before_the_payload_is_read() {
        let mut big = Buffer::new();
        wire::encode_header(&mut big, &wire::Header::new(wire::MsgType::Result, 1, MAX_PAYLOAD_SIZE as u32 + 1));
        assert!(matches!(read_message(&mut Cursor::new(big.into_vec())), Err(Error::Protocol(_))));

        let mut wrong = frame(b"x");
        wrong[0] ^= 0xFF;
        assert!(matches!(read_message(&mut Cursor::new(wrong)), Err(Error::Protocol(_))));
    }

    #[test]
    fn pipe_transport_moves_messages_and_reports_the_end() {
        let incoming = Cursor::new(frame(b"hi"));
        let mut out = Vec::new();
        {
            let mut t = PipeTransport::new(incoming, &mut out);
            assert!(t.is_connected());
            t.send(&frame(b"yo")).unwrap();
            assert!(t.receive(None).unwrap().is_some());
            assert!(t.receive(None).unwrap().is_none());
            assert!(!t.is_connected());
            assert!(matches!(t.send(&frame(b"")), Err(Error::Closed)));
        }
        assert_eq!(out, frame(b"yo"));
    }
}
