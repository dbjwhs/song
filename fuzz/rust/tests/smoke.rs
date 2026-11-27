// MIT License
// Copyright (c) 2026 dbjwhs

//! The fuzz harnesses on deterministic input, so they run in `cargo test`
//! (and from ctest) on any toolchain, with no fuzzer: random bytes, and the
//! shared golden vectors (test/golden/wire_vectors.txt) truncated, extended
//! and with bytes changed. The seeds are fixed: a failure reproduces.
//!
//! This is the Rust counterpart of test/fuzz_test.cpp. It is not a
//! substitute for coverage-guided fuzzing (build.sh).

#![deny(warnings)]

use song_fuzz::{buffer_decoder, wire_decoder};
use std::path::PathBuf;

/// xorshift64*: small, deterministic, good enough to pick bytes.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }

    fn below(&mut self, bound: usize) -> usize {
        (self.next() % bound as u64) as usize
    }
}

fn golden_vectors() -> Vec<Vec<u8>> {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../test/golden/wire_vectors.txt");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
    let mut vectors = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let hex = line.split_whitespace().nth(1).unwrap_or("");
        let bytes: Vec<u8> =
            (0..hex.len() / 2).map(|ndx| u8::from_str_radix(&hex[ndx * 2..ndx * 2 + 2], 16).expect("hex")).collect();
        vectors.push(bytes);
    }
    assert!(vectors.len() > 20, "the golden vector file was not read");
    vectors
}

fn both(data: &[u8]) {
    wire_decoder(data);
    buffer_decoder(data);
}

#[test]
fn random_bytes() {
    let mut rng = Rng(0x534F_4E47_0000_0001);
    for _ in 0..20_000 {
        let len = rng.below(96);
        let data: Vec<u8> = (0..len).map(|_| rng.next() as u8).collect();
        both(&data);
    }
}

/// Inputs that look like messages: a sound header, then random payload
/// with a payload size that may or may not match.
#[test]
fn random_payloads_behind_a_sound_header() {
    let mut rng = Rng(0x534F_4E47_0000_0002);
    for _ in 0..20_000 {
        let len = rng.below(64);
        let claimed = match rng.below(4) {
            0 => len as u32,
            1 => rng.next() as u32,
            2 => u32::MAX,
            _ => rng.below(128) as u32,
        };
        let mut msg = song::Buffer::new();
        let msg_type = rng.below(0x14) as u8;
        song::wire::encode_header(
            &mut msg,
            &song::wire::Header {
                magic: song::wire::MAGIC,
                flags: rng.next() as u8,
                type_byte: msg_type,
                reserved: 0,
                payload_size: claimed,
                sequence_id: rng.next() as u32,
            },
        );
        let payload: Vec<u8> = (0..len).map(|_| rng.next() as u8).collect();
        msg.write(&payload);
        both(msg.data());
    }
}

/// Length prefixes at and around every cap, with almost nothing behind
/// them: a decoder that allocated from the prefix would ask for gigabytes.
#[test]
fn huge_length_prefixes() {
    for count in [0u32, 1, 255, 65_536, 1_000_000, 1_000_001, 16 << 20, (16 << 20) + 1, i32::MAX as u32, u32::MAX] {
        for tail in 0..6 {
            let mut data = count.to_ne_bytes().to_vec();
            data.extend(std::iter::repeat(0xFF).take(tail));
            both(&data);
            // Nested: an array whose first element has the huge prefix.
            let mut nested = 1u32.to_ne_bytes().to_vec();
            nested.extend_from_slice(&data);
            both(&nested);
        }
    }
}

#[test]
fn golden_vectors_cut_short_extended_and_changed() {
    let mut rng = Rng(0x534F_4E47_0000_0003);
    for vector in golden_vectors() {
        both(&vector);
        for len in 0..vector.len() {
            both(&vector[..len]);
        }
        let mut longer = vector.clone();
        longer.extend_from_slice(&[0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF]);
        both(&longer);
        if vector.is_empty() {
            continue;
        }
        for _ in 0..400 {
            let mut changed = vector.clone();
            for _ in 0..1 + rng.below(3) {
                let at = rng.below(changed.len());
                changed[at] = match rng.below(4) {
                    0 => 0x00,
                    1 => 0xFF,
                    2 => changed[at] ^ (1 << rng.below(8)),
                    _ => rng.next() as u8,
                };
            }
            both(&changed);
        }
    }
}
