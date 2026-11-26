// MIT License
// Copyright (c) 2026 dbjwhs

//! `songc --lang rust` output, compiled and run. The modules under
//! `tests/generated/` are checked in (ctest regenerates them and fails when
//! they are stale). `deny(warnings)` here is the proof that generated code
//! compiles clean; `cargo clippy --all-targets -- -D warnings` lints it.
//!
//! The service tests run the generated proxy against the generated trait
//! and dispatchers, a Rust client to a Rust service over a real socket.
//! The same service against C++ and Python peers is in `interop.rs`.

#![deny(warnings)]

mod common;
#[path = "generated/calculator.rs"]
mod calculator;
#[path = "generated/coverage.rs"]
mod coverage;
#[path = "generated/interop.rs"]
mod interop;
#[path = "common/interop_impl.rs"]
mod interop_impl;

use interop::{Access, InteropProxy, Sample, SecondProxy, Status};
use interop_impl::{interop_runtime, make_batch, make_sample};
use song::error::code;
use song::wire::method_flags;
use song::{Buffer, Error, ServiceConnection};
use std::net::TcpListener;
use std::sync::Arc;
use std::time::Duration;

fn serve(runtime: song::ServiceRuntime) -> u16 {
    let listener = TcpListener::bind(("127.0.0.1", 0)).expect("bind");
    let port = listener.local_addr().expect("addr").port();
    let runtime = Arc::new(runtime);
    std::thread::spawn(move || {
        let _ = runtime.serve_listener(listener);
    });
    port
}

fn connect() -> ServiceConnection {
    ServiceConnection::connect("127.0.0.1", serve(interop_runtime())).expect("connect")
}

const CHUNK: Option<Duration> = Some(Duration::from_secs(5));

// ---- ids ------------------------------------------------------------------------

/// The numbering the C++ header and the Python module have for the same IDL.
#[test]
fn ids_match_the_other_generators() {
    assert_eq!(interop::SERVICE_Interop, 1);
    assert_eq!(interop::SERVICE_Interop_STREAM, 2, "a service with streams takes a second id");
    assert_eq!(interop::SERVICE_Second, 3);
    assert_eq!(interop::METHOD_Interop_add, 1);
    assert_eq!(interop::METHOD_Interop_ping, 6);
    assert_eq!(interop::METHOD_Interop_count_to, 7, "method ids run on across unary and stream methods");
    assert_eq!(interop::METHOD_Interop_fail_after, 9);
    assert_eq!(interop::METHOD_Second_whoami, 1, "and start again in each service");
    assert_eq!(calculator::SERVICE_Calculator, 1);
    assert_eq!(calculator::METHOD_Calculator_flatten_3d, 9);
}

#[test]
fn register_announces_every_method_with_its_flags() {
    let conn = connect();
    let got: Vec<(u16, u16, u16)> = conn.methods().iter().map(|m| (m.service_id, m.method_id, m.flags)).collect();
    let mut want = Vec::new();
    for method in 1..=6 {
        want.push((interop::SERVICE_Interop, method, method_flags::NONE));
    }
    for method in 7..=9 {
        want.push((interop::SERVICE_Interop_STREAM, method, method_flags::STREAMING));
    }
    want.push((interop::SERVICE_Second, 1, method_flags::NONE));
    assert_eq!(got, want);
    assert!(conn.has_capability(song::wire::capability::STREAMING));
}

// ---- codecs ---------------------------------------------------------------------

