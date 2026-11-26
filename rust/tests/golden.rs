// MIT License
// Copyright (c) 2026 dbjwhs

//! The Rust encoders against the byte vectors the C++ and Python tests
//! check too (`test/golden/wire_vectors.txt`), and the decoders back again.

mod common;

use common::{golden_vectors, hex};
use song::wire::{self, MethodDescriptor};
use song::{Buffer, Result};
use std::collections::BTreeMap;

fn buf(build: impl FnOnce(&mut Buffer) -> Result<()>) -> Vec<u8> {
    let mut b = Buffer::new();
    build(&mut b).expect("encode");
    b.into_vec()
}

/// Every vector this runtime builds itself, by name.
fn built() -> BTreeMap<&'static str, Vec<u8>> {
    let mut m = BTreeMap::new();
    m.insert("bool_true", buf(|b| { b.encode_bool(true); Ok(()) }));
    m.insert("bool_false", buf(|b| { b.encode_bool(false); Ok(()) }));
    m.insert("i8_minus_2", buf(|b| { b.encode_i8(-2); Ok(()) }));
    m.insert("i16_minus_2", buf(|b| { b.encode_i16(-2); Ok(()) }));
    m.insert("i32_0x12345678", buf(|b| { b.encode_i32(0x1234_5678); Ok(()) }));
    m.insert("i64_minus_2", buf(|b| { b.encode_i64(-2); Ok(()) }));
    m.insert("u8_0xab", buf(|b| { b.encode_u8(0xAB); Ok(()) }));
    m.insert("u16_0xbeef", buf(|b| { b.encode_u16(0xBEEF); Ok(()) }));
    m.insert("u32_0xdeadbeef", buf(|b| { b.encode_u32(0xDEAD_BEEF); Ok(()) }));
    m.insert("u64_0x0102030405060708", buf(|b| { b.encode_u64(0x0102_0304_0506_0708); Ok(()) }));
    m.insert("f32_1_5", buf(|b| { b.encode_f32(1.5); Ok(()) }));
    m.insert("f64_minus_2_25", buf(|b| { b.encode_f64(-2.25); Ok(()) }));
    m.insert("string_hello", buf(|b| b.encode_string("hello, song")));
    m.insert("string_empty", buf(|b| b.encode_string("")));
    m.insert("string_utf8", buf(|b| b.encode_string("h\u{e9}llo \u{266b}")));
    m.insert("bytes_00_ff_10", buf(|b| b.encode_bytes(&[0x00, 0xFF, 0x10])));
    m.insert("array_i32", buf(|b| b.encode_array(&[1i32, -1, 65536], |b, v| { b.encode_i32(*v); Ok(()) })));
    m.insert(
        "array_string",
        buf(|b| b.encode_array(&["a".to_string(), "bc".to_string()], |b, v| b.encode_string(v))),
    );
    m.insert(
        "array_nested_i32",
        buf(|b| {
            b.encode_array(&[vec![1i32, 2], vec![], vec![3]], |b, inner| {
                b.encode_array(inner, |b, v| { b.encode_i32(*v); Ok(()) })
            })
        }),
    );
    m.insert("optional_i32_some_7", buf(|b| b.encode_optional(Some(&7i32), |b, v| { b.encode_i32(*v); Ok(()) })));
    m.insert("optional_none", buf(|b| b.encode_optional(None::<&i32>, |b, v| { b.encode_i32(*v); Ok(()) })));
    m.insert(
        "header_call_seq7_size4",
        buf(|b| { wire::encode_header(b, &wire::Header::new(wire::MsgType::Call, 7, 4)); Ok(()) }),
    );
    let methods = [
        MethodDescriptor { service_id: 1, method_id: 1, flags: wire::method_flags::NONE, reserved: 0 },
        MethodDescriptor { service_id: 2, method_id: 3, flags: wire::method_flags::STREAMING, reserved: 0 },
    ];
    let caps = wire::capability::STREAMING | wire::capability::HMAC;
    m.insert(
        "msg_init",
        wire::create_init_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, caps, &methods).unwrap().into_vec(),
    );
    m.insert(
        "msg_init_ack",
        wire::create_init_ack_message(wire::FIRST_VERSION, wire::CURRENT_VERSION, wire::capability::HMAC)
            .unwrap()
            .into_vec(),
    );
    let args = buf(|b| { b.encode_i32(5); b.encode_i32(9); Ok(()) });
    m.insert("msg_call", wire::create_call_message(7, 1, 2, &args).unwrap().into_vec());
    m.insert("msg_result", wire::create_result_message(7, &buf(|b| { b.encode_i32(14); Ok(()) })).unwrap().into_vec());
    m.insert("msg_error", wire::create_error_message(7, 3, "boom").unwrap().into_vec());
    m.insert("msg_stream", wire::create_stream_message(9, &buf(|b| { b.encode_i32(1); Ok(()) })).unwrap().into_vec());
    m.insert("msg_stream_end", wire::create_stream_end_message(9).unwrap().into_vec());
    m.insert("msg_shutdown", wire::create_shutdown_message().unwrap().into_vec());
    m
}

