// MIT License
// Copyright (c) 2026 dbjwhs

//! The interop service (sing/network/rust_interop/interop.song) served by
//! the Rust runtime, for the cross-language tests and as an example of a
//! Rust service.
//!
//!     interop_service                 serve one client on stdin/stdout (pipes)
//!     interop_service <port> [key]    serve TCP on 127.0.0.1, a thread per
//!                                     client; with a key, every client is
//!                                     served through SecureTransport
//!
//! Port 0 picks a free port. The port in use is printed to stderr as
//! `listening on 127.0.0.1:<port>`.

#![deny(warnings)]

#[path = "../tests/generated/interop.rs"]
mod interop;
#[path = "../tests/common/interop_impl.rs"]
mod interop_impl;

use song::wire::capability;
use song::SecureTransport;
use std::net::TcpListener;
use std::process::ExitCode;
use std::sync::Arc;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut runtime = interop_impl::interop_runtime();

    if args.is_empty() {
        // Pipes: stdout carries the protocol, so nothing else is printed.
        return match runtime.run() {
            Ok(_) => ExitCode::SUCCESS,
            Err(e) => {
                eprintln!("interop_service: {e}");
                ExitCode::FAILURE
            }
        };
    }

    let port: u16 = match args[0].parse() {
        Ok(port) => port,
        Err(_) => {
            eprintln!("usage: interop_service [<port> [key]]");
            return ExitCode::from(2);
        }
    };
    if let Some(key) = args.get(1) {
        let key = key.clone().into_bytes();
        runtime.set_capability(capability::HMAC);
        runtime.set_transport_wrapper(move |t| Box::new(SecureTransport::new(t, key.clone())));
    }

    let listener = match TcpListener::bind(("127.0.0.1", port)) {
        Ok(listener) => listener,
        Err(e) => {
            eprintln!("interop_service: cannot listen on port {port}: {e}");
            return ExitCode::FAILURE;
        }
    };
    if let Ok(addr) = listener.local_addr() {
        eprintln!("listening on {addr}");
    }
    match Arc::new(runtime).serve_listener(listener) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("interop_service: {e}");
            ExitCode::FAILURE
        }
    }
}
