// MIT License
// Copyright (c) 2026 dbjwhs

//! SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), written against the
//! standards because the crate takes no dependencies and `std` has neither.
//! The tests check the FIPS 180-4 example messages and the RFC 4231 HMAC
//! test cases; the interop tests check the tags against the C++ runtime
//! (CommonCrypto or OpenSSL) and Python's `hmac` on the wire.

const K: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
];

const H0: [u32; 8] =
    [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];

pub const DIGEST_SIZE: usize = 32;
pub const BLOCK_SIZE: usize = 64;

/// An incremental SHA-256.
#[derive(Clone)]
pub struct Sha256 {
    state: [u32; 8],
    block: [u8; BLOCK_SIZE],
    filled: usize,
    length: u64,
}

impl Default for Sha256 {
    fn default() -> Sha256 {
        Sha256::new()
    }
}

impl Sha256 {
    pub fn new() -> Sha256 {
        Sha256 { state: H0, block: [0; BLOCK_SIZE], filled: 0, length: 0 }
    }

    pub fn update(&mut self, mut data: &[u8]) {
        self.length = self.length.wrapping_add(data.len() as u64);
        while !data.is_empty() {
            let take = (BLOCK_SIZE - self.filled).min(data.len());
            self.block[self.filled..self.filled + take].copy_from_slice(&data[..take]);
            self.filled += take;
            data = &data[take..];
            if self.filled == BLOCK_SIZE {
                let block = self.block;
                self.compress(&block);
                self.filled = 0;
            }
        }
    }

    pub fn finalize(mut self) -> [u8; DIGEST_SIZE] {
        let bits = self.length.wrapping_mul(8);
        // Padding: a 1 bit, zeros to 56 mod 64, then the length in bits.
        let mut pad = [0u8; BLOCK_SIZE + 8];
        pad[0] = 0x80;
        let pad_len = if self.filled < 56 { 56 - self.filled } else { 120 - self.filled };
        self.update(&pad[..pad_len]);
        self.update(&bits.to_be_bytes());
        debug_assert_eq!(self.filled, 0);
        let mut out = [0u8; DIGEST_SIZE];
        for (ndx, word) in self.state.iter().enumerate() {
            out[ndx * 4..ndx * 4 + 4].copy_from_slice(&word.to_be_bytes());
        }
        out
    }

    fn compress(&mut self, block: &[u8; BLOCK_SIZE]) {
        let mut w = [0u32; 64];
        for ndx in 0..16 {
            w[ndx] = u32::from_be_bytes([block[ndx * 4], block[ndx * 4 + 1], block[ndx * 4 + 2], block[ndx * 4 + 3]]);
        }
        for ndx in 16..64 {
            let s0 = w[ndx - 15].rotate_right(7) ^ w[ndx - 15].rotate_right(18) ^ (w[ndx - 15] >> 3);
            let s1 = w[ndx - 2].rotate_right(17) ^ w[ndx - 2].rotate_right(19) ^ (w[ndx - 2] >> 10);
            w[ndx] = w[ndx - 16].wrapping_add(s0).wrapping_add(w[ndx - 7]).wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = self.state;
        for ndx in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ (!e & g);
            let t1 = h.wrapping_add(s1).wrapping_add(ch).wrapping_add(K[ndx]).wrapping_add(w[ndx]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        for (word, add) in self.state.iter_mut().zip([a, b, c, d, e, f, g, h]) {
            *word = word.wrapping_add(add);
        }
    }
}

pub fn sha256(data: &[u8]) -> [u8; DIGEST_SIZE] {
    let mut h = Sha256::new();
    h.update(data);
    h.finalize()
}

/// HMAC-SHA256 of `data` under `key`.
pub fn hmac_sha256(key: &[u8], data: &[u8]) -> [u8; DIGEST_SIZE] {
    let mut block = [0u8; BLOCK_SIZE];
    if key.len() > BLOCK_SIZE {
        block[..DIGEST_SIZE].copy_from_slice(&sha256(key));
    } else {
        block[..key.len()].copy_from_slice(key);
    }
    let mut inner = Sha256::new();
    let mut outer = Sha256::new();
    let mut ipad = [0u8; BLOCK_SIZE];
    let mut opad = [0u8; BLOCK_SIZE];
    for ndx in 0..BLOCK_SIZE {
        ipad[ndx] = block[ndx] ^ 0x36;
        opad[ndx] = block[ndx] ^ 0x5c;
    }
    inner.update(&ipad);
    inner.update(data);
    outer.update(&opad);
    outer.update(&inner.finalize());
    outer.finalize()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(bytes: &[u8]) -> String {
        use std::fmt::Write;
        let mut out = String::new();
        for b in bytes {
            write!(out, "{b:02x}").unwrap();
        }
        out
    }

    /// The example messages of FIPS 180-4 (and the one-million-'a' message
    /// of its test vector set).
    #[test]
    fn sha256_matches_the_fips_examples() {
        assert_eq!(hex(&sha256(b"abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        assert_eq!(hex(&sha256(b"")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert_eq!(
            hex(&sha256(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        );
        assert_eq!(
            hex(&sha256(&vec![b'a'; 1_000_000])),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
        );
    }

    /// Lengths on both sides of the padding boundaries (55, 56, 63, 64, 65
    /// bytes and their two-block twins); the expected digests are Python
    /// hashlib's for bytes i % 251.
    #[test]
    fn sha256_padding_boundaries() {
        let cases = [
            (55, "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"),
            (56, "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"),
            (63, "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"),
            (64, "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"),
            (65, "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"),
            (119, "da18797ed7c3a777f0847f429724a2d8cd5138e6ed2895c3fa1a6d39d18f7ec6"),
            (120, "f52b23db1fbb6ded89ef42a23ce0c8922c45f25c50b568a93bf1c075420bbb7c"),
        ];
        for (len, want) in cases {
            let data: Vec<u8> = (0..len).map(|ndx| (ndx % 251) as u8).collect();
            assert_eq!(hex(&sha256(&data)), want, "length {len}");
        }
    }

    #[test]
    fn sha256_is_the_same_fed_in_pieces() {
        let data: Vec<u8> = (0..1000u32).map(|ndx| (ndx * 7) as u8).collect();
        let whole = sha256(&data);
        for step in [1, 3, 63, 64, 65, 500] {
            let mut h = Sha256::new();
            for piece in data.chunks(step) {
                h.update(piece);
            }
            assert_eq!(h.finalize(), whole, "pieces of {step}");
        }
    }

    /// RFC 4231 test cases 1, 2, 3, 4, 6 and 7 (case 5 is a truncation
    /// case with no full digest).
    #[test]
    fn hmac_matches_rfc_4231() {
        let long_key = [0xaau8; 131];
        let key_4: Vec<u8> = (1..=25).collect();
        let cases: [(&[u8], &[u8], &str); 6] = [
            (&[0x0b; 20], b"Hi There", "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
            (b"Jefe", b"what do ya want for nothing?", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
            (&[0xaa; 20], &[0xdd; 50], "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"),
            (&key_4, &[0xcd; 50], "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"),
            (
                &long_key,
                b"Test Using Larger Than Block-Size Key - Hash Key First",
                "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
            ),
            (
                &long_key,
                b"This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.",
                "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
            ),
        ];
        for (ndx, (key, data, want)) in cases.iter().enumerate() {
            assert_eq!(hex(&hmac_sha256(key, data)), *want, "case {ndx}");
        }
    }
}
