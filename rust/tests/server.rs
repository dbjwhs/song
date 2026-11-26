// MIT License
// Copyright (c) 2026 dbjwhs

//! The service runtime: a Rust client against a Rust service over TCP, and
//! the exact frames a service writes over pipes.

mod common;

use common::i32_payload;
use song::error::code;
use song::wire::{self, capability, method_flags, MsgType};
use song::{
    Buffer, ConnectOptions, Error, PipeTransport, SecureTransport, ServeEnd, ServiceConnection, ServiceRuntime,
};
use std::io::Cursor;
use std::net::TcpListener;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use std::time::Duration;

const CALC: u16 = 1;
const FEED: u16 = 2;
const ADD: u16 = 1;
const DIVIDE: u16 = 2;
const PANIC: u16 = 3;
const COUNT_TO: u16 = 1;
const FAIL_AFTER: u16 = 2;

/// A small service: a calculator on one id, a stream on another.
fn runtime() -> ServiceRuntime {
    let mut rt = ServiceRuntime::new();
    rt.register_dispatcher(CALC, |method, req, resp| match method {
        ADD => {
            let sum = req.decode_i32()? + req.decode_i32()?;
            resp.encode_i32(sum);
            Ok(())
        }
        DIVIDE => {
            let (a, b) = (req.decode_i32()?, req.decode_i32()?);
            if b == 0 {
                return Err(Error::service("Division by zero"));
            }
            resp.encode_i32(a / b);
            Ok(())
        }
        PANIC => panic!("the implementation fell over"),
        other => Err(Error::service(format!("Unknown method ID: {other}"))),
    });
    rt.register_stream_dispatcher(FEED, |method, req, writer| match method {
        COUNT_TO => {
            let n = req.decode_i32()?;
            for value in 1..=n {
                writer.write_bytes(&i32_payload(value))?;
            }
            Ok(())
        }
        FAIL_AFTER => {
            let n = req.decode_i32()?;
            for value in 1..=n {
                writer.write_bytes(&i32_payload(value))?;
            }
            Err(Error::service("the source failed part way"))
        }
        other => Err(Error::service(format!("Unknown streaming method ID: {other}"))),
    });
    rt.register_method(CALC, ADD, method_flags::NONE);
    rt.register_method(CALC, DIVIDE, method_flags::NONE);
    rt.register_method(FEED, COUNT_TO, method_flags::STREAMING);
    rt.register_method(FEED, FAIL_AFTER, method_flags::STREAMING);
    rt
}

/// Serve `rt` on a free local port, a thread per client; the port.
fn serve(rt: ServiceRuntime) -> u16 {
    let listener = TcpListener::bind(("127.0.0.1", 0)).expect("bind");
    let port = listener.local_addr().expect("addr").port();
    let rt = Arc::new(rt);
    std::thread::spawn(move || {
        let _ = rt.serve_listener(listener);
    });
    port
}

fn args(values: &[i32]) -> Buffer {
    let mut b = Buffer::new();
    for v in values {
        b.encode_i32(*v);
    }
    b
}

fn ints(conn: &mut ServiceConnection, method: u16, n: i32) -> (Vec<i32>, Option<Error>) {
    let mut got = Vec::new();
    let mut failure = None;
    for item in conn.call_streaming(FEED, method, &args(&[n]), Some(Duration::from_secs(5))).unwrap() {
        match item {
            Ok(mut chunk) => got.push(chunk.decode_i32().unwrap()),
            Err(e) => failure = Some(e),
        }
    }
    (got, failure)
}

