// MIT License
// Copyright (c) 2026 dbjwhs

//! `SecureTransport`: HMAC-SHA256 message authentication over any
//! transport, byte-compatible with the C++ `SecureTransport` and the Python
//! client's `key=` option.
//!
//! On the wire a tagged message is the original header with `payload_size`
//! grown by 8, the original payload, then the tag: the first 8 bytes of
//! HMAC-SHA256, under the shared key, over the untagged message (header
//! with the original `payload_size`, then the payload).
//!
//! This authenticates messages; it does not encrypt them, and it does not
//! stop a recorded message from being replayed.

use crate::buffer::Buffer;
use crate::error::{Error, Result};
use crate::sha256::hmac_sha256;
use crate::transport::Transport;
use crate::wire::{HEADER_SIZE, MAX_PAYLOAD_SIZE, PAYLOAD_SIZE_OFFSET};
use std::time::Duration;

/// Bytes of the HMAC kept as the tag (C++ `kHmacTagSize`).
pub const HMAC_TAG_SIZE: usize = 8;

pub type HmacTag = [u8; HMAC_TAG_SIZE];

/// The tag of an untagged message under `key`.
pub fn compute_tag(key: &[u8], msg: &[u8]) -> HmacTag {
    let mut tag = [0u8; HMAC_TAG_SIZE];
    tag.copy_from_slice(&hmac_sha256(key, msg)[..HMAC_TAG_SIZE]);
    tag
}

/// Whether `tag` is the tag of `msg` under `key`, compared in constant time.
pub fn verify_tag(key: &[u8], msg: &[u8], tag: &HmacTag) -> bool {
    let computed = compute_tag(key, msg);
    let mut diff = 0u8;
    for ndx in 0..HMAC_TAG_SIZE {
        diff |= computed[ndx] ^ tag[ndx];
    }
    diff == 0
}

fn payload_size_of(msg: &[u8]) -> u32 {
    let mut raw = [0u8; 4];
    raw.copy_from_slice(&msg[PAYLOAD_SIZE_OFFSET..PAYLOAD_SIZE_OFFSET + 4]);
    u32::from_ne_bytes(raw)
}

fn set_payload_size(msg: &mut [u8], size: u32) {
    msg[PAYLOAD_SIZE_OFFSET..PAYLOAD_SIZE_OFFSET + 4].copy_from_slice(&size.to_ne_bytes());
}

/// The wire form of `msg` (a whole untagged message) tagged under `key`.
pub fn tag_message(key: &[u8], msg: &[u8]) -> Result<Vec<u8>> {
    if msg.len() < HEADER_SIZE {
        return Err(Error::Security("message shorter than a header".into()));
    }
    let size = payload_size_of(msg) as usize;
    if size > MAX_PAYLOAD_SIZE - HMAC_TAG_SIZE {
        return Err(Error::Security("payload too large for HMAC authentication".into()));
    }
    if msg.len() != HEADER_SIZE + size {
        return Err(Error::Security("message length does not match its header".into()));
    }
    let tag = compute_tag(key, msg);
    let mut out = Vec::with_capacity(msg.len() + HMAC_TAG_SIZE);
    out.extend_from_slice(msg);
    set_payload_size(&mut out, (size + HMAC_TAG_SIZE) as u32);
    out.extend_from_slice(&tag);
    Ok(out)
}

/// The untagged message inside `tagged` (a whole message as received),
/// after checking its tag under `key`.
pub fn untag_message(key: &[u8], tagged: &[u8]) -> Result<Vec<u8>> {
    if tagged.len() < HEADER_SIZE {
        return Err(Error::Security("message shorter than a header".into()));
    }
    let ext_size = payload_size_of(tagged) as usize;
    if ext_size < HMAC_TAG_SIZE || tagged.len() != HEADER_SIZE + ext_size {
        return Err(Error::Security("message payload too short for HMAC verification".into()));
    }
    let size = ext_size - HMAC_TAG_SIZE;
    let mut msg = tagged[..HEADER_SIZE + size].to_vec();
    set_payload_size(&mut msg, size as u32);
    let mut tag = [0u8; HMAC_TAG_SIZE];
    tag.copy_from_slice(&tagged[HEADER_SIZE + size..]);
    if !verify_tag(key, &msg, &tag) {
        return Err(Error::Security("HMAC verification failed - message may be tampered".into()));
    }
    Ok(msg)
}

/// A transport that tags every message it sends and verifies every message
/// it receives. A message that fails verification closes the transport: the
/// peer does not hold the key, or the bytes were changed on the way.
///
/// An empty key turns authentication off (every message passes through
/// unchanged), as an empty `SecurityConfig` does in C++.
pub struct SecureTransport<T: Transport> {
    inner: T,
    key: Vec<u8>,
}

impl<T: Transport> SecureTransport<T> {
    pub fn new(inner: T, key: impl Into<Vec<u8>>) -> SecureTransport<T> {
        SecureTransport { inner, key: key.into() }
    }

    pub fn is_enabled(&self) -> bool {
        !self.key.is_empty()
    }
}