/// The exact bytes of a struct: field order, an enum as i32, flags as u32,
/// an optional as a bool then the value, arrays as a u32 count then items.
#[test]
fn a_struct_encodes_field_by_field() {
    let sample = Sample {
        id: 7,
        label: "ab".into(),
        value: 1.5,
        status: Status::failed,
        access: Access::read | Access::exec,
        note_id: Some(-2),
        note: None,
        blob: vec![0xAA, 0xBB],
        counts: vec![1, 2],
        tags: vec!["x".into()],
    };
    let mut got = Buffer::new();
    interop::encode_Sample(&mut got, &sample).unwrap();

    let mut want = Buffer::new();
    want.encode_i32(7);
    want.encode_u32(2);
    want.write(b"ab");
    want.encode_f64(1.5);
    want.encode_i32(5);
    want.encode_u32(5);
    want.encode_bool(true);
    want.encode_i64(-2);
    want.encode_bool(false);
    want.encode_u32(2);
    want.write(&[0xAA, 0xBB]);
    want.encode_u32(2);
    want.encode_i32(1);
    want.encode_i32(2);
    want.encode_u32(1);
    want.encode_u32(1);
    want.write(b"x");
    assert_eq!(common::hex(got.data()), common::hex(want.data()));

    assert_eq!(interop::decode_Sample(&mut got).unwrap(), sample);
    assert!(!got.has_data(), "decode reads exactly what encode wrote");
}

#[test]
fn enums_and_flags() {
    assert_eq!(Status::default(), Status::ok);
    assert_eq!((Status::ok as i32, Status::failed as i32, Status::retry as i32), (0, 5, 6), "the IDL's numbering");
    assert_eq!(Status::retry.name(), "retry");
    assert_eq!(Status::from_i32(5), Some(Status::failed));
    assert_eq!(Status::from_i32(1), None);

    let mut buf = Buffer::new();
    buf.encode_i32(99);
    let err = interop::decode_Status(&mut buf).unwrap_err();
    assert!(matches!(&err, Error::Decode(m) if m == "unknown Status value: 99"), "{err}");

    let rw = Access::read | Access::write;
    assert_eq!(rw.bits(), 3);
    assert!(rw.contains(Access::read) && !rw.contains(Access::exec));
    assert_eq!(rw & Access::write, Access::write);
    let mut buf = Buffer::new();
    interop::encode_Access(&mut buf, Access(0xFFFF_FFFF));
    assert_eq!(interop::decode_Access(&mut buf).unwrap(), Access(0xFFFF_FFFF), "any bits are a flags value");

    assert_eq!(
        (coverage::Level::low as i32, coverage::Level::top as i32, coverage::Level::off as i32),
        (1, 3, 10),
        "an item with no value is one more than the item before"
    );
    assert_eq!(coverage::Mode::none.bits(), 0);
}

