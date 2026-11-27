// MIT License
// Copyright (c) 2026 dbjwhs

//! Fuzz harnesses for the Rust runtime's decoders: the Rust counterparts of
//! fuzz/fuzz_wire_decoder.cpp and fuzz/fuzz_buffer_decoder.cpp.
//!
//! A harness passes when every decoder returns (a value or an error) for
//! any input: no panic, no arithmetic overflow, no allocation larger than
//! the input can justify. Each also checks one property a plain "did not
//! crash" would miss: a decoder never reads past the bytes it was given.
//!
//! `wire_decoder` and `buffer_decoder` are plain functions, so the tests
//! (tests/smoke.rs) drive them with deterministic input on any toolchain.
//! With the `wire` or `buffer` feature the crate also exports
//! `LLVMFuzzerTestOneInput` for a libFuzzer binary; see build.sh.

#![deny(warnings)]

#[path = "../../../rust/tests/generated/interop.rs"]
mod interop;

use song::wire::{self, MsgType};
use song::Buffer;

/// Run `decode` on a fresh buffer over `data`; whatever it returns, the
/// read position must still be inside the data.
fn check<T>(data: &[u8], decode: impl FnOnce(&mut Buffer) -> song::Result<T>) {
    let mut buf = Buffer::from_slice(data);
    let _ = decode(&mut buf);
    assert!(buf.read_pos() <= data.len(), "read past the end of the input");
    assert_eq!(buf.size(), data.len(), "a decoder changed the buffer");
}

/// The message decoders: framing, header, and each payload decoder.
pub fn wire_decoder(data: &[u8]) {
    // A whole message as a transport reads it off a stream: the magic and
    // the payload cap are checked before the payload is allocated.
    let mut stream = data;
    if let Ok(Some(msg)) = song::transport::read_message(&mut stream) {
        assert!(msg.size() <= data.len(), "a message larger than the input");
    }

    // Header first, then the payload decoder its type selects.
    let mut buf = Buffer::from_slice(data);
    if let Ok(hdr) = wire::decode_header_validated(&mut buf) {
        match hdr.msg_type() {
            Some(MsgType::Init) | Some(MsgType::InitAck) => {
                let _ = wire::decode_init(&mut buf);
            }
            Some(MsgType::Call) => {
                let _ = wire::decode_method_call_header(&mut buf);
            }
            Some(MsgType::Error) => {
                let _ = wire::decode_error(&mut buf);
            }
            _ => {}
        }
        assert!(buf.read_pos() <= data.len());
    }

    // Each payload decoder on the raw input too.
    check(data, wire::decode_header);
    check(data, wire::decode_init);
    check(data, wire::decode_method_call_header);
    check(data, wire::decode_error);

    // The HMAC transport's untagging: sizes are checked before any slice.
    let _ = song::security::untag_message(b"fuzz key", data);
}

/// The value decoders: every primitive, strings, bytes, arrays, optionals,
/// and generated struct decoders with nested arrays.
pub fn buffer_decoder(data: &[u8]) {
    check(data, Buffer::decode_bool);
    check(data, Buffer::decode_i8);
    check(data, Buffer::decode_i16);
    check(data, Buffer::decode_i32);
    check(data, Buffer::decode_i64);
    check(data, Buffer::decode_u8);
    check(data, Buffer::decode_u16);
    check(data, Buffer::decode_u32);
    check(data, Buffer::decode_u64);
    check(data, Buffer::decode_f32);
    check(data, Buffer::decode_f64);
    check(data, Buffer::decode_string);
    check(data, Buffer::decode_bytes);
    check(data, |buf| buf.decode_array(Buffer::decode_i32));
    check(data, |buf| buf.decode_array(Buffer::decode_string));
    check(data, |buf| buf.decode_array(Buffer::decode_f64));
    check(data, |buf| buf.decode_array(|inner: &mut Buffer| inner.decode_array(Buffer::decode_bytes)));
    check(data, |buf| buf.decode_optional(Buffer::decode_string));

    // A decoded string or byte array is never longer than the input, and
    // an array never holds more elements than the input has bytes for.
    let mut buf = Buffer::from_slice(data);
    if let Ok(text) = buf.decode_string() {
        assert!(text.len() <= data.len());
    }
    let mut buf = Buffer::from_slice(data);
    if let Ok(values) = buf.decode_array(Buffer::decode_u8) {
        assert!(values.len() <= data.len());
    }

    // Generated code: structs with enums, flags, strings and nested arrays.
    check(data, interop::decode_Sample);
    check(data, interop::decode_Batch);
    check(data, interop::decode_Status);
}

#[cfg(any(feature = "wire", feature = "buffer"))]
mod entry {
    /// The libFuzzer entry point.
    ///
    /// # Safety
    /// libFuzzer passes a pointer to `size` readable bytes (or any pointer
    /// when `size` is 0).
    #[no_mangle]
    pub unsafe extern "C" fn LLVMFuzzerTestOneInput(data: *const u8, size: usize) -> i32 {
        let input = if size == 0 { &[][..] } else { std::slice::from_raw_parts(data, size) };
        #[cfg(feature = "wire")]
        super::wire_decoder(input);
        #[cfg(feature = "buffer")]
        super::buffer_decoder(input);
        0
    }
}
