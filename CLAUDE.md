# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Song (**S**ervices **O**ver **N**ative **G**ateways) is a C++20 service framework: process-isolated services that talk over Unix pipes or TCP, with an IDL compiler (`songc`) that generates the proxy/dispatcher code.

## Reference Material Outside This Repo

The design document is `../new-world/SONG_DESIGN.md` (architecture, wire protocol, IDL grammar, punch list). Read it before design-level work on the runtime, wire protocol, or IDL; it is not needed for a localized fix. `../new-world/SONG_NETWORK_RESEARCH.md` covers the network distribution design.

Song descends from two legacy codebases, useful as reference when a behavior is unclear:

- **Thor** (`../new-world/thor/`), service runtime: `pitond/transaction.c` (fork/exec spawn, pipe redirection, process reuse), `resplib/servicelib.c` (init confirmation, version negotiation, transaction loop, streams).
- **DAG** (`../new-world/dag/`), IDL compiler and type system: `dagger/database.h` (type definitions, arrays, struct/class types, field flags), `dagger/wire.h` (wire operations, version constants, properties).

SONG_DESIGN.md Sections 15 and 17 cross-reference these against Song's design.

## Commands

```bash
# Configure and build everything (library, songc, tests, examples, sing)
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # CI uses Debug; Release adds -O3 -march=native -flto
cmake --build build -j

# All tests (unit + integration)
ctest --test-dir build --output-on-failure

# Unit tests only: one gtest binary
./build/test/song_tests
./build/test/song_tests --gtest_filter='BufferTest.*'        # one suite
./build/test/song_tests --gtest_filter='LexerTest.Keywords'  # one test
ctest --test-dir build -R '^BufferTest\.'                    # same, via ctest

# Integration tests only
ctest --test-dir build -R sing_
(cd build/sing/ipc/calculator && ./sing_ipc_calculator_test)

# IDL compiler
./build/songc foo.song -o outdir               # single header (default)
./build/songc foo.song -o outdir --split       # types / wire / client / server files
./build/songc foo.song -o outdir --scaffold    # implementation skeleton; re-run appends a sync report
./build/songc foo.song -o outdir --lang python # Python client proxy

# Python client library (tests spawn the built C++ TCP calculator service, so build first)
cd python && python -m unittest discover tests

# libFuzzer harnesses (needs LLVM clang, not Apple clang)
cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug -DSONG_ENABLE_FUZZ=ON
```

Notes on running tests:

- Integration test binaries locate their service executable relative to the working directory, so run them from their own build directory (ctest does this for you).
- Unit tests that bind network ports share a ctest `RESOURCE_LOCK`, so `ctest -j` is safe.
- Tests for optional features skip (not fail) when the dependency is absent: TLS needs mbedTLS (`SONG_HAS_TLS`), Linux mDNS needs Avahi (`SONG_HAS_AVAHI`). A green local run does not prove those paths compile; CI builds both macOS (clang, Bonjour) and Linux (GCC, Avahi with a live daemon) for that reason.
- GoogleTest is fetched via FetchContent at configure time (network needed on first configure).

## Code Style

- C++20, 2-space indentation, snake_case for variables and functions
- Constants use `kConstantName` (generated IDs look like `kService_Echo`, `kMethod_add`)
- Braces on every `if`/`else`/`for`/`while`, including single-line bodies
- Loop indices are named `ndx`, not `i`
- **No warnings**: everything first-party compiles with `-Wall -Wextra -Werror` on both clang and GCC. Use `[[maybe_unused]]` for intentionally unused parameters.
- No emojis anywhere (code, comments, docs, commit messages). Use plain markers: [DONE], [TODO], [WIP], [WARNING].
- Commit messages: `area: summary` (e.g. `codegen: emit <Enum>_name() for every IDL enum`).
- Every source file and CMakeLists.txt starts with the two-line MIT header (`MIT License` / `Copyright (c) 2026 dbjwhs`). The pre-commit hook runs `tooling/new_line.sh` and `tooling/update_header_license.sh`, which add missing headers and trailing newlines.

## Architecture

### Layout

- `include/song/`, `src/`: `libsong`, the runtime. `song.hpp` is the umbrella header.
- `compiler/`: `songc`. Pipeline is `lexer -> parser (ast.hpp) -> resolver -> codegen`, with three back ends: `codegen.cpp` (C++), `python_codegen.cpp`, `scaffold.cpp` (implementation skeleton plus sync report against an existing file).
- `test/`: all unit tests, linked into one `song_tests` binary. It compiles the `compiler/*.cpp` sources directly (minus `main.cpp`), so compiler internals are testable without going through the CLI.
- `sing/`: integration projects (`ipc/` over pipes, `network/` over TCP). Each has a `.song` file, the generated header, a service executable, and a gtest binary named `sing_<kind>_<project>_test`.
- `examples/`: hand-written (non-IDL) services, including `registry_service`, the cross-subnet discovery registry.
- `python/song/`: a pure-Python reimplementation of Buffer, wire, and connection (not FFI). It must stay byte-compatible with the C++ runtime. Client only: unary and streaming calls over TCP, optional HMAC (`key=`); no TLS, discovery, objects, or properties.
- `fuzz/`: libFuzzer harnesses for the wire and buffer decoders.

