// MIT License
// Copyright (c) 2026 dbjwhs

//! Rust client against services run by the C++ runtime: the interop service
//! (sing/network/rust_interop) and the TCP calculator
//! (sing/network/tcp_calculator), with and without HMAC.
//!
//! The other directions are tested where the client lives: a C++ client to
//! the Rust service in sing/network/rust_interop/interop_test.cpp, a Python
//! client to the Rust service in python/tests/test_rust_interop.py.
//!
//! Each test starts its own service process from the C++ build tree
//! ($SONG_BUILD_DIR, else build/) and skips, with a line on stderr, when
//! the binary is not built.

#![deny(warnings)]

mod common;
#[path = "generated/calculator.rs"]
mod calculator;
#[path = "generated/interop.rs"]
mod interop;
#[path = "common/interop_impl.rs"]
#[allow(dead_code)]
mod interop_impl;

use calculator::CalculatorProxy;
use common::PeerService;
use interop::{Access, InteropProxy, Sample, SecondProxy, Status};
use interop_impl::{make_batch, make_sample};
use song::error::code;
use song::wire::{capability, method_flags};
use song::{Buffer, ConnectOptions, Error, ServiceConnection};
use std::time::Duration;

const KEY: &str = "song-interop-test-secret-32bytes";
const CHUNK: Option<Duration> = Some(Duration::from_secs(5));

fn cpp_interop(key: Option<&str>) -> Option<PeerService> {
    let binary = common::build_dir().join("sing/network/rust_interop/sing_network_rust_interop_service");
    PeerService::start(&binary, key)
}

fn cpp_calculator(key: Option<&str>) -> Option<PeerService> {
    let binary = common::build_dir().join("sing/network/tcp_calculator/sing_network_tcp_calculator_service");
    PeerService::start(&binary, key)
}

fn options(key: Option<&str>) -> ConnectOptions {
    ConnectOptions {
        key: key.map(|k| k.as_bytes().to_vec()),
        timeout: Duration::from_secs(3),
        ..ConnectOptions::default()
    }
}

fn connect(service: &PeerService, key: Option<&str>) -> song::Result<ServiceConnection> {
    ServiceConnection::connect_with("127.0.0.1", service.port, options(key))
}

/// The calls every transport must carry: each value kind, both stream
/// outcomes, and a failing unary call. The same list as `exercise` in
/// interop_test.cpp.
fn exercise(conn: &mut ServiceConnection) {
    assert_eq!(SecondProxy::new(conn).whoami("rust").unwrap(), "cpp serving rust", "the peer is the C++ one");
    let mut proxy = InteropProxy::new(conn);
    assert_eq!(proxy.add(40, 2).unwrap(), 42);
    proxy.ping().unwrap();

    let sample = make_sample(5, "five");
    assert_eq!(proxy.echo_sample(&sample).unwrap(), sample);
    assert_eq!(proxy.echo_sample(&Sample::default()).unwrap(), Sample::default());
    let batch = make_batch();
    assert_eq!(proxy.echo_batch(&batch).unwrap(), batch);

    assert_eq!(proxy.describe(Status::failed, Access::read | Access::exec, "job ").unwrap(), "job failed:5");
    assert_eq!(proxy.describe(Status::ok, Access::default(), "").unwrap(), "ok:0");

    // Optional parameters and an optional return, present and absent.
    assert_eq!(proxy.bump(Some(5), Some("abc")).unwrap(), Some(8));
    assert_eq!(proxy.bump(Some(-2), None).unwrap(), Some(-2));
    assert_eq!(proxy.bump(None, Some("ignored")).unwrap(), None);
    assert_eq!(proxy.bump(Some(i64::MAX - 1), Some("x")).unwrap(), Some(i64::MAX));

    // A failing unary call is a service error carrying the message, and the
    // connection goes on.
    let err = proxy.fail("no such job").unwrap_err();
    assert!(
        matches!(&err, Error::Service { code: code::UNKNOWN_METHOD, message } if message == "no such job"),
        "{err}"
    );
    assert_eq!(proxy.add(1, 1).unwrap(), 2);

    // Streams: scalars and structs, the values the C++ service computes.
    let counted: Vec<i32> = proxy.count_to(5, CHUNK).unwrap().map(|c| c.unwrap()).collect();
    assert_eq!(counted, vec![1, 2, 3, 4, 5]);
    assert_eq!(proxy.count_to(0, CHUNK).unwrap().count(), 0);
    let samples: Vec<Sample> = proxy.samples(6, "s", CHUNK).unwrap().map(|c| c.unwrap()).collect();
    assert_eq!(samples, (1..=6).map(|ndx| make_sample(ndx, "s")).collect::<Vec<_>>());

    // A stream that fails part way: the chunks written first arrive, then
    // the error ends the stream. A clean end here would be the bug.
    let items: Vec<song::Result<i32>> = proxy.fail_after(3, "ran dry", CHUNK).unwrap().collect();
    assert_eq!(items.len(), 4, "three chunks, then the error: {items:?}");
    assert_eq!(items[..3].iter().map(|c| *c.as_ref().unwrap()).collect::<Vec<_>>(), vec![1, 2, 3]);
    assert!(
        matches!(&items[3], Err(Error::Service { code: code::UNKNOWN_METHOD, message }) if message == "ran dry"),
        "{:?}",
        items[3]
    );
    assert_eq!(proxy.add(2, 3).unwrap(), 5, "the connection is usable after a failed stream");
}