#[test]
fn every_value_kind_round_trips() {
    use coverage::{Empty, Everything, Level, Mode, NoItems};
    let inner = Everything { l: "inner".into(), nested: vec![Everything::default()], ..Everything::default() };
    let all = Everything {
        a: true,
        b: -8,
        c: -16,
        d: -32,
        e: -64,
        f: 8,
        g: 16,
        h: 32,
        i: 64,
        j: 1.25,
        k: -2.5,
        l: "text \u{00e9}".into(),
        m: vec![0, 255],
        n: Level::high,
        o: Mode::fast | Mode::safe,
        p: NoItems(-3),
        q: Empty {},
        oa: Some(false),
        om: Some(vec![]),
        on: Some(Level::off),
        oo: None,
        oq: Some(Empty {}),
        am: vec![vec![1], vec![], vec![2, 3]],
        an: vec![Level::low, Level::top],
        ao: vec![Mode::none, Mode(7)],
        aq: vec![Empty {}, Empty {}, Empty {}],
        deep: vec![vec!["a".into(), "b".into()], vec![]],
        nested: vec![inner.clone(), inner],
        optional_array: vec![4, 5],
    };
    let mut buf = Buffer::new();
    coverage::encode_Everything(&mut buf, &all).unwrap();
    assert_eq!(coverage::decode_Everything(&mut buf).unwrap(), all);
    assert!(!buf.has_data());

    let mut buf = Buffer::new();
    coverage::encode_Everything(&mut buf, &Everything::default()).unwrap();
    assert_eq!(coverage::decode_Everything(&mut buf).unwrap(), Everything::default());

    // Keyword field names are raw identifiers; the wire does not see names.
    let kw = coverage::Keywords { r#type: 1, r#match: "m".into(), r#fn: true, self_: 2, r#impl: Level::low };
    let mut buf = Buffer::new();
    coverage::encode_Keywords(&mut buf, &kw).unwrap();
    assert_eq!(coverage::decode_Keywords(&mut buf).unwrap(), kw);
}

#[test]
fn a_truncated_struct_is_a_decode_error() {
    let mut whole = Buffer::new();
    interop::encode_Batch(&mut whole, &make_batch()).unwrap();
    for len in 0..whole.size() {
        let mut cut = Buffer::from_slice(&whole.data()[..len]);
        assert!(interop::decode_Batch(&mut cut).is_err(), "cut at {len} of {}", whole.size());
    }
}

// ---- the generated proxy against the generated service -----------------------------

#[test]
fn unary_methods() {
    let mut conn = connect();
    let mut proxy = InteropProxy::new(&mut conn);
    assert_eq!(proxy.add(40, 2).unwrap(), 42);
    proxy.ping().unwrap();

    let sample = make_sample(5, "five");
    assert_eq!(proxy.echo_sample(&sample).unwrap(), sample);
    assert_eq!(proxy.echo_sample(&Sample::default()).unwrap(), Sample::default());
    let batch = make_batch();
    assert_eq!(proxy.echo_batch(&batch).unwrap(), batch);

    assert_eq!(proxy.describe(Status::failed, Access::read | Access::exec, Some("job ")).unwrap(), "job failed:5");
    assert_eq!(proxy.describe(Status::ok, Access::default(), None).unwrap(), "ok:0");

    assert_eq!(SecondProxy::new(&mut conn).whoami().unwrap(), "rust");
}

#[test]
fn a_failing_method_is_a_service_error_and_the_connection_goes_on() {
    let mut conn = connect();
    let mut proxy = InteropProxy::new(&mut conn);
    let err = proxy.fail("no such job").unwrap_err();
    assert!(
        matches!(&err, Error::Service { code: code::UNKNOWN_METHOD, message } if message == "no such job"),
        "{err}"
    );
    assert_eq!(proxy.add(1, 1).unwrap(), 2);

    let err = conn.call(interop::SERVICE_Interop, 99, &Buffer::new()).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message == "Unknown method ID: 99"), "{err}");
}

#[test]
fn stream_methods_yield_decoded_chunks() {
    let mut conn = connect();
    let mut proxy = InteropProxy::new(&mut conn);
    let counted: Vec<i32> = proxy.count_to(5, CHUNK).unwrap().map(|c| c.unwrap()).collect();
    assert_eq!(counted, vec![1, 2, 3, 4, 5]);
    assert_eq!(proxy.count_to(0, CHUNK).unwrap().count(), 0);

    let samples: Vec<Sample> = proxy.samples(6, "s", CHUNK).unwrap().map(|c| c.unwrap()).collect();
    let want: Vec<Sample> = (1..=6).map(|ndx| make_sample(ndx, "s")).collect();
    assert_eq!(samples, want);

    assert_eq!(proxy.add(2, 3).unwrap(), 5, "a stream read to its end leaves the connection usable");
}

#[test]
fn a_stream_that_fails_part_way_ends_with_the_error() {
    let mut conn = connect();
    let mut proxy = InteropProxy::new(&mut conn);
    let items: Vec<song::Result<i32>> = proxy.fail_after(3, "ran dry", CHUNK).unwrap().collect();
    assert_eq!(items.len(), 4, "three chunks, then the error");
    assert_eq!(items[..3].iter().map(|c| *c.as_ref().unwrap()).collect::<Vec<_>>(), vec![1, 2, 3]);
    assert!(
        matches!(&items[3], Err(Error::Service { code: code::UNKNOWN_METHOD, message }) if message == "ran dry"),
        "{:?}",
        items[3]
    );
    assert_eq!(proxy.add(1, 2).unwrap(), 3);

    let err = conn
        .call_streaming(interop::SERVICE_Interop_STREAM, 99, &Buffer::new(), CHUNK)
        .unwrap()
        .next()
        .unwrap()
        .unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message == "Unknown streaming method ID: 99"), "{err}");
}