/// The vectors another test file covers (the HMAC transport).
const ELSEWHERE: &[&str] = &["hmac_key", "msg_call_tagged", "msg_stream_end_tagged"];

#[test]
fn encoders_match_the_golden_vectors() {
    if cfg!(target_endian = "big") {
        eprintln!("skipped: the golden vectors are little-endian and this runtime writes native order");
        return;
    }
    let golden = golden_vectors();
    let ours = built();
    for (name, want) in &golden {
        if ELSEWHERE.contains(&name.as_str()) {
            continue;
        }
        let got = ours.get(name.as_str()).unwrap_or_else(|| panic!("no Rust encoder for golden vector {name}"));
        assert_eq!(hex(got), hex(want), "golden vector {name}");
    }
    for name in ours.keys() {
        assert!(golden.contains_key(*name), "{name} is not in the golden vector file");
    }
}

#[test]
fn decoders_read_the_golden_vectors() {
    if cfg!(target_endian = "big") {
        return;
    }
    let g = golden_vectors();
    let b = |name: &str| Buffer::from_slice(&g[name]);
    assert!(b("bool_true").decode_bool().unwrap());
    assert!(!b("bool_false").decode_bool().unwrap());
    assert_eq!(b("i8_minus_2").decode_i8().unwrap(), -2);
    assert_eq!(b("i16_minus_2").decode_i16().unwrap(), -2);
    assert_eq!(b("i32_0x12345678").decode_i32().unwrap(), 0x1234_5678);
    assert_eq!(b("i64_minus_2").decode_i64().unwrap(), -2);
    assert_eq!(b("u8_0xab").decode_u8().unwrap(), 0xAB);
    assert_eq!(b("u16_0xbeef").decode_u16().unwrap(), 0xBEEF);
    assert_eq!(b("u32_0xdeadbeef").decode_u32().unwrap(), 0xDEAD_BEEF);
    assert_eq!(b("u64_0x0102030405060708").decode_u64().unwrap(), 0x0102_0304_0506_0708);
    assert_eq!(b("f32_1_5").decode_f32().unwrap(), 1.5);
    assert_eq!(b("f64_minus_2_25").decode_f64().unwrap(), -2.25);
    assert_eq!(b("string_hello").decode_string().unwrap(), "hello, song");
    assert_eq!(b("string_empty").decode_string().unwrap(), "");
    assert_eq!(b("string_utf8").decode_string().unwrap(), "h\u{e9}llo \u{266b}");
    assert_eq!(b("bytes_00_ff_10").decode_bytes().unwrap(), vec![0x00, 0xFF, 0x10]);
    assert_eq!(b("array_i32").decode_array(|b| b.decode_i32()).unwrap(), vec![1, -1, 65536]);
    assert_eq!(b("array_string").decode_array(|b| b.decode_string()).unwrap(), vec!["a", "bc"]);
    assert_eq!(
        b("array_nested_i32").decode_array(|b| b.decode_array(|b| b.decode_i32())).unwrap(),
        vec![vec![1, 2], vec![], vec![3]]
    );
    assert_eq!(b("optional_i32_some_7").decode_optional(|b| b.decode_i32()).unwrap(), Some(7));
    assert_eq!(b("optional_none").decode_optional(|b| b.decode_i32()).unwrap(), None);

    let mut init = b("msg_init");
    let hdr = wire::decode_header_validated(&mut init).unwrap();
    assert_eq!(hdr.msg_type(), Some(wire::MsgType::Init));
    assert_eq!(hdr.payload_size as usize, init.remaining());
    let msg = wire::decode_init(&mut init).unwrap();
    assert_eq!((msg.first_version, msg.current_version, msg.capabilities, msg.method_count), (0x0100, 0x0101, 0x41, 2));
    let first = wire::decode_method_descriptor(&mut init).unwrap();
    let second = wire::decode_method_descriptor(&mut init).unwrap();
    assert_eq!((first.service_id, first.method_id, first.flags), (1, 1, 0));
    assert_eq!((second.service_id, second.method_id, second.flags), (2, 3, wire::method_flags::STREAMING));
    assert!(!init.has_data());

    let mut call = b("msg_call");
    let hdr = wire::decode_header_validated(&mut call).unwrap();
    assert_eq!((hdr.msg_type(), hdr.sequence_id), (Some(wire::MsgType::Call), 7));
    assert_eq!(wire::decode_method_call_header(&mut call).unwrap(), (1, 2));
    assert_eq!((call.decode_i32().unwrap(), call.decode_i32().unwrap()), (5, 9));

    let mut error = b("msg_error");
    let hdr = wire::decode_header_validated(&mut error).unwrap();
    assert_eq!(hdr.msg_type(), Some(wire::MsgType::Error));
    assert_eq!(wire::decode_error(&mut error).unwrap(), (3, "boom".to_string()));

    let mut end = b("msg_stream_end");
    let hdr = wire::decode_header_validated(&mut end).unwrap();
    assert_eq!((hdr.msg_type(), hdr.sequence_id, hdr.payload_size), (Some(wire::MsgType::StreamEnd), 9, 0));
}