### Generated code is checked in and not rebuilt by CMake

The `.hpp` next to each `.song` in `sing/` and `examples/`, and `python/song/generated/`, are `songc` output committed to the repo. CMake never invokes `songc` on them. After changing a code generator or a `.song` file, regenerate the affected headers by hand and commit them, or the integration tests keep exercising stale output.

### Wire protocol (`wire.hpp`)

16-byte fixed header (magic `0x534F4E47`, flags, type, reserved, payload size, sequence id) plus payload. Native endianness, no byte swapping. Protocol version is 1.1; 1.0 peers are still accepted (`kFirstVersion`).

Message types go beyond plain RPC: `init`/`init_ack` (handshake), `call`/`result`/`error`, `stream`/`stream_end`, `ping`, `shutdown`, object lifecycle (`create`, `release`, `object_call`), and properties (`prop_get`, `prop_set`, `prop_subscribe`, `prop_unsubscribe`, `prop_notify`).

The init handshake negotiates a 32-bit `Capability` bitfield (the AND of both peers): bits 0-15 are framework features, 16-23 are runtime-registered extensions (`ServiceRuntime::register_extension`), 24-31 are vendor slots. Gate any new optional protocol feature on a capability bit rather than a version bump.

A change to wire layout or Buffer encoding has to land in three places: `src/wire.cpp`/`src/buffer.cpp`, the C++ and Python code generators, and `python/song/wire.py`/`buffer.py`.

### Runtime

- `Buffer`: move-only container with 4KB small-buffer optimization; all encode/decode goes through it.
- `Transport` (`transport.hpp`) is the abstraction everything above the byte layer uses. Implementations: `PipeTransport`, `TcpTransport`, and the wrappers `SecureTransport` (HMAC-SHA256; CommonCrypto on macOS, OpenSSL on Linux) and `TlsTransport` (`src/tls.cpp`, mbedTLS, only compiled when found). A service layers security onto its TCP loops with `ServiceRuntime::set_transport_wrapper`, which wraps each accepted transport before any bytes are exchanged.
- Client side: `ServiceManager` registers services (local executable, explicit TCP address, or discoverable name), lazily starts or connects, and auto-restarts crashed local services. `ServiceProcess` does fork/exec and the init handshake; `ServiceConnection` is the RPC proxy (sequence-ID matching, stream reading, object and property operations, and demuxing unsolicited `prop_notify` from call replies).
- Service side: `ServiceRuntime` holds dispatchers (`register_dispatcher`, `register_stream_dispatcher`, `register_method`, `register_factory` for remote objects) and has several main loops: `run()` (stdin/stdout pipes, single client), `run_tcp()` (one client at a time), `run_tcp_multi()` (thread per client, shared object registry and subscriptions), `run_tcp_discoverable()` (TCP plus mDNS advertisement).
- Discovery (`discovery.cpp`): Bonjour on macOS, Avahi on Linux, with `registry.cpp` as a fallback for networks where mDNS does not cross subnets.
- Objects and properties: `object.cpp` is a reference-counted remote object registry; `subscription.cpp` fans `prop_notify` out to subscribers.

### Constraints that are easy to violate

- Every serving loop, pipe and TCP, goes through the one `ServiceRuntime::handle_message`. Do not add a per-transport dispatch copy; the old fd-only one silently lacked property push.
- A stream dispatcher and a unary dispatcher may share a service id only because calls are routed by `register_method` flags. An undeclared method on a shared id goes to the stream dispatcher. Generated code still uses a separate `kService_<Name>_Stream` id.
- Size limits: 16 MB per message payload, and a string or byte array may be that large. Decoders must check a length prefix against `Buffer::remaining()` before allocating.
- Remote objects are owned by their creating connection and destroyed when it drops, even if another client holds the id. Object ids carry no ownership check.
- The parser and resolver accept more than the generators emit: `error` types and `throws` clauses, struct inheritance, and the `optional` method modifier validate but generate nothing; `T[]?` generates as a plain `std::vector<T>`. See "Accepted but Not Yet Generated" in README.md before assuming an IDL feature works end to end.

## Downstream consumer

`../savannah` builds against this checkout: it links `build/libsong.a`, runs `build/songc` on its own IDL at build time, and includes the public headers. A change to wire layout, `songc` output, or `include/song/` can break it, so after such a change rebuild savannah and run its tests (`cmake --build ../savannah/build && ctest --test-dir ../savannah/build`). Its CLAUDE.md keeps a numbered list of song findings.

## Documentation

README.md is the user-facing reference (IDL reference, security, streaming, registry, status table, test counts) and is kept in step with the code: a change to IDL syntax, generated output, `songc` flags, or platform support should update the matching README section. `sing/README.md` documents each integration project.