impl<T: Transport> Transport for SecureTransport<T> {
    fn send(&mut self, msg: &[u8]) -> Result<()> {
        if !self.is_enabled() {
            return self.inner.send(msg);
        }
        let tagged = tag_message(&self.key, msg)?;
        self.inner.send(&tagged)
    }

    fn receive(&mut self, timeout: Option<Duration>) -> Result<Option<Buffer>> {
        let tagged = match self.inner.receive(timeout)? {
            Some(msg) => msg,
            None => return Ok(None),
        };
        if !self.is_enabled() {
            return Ok(Some(tagged));
        }
        match untag_message(&self.key, tagged.data()) {
            Ok(msg) => Ok(Some(Buffer::from_vec(msg))),
            Err(e) => {
                self.inner.close();
                Err(e)
            }
        }
    }

    fn close(&mut self) {
        self.inner.close();
    }

    fn is_connected(&self) -> bool {
        self.inner.is_connected()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transport::PipeTransport;
    use crate::wire;
    use std::io::Cursor;

    const KEY: &[u8] = b"song-rust-test-secret";

    fn call() -> Vec<u8> {
        wire::create_call_message(4, 1, 2, &[9, 9, 9]).unwrap().into_vec()
    }

    #[test]
    fn a_tagged_message_grows_by_the_tag_and_untags_to_the_original() {
        let msg = call();
        let tagged = tag_message(KEY, &msg).unwrap();
        assert_eq!(tagged.len(), msg.len() + HMAC_TAG_SIZE);
        assert_eq!(payload_size_of(&tagged), payload_size_of(&msg) + HMAC_TAG_SIZE as u32);
        assert_eq!(&tagged[HEADER_SIZE..msg.len()], &msg[HEADER_SIZE..], "the payload is not changed");
        assert_eq!(&tagged[msg.len()..], &compute_tag(KEY, &msg)[..]);
        assert_eq!(untag_message(KEY, &tagged).unwrap(), msg);
    }

    #[test]
    fn a_wrong_key_or_any_changed_byte_is_refused() {
        let tagged = tag_message(KEY, &call()).unwrap();
        assert!(matches!(untag_message(b"another key", &tagged), Err(Error::Security(_))));
        for ndx in 0..tagged.len() {
            if (PAYLOAD_SIZE_OFFSET..PAYLOAD_SIZE_OFFSET + 4).contains(&ndx) {
                continue; // a changed size is caught by the length check, below
            }
            let mut bad = tagged.clone();
            bad[ndx] ^= 0x01;
            assert!(matches!(untag_message(KEY, &bad), Err(Error::Security(_))), "byte {ndx}");
        }
        let mut resized = tagged.clone();
        set_payload_size(&mut resized, payload_size_of(&tagged) - 1);
        assert!(matches!(untag_message(KEY, &resized), Err(Error::Security(_))));
    }

    #[test]
    fn a_message_too_short_to_hold_a_tag_is_refused() {
        let untagged = wire::create_stream_end_message(1).unwrap().into_vec();
        let err = untag_message(KEY, &untagged).unwrap_err();
        assert!(err.to_string().contains("too short"), "{err}");
        assert!(untag_message(KEY, &untagged[..10]).is_err());
        assert!(tag_message(KEY, &untagged[..10]).is_err());
    }

    #[test]
    fn a_payload_that_cannot_take_the_tag_is_not_sent() {
        let mut big = wire::create_result_message(1, &vec![0u8; MAX_PAYLOAD_SIZE]).unwrap().into_vec();
        assert!(matches!(tag_message(KEY, &big), Err(Error::Security(_))));
        big.truncate(HEADER_SIZE + MAX_PAYLOAD_SIZE - HMAC_TAG_SIZE);
        set_payload_size(&mut big, (MAX_PAYLOAD_SIZE - HMAC_TAG_SIZE) as u32);
        assert!(tag_message(KEY, &big).is_ok());
    }

    #[test]
    fn the_transport_tags_what_it_sends_and_verifies_what_it_receives() {
        let incoming = tag_message(KEY, &call()).unwrap();
        let mut sent = Vec::new();
        {
            let mut t = SecureTransport::new(PipeTransport::new(Cursor::new(incoming), &mut sent), KEY);
            t.send(&call()).unwrap();
            assert_eq!(t.receive(None).unwrap().unwrap().data(), &call()[..]);
        }
        assert_eq!(sent, tag_message(KEY, &call()).unwrap());
    }

    #[test]
    fn a_failed_verification_closes_the_transport() {
        let incoming = tag_message(b"not our key", &call()).unwrap();
        let mut t = SecureTransport::new(PipeTransport::new(Cursor::new(incoming), Vec::new()), KEY);
        assert!(matches!(t.receive(None), Err(Error::Security(_))));
        assert!(!t.is_connected());
    }

    #[test]
    fn an_empty_key_passes_messages_through() {
        let mut sent = Vec::new();
        {
            let mut t = SecureTransport::new(PipeTransport::new(Cursor::new(call()), &mut sent), Vec::new());
            assert!(!t.is_enabled());
            t.send(&call()).unwrap();
            assert_eq!(t.receive(None).unwrap().unwrap().data(), &call()[..]);
        }
        assert_eq!(sent, call());
    }
}
