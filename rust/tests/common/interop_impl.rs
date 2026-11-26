// MIT License
// Copyright (c) 2026 dbjwhs

//! The Rust implementation of the interop service
//! (sing/network/rust_interop/interop.song). The C++ implementation
//! (interop_service.cpp) does the same thing value for value, so a test can
//! state what either peer must answer. Used by tests and by the
//! `interop_service` example binary; expects the generated module at
//! `crate::interop`.

use crate::interop::{Access, Batch, Interop, Sample, Second, Status};
use song::{Error, Result, TypedStreamWriter};

/// Sample number `ndx` of a `samples(n, label)` stream. Every field depends
/// on `ndx`, optional fields are present on alternate samples.
pub fn make_sample(ndx: i32, label: &str) -> Sample {
    let odd = ndx % 2 == 1;
    Sample {
        id: ndx,
        label: label.to_string(),
        value: f64::from(ndx) * 0.5,
        status: if odd { Status::failed } else { Status::retry },
        access: Access((ndx as u32) & 7),
        note_id: if odd { Some(i64::from(ndx) * 1_000_000_007) } else { None },
        note: if odd { None } else { Some(format!("note {ndx}")) },
        blob: (0..ndx).map(|v| v as u8).collect(),
        counts: (1..=ndx).collect(),
        tags: (0..ndx % 3).map(|v| format!("{label}-{v}")).collect(),
    }
}

pub struct InteropImpl;

impl Interop for InteropImpl {
    fn add(&self, a: i32, b: i32) -> Result<i32> {
        Ok(a.wrapping_add(b))
    }

    fn echo_sample(&self, s: &Sample) -> Result<Sample> {
        Ok(s.clone())
    }

    fn echo_batch(&self, b: &Batch) -> Result<Batch> {
        Ok(b.clone())
    }

    fn describe(&self, status: Status, access: Access, prefix: Option<&str>) -> Result<String> {
        Ok(format!("{}{}:{}", prefix.unwrap_or(""), status.name(), access.bits()))
    }

    fn fail(&self, message: &str) -> Result<()> {
        Err(Error::service(message))
    }

    fn ping(&self) -> Result<()> {
        Ok(())
    }

    fn count_to(&self, n: i32, writer: &mut TypedStreamWriter<'_, '_, i32>) -> Result<()> {
        for value in 1..=n {
            writer.write(&value)?;
        }
        Ok(())
    }

    fn samples(&self, n: i32, label: &str, writer: &mut TypedStreamWriter<'_, '_, Sample>) -> Result<()> {
        for ndx in 1..=n {
            writer.write(&make_sample(ndx, label))?;
        }
        Ok(())
    }

    fn fail_after(&self, n: i32, message: &str, writer: &mut TypedStreamWriter<'_, '_, i32>) -> Result<()> {
        for value in 1..=n {
            writer.write(&value)?;
        }
        Err(Error::service(message))
    }
}

/// Answers `whoami` with the language of the peer, so a test can tell which
/// implementation it reached.
pub struct SecondImpl;

impl Second for SecondImpl {
    fn whoami(&self) -> Result<String> {
        Ok("rust".to_string())
    }
}

/// A runtime with both services registered.
pub fn interop_runtime() -> song::ServiceRuntime {
    let mut runtime = song::ServiceRuntime::new();
    crate::interop::register_Interop(&mut runtime, std::sync::Arc::new(InteropImpl));
    crate::interop::register_Second(&mut runtime, std::sync::Arc::new(SecondImpl));
    runtime
}

/// A batch that uses every field, for echo tests.
pub fn make_batch() -> Batch {
    Batch {
        samples: (0..4).map(|ndx| make_sample(ndx, "batch")).collect(),
        best: Some(make_sample(7, "best")),
        grid: vec![vec![1, 2, 3], vec![], vec![-4]],
        history: vec![Status::ok, Status::failed, Status::retry, Status::ok],
    }
}
