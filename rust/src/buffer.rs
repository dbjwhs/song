// MIT License
// Copyright (c) 2026 dbjwhs

//! `Buffer`: the byte container every encode and decode goes through, with
//! the same encoding as the C++ `Buffer` and the Python `song.buffer`.
//!
//! Integers and floats are written in the machine's native byte order, as
//! the C++ runtime does (it copies the value's bytes). Strings and byte
//! arrays are a u32 length followed by the bytes; an array is a u32 count
//! followed by its elements; an optional is a bool followed by the value
//! when the bool is true.

use crate::error::{Error, Result};

/// Largest string, in bytes (C++ `kMaxStringSize`).
pub const MAX_STRING_SIZE: usize = 16 * 1024 * 1024;
/// Largest byte array (C++ `kMaxBytesSize`).
pub const MAX_BYTES_SIZE: usize = 16 * 1024 * 1024;
/// Most elements in one array (C++ `kMaxArrayCount`).
pub const MAX_ARRAY_COUNT: usize = 1024 * 1024;

/// A growable byte buffer with a read position.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Buffer {
    data: Vec<u8>,
    read_pos: usize,
}

macro_rules! primitive_codec {
    ($enc:ident, $dec:ident, $ty:ty) => {
        pub fn $enc(&mut self, val: $ty) {
            self.data.extend_from_slice(&val.to_ne_bytes());
        }

        pub fn $dec(&mut self) -> Result<$ty> {
            let mut raw = [0u8; std::mem::size_of::<$ty>()];
            raw.copy_from_slice(self.read(std::mem::size_of::<$ty>())?);
            Ok(<$ty>::from_ne_bytes(raw))
        }
    };
}

impl Buffer {
    pub fn new() -> Buffer {
        Buffer::default()
    }

    /// A buffer holding `data`, read from the start.
    pub fn from_vec(data: Vec<u8>) -> Buffer {
        Buffer { data, read_pos: 0 }
    }

    /// A buffer holding a copy of `data`, read from the start.
    pub fn from_slice(data: &[u8]) -> Buffer {
        Buffer { data: data.to_vec(), read_pos: 0 }
    }

    /// Append raw bytes.
    pub fn write(&mut self, data: &[u8]) {
        self.data.extend_from_slice(data);
    }

    /// The next `len` bytes, advancing the read position.
    pub fn read(&mut self, len: usize) -> Result<&[u8]> {
        if len > self.remaining() {
            return Err(Error::decode(format!(
                "buffer underflow: need {len} bytes, have {}",
                self.remaining()
            )));
        }
        let start = self.read_pos;
        self.read_pos += len;
        Ok(&self.data[start..start + len])
    }

    /// Everything written so far.
    pub fn data(&self) -> &[u8] {
        &self.data
    }

    /// The bytes not read yet.
    pub fn unread(&self) -> &[u8] {
        &self.data[self.read_pos..]
    }

    pub fn into_vec(self) -> Vec<u8> {
        self.data
    }

