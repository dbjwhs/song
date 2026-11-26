// MIT License
// Copyright (c) 2026 dbjwhs

//! The client against a scripted peer that speaks the wire protocol over a
//! real TCP socket: each test states the exact frames the peer sends. No
//! built C++ service is needed (those tests are in `interop.rs`).

mod common;

use common::{i32_payload, init_frame, init_v11, send, seq_of, ScriptedServer};
use song::security::tag_message;
use song::wire::{self, MethodDescriptor, MsgType};
use song::{Buffer, ConnectOptions, Error, ServiceConnection};
use std::sync::mpsc;
use std::time::{Duration, Instant};

fn connect(port: u16) -> ServiceConnection {
    ServiceConnection::connect("127.0.0.1", port).expect("connect")
}

fn options(timeout_ms: u64) -> ConnectOptions {
    ConnectOptions { timeout: Duration::from_millis(timeout_ms), ..ConnectOptions::default() }
}

fn decode_i32(mut chunk: Buffer) -> i32 {
    chunk.decode_i32().expect("i32 chunk")
}

#[test]
fn handshake_records_the_service_and_acknowledges_a_v1_1_peer() {
    let methods = [
        MethodDescriptor { service_id: 1, method_id: 1, flags: wire::method_flags::NONE, reserved: 0 },
        MethodDescriptor { service_id: 2, method_id: 1, flags: wire::method_flags::STREAMING, reserved: 0 },
    ];
    let caps = wire::capability::STREAMING | wire::capability::OBJECTS | wire::capability::EXT_0;
    let server = ScriptedServer::start(init_frame(wire::FIRST_VERSION, wire::CURRENT_VERSION, caps, &methods), |_, _, _| {});
    let conn = connect(server.port);
    assert_eq!(conn.negotiated_version(), wire::CURRENT_VERSION);
    assert_eq!(conn.peer_capabilities(), caps);
    assert_eq!(conn.negotiated_capabilities(), wire::capability::STREAMING, "the AND of both sides");
    assert!(conn.has_capability(wire::capability::STREAMING));
    assert!(!conn.has_capability(wire::capability::OBJECTS));
    assert_eq!(conn.methods(), &methods);
    drop(conn);

    let received = server.received();
    let want = wire::create_init_ack_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, wire::capability::STREAMING)
        .unwrap()
        .into_vec();
    assert_eq!(received, vec![want], "exactly one message: the init_ack");
}

#[test]
fn no_init_ack_goes_to_a_v1_0_peer() {
    let greeting = init_frame(wire::FIRST_VERSION, wire::make_version(1, 0), 0, &[]);
    let server = ScriptedServer::start(greeting, |_, _, _| {});
    let conn = connect(server.port);
    assert_eq!(conn.negotiated_version(), wire::make_version(1, 0));
    drop(conn);
    assert!(server.received().is_empty());
}

#[test]
fn versions_that_cannot_talk_are_refused() {
    let too_new = init_frame(wire::make_version(2, 0), wire::make_version(2, 0), 0, &[]);
    let server = ScriptedServer::start(too_new, |_, _, _| {});
    let err = ServiceConnection::connect("127.0.0.1", server.port).err().expect("refused");
    assert!(matches!(&err, Error::VersionMismatch(m) if m.contains("client version too old")), "{err}");

    let too_old = init_frame(wire::make_version(0, 9), wire::make_version(0, 9), 0, &[]);
    let server = ScriptedServer::start(too_old, |_, _, _| {});
    let err = ServiceConnection::connect("127.0.0.1", server.port).err().expect("refused");
    assert!(matches!(&err, Error::VersionMismatch(m) if m.contains("service version too old")), "{err}");
}

#[test]
fn a_greeting_that_is_not_a_sound_init_is_refused() {
    let not_init = wire::create_result_message(0, &[]).unwrap().into_vec();
    let server = ScriptedServer::start(not_init, |_, _, _| {});
    assert!(matches!(ServiceConnection::connect("127.0.0.1", server.port), Err(Error::Protocol(_))));

    // A method count the payload cannot hold is refused, not allocated.
    let mut payload = Buffer::new();
    wire::encode_init(
        &mut payload,
        &wire::InitMessage {
            magic: wire::MAGIC,
            first_version: wire::FIRST_VERSION,
            current_version: wire::CURRENT_VERSION,
            capabilities: 0,
            method_count: u32::MAX,
        },
    );
    let mut forged = Buffer::new();
    wire::encode_header(&mut forged, &wire::Header::new(MsgType::Init, 0, payload.size() as u32));
    forged.write(payload.data());
    let server = ScriptedServer::start(forged.into_vec(), |_, _, _| {});
    let err = ServiceConnection::connect("127.0.0.1", server.port).err().expect("refused");
    assert!(matches!(&err, Error::Protocol(m) if m.contains("method count")), "{err}");

    // A peer that says nothing: the wait is the connection timeout.
    let server = ScriptedServer::start(Vec::new(), |_, _, _| {});
    let started = Instant::now();
    let err = ServiceConnection::connect_with("127.0.0.1", server.port, options(150)).err().expect("timed out");
    assert!(matches!(err, Error::Timeout), "{err}");
    assert!(started.elapsed() < Duration::from_secs(3));
}