#[test]
fn the_init_message_announces_methods_and_capabilities() {
    let mut rt = runtime();
    let ext = rt.register_extension("song.test.ext").unwrap();
    assert_eq!(ext, capability::EXT_0);
    assert_eq!(rt.register_extension("song.test.ext").unwrap(), ext, "the same name keeps its bit");
    assert!(rt.has_extension("song.test.ext") && !rt.has_extension("other"));
    rt.set_capability(capability::HMAC | capability::ONEWAY);
    rt.clear_capability(capability::ONEWAY);
    let want_caps = capability::STREAMING | capability::HMAC | capability::EXT_0;
    assert_eq!(rt.capabilities(), want_caps, "streaming is announced because a stream dispatcher exists");

    let port = serve(rt);
    let conn = ServiceConnection::connect_with(
        "127.0.0.1",
        port,
        ConnectOptions { capabilities: capability::STREAMING | capability::EXT_0, ..ConnectOptions::default() },
    )
    .unwrap();
    assert_eq!(conn.peer_capabilities(), want_caps);
    assert_eq!(conn.negotiated_capabilities(), capability::STREAMING | capability::EXT_0);
    assert_eq!(conn.negotiated_version(), wire::CURRENT_VERSION);
    let methods: Vec<(u16, u16, u16)> = conn.methods().iter().map(|m| (m.service_id, m.method_id, m.flags)).collect();
    assert_eq!(
        methods,
        vec![
            (CALC, ADD, method_flags::NONE),
            (CALC, DIVIDE, method_flags::NONE),
            (FEED, COUNT_TO, method_flags::STREAMING),
            (FEED, FAIL_AFTER, method_flags::STREAMING)
        ]
    );
}

#[test]
fn a_ninth_extension_is_refused() {
    let mut rt = ServiceRuntime::new();
    for ndx in 0..8 {
        assert_eq!(rt.register_extension(&format!("ext-{ndx}")).unwrap(), capability::EXT_0 << ndx);
    }
    assert!(rt.register_extension("one too many").is_err());
    assert_eq!(rt.capabilities(), 0x00FF_0000);
}

#[test]
fn unary_calls_results_and_errors() {
    let port = serve(runtime());
    let mut conn = ServiceConnection::connect("127.0.0.1", port).unwrap();
    assert_eq!(conn.call(CALC, ADD, &args(&[5, 9])).unwrap().decode_i32().unwrap(), 14);
    assert_eq!(conn.call(CALC, DIVIDE, &args(&[9, 3])).unwrap().decode_i32().unwrap(), 3);

    let err = conn.call(CALC, DIVIDE, &args(&[1, 0])).unwrap_err();
    assert!(
        matches!(&err, Error::Service { code: code::UNKNOWN_METHOD, message } if message == "Division by zero"),
        "{err}"
    );
    let err = conn.call(CALC, 99, &Buffer::new()).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message == "Unknown method ID: 99"), "{err}");
    let err = conn.call(77, 1, &Buffer::new()).unwrap_err();
    assert!(
        matches!(&err, Error::Service { code: code::UNKNOWN_SERVICE, message } if message == "Unknown service ID"),
        "{err}"
    );
    // A request too short for its arguments is the dispatcher's decode error.
    let err = conn.call(CALC, ADD, &args(&[5])).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message.contains("buffer underflow")), "{err}");

    assert_eq!(conn.call(CALC, ADD, &args(&[1, 1])).unwrap().decode_i32().unwrap(), 2, "still serving");
}

#[test]
fn a_panicking_dispatcher_is_an_error_reply_and_the_service_goes_on() {
    let port = serve(runtime());
    let mut conn = ServiceConnection::connect("127.0.0.1", port).unwrap();
    let err = conn.call(CALC, PANIC, &Buffer::new()).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message.contains("the implementation fell over")), "{err}");
    assert_eq!(conn.call(CALC, ADD, &args(&[2, 2])).unwrap().decode_i32().unwrap(), 4);
}

#[test]
fn a_stream_ends_cleanly_and_the_connection_goes_on() {
    let port = serve(runtime());
    let mut conn = ServiceConnection::connect("127.0.0.1", port).unwrap();
    let (got, failure) = ints(&mut conn, COUNT_TO, 4);
    assert!(failure.is_none());
    assert_eq!(got, vec![1, 2, 3, 4]);
    let (none, failure) = ints(&mut conn, COUNT_TO, 0);
    assert!(none.is_empty() && failure.is_none(), "an empty stream is just a stream_end");
    assert_eq!(conn.call(CALC, ADD, &args(&[1, 2])).unwrap().decode_i32().unwrap(), 3);
}

