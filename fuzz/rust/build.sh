#!/bin/bash
# MIT License
# Copyright (c) 2026 dbjwhs
#
# Build libFuzzer binaries for the Rust decoders, beside the C++ harnesses.
#
#   ./build.sh                       builds fuzz_rust_wire_decoder and
#                                    fuzz_rust_buffer_decoder in this directory
#   CLANG=/path/to/clang ./build.sh  with a named LLVM clang
#   LDFLAGS="-isysroot $(xcrun --show-sdk-path)" ./build.sh
#                                    extra link flags, for example when a
#                                    Homebrew clang does not find the macOS SDK
#
# Needs cargo and an LLVM clang that ships the libFuzzer runtime (not Apple
# clang), as the C++ harnesses do. No third-party crate is used: the crate
# exports LLVMFuzzerTestOneInput itself, rustc adds the coverage
# instrumentation libFuzzer reads (sanitizer coverage, stable flags), and
# clang links the fuzzer runtime.
#
# Not instrumented with AddressSanitizer: that needs a nightly rustc. The
# decoders are safe Rust, so what the fuzzer finds here is a panic, an
# arithmetic overflow, or a runaway allocation.
#
# Run:
#   ./fuzz_rust_wire_decoder corpus_wire/ -max_len=256 -runs=1000000
#   ./fuzz_rust_buffer_decoder corpus_buffer/ -max_len=128 -runs=1000000

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

CLANG="${CLANG:-}"
if [ -z "$CLANG" ]; then
    for candidate in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang; do
        if command -v "$candidate" >/dev/null 2>&1; then
            CLANG="$candidate"
            break
        fi
    done
fi
if [ -z "$CLANG" ]; then
    echo "no clang found; set CLANG to an LLVM clang with libFuzzer" >&2
    exit 1
fi
if ! command -v cargo >/dev/null 2>&1; then
    echo "cargo not found" >&2
    exit 1
fi

SANCOV="-Cpasses=sancov-module \
 -Cllvm-args=-sanitizer-coverage-level=4 \
 -Cllvm-args=-sanitizer-coverage-inline-8bit-counters \
 -Cllvm-args=-sanitizer-coverage-pc-table \
 -Cllvm-args=-sanitizer-coverage-trace-compares"

for target in wire buffer; do
    RUSTFLAGS="$SANCOV" cargo build --release --features "$target" --target-dir "target/$target"
    # shellcheck disable=SC2086  # LDFLAGS is a list of flags
    "$CLANG" -fsanitize=fuzzer ${LDFLAGS:-} "target/$target/release/libsong_fuzz.a" -o "fuzz_rust_${target}_decoder"
    echo "built $DIR/fuzz_rust_${target}_decoder"
done

# Seed the corpora from the golden vectors shared by the C++, Python and
# Rust tests: whole messages for the wire harness, values for the buffer
# harness. The fuzzer then starts from inputs that already parse.
mkdir -p corpus_wire corpus_buffer
while read -r name hex; do
    case "$name" in
        ""|"#"*) continue ;;
        msg_*|header*) out="corpus_wire/$name" ;;
        *) out="corpus_buffer/$name" ;;
    esac
    printf '%s' "$hex" | xxd -r -p > "$out"
done < "$DIR/../../test/golden/wire_vectors.txt"
echo "seeded corpus_wire/ and corpus_buffer/ from test/golden/wire_vectors.txt"