#[test]
fn unary_call_sends_the_call_and_returns_the_result() {
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        send(stream, wire::create_result_message(seq_of(raw), &i32_payload(14)).unwrap().data());
    });
    let mut conn = connect(server.port);
    let mut request = Buffer::new();
    request.encode_i32(5);
    request.encode_i32(9);
    let mut reply = conn.call(1, 2, &request).unwrap();
    assert_eq!(reply.decode_i32().unwrap(), 14);
    assert!(conn.is_connected());
    drop(conn);

    let received = server.received();
    let want = wire::create_call_message(1, 1, 2, request.data()).unwrap().into_vec();
    assert_eq!(received[1], want, "the call on the wire, sequence id 1");
}

#[test]
fn an_error_reply_is_a_service_error_and_the_connection_goes_on() {
    let server = ScriptedServer::start(init_v11(), |ndx, raw, stream| {
        let frame = if ndx == 0 {
            wire::create_error_message(seq_of(raw), 3, "Division by zero").unwrap()
        } else {
            wire::create_result_message(seq_of(raw), &i32_payload(1)).unwrap()
        };
        send(stream, frame.data());
    });
    let mut conn = connect(server.port);
    let err = conn.call(1, 4, &Buffer::new()).unwrap_err();
    assert!(matches!(&err, Error::Service { code: 3, message } if message == "Division by zero"), "{err}");
    assert!(conn.is_connected());
    assert_eq!(conn.call(1, 1, &Buffer::new()).unwrap().decode_i32().unwrap(), 1);
}

#[test]
fn a_reply_out_of_step_closes_the_connection() {
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        send(stream, wire::create_result_message(seq_of(raw) + 5, &[]).unwrap().data());
    });
    let mut conn = connect(server.port);
    let err = conn.call(1, 1, &Buffer::new()).unwrap_err();
    assert!(matches!(&err, Error::Protocol(m) if m.contains("sequence id mismatch")), "{err}");
    assert!(!conn.is_connected());
    assert!(matches!(conn.call(1, 1, &Buffer::new()), Err(Error::Closed)));

    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        send(stream, wire::create_stream_end_message(seq_of(raw)).unwrap().data());
    });
    let mut conn = connect(server.port);
    assert!(matches!(conn.call(1, 1, &Buffer::new()), Err(Error::Protocol(_))), "a stream_end is no unary reply");
}

#[test]
fn a_unary_call_times_out_on_the_connection_timeout() {
    let server = ScriptedServer::start(init_v11(), |_, _, _| {});
    let mut conn = ServiceConnection::connect_with("127.0.0.1", server.port, options(150)).unwrap();
    assert_eq!(conn.timeout(), Duration::from_millis(150));
    let started = Instant::now();
    assert!(matches!(conn.call(1, 1, &Buffer::new()), Err(Error::Timeout)));
    assert!(started.elapsed() < Duration::from_secs(3));
    assert!(!conn.is_connected(), "a late reply would be read as the next call's");
}

/// The proof that streaming is incremental: the peer sends chunk 2 only
/// after the test has been handed chunk 1.
#[test]
fn streaming_yields_each_chunk_as_it_arrives() {
    let (seen_first, wait_first) = mpsc::channel::<()>();
    let server = ScriptedServer::start(init_v11(), move |_, raw, stream| {
        let seq = seq_of(raw);
        send(stream, wire::create_stream_message(seq, &i32_payload(1)).unwrap().data());
        wait_first.recv_timeout(Duration::from_secs(5)).expect("the client read chunk 1 before chunk 2 was sent");
        send(stream, wire::create_stream_message(seq, &i32_payload(2)).unwrap().data());
        send(stream, wire::create_stream_end_message(seq).unwrap().data());
    });
    let mut conn = connect(server.port);
    let mut stream = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap();
    assert_eq!(decode_i32(stream.next().unwrap().unwrap()), 1);
    seen_first.send(()).unwrap();
    assert_eq!(decode_i32(stream.next().unwrap().unwrap()), 2);
    assert!(stream.next().is_none());
    assert!(stream.next().is_none(), "an ended stream stays ended");
    drop(stream);
    assert!(conn.is_connected(), "a stream read to its end leaves the connection usable");
}