#[test]
fn a_stream_dispatcher_that_fails_ends_the_stream_with_the_error() {
    let port = serve(runtime());
    let mut conn = ServiceConnection::connect("127.0.0.1", port).unwrap();
    let (got, failure) = ints(&mut conn, FAIL_AFTER, 3);
    assert_eq!(got, vec![1, 2, 3], "the chunks written before the failure arrive");
    let failure = failure.expect("the stream ended with an error, not a clean stream_end");
    assert!(
        matches!(&failure, Error::Service { code: code::UNKNOWN_METHOD, message } if message == "the source failed part way"),
        "{failure}"
    );

    // Failing before any chunk, and an unknown streaming method.
    let (got, failure) = ints(&mut conn, FAIL_AFTER, 0);
    assert!(got.is_empty() && matches!(failure, Some(Error::Service { .. })));
    let (got, failure) = ints(&mut conn, 55, 0);
    assert!(got.is_empty());
    assert!(matches!(&failure, Some(Error::Service { message, .. }) if message == "Unknown streaming method ID: 55"));

    assert!(conn.is_connected());
    assert_eq!(ints(&mut conn, COUNT_TO, 2).0, vec![1, 2], "the connection is at a message boundary");
}

/// On the wire: chunk, chunk, error. Never a stream_end after a failure.
#[test]
fn the_frames_of_a_failed_stream_over_pipes() {
    let mut input = Vec::new();
    input.extend_from_slice(wire::create_call_message(9, FEED, FAIL_AFTER, args(&[2]).data()).unwrap().data());
    input.extend_from_slice(wire::create_call_message(10, FEED, COUNT_TO, args(&[1]).data()).unwrap().data());
    let mut output = Vec::new();
    let rt = runtime();
    let end = {
        let mut transport = PipeTransport::new(Cursor::new(input), &mut output);
        rt.serve(&mut transport).unwrap()
    };
    assert_eq!(end, ServeEnd::Disconnected);

    let mut want = Vec::new();
    want.extend_from_slice(
        wire::create_init_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, capability::STREAMING, rt.methods())
            .unwrap()
            .data(),
    );
    want.extend_from_slice(wire::create_stream_message(9, &i32_payload(1)).unwrap().data());
    want.extend_from_slice(wire::create_stream_message(9, &i32_payload(2)).unwrap().data());
    want.extend_from_slice(wire::create_error_message(9, code::UNKNOWN_METHOD, "the source failed part way").unwrap().data());
    want.extend_from_slice(wire::create_stream_message(10, &i32_payload(1)).unwrap().data());
    want.extend_from_slice(wire::create_stream_end_message(10).unwrap().data());
    assert_eq!(common::hex(&output), common::hex(&want));
}

#[test]
fn a_pipe_service_answers_until_shutdown_and_ignores_init_ack() {
    let mut input = Vec::new();
    input.extend_from_slice(
        wire::create_init_ack_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, capability::STREAMING).unwrap().data(),
    );
    input.extend_from_slice(wire::create_call_message(1, CALC, ADD, args(&[20, 22]).data()).unwrap().data());
    input.extend_from_slice(wire::create_shutdown_message().unwrap().data());
    input.extend_from_slice(wire::create_call_message(2, CALC, ADD, args(&[1, 1]).data()).unwrap().data());
    let mut output = Vec::new();
    let rt = runtime();
    let end = {
        let mut transport = PipeTransport::new(Cursor::new(input), &mut output);
        rt.serve(&mut transport).unwrap()
    };
    assert_eq!(end, ServeEnd::Shutdown);
    let init = wire::create_init_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, capability::STREAMING, rt.methods())
        .unwrap();
    let reply = wire::create_result_message(1, &i32_payload(42)).unwrap();
    let mut want = init.into_vec();
    want.extend_from_slice(reply.data());
    assert_eq!(common::hex(&output), common::hex(&want), "init, one result, nothing after the shutdown");
}