// ---- the interop service in C++ -----------------------------------------------------

#[test]
fn handshake_with_a_cpp_service() {
    let Some(service) = cpp_interop(None) else { return };
    let conn = connect(&service, None).unwrap();
    assert_eq!(conn.negotiated_version(), song::wire::CURRENT_VERSION);
    assert!(conn.has_capability(capability::STREAMING));

    // The method list the C++ runtime announces is the one the Rust
    // generator numbers: same ids, the streaming flag on the stream methods.
    let got: Vec<(u16, u16, u16)> = conn.methods().iter().map(|m| (m.service_id, m.method_id, m.flags)).collect();
    let mut want = Vec::new();
    for method in interop::METHOD_Interop_add..=interop::METHOD_Interop_ping {
        want.push((interop::SERVICE_Interop, method, method_flags::NONE));
    }
    // The C++ service registers its unary methods first, in its own order.
    want.push((interop::SERVICE_Interop, interop::METHOD_Interop_bump, method_flags::NONE));
    for method in interop::METHOD_Interop_count_to..=interop::METHOD_Interop_fail_after {
        want.push((interop::SERVICE_Interop_STREAM, method, method_flags::STREAMING));
    }
    want.push((interop::SERVICE_Second, interop::METHOD_Second_whoami, method_flags::NONE));
    assert_eq!(got, want);
}

#[test]
fn unary_and_streaming_calls_to_a_cpp_service() {
    let Some(service) = cpp_interop(None) else { return };
    exercise(&mut connect(&service, None).unwrap());
}

#[test]
fn unknown_service_and_method_on_a_cpp_service() {
    let Some(service) = cpp_interop(None) else { return };
    let mut conn = connect(&service, None).unwrap();
    let err = conn.call(77, 1, &Buffer::new()).unwrap_err();
    assert!(
        matches!(&err, Error::Service { code: code::UNKNOWN_SERVICE, message } if message == "Unknown service ID"),
        "{err}"
    );
    let err = conn.call(interop::SERVICE_Interop, 99, &Buffer::new()).unwrap_err();
    assert!(
        matches!(&err, Error::Service { code: code::UNKNOWN_METHOD, message } if message == "Unknown method ID: 99"),
        "{err}"
    );
    assert_eq!(InteropProxy::new(&mut conn).add(1, 2).unwrap(), 3);
}

/// An array count far beyond the bytes sent: the generated C++ decoder
/// refuses it as too large, at once, and the service carries on.
#[test]
fn a_forged_array_count_is_refused_by_a_cpp_service() {
    let Some(service) = cpp_interop(None) else { return };
    let mut conn = connect(&service, None).unwrap();
    // echo_batch(Batch b): a Batch starts with the count of its samples.
    let mut request = Buffer::new();
    request.encode_u32(u32::MAX);
    let started = std::time::Instant::now();
    let err = conn.call(interop::SERVICE_Interop, interop::METHOD_Interop_echo_batch, &request).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message.contains("Array too large")), "{err}");
    assert!(started.elapsed() < Duration::from_millis(250), "{:?}", started.elapsed());
    assert_eq!(InteropProxy::new(&mut conn).add(1, 2).unwrap(), 3);
}

