#!/bin/bash
# MIT License
# Copyright (c) 2026 dbjwhs
#
# Generate seed corpus for libFuzzer harnesses.
# Run from the fuzz/ directory after building.
# Usage: ./generate_corpus.sh

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"

# Create corpus directories
mkdir -p "$DIR/corpus_wire" "$DIR/corpus_buffer"

echo "Seed corpus generation requires running the test suite first."
echo "The fuzz tests in test/fuzz_test.cpp use deterministic seeds"
echo "for reproducibility. For libFuzzer, use these directories:"
echo "  corpus_wire/   - seed inputs for fuzz_wire_decoder"
echo "  corpus_buffer/ - seed inputs for fuzz_buffer_decoder"
echo ""
echo "To run fuzz harnesses (requires LLVM clang):"
echo "  cmake .. -DSONG_ENABLE_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++"
echo "  make fuzz_wire_decoder fuzz_buffer_decoder"
echo "  ./fuzz_wire_decoder corpus_wire/ -max_len=256 -runs=1000000"
echo "  ./fuzz_buffer_decoder corpus_buffer/ -max_len=128 -runs=1000000"
