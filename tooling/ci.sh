#!/usr/bin/env bash
#
# MIT License
# Copyright (c) 2026 dbjwhs
#
# ci.sh - Reproducible local CI gate for Song.
#
# Runs the same build-and-test sequence that CI runs and that every commit is
# expected to pass: a clean-configurable CMake build under Song's strict
# warning policy (-Wall -Wextra -Werror, so a successful build implies zero
# warnings) followed by the full ctest suite.
#
# Usage:
#   tooling/ci.sh [options]
#
# Options:
#   -d, --build-dir DIR     Build directory (default: build)
#   -t, --build-type TYPE   CMake build type (default: Release)
#   -j, --jobs N            Parallel build jobs (default: CPU count)
#   -c, --clean             Remove the build directory first (from-scratch build)
#   -h, --help              Show this help and exit
#
# Exit status is non-zero if configuration, build, or any test fails.

set -euo pipefail

# --- defaults ---------------------------------------------------------------
BUILD_DIR="build"
BUILD_TYPE="Release"
CLEAN=0

# Portable CPU count (Linux: nproc, macOS: sysctl).
if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=4
fi

# --- locate repo root (this script lives in tooling/) -----------------------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." >/dev/null 2>&1 && pwd)"

usage() {
    sed -n '7,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# --- argument parsing -------------------------------------------------------
while [ "$#" -gt 0 ]; do
    case "$1" in
        -d|--build-dir)  BUILD_DIR="$2"; shift 2 ;;
        -t|--build-type) BUILD_TYPE="$2"; shift 2 ;;
        -j|--jobs)       JOBS="$2"; shift 2 ;;
        -c|--clean)      CLEAN=1; shift ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "ci.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

cd "${REPO_ROOT}"

echo "== Song CI gate =="
echo "   repo:       ${REPO_ROOT}"
echo "   build dir:  ${BUILD_DIR}"
echo "   build type: ${BUILD_TYPE}"
echo "   jobs:       ${JOBS}"
echo "   clean:      $([ "${CLEAN}" -eq 1 ] && echo yes || echo no)"
echo

if [ "${CLEAN}" -eq 1 ]; then
    echo "-- removing ${BUILD_DIR} for a from-scratch build"
    rm -rf -- "${BUILD_DIR}"
fi

echo "-- configuring"
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "-- building (strict warnings are errors; a clean build implies zero warnings)"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo "-- testing (full suite)"
# --output-on-failure surfaces the failing assertion; environmental skips
# (mDNS discovery, codegen-compile) are reported as Skipped, not failures.
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo
echo "== CI gate PASSED =="