    pub fn size(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    pub fn read_pos(&self) -> usize {
        self.read_pos
    }

    pub fn remaining(&self) -> usize {
        self.data.len() - self.read_pos
    }

    pub fn has_data(&self) -> bool {
        self.read_pos < self.data.len()
    }

    /// Drop the contents and the read position.
    pub fn reset(&mut self) {
        self.data.clear();
        self.read_pos = 0;
    }

    /// Read from the start again.
    pub fn reset_read(&mut self) {
        self.read_pos = 0;
    }

    pub fn encode_bool(&mut self, val: bool) {
        self.data.push(u8::from(val));
    }

    pub fn decode_bool(&mut self) -> Result<bool> {
        Ok(self.read(1)?[0] != 0)
    }

    primitive_codec!(encode_i8, decode_i8, i8);
    primitive_codec!(encode_i16, decode_i16, i16);
    primitive_codec!(encode_i32, decode_i32, i32);
    primitive_codec!(encode_i64, decode_i64, i64);
    primitive_codec!(encode_u8, decode_u8, u8);
    primitive_codec!(encode_u16, decode_u16, u16);
    primitive_codec!(encode_u32, decode_u32, u32);
    primitive_codec!(encode_u64, decode_u64, u64);
    primitive_codec!(encode_f32, decode_f32, f32);
    primitive_codec!(encode_f64, decode_f64, f64);

    pub fn encode_string(&mut self, val: &str) -> Result<()> {
        if val.len() > MAX_STRING_SIZE {
            return Err(Error::decode("string too large to encode"));
        }
        self.encode_u32(val.len() as u32);
        self.write(val.as_bytes());
        Ok(())
    }

    /// A string: the length is checked against its cap and against the
    /// bytes actually present before anything is allocated.
    pub fn decode_string(&mut self) -> Result<String> {
        let len = self.decode_u32()? as usize;
        if len > MAX_STRING_SIZE {
            return Err(Error::decode("string too large"));
        }
        if len > self.remaining() {
            return Err(Error::decode("string length exceeds remaining data"));
        }
        let raw = self.read(len)?.to_vec();
        String::from_utf8(raw).map_err(|_| Error::decode("string is not valid UTF-8"))
    }

    pub fn encode_bytes(&mut self, val: &[u8]) -> Result<()> {
        if val.len() > MAX_BYTES_SIZE {
            return Err(Error::decode("byte array too large to encode"));
        }
        self.encode_u32(val.len() as u32);
        self.write(val);
        Ok(())
    }

    pub fn decode_bytes(&mut self) -> Result<Vec<u8>> {
        let len = self.decode_u32()? as usize;
        if len > MAX_BYTES_SIZE {
            return Err(Error::decode("byte array too large"));
        }
        if len > self.remaining() {
            return Err(Error::decode("byte array length exceeds remaining data"));
        }
        Ok(self.read(len)?.to_vec())
    }

    /// An array: a u32 count, then each element through `encode`.
    pub fn encode_array<T, F>(&mut self, vals: &[T], mut encode: F) -> Result<()>
    where
        F: FnMut(&mut Buffer, &T) -> Result<()>,
    {
        if vals.len() > MAX_ARRAY_COUNT {
            return Err(Error::decode("array too large"));
        }
        self.encode_u32(vals.len() as u32);
        for val in vals {
            encode(self, val)?;
        }
        Ok(())
    }

    /// An array: the count is checked against its cap, and the space
    /// reserved up front is never more than the bytes left in the buffer,
    /// so a forged count cannot force a large allocation. (An element may
    /// encode to zero bytes, an empty struct, so a count larger than the
    /// bytes left is not by itself an error.)
    pub fn decode_array<T, F>(&mut self, mut decode: F) -> Result<Vec<T>>
    where
        F: FnMut(&mut Buffer) -> Result<T>,
    {
        let count = self.decode_u32()? as usize;
        if count > MAX_ARRAY_COUNT {
            return Err(Error::decode("array too large"));
        }
        let mut out = Vec::with_capacity(count.min(self.remaining()));
        for _ in 0..count {
            out.push(decode(self)?);
        }
        Ok(out)
    }

    /// An optional: a bool, then the value when it is true.
    pub fn encode_optional<T, F>(&mut self, val: Option<&T>, encode: F) -> Result<()>
    where
        F: FnOnce(&mut Buffer, &T) -> Result<()>,
    {
        match val {
            Some(inner) => {
                self.encode_bool(true);
                encode(self, inner)
            }
            None => {
                self.encode_bool(false);
                Ok(())
            }
        }
    }

    pub fn decode_optional<T, F>(&mut self, decode: F) -> Result<Option<T>>
    where
        F: FnOnce(&mut Buffer) -> Result<T>,
    {
        if self.decode_bool()? {
            Ok(Some(decode(self)?))
        } else {
            Ok(None)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn primitives_round_trip() {
        let mut b = Buffer::new();
        b.encode_bool(true);
        b.encode_i8(i8::MIN);
        b.encode_i16(i16::MIN);
        b.encode_i32(i32::MIN);
        b.encode_i64(i64::MIN);
        b.encode_u8(u8::MAX);
        b.encode_u16(u16::MAX);
        b.encode_u32(u32::MAX);
        b.encode_u64(u64::MAX);
        b.encode_f32(-0.5);
        b.encode_f64(1e300);
        assert_eq!(b.size(), 1 + 1 + 2 + 4 + 8 + 1 + 2 + 4 + 8 + 4 + 8);
        assert!(b.decode_bool().unwrap());
        assert_eq!(b.decode_i8().unwrap(), i8::MIN);
        assert_eq!(b.decode_i16().unwrap(), i16::MIN);
        assert_eq!(b.decode_i32().unwrap(), i32::MIN);
        assert_eq!(b.decode_i64().unwrap(), i64::MIN);
        assert_eq!(b.decode_u8().unwrap(), u8::MAX);
        assert_eq!(b.decode_u16().unwrap(), u16::MAX);
        assert_eq!(b.decode_u32().unwrap(), u32::MAX);
        assert_eq!(b.decode_u64().unwrap(), u64::MAX);
        assert_eq!(b.decode_f32().unwrap(), -0.5);
        assert_eq!(b.decode_f64().unwrap(), 1e300);
        assert!(!b.has_data());
    }

    #[test]
    fn strings_bytes_arrays_and_optionals_round_trip() {
        let mut b = Buffer::new();
        b.encode_string("caf\u{e9}").unwrap();
        b.encode_bytes(&[1, 2, 3]).unwrap();
        b.encode_array(&[1.5f64, -2.0], |b, v| {
            b.encode_f64(*v);
            Ok(())
        })
        .unwrap();
        b.encode_optional(Some(&"x".to_string()), |b, v| b.encode_string(v)).unwrap();
        b.encode_optional(None::<&String>, |b, v| b.encode_string(v)).unwrap();
        assert_eq!(b.decode_string().unwrap(), "caf\u{e9}");
        assert_eq!(b.decode_bytes().unwrap(), vec![1, 2, 3]);
        assert_eq!(b.decode_array(|b| b.decode_f64()).unwrap(), vec![1.5, -2.0]);
        assert_eq!(b.decode_optional(|b| b.decode_string()).unwrap(), Some("x".to_string()));
        assert_eq!(b.decode_optional(|b| b.decode_string()).unwrap(), None);
        assert_eq!(b.remaining(), 0);
    }

    #[test]
    fn reading_past_the_end_is_an_error_and_moves_nothing() {
        let mut b = Buffer::from_slice(&[1, 2, 3]);
        assert!(matches!(b.decode_u32(), Err(Error::Decode(_))));
        assert_eq!(b.read_pos(), 0);
        assert_eq!(b.decode_u16().unwrap(), u16::from_ne_bytes([1, 2]));
        assert!(b.decode_u16().is_err());
        b.reset_read();
        assert_eq!(b.remaining(), 3);
        b.reset();
        assert!(b.is_empty());
    }

    /// A length prefix larger than the bytes present is refused before any
    /// allocation of that size (the C++ decoders' rule).
    #[test]
    fn a_forged_length_is_refused_before_allocating() {
        let mut forged = Buffer::new();
        forged.encode_u32(MAX_STRING_SIZE as u32);
        forged.write(b"abc");
        let err = forged.clone().decode_string().unwrap_err();
        assert!(err.to_string().contains("exceeds remaining data"), "{err}");
        let err = forged.decode_bytes().unwrap_err();
        assert!(err.to_string().contains("exceeds remaining data"), "{err}");
    }

    #[test]
    fn lengths_over_the_caps_are_refused() {
        let mut big = Buffer::new();
        big.encode_u32(MAX_STRING_SIZE as u32 + 1);
        assert!(big.clone().decode_string().unwrap_err().to_string().contains("too large"));
        assert!(big.decode_bytes().unwrap_err().to_string().contains("too large"));

        let mut many = Buffer::new();
        many.encode_u32(MAX_ARRAY_COUNT as u32 + 1);
        assert!(many.decode_array(|b| b.decode_u8()).unwrap_err().to_string().contains("array too large"));

        let mut out = Buffer::new();
        assert!(out.encode_string(&"x".repeat(MAX_STRING_SIZE + 1)).is_err());
        assert!(out.encode_bytes(&vec![0u8; MAX_BYTES_SIZE + 1]).is_err());
        assert!(out.encode_array(&vec![0u8; MAX_ARRAY_COUNT + 1], |b, v| {
            b.encode_u8(*v);
            Ok(())
        })
        .is_err());
        assert!(out.is_empty(), "a refused value writes nothing");
    }

    /// A count at the cap with no elements behind it fails on the first
    /// element, having reserved nothing.
    #[test]
    fn a_forged_array_count_fails_on_the_first_missing_element() {
        let mut forged = Buffer::new();
        forged.encode_u32(MAX_ARRAY_COUNT as u32);
        assert!(matches!(forged.decode_array(|b| b.decode_u64()), Err(Error::Decode(_))));
    }

    /// Elements may be empty on the wire, so a count with no bytes behind
    /// it is valid when the element decoder reads nothing.
    #[test]
    fn an_array_of_zero_size_elements_decodes() {
        let mut b = Buffer::new();
        b.encode_u32(3);
        assert_eq!(b.decode_array(|_| Ok(())).unwrap().len(), 3);
    }

    #[test]
    fn invalid_utf8_is_a_decode_error() {
        let mut b = Buffer::new();
        b.encode_u32(2);
        b.write(&[0xFF, 0xFE]);
        assert!(b.decode_string().unwrap_err().to_string().contains("UTF-8"));
    }
}