#[test]
fn messages_the_runtime_has_no_feature_for() {
    let frame = |msg_type: MsgType, seq: u32| {
        let mut b = Buffer::new();
        wire::encode_header(&mut b, &wire::Header::new(msg_type, seq, 12));
        b.write(&[0u8; 12]);
        b.into_vec()
    };
    let mut input = Vec::new();
    input.extend(frame(MsgType::Ping, 0));
    input.extend(frame(MsgType::PropSubscribe, 0));
    input.extend(frame(MsgType::Release, 0));
    input.extend(frame(MsgType::Create, 5));
    input.extend(frame(MsgType::PropGet, 6));
    let mut output = Vec::new();
    let rt = ServiceRuntime::new();
    {
        let mut transport = PipeTransport::new(Cursor::new(input), &mut output);
        rt.serve(&mut transport).unwrap();
    }
    let text = "objects and properties are not supported by the Rust runtime";
    let mut want = wire::create_init_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, 0, &[]).unwrap().into_vec();
    want.extend_from_slice(wire::create_error_message(5, code::INVALID_MESSAGE, text).unwrap().data());
    want.extend_from_slice(wire::create_error_message(6, code::INVALID_MESSAGE, text).unwrap().data());
    assert_eq!(common::hex(&output), common::hex(&want), "requests that wait are answered; the rest are dropped");
}

/// A unary and a stream dispatcher on one service id: the registered flags
/// route each call, and an undeclared method goes to the stream dispatcher
/// (the C++ runtime's rule).
#[test]
fn a_shared_service_id_routes_by_method_flags() {
    let mut rt = ServiceRuntime::new();
    rt.register_dispatcher(7, |_, _, resp| {
        resp.encode_i32(100);
        Ok(())
    });
    rt.register_stream_dispatcher(7, |_, _, writer| writer.write_bytes(&i32_payload(200)));
    rt.register_method(7, 1, method_flags::NONE);
    rt.register_method(7, 2, method_flags::STREAMING);
    assert!(!rt.routes_to_stream(7, 1) && rt.routes_to_stream(7, 2) && rt.routes_to_stream(7, 3));
    assert!(!rt.routes_to_stream(8, 1));

    let port = serve(rt);
    let mut conn = ServiceConnection::connect("127.0.0.1", port).unwrap();
    assert_eq!(conn.call(7, 1, &Buffer::new()).unwrap().decode_i32().unwrap(), 100);
    for method in [2, 3] {
        let chunks: Vec<i32> = conn
            .call_streaming(7, method, &Buffer::new(), Some(Duration::from_secs(5)))
            .unwrap()
            .map(|c| c.unwrap().decode_i32().unwrap())
            .collect();
        assert_eq!(chunks, vec![200], "method {method}");
    }
}

#[test]
fn clients_are_served_at_the_same_time() {
    // A stream that waits for a second client's call before it ends: with
    // one client at a time this would never finish.
    let seen = Arc::new(AtomicU32::new(0));
    let mut rt = ServiceRuntime::new();
    let counter = Arc::clone(&seen);
    rt.register_dispatcher(1, move |_, _, resp| {
        resp.encode_u32(counter.fetch_add(1, Ordering::SeqCst) + 1);
        Ok(())
    });
    let waiting = Arc::clone(&seen);
    rt.register_stream_dispatcher(2, move |_, _, writer| {
        writer.write_bytes(&i32_payload(1))?;
        for _ in 0..500 {
            if waiting.load(Ordering::SeqCst) > 0 {
                return writer.write_bytes(&i32_payload(2));
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        Err(Error::service("no other client was served while this stream was open"))
    });
    let port = serve(rt);

    let mut first = ServiceConnection::connect("127.0.0.1", port).unwrap();
    let mut stream = first.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(10))).unwrap();
    assert_eq!(stream.next().unwrap().unwrap().decode_i32().unwrap(), 1);

    let mut second = ServiceConnection::connect("127.0.0.1", port).unwrap();
    assert_eq!(second.call(1, 1, &Buffer::new()).unwrap().decode_u32().unwrap(), 1);

    assert_eq!(stream.next().unwrap().unwrap().decode_i32().unwrap(), 2);
    assert!(stream.next().is_none());
}

