// MIT License
// Copyright (c) 2026 dbjwhs

//! Shared by the integration tests: the golden vector file.

#![allow(dead_code)]

use std::collections::BTreeMap;
use std::path::PathBuf;

/// The repository root (the crate lives in `<root>/rust`).
pub fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).parent().expect("crate has a parent directory").to_path_buf()
}

pub fn unhex(text: &str) -> Vec<u8> {
    assert!(text.len() % 2 == 0, "odd hex length: {text}");
    (0..text.len() / 2)
        .map(|ndx| u8::from_str_radix(&text[ndx * 2..ndx * 2 + 2], 16).expect("hex digit"))
        .collect()
}

pub fn hex(bytes: &[u8]) -> String {
    use std::fmt::Write;
    let mut out = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        write!(out, "{b:02x}").expect("write to a String");
    }
    out
}

/// `test/golden/wire_vectors.txt` as name -> bytes.
pub fn golden_vectors() -> BTreeMap<String, Vec<u8>> {
    let path = repo_root().join("test").join("golden").join("wire_vectors.txt");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
    let mut out = BTreeMap::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (name, value) = line.split_once('=').unwrap_or_else(|| panic!("not a vector line: {line}"));
        out.insert(name.trim().to_string(), unhex(value.trim()));
    }
    out
}

// ---- a scripted peer ---------------------------------------------------------

use song::wire;
use std::net::{TcpListener, TcpStream};
use std::thread::JoinHandle;

/// Accepts one client on a real TCP socket, sends `greeting`, then hands
/// every message the client sends (init_ack apart) to `script`, which
/// writes whatever frames it likes. The port of `test_streaming.py`'s
/// ScriptedServer: the tests state the exact bytes a peer sends.
pub struct ScriptedServer {
    pub port: u16,
    handle: Option<JoinHandle<Vec<Vec<u8>>>>,
}

impl ScriptedServer {
    pub fn start<F>(greeting: Vec<u8>, mut script: F) -> ScriptedServer
    where
        F: FnMut(usize, &[u8], &mut TcpStream) + Send + 'static,
    {
        let listener = TcpListener::bind(("127.0.0.1", 0)).expect("bind");
        let port = listener.local_addr().expect("local addr").port();
        let handle = std::thread::spawn(move || {
            use std::io::Write;
            let mut received = Vec::new();
            let (mut stream, _) = match listener.accept() {
                Ok(pair) => pair,
                Err(_) => return received,
            };
            if stream.write_all(&greeting).is_err() {
                return received;
            }
            let mut calls = 0;
            while let Ok(Some(msg)) = song::transport::read_message(&mut stream) {
                let raw = msg.into_vec();
                let is_ack = raw[5] == wire::MsgType::InitAck as u8;
                received.push(raw.clone());
                if is_ack {
                    continue;
                }
                script(calls, &raw, &mut stream);
                calls += 1;
            }
            received
        });
        ScriptedServer { port, handle: Some(handle) }
    }

    /// Every message the client sent, once the client has gone away.
    pub fn received(mut self) -> Vec<Vec<u8>> {
        self.handle.take().expect("joined once").join().expect("server thread")
    }
}

/// The sequence id of a raw message.
pub fn seq_of(raw: &[u8]) -> u32 {
    u32::from_ne_bytes([raw[12], raw[13], raw[14], raw[15]])
}

pub fn init_frame(first: u16, current: u16, caps: u32, methods: &[wire::MethodDescriptor]) -> Vec<u8> {
    wire::create_init_message(first, current, caps, methods).expect("init").into_vec()
}

/// The greeting of a current C++ service with no methods announced.
pub fn init_v11() -> Vec<u8> {
    init_frame(wire::FIRST_VERSION, wire::CURRENT_VERSION, wire::capability::STREAMING, &[])
}

pub fn i32_payload(value: i32) -> Vec<u8> {
    let mut b = song::Buffer::new();
    b.encode_i32(value);
    b.into_vec()
}

pub fn send(stream: &mut TcpStream, frame: &[u8]) {
    use std::io::Write;
    let _ = stream.write_all(frame);
}
