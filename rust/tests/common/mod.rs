// MIT License
// Copyright (c) 2026 dbjwhs

//! Shared by the integration tests: the golden vector file.

#![allow(dead_code)]

use std::collections::BTreeMap;
use std::path::PathBuf;

/// The repository root (the crate lives in `<root>/rust`).
pub fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).parent().expect("crate has a parent directory").to_path_buf()
}

pub fn unhex(text: &str) -> Vec<u8> {
    assert!(text.len() % 2 == 0, "odd hex length: {text}");
    (0..text.len() / 2)
        .map(|ndx| u8::from_str_radix(&text[ndx * 2..ndx * 2 + 2], 16).expect("hex digit"))
        .collect()
}

pub fn hex(bytes: &[u8]) -> String {
    use std::fmt::Write;
    let mut out = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        write!(out, "{b:02x}").expect("write to a String");
    }
    out
}

/// `test/golden/wire_vectors.txt` as name -> bytes.
pub fn golden_vectors() -> BTreeMap<String, Vec<u8>> {
    let path = repo_root().join("test").join("golden").join("wire_vectors.txt");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
    let mut out = BTreeMap::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (name, value) = line.split_once('=').unwrap_or_else(|| panic!("not a vector line: {line}"));
        out.insert(name.trim().to_string(), unhex(value.trim()));
    }
    out
}