#[test]
fn an_empty_stream_and_a_following_call_share_the_connection() {
    let server = ScriptedServer::start(init_v11(), |ndx, raw, stream| {
        let frame = if ndx == 0 {
            wire::create_stream_end_message(seq_of(raw)).unwrap()
        } else {
            wire::create_result_message(seq_of(raw), &i32_payload(7)).unwrap()
        };
        send(stream, frame.data());
    });
    let mut conn = connect(server.port);
    assert_eq!(conn.call_streaming(2, 1, &Buffer::new(), None).unwrap().count(), 0);
    assert_eq!(conn.call(1, 1, &Buffer::new()).unwrap().decode_i32().unwrap(), 7);
}

#[test]
fn a_stream_that_errors_part_way_yields_its_chunks_then_the_error() {
    let server = ScriptedServer::start(init_v11(), |ndx, raw, stream| {
        let seq = seq_of(raw);
        if ndx == 0 {
            send(stream, wire::create_stream_message(seq, &i32_payload(10)).unwrap().data());
            send(stream, wire::create_stream_message(seq, &i32_payload(20)).unwrap().data());
            send(stream, wire::create_error_message(seq, 3, "disk went away").unwrap().data());
        } else {
            send(stream, wire::create_result_message(seq, &i32_payload(3)).unwrap().data());
        }
    });
    let mut conn = connect(server.port);
    let mut got = Vec::new();
    let mut failure = None;
    for item in conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap() {
        match item {
            Ok(chunk) => got.push(decode_i32(chunk)),
            Err(e) => failure = Some(e),
        }
    }
    assert_eq!(got, vec![10, 20], "the chunks before the error are genuine");
    let failure = failure.expect("the stream ended with the error, not cleanly");
    assert!(matches!(&failure, Error::Service { code: 3, message } if message == "disk went away"), "{failure}");
    assert!(conn.is_connected(), "an error message ends the stream at a message boundary");
    assert_eq!(conn.call(1, 1, &Buffer::new()).unwrap().decode_i32().unwrap(), 3);
}

/// The per-chunk timeout is the caller's: a short one gives up on a silent
/// peer quickly, a long one waits out the same silence.
#[test]
fn the_chunk_timeout_is_the_one_passed_in() {
    let slow = |_: usize, raw: &[u8], stream: &mut std::net::TcpStream| {
        let seq = seq_of(raw);
        std::thread::sleep(Duration::from_millis(1500));
        send(stream, wire::create_stream_message(seq, &i32_payload(1)).unwrap().data());
        send(stream, wire::create_stream_end_message(seq).unwrap().data());
    };

    // A connection timeout far longer than the chunk timeout: it is not
    // what bounds the wait.
    let server = ScriptedServer::start(init_v11(), slow);
    let mut conn = ServiceConnection::connect_with("127.0.0.1", server.port, options(30_000)).unwrap();
    let started = Instant::now();
    let mut stream = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_millis(100))).unwrap();
    assert!(matches!(stream.next(), Some(Err(Error::Timeout))));
    assert!(started.elapsed() < Duration::from_millis(1400), "gave up before the peer spoke: {:?}", started.elapsed());
    assert!(stream.next().is_none());
    drop(stream);
    assert!(!conn.is_connected(), "a timed-out stream cannot be resumed");

    // A connection timeout far shorter than the chunk timeout: it does not
    // cut the stream short either.
    let server = ScriptedServer::start(init_v11(), slow);
    let mut conn = ServiceConnection::connect_with("127.0.0.1", server.port, options(50)).unwrap();
    let chunks: Vec<i32> = conn
        .call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(10)))
        .unwrap()
        .map(|c| decode_i32(c.unwrap()))
        .collect();
    assert_eq!(chunks, vec![1]);
}

#[test]
fn an_abandoned_stream_closes_the_connection() {
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        let seq = seq_of(raw);
        for value in 0..3 {
            send(stream, wire::create_stream_message(seq, &i32_payload(value)).unwrap().data());
        }
        send(stream, wire::create_stream_end_message(seq).unwrap().data());
    });
    let mut conn = connect(server.port);
    {
        let mut stream = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap();
        assert_eq!(decode_i32(stream.next().unwrap().unwrap()), 0);
    }
    assert!(!conn.is_connected(), "unread chunks are still on the socket");
    assert!(matches!(conn.call_streaming(2, 1, &Buffer::new(), None).err(), Some(Error::Closed)));
}