/// A chunk that is not what the proxy expects is that chunk's decode error.
#[test]
fn a_chunk_that_does_not_decode_is_an_error_item() {
    let mut conn = connect();
    let request = {
        let mut b = Buffer::new();
        b.encode_i32(2);
        b
    };
    // count_to's i32 chunks read as Samples: each is 4 bytes, far too short.
    let stream = conn.call_streaming(interop::SERVICE_Interop_STREAM, interop::METHOD_Interop_count_to, &request, CHUNK);
    let items: Vec<song::Result<Sample>> = stream.unwrap().decoded(interop::decode_Sample).collect();
    assert_eq!(items.len(), 2);
    assert!(items.iter().all(|c| matches!(c, Err(Error::Decode(_)))));
    assert!(conn.is_connected());
}

#[test]
fn the_coverage_services_dispatch() {
    use coverage::{Empty, Keywords, Level, Mixed, MixedProxy, OnlyStreams, OnlyStreamsProxy, Quiet, QuietProxy};
    struct Impl;
    impl OnlyStreams for Impl {
        fn ticks(&self, writer: &mut song::TypedStreamWriter<'_, '_, u64>) -> song::Result<()> {
            writer.write(&u64::MAX)
        }
        fn levels(&self, from: Level, writer: &mut song::TypedStreamWriter<'_, '_, Level>) -> song::Result<()> {
            writer.write(&from)?;
            writer.write(&Level::off)
        }
        fn rows(&self, seed: &[i32], writer: &mut song::TypedStreamWriter<'_, '_, Vec<i32>>) -> song::Result<()> {
            writer.write(&seed.to_vec())?;
            writer.write(&Vec::new())
        }
        fn maybe(
            &self,
            name: Option<&str>,
            writer: &mut song::TypedStreamWriter<'_, '_, Option<String>>,
        ) -> song::Result<()> {
            writer.write(&name.map(str::to_string))?;
            writer.write(&None)
        }
        fn writer_named(
            &self,
            writer: i32,
            song_writer: &mut song::TypedStreamWriter<'_, '_, Empty>,
        ) -> song::Result<()> {
            for _ in 0..writer {
                song_writer.write(&Empty {})?;
            }
            Ok(())
        }
    }
    impl Quiet for Impl {
        fn nop(&self) -> song::Result<()> {
            Ok(())
        }
        fn also_nop(&self) -> song::Result<()> {
            Err(Error::service("not so quiet"))
        }
    }
    impl Mixed for Impl {
        fn all(
            &self,
            scalar: i32,
            text: &str,
            raw: &[u8],
            level: Level,
            mode: coverage::Mode,
            _empty: &Empty,
            every: &coverage::Everything,
            oscalar: Option<i32>,
            otext: Option<&str>,
            oraw: Option<&[u8]>,
            olevel: Option<Level>,
            oempty: Option<&Empty>,
            ascalar: &[i32],
            atext: &[String],
            aempty: &[Empty],
            alevel: &[Level],
            grid: &[Vec<i32>],
        ) -> song::Result<coverage::Everything> {
            let mut out = every.clone();
            out.d = scalar + oscalar.unwrap_or(0) + ascalar.iter().sum::<i32>() + grid.iter().flatten().sum::<i32>();
            out.l = format!("{text}|{}|{}", otext.unwrap_or("-"), atext.join(","));
            out.m = [raw, oraw.unwrap_or(&[])].concat();
            out.n = olevel.unwrap_or(level);
            out.o = mode;
            out.oq = oempty.cloned();
            out.aq = aempty.to_vec();
            out.an = alevel.to_vec();
            Ok(out)
        }
        fn opt_out(&self, r#type: i32) -> song::Result<Option<String>> {
            Ok((r#type > 0).then(|| r#type.to_string()))
        }
        fn arr_out(&self, r#match: &str) -> song::Result<Vec<Empty>> {
            Ok(vec![Empty {}; r#match.len()])
        }
        fn enum_out(&self) -> song::Result<Level> {
            Ok(Level::top)
        }
        fn flags_out(&self) -> song::Result<coverage::Mode> {
            Ok(coverage::Mode::safe)
        }
        fn bytes_out(&self) -> song::Result<Vec<u8>> {
            Ok(vec![1, 2, 3])
        }
        fn watch(&self, self_: &Keywords, writer: &mut song::TypedStreamWriter<'_, '_, Keywords>) -> song::Result<()> {
            writer.write(self_)
        }
    }

    let mut runtime = song::ServiceRuntime::new();
    let service = Arc::new(Impl);
    coverage::register_OnlyStreams(&mut runtime, Arc::clone(&service));
    coverage::register_Quiet(&mut runtime, Arc::clone(&service));
    coverage::register_Mixed(&mut runtime, service);
    assert_eq!(
        (coverage::SERVICE_OnlyStreams, coverage::SERVICE_OnlyStreams_STREAM, coverage::SERVICE_Quiet),
        (1, 2, 3)
    );
    assert_eq!((coverage::SERVICE_Mixed, coverage::SERVICE_Mixed_STREAM), (4, 5));
    let mut conn = ServiceConnection::connect("127.0.0.1", serve(runtime)).unwrap();

    let mut streams = OnlyStreamsProxy::new(&mut conn);
    assert_eq!(streams.ticks(CHUNK).unwrap().map(|c| c.unwrap()).collect::<Vec<_>>(), vec![u64::MAX]);
    let levels: Vec<Level> = streams.levels(Level::high, CHUNK).unwrap().map(|c| c.unwrap()).collect();
    assert_eq!(levels, vec![Level::high, Level::off]);
    let rows: Vec<Vec<i32>> = streams.rows(&[3, 4], CHUNK).unwrap().map(|c| c.unwrap()).collect();
    assert_eq!(rows, vec![vec![3, 4], vec![]]);
    let maybe: Vec<Option<String>> = streams.maybe(Some("x"), CHUNK).unwrap().map(|c| c.unwrap()).collect();
    assert_eq!(maybe, vec![Some("x".to_string()), None]);
    assert_eq!(streams.writer_named(3, CHUNK).unwrap().count(), 3, "an empty struct is an empty chunk");
    // The unary id of a service with only streams answers "unknown method".
    let err = conn.call(coverage::SERVICE_OnlyStreams, 1, &Buffer::new()).unwrap_err();
    assert!(matches!(&err, Error::Service { message, .. } if message == "Unknown method ID: 1"), "{err}");

    let mut quiet = QuietProxy::new(&mut conn);
    quiet.nop().unwrap();
    assert!(matches!(quiet.also_nop(), Err(Error::Service { .. })));

    let mut mixed = MixedProxy::new(&mut conn);
    let every = coverage::Everything { k: 9.5, ..Default::default() };
    let got = mixed
        .all(
            1,
            "t",
            &[1],
            Level::low,
            coverage::Mode::fast,
            &Empty {},
            &every,
            Some(10),
            Some("o"),
            Some(&[2]),
            None,
            Some(&Empty {}),
            &[100, 200],
            &["a".to_string(), "b".to_string()],
            &[Empty {}],
            &[Level::top],
            &[vec![1000], vec![2000, 3000]],
        )
        .unwrap();
    assert_eq!(got.d, 1 + 10 + 300 + 6000);
    assert_eq!(got.l, "t|o|a,b");
    assert_eq!(got.m, vec![1, 2]);
    assert_eq!((got.n, got.o, got.k), (Level::low, coverage::Mode::fast, 9.5));
    assert_eq!((got.oq, got.aq.len(), got.an), (Some(Empty {}), 1, vec![Level::top]));
    assert_eq!(mixed.opt_out(4).unwrap(), Some("4".to_string()));
    assert_eq!(mixed.opt_out(0).unwrap(), None);
    assert_eq!(mixed.arr_out("abc").unwrap().len(), 3);
    assert_eq!(mixed.enum_out().unwrap(), Level::top);
    assert_eq!(mixed.flags_out().unwrap(), coverage::Mode::safe);
    assert_eq!(mixed.bytes_out().unwrap(), vec![1, 2, 3]);
    let kw = Keywords { r#type: 3, ..Default::default() };
    assert_eq!(mixed.watch(&kw, CHUNK).unwrap().map(|c| c.unwrap()).collect::<Vec<_>>(), vec![kw]);
}
