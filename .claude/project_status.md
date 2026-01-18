# Song Project Status

## Current State: Phase 1 Complete, Tests Needed

**Last Updated:** 2026-01-18

## PRIORITY FOR NEXT SESSION

**Automated tests are the #1 priority.** The codebase has grown without test coverage. Before adding new features, we need to establish a test suite and adopt TDD going forward.

## What is Song?

Song (Services Over Native Gateways) is a high-performance C++ service framework for building process-isolated microservices that communicate via Unix pipes. It provides zero-copy IPC, type-safe RPC, and automatic service lifecycle management.

## Project History

1. Started as "Bolt" based on design document in ../new-world/SONG_DESIGN.md
2. Renamed to "Song" with complete code transformation
3. Initial implementation completed and committed to GitHub: https://github.com/dbjwhs/song
4. Phase 1 punch list completed (P1.1-P1.8)
5. Code review and cleanup performed (2026-01-18)

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
├── test/                   # TEST SUITE NEEDED
├── tooling/                # Pre-commit hook scripts
└── .claude/                # Claude Code configuration
```

## Testing Status - NEEDS ATTENTION

**Current state:** No automated tests. Only manual testing via examples.

**Test plan for next session:**
1. Choose test framework (GoogleTest or Catch2)
2. Add unit tests for Buffer (encode/decode round-trips, SBO, move semantics)
3. Add unit tests for wire protocol (header encoding, message creation)
4. Add integration tests for ServiceProcess/ServiceManager
5. Establish rule: new code requires tests

## What's Next

### Immediate Priority
- Automated test suite

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
- **New code must include tests**
- Compiler lexer/parser files were removed (were empty stubs)