#[test]
fn a_stream_cut_off_or_out_of_step_is_an_error_not_a_clean_end() {
    // The peer hangs up after one chunk, with no stream_end.
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        send(stream, wire::create_stream_message(seq_of(raw), &i32_payload(1)).unwrap().data());
        let _ = stream.shutdown(std::net::Shutdown::Both);
    });
    let mut conn = connect(server.port);
    let items: Vec<_> = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap().collect();
    assert_eq!(items.len(), 2);
    assert!(items[0].is_ok());
    assert!(matches!(items[1], Err(Error::Closed)), "{:?}", items[1]);

    // A chunk for another call.
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        send(stream, wire::create_stream_message(seq_of(raw) + 1, &i32_payload(1)).unwrap().data());
    });
    let mut conn = connect(server.port);
    let first = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap().next();
    assert!(matches!(first, Some(Err(Error::Protocol(_)))));
    assert!(!conn.is_connected());

    // A unary result where a chunk belongs.
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        send(stream, wire::create_result_message(seq_of(raw), &[]).unwrap().data());
    });
    let mut conn = connect(server.port);
    let first = conn.call_streaming(2, 1, &Buffer::new(), Some(Duration::from_secs(5))).unwrap().next();
    assert!(matches!(first, Some(Err(Error::Protocol(_)))));
}

#[test]
fn property_notifications_between_replies_are_skipped() {
    let server = ScriptedServer::start(init_v11(), |_, raw, stream| {
        let mut notify = Buffer::new();
        wire::encode_header(&mut notify, &wire::Header::new(MsgType::PropNotify, 0, 4));
        notify.write(&[1, 2, 3, 4]);
        send(stream, notify.data());
        send(stream, wire::create_result_message(seq_of(raw), &i32_payload(5)).unwrap().data());
    });
    let mut conn = connect(server.port);
    assert_eq!(conn.call(1, 1, &Buffer::new()).unwrap().decode_i32().unwrap(), 5);
}

// ---- HMAC, against exact frames ---------------------------------------------

const KEY: &[u8] = b"song-rust-test-secret-32-bytes!!";

fn keyed(key: &[u8]) -> ConnectOptions {
    ConnectOptions { key: Some(key.to_vec()), timeout: Duration::from_secs(2), ..ConnectOptions::default() }
}

#[test]
fn a_keyed_client_tags_what_it_sends_and_verifies_what_it_receives() {
    let server = ScriptedServer::start(tag_message(KEY, &init_v11()).unwrap(), |_, raw, stream| {
        // raw is tagged: the sequence id is still at its offset.
        let reply = wire::create_result_message(seq_of(raw), &i32_payload(42)).unwrap();
        send(stream, &tag_message(KEY, reply.data()).unwrap());
    });
    let mut conn = ServiceConnection::connect_with("127.0.0.1", server.port, keyed(KEY)).unwrap();
    let mut request = Buffer::new();
    request.encode_i32(40);
    assert_eq!(conn.call(1, 1, &request).unwrap().decode_i32().unwrap(), 42);
    drop(conn);

    let received = server.received();
    let ack = wire::create_init_ack_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, wire::capability::STREAMING).unwrap();
    let call = wire::create_call_message(1, 1, 1, request.data()).unwrap();
    assert_eq!(received[0], tag_message(KEY, ack.data()).unwrap(), "the init_ack is tagged too");
    assert_eq!(received[1], tag_message(KEY, call.data()).unwrap());
}

#[test]
fn a_peer_with_another_key_or_none_is_refused() {
    // The service tags with a different key.
    let server = ScriptedServer::start(tag_message(b"some other key", &init_v11()).unwrap(), |_, _, _| {});
    let err = ServiceConnection::connect_with("127.0.0.1", server.port, keyed(KEY)).err().expect("refused");
    assert!(matches!(&err, Error::Security(m) if m.contains("verification failed")), "{err}");

    // The service does not tag at all: its init is too short to hold a tag,
    // or its last 8 bytes are not one.
    let server = ScriptedServer::start(init_v11(), |_, _, _| {});
    assert!(matches!(ServiceConnection::connect_with("127.0.0.1", server.port, keyed(KEY)), Err(Error::Security(_))));

    // A reply changed on the way: one payload byte flipped under a good tag.
    let server = ScriptedServer::start(tag_message(KEY, &init_v11()).unwrap(), |_, raw, stream| {
        let reply = wire::create_result_message(seq_of(raw), &i32_payload(42)).unwrap();
        let mut tagged = tag_message(KEY, reply.data()).unwrap();
        tagged[wire::HEADER_SIZE] ^= 0x01;
        send(stream, &tagged);
    });
    let mut conn = ServiceConnection::connect_with("127.0.0.1", server.port, keyed(KEY)).unwrap();
    assert!(matches!(conn.call(1, 1, &Buffer::new()), Err(Error::Security(_))));
    assert!(!conn.is_connected());
}
