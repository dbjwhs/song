# Song Project Status

## Current State: Phase 1 Complete with Full Test Coverage

**Last Updated:** 2026-01-19

## What is Song?

Song (Services Over Native Gateways) is a high-performance C++ service framework for building process-isolated microservices that communicate via Unix pipes. It provides zero-copy IPC, type-safe RPC, and automatic service lifecycle management.

## Project History

1. Started as "Bolt" based on design document in ../new-world/SONG_DESIGN.md
2. Renamed to "Song" with complete code transformation
3. Initial implementation completed and committed to GitHub: https://github.com/dbjwhs/song
4. Phase 1 punch list completed (P1.1-P1.8)
5. Code review and cleanup performed (2026-01-18)
6. Comprehensive test suite added (2026-01-19) - 90 tests, all passing

## What's Been Implemented (Phase 1 - Complete)

### Core Runtime
- Buffer class with small-buffer optimization (4KB inline storage)
- Pipe class with RAII for Unix pipe management
- Wire protocol with 16-byte fixed headers (magic: 0x534F4E47 "SONG")
- ServiceProcess for spawning and managing child processes
- ServiceManager for managing multiple services with auto-restart
- ServiceRuntime for service-side request handling
- Init handshake with version negotiation
- Method list capability exchange (supports() API)
- Array serialization (encode_array/decode_array)

### Compiler (Partial)
- AST definitions (compiler/ast.hpp)
- Code generator for structs and enums (compiler/codegen.cpp)
- Lexer/parser not yet implemented

### Examples
- Echo service and client (examples/echo/)
- Crash/restart test (examples/crash/)

### Test Suite (Complete)
- GoogleTest framework integrated via CMake FetchContent
- 90 tests across 5 test suites, all passing

## Project Structure

```
song/
├── include/song/           # Public API headers
│   ├── buffer.hpp         # Buffer with SBO, encode/decode functions
│   ├── pipe.hpp           # RAII pipe wrapper
│   ├── wire.hpp           # Wire protocol definitions
│   ├── process.hpp        # ServiceProcess, ServiceConnection
│   ├── manager.hpp        # ServiceManager with auto-restart
│   ├── runtime.hpp        # ServiceRuntime (service-side)
│   ├── types.hpp          # Type aliases (i8-i64, u8-u64, f32, f64)
│   ├── error.hpp          # Error codes and exception types
│   └── song.hpp           # Convenience header
├── src/                    # Implementation files
├── compiler/
│   ├── ast.hpp            # AST node definitions
│   ├── codegen.hpp/.cpp   # Code generator
│   └── main.cpp           # songc entry point
├── examples/
│   ├── echo/              # Echo service example
│   └── crash/             # Auto-restart test
├── test/                   # Automated test suite
│   ├── buffer_test.cpp    # 31 tests
│   ├── wire_test.cpp      # 17 tests
│   ├── pipe_test.cpp      # 15 tests
│   ├── process_test.cpp   # 11 tests
│   └── manager_test.cpp   # 16 tests
├── tooling/                # Pre-commit hook scripts
└── .claude/                # Claude Code configuration
```

## Testing Status - COMPLETE

**Test Framework:** GoogleTest v1.14.0 (via CMake FetchContent)

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| BufferTest | 31 | SBO, move semantics, all primitive encode/decode round-trips, arrays, error handling |
| WireTest | 17 | Header encoding, init messages, method descriptors, message creation, version helpers |
| PipeTest | 15 | Basic I/O, closure semantics, move semantics, timeout functionality |
| ProcessTest | 11 | Spawn, communication, lifecycle, method list, ServiceConnection |
| ManagerTest | 16 | Start/stop, connect, restart, replace, auto-restart on crash, monitor thread |
| **Total** | **90** | **All passing** |

**Running tests:**
```bash
cd build && make -j8 && ./test/song_tests
```

**Key testing features:**
- Tests auto-discovered with `gtest_discover_tests()`
- Integration tests gracefully skip when example services aren't built
- Timing-sensitive tests use polling with timeouts for reliability

## What's Next

### Phase 2 (Compiler)
- P2.1: Lexer implementation
- P2.2: Parser implementation
- P2.3: Semantic resolver
- P2.4: Complete code generation with parsed AST

### Future
- Class support (DAG-style reference types)
- Streaming support
- Property support

## Key Technical Details

**Magic Number:** 0x534F4E47 ("SONG")
**Wire Protocol:** 16-byte fixed header + variable payload
**IPC Mechanism:** Unix pipes (stdin/stdout redirection)
**Service Model:** Fork/exec with init handshake and version negotiation
**Language:** C++20 with modern idioms (RAII, move semantics)

## Design Document

The authoritative design document is at: **../new-world/SONG_DESIGN.md**

## Repository

GitHub: https://github.com/dbjwhs/song
Branch: main

## Notes for Future Sessions

- Pre-commit hooks are active (license headers, trailing newlines)
- No emojis in code or commits
- **New code must include tests** - TDD going forward
- Compiler lexer/parser files were removed (were empty stubs)
- Test suite uses GoogleTest - add tests to appropriate *_test.cpp file