/// The chunk timeout is the caller's against a real service too: a stream
/// of many chunks is read with a timeout far shorter than the whole stream
/// takes, because the timeout is per chunk.
#[test]
fn a_long_stream_from_a_cpp_service() {
    let Some(service) = cpp_interop(None) else { return };
    let mut conn = connect(&service, None).unwrap();
    let mut proxy = InteropProxy::new(&mut conn);
    let mut next = 1;
    for sample in proxy.samples(300, "long", Some(Duration::from_secs(2))).unwrap() {
        assert_eq!(sample.unwrap(), make_sample(next, "long"));
        next += 1;
    }
    assert_eq!(next, 301);
}

// ---- HMAC -----------------------------------------------------------------------

#[test]
fn hmac_carries_unary_and_streaming_calls_to_a_cpp_service() {
    let Some(service) = cpp_interop(Some(KEY)) else { return };
    exercise(&mut connect(&service, Some(KEY)).unwrap());
}

#[test]
fn a_wrong_key_is_refused_by_a_cpp_service() {
    let Some(service) = cpp_interop(Some(KEY)) else { return };

    // The client cannot verify the service's init message.
    let err = connect(&service, Some("some-other-secret-of-32-bytes!!!")).err().expect("refused");
    assert!(matches!(&err, Error::Security(m) if m.contains("verification failed")), "{err}");

    // A client with no key: the tagged init reads as an init with trailing
    // bytes, but the service refuses the untagged call and hangs up.
    let mut keyless = connect(&service, None).unwrap();
    let err = InteropProxy::new(&mut keyless).add(1, 2).unwrap_err();
    assert!(matches!(err, Error::Closed | Error::Io(_) | Error::Timeout), "{err}");

    // The service still serves a client that has the key.
    let mut good = connect(&service, Some(KEY)).unwrap();
    assert_eq!(InteropProxy::new(&mut good).add(1, 2).unwrap(), 3);
}

#[test]
fn a_keyed_client_refuses_an_unkeyed_cpp_service() {
    let Some(service) = cpp_interop(None) else { return };
    let err = connect(&service, Some(KEY)).err().expect("refused");
    assert!(matches!(err, Error::Security(_)), "{err}");
}

// ---- the TCP calculator, an existing example service -------------------------------

#[test]
fn the_cpp_tcp_calculator() {
    let Some(service) = cpp_calculator(None) else { return };
    let mut conn = connect(&service, None).unwrap();
    let mut calc = CalculatorProxy::new(&mut conn);
    assert_eq!(calc.add(5, 3).unwrap(), 8);
    assert_eq!(calc.subtract(10, 4).unwrap(), 6);
    assert_eq!(calc.multiply(6, 7).unwrap(), 42);
    let result = calc.divide(17, 5).unwrap();
    assert_eq!((result.quotient, result.remainder), (3, 2));
    assert_eq!(calc.factorial(20).unwrap(), 2_432_902_008_176_640_000);
    assert_eq!(calc.sum(&[1, 2, 3, 4]).unwrap(), 10);
    assert_eq!(calc.sum(&[]).unwrap(), 0);

    let err = calc.divide(1, 0).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message.contains("zero")), "{err}");
    assert_eq!(calc.add(1, 1).unwrap(), 2);
}

#[test]
fn the_cpp_tcp_calculator_with_hmac() {
    let Some(service) = cpp_calculator(Some(KEY)) else { return };
    {
        let mut conn = connect(&service, Some(KEY)).unwrap();
        let mut calc = CalculatorProxy::new(&mut conn);
        assert_eq!(calc.add(5, 3).unwrap(), 8);
        assert_eq!(calc.sum(&[1, 2, 3, 4]).unwrap(), 10);
        assert_eq!(calc.divide(17, 5).unwrap().remainder, 2);
    }

    // This service takes one client at a time (run_tcp): the first
    // connection is closed before the next is made.
    let err = connect(&service, Some("yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy")).err().expect("refused");
    assert!(matches!(err, Error::Security(_)), "{err}");
}