#[test]
fn a_client_that_disconnects_mid_stream_does_not_stop_the_service() {
    let mut rt = ServiceRuntime::new();
    rt.register_stream_dispatcher(2, |_, _, writer| {
        // Far more than the socket buffers hold, so the writes fail once
        // the client has gone.
        let chunk = vec![7u8; 64 * 1024];
        for _ in 0..4096 {
            writer.write_bytes(&chunk)?;
        }
        Ok(())
    });
    rt.register_dispatcher(1, |_, _, resp| {
        resp.encode_i32(1);
        Ok(())
    });
    let port = serve(rt);
    {
        let mut conn = ServiceConnection::connect("127.0.0.1", port).unwrap();
        let mut stream = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap();
        assert_eq!(stream.next().unwrap().unwrap().size(), 64 * 1024);
    }
    let mut again = ServiceConnection::connect("127.0.0.1", port).unwrap();
    assert_eq!(again.call(1, 1, &Buffer::new()).unwrap().decode_i32().unwrap(), 1);
}

// ---- HMAC on the service side -------------------------------------------------

const KEY: &[u8] = b"song-rust-server-secret";

fn keyed_runtime() -> ServiceRuntime {
    let mut rt = runtime();
    rt.set_capability(capability::HMAC);
    rt.set_transport_wrapper(|t| Box::new(SecureTransport::new(t, KEY)));
    rt
}

fn with_key(key: Option<&[u8]>) -> ConnectOptions {
    ConnectOptions { key: key.map(|k| k.to_vec()), timeout: Duration::from_secs(2), ..ConnectOptions::default() }
}

#[test]
fn hmac_on_both_sides_carries_unary_and_streaming_calls() {
    let port = serve(keyed_runtime());
    let mut conn = ServiceConnection::connect_with("127.0.0.1", port, with_key(Some(KEY))).unwrap();
    assert!(conn.peer_capabilities() & capability::HMAC != 0);
    assert_eq!(conn.call(CALC, ADD, &args(&[40, 2])).unwrap().decode_i32().unwrap(), 42);
    assert_eq!(ints(&mut conn, COUNT_TO, 3).0, vec![1, 2, 3]);
    let (got, failure) = ints(&mut conn, FAIL_AFTER, 1);
    assert_eq!(got, vec![1]);
    assert!(matches!(failure, Some(Error::Service { .. })));
}

#[test]
fn a_wrong_key_is_refused_in_both_directions() {
    let port = serve(keyed_runtime());

    // The client cannot verify the service's init.
    let err = ServiceConnection::connect_with("127.0.0.1", port, with_key(Some(b"not the key"))).err().expect("refused");
    assert!(matches!(err, Error::Security(_)), "{err}");

    // A client with no key reads the tagged init as an init with trailing
    // bytes, but its untagged call is refused by the service, which hangs
    // up without answering.
    let mut conn = ServiceConnection::connect_with("127.0.0.1", port, with_key(None)).unwrap();
    let err = conn.call(CALC, ADD, &args(&[1, 2])).unwrap_err();
    assert!(matches!(err, Error::Closed | Error::Io(_) | Error::Timeout), "{err}");

    // The service still serves a client with the key.
    let mut good = ServiceConnection::connect_with("127.0.0.1", port, with_key(Some(KEY))).unwrap();
    assert_eq!(good.call(CALC, ADD, &args(&[1, 2])).unwrap().decode_i32().unwrap(), 3);
}

#[test]
fn a_keyed_client_refuses_an_unkeyed_service() {
    let port = serve(runtime());
    let err = ServiceConnection::connect_with("127.0.0.1", port, with_key(Some(KEY))).err().expect("refused");
    assert!(matches!(err, Error::Security(_)), "{err}");
}
