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

// ---- a real peer process ---------------------------------------------------------

use std::process::{Child, Command, Stdio};

/// The C++ build tree: $SONG_BUILD_DIR, else `build/` in this checkout.
pub fn build_dir() -> PathBuf {
    match std::env::var_os("SONG_BUILD_DIR") {
        Some(dir) => PathBuf::from(dir),
        None => repo_root().join("build"),
    }
}

/// One peer service at a time in a test process. The tests of one binary
/// run on parallel threads, and a child started while another thread is
/// creating a socket can inherit that socket (the close-on-exec flag is set
/// in a second step on macOS). A service holding another test's listening
/// or client socket keeps it open: connections are accepted by nobody, or
/// never seen to close. Holding this lock for the life of the service
/// keeps starting a process and using sockets apart.
static ONE_PEER: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// A service process listening on a local port, killed when dropped. The
/// same shape as the C++ and Python tests: pick a port, start
/// `<binary> <port> [key]`, wait until it accepts connections.
pub struct PeerService {
    pub port: u16,
    child: Child,
    _one_at_a_time: std::sync::MutexGuard<'static, ()>,
}

impl PeerService {
    /// `None` when the binary is not built: the caller skips. With
    /// SONG_REQUIRE_PEERS set (ctest sets it) a missing binary is a
    /// failure instead, so a ctest run cannot pass by skipping.
    pub fn start(binary: &std::path::Path, key: Option<&str>) -> Option<PeerService> {
        if !binary.exists() {
            let message = format!("{} is not built (build the C++ tree: cmake --build build)", binary.display());
            if std::env::var_os("SONG_REQUIRE_PEERS").is_some() {
                panic!("{message}");
            }
            eprintln!("SKIPPED: {message}");
            return None;
        }
        // A failed test poisons the lock; the next test still takes its turn.
        let turn = ONE_PEER.lock().unwrap_or_else(|e| e.into_inner());
        // A service that exits before it listens lost its port to another
        // process: try again on a new one.
        for _ in 0..5 {
            let port = {
                let probe = TcpListener::bind(("127.0.0.1", 0)).expect("bind");
                probe.local_addr().expect("addr").port()
            };
            let mut command = Command::new(binary);
            command.arg(port.to_string()).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null());
            if let Some(key) = key {
                command.arg(key);
            }
            let mut child = command.spawn().expect("start the peer service");
            let mut listening = false;
            for _ in 0..200 {
                std::thread::sleep(std::time::Duration::from_millis(25));
                if child.try_wait().expect("poll the peer service").is_some() {
                    break;
                }
                if TcpStream::connect(("127.0.0.1", port)).is_ok() {
                    listening = true;
                    break;
                }
            }
            if listening {
                return Some(PeerService { port, child, _one_at_a_time: turn });
            }
            let _ = child.kill();
            let _ = child.wait();
        }
        panic!("{} did not start listening", binary.display());
    }
}

impl Drop for PeerService {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}
