# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**IMPORTANT**: Always read the design document at `../new-world/SONG_DESIGN.md` when starting work on this project. It contains the complete architecture specification, wire protocol details, IDL grammar, testing strategy, and a prioritized punch list of 55+ implementation tasks.

## Reference Codebases

Song is inspired by two legacy codebases in `../new-world/`. Use these as reference when implementing features:

**Thor** (`../new-world/thor/`) - Service runtime patterns:
- `pitond/transaction.c` - Fork/exec service spawn (lines 319-413), pipe redirection (353-370), service process reuse (522-553)
- `resplib/servicelib.c` - Init confirmation (450-455), version negotiation (477-480), transaction loop (456-486), stream handling (169-185)

**DAG** (`../new-world/dag/`) - IDL compiler and type system:
- `dagger/database.h` - Type definitions (45-69), multi-dimensional arrays (312-313), struct/class types (310-353), field flags (79-86)
- `dagger/wire.h` - Wire operations (70-93), version constants (111-112), properties (50, 75-76)

See SONG_DESIGN.md Sections 15 and 17 for detailed cross-reference validation between these codebases and Song's design.

## Build Commands

```bash
# Build the project
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j

# Run the echo example (from build directory)
cd examples
./echo_client
```

Build targets:
- `libsong` - Static library with core runtime
- `songc` - IDL compiler for .song files
- `song_tests` - Unit test suite (216+ tests)
- `sing_*` - Integration test services and tests (76 tests)

## Code Style

- C++20 with modern idioms (RAII, move semantics, std::span)
- 2-space indentation
- snake_case for variables and functions
- Constants use `kConstantName` pattern (e.g., `kService_Echo`, `kMethod_add`)
- No emojis in code, comments, documentation, or commit messages
- Use plain text markers: [DONE], [TODO], [WIP], [WARNING]
- MIT License headers required on all source files (enforced by pre-commit hook)
- **NO WARNINGS ALLOWED**: Code must compile cleanly with `-Wall -Wextra -Werror`. Use `[[maybe_unused]]` for intentionally unused parameters.

## Architecture

**Song** (**S**ervices **O**ver **N**ative **G**ateways) is a C++ service framework for building process-isolated microservices communicating via Unix pipes.

### Core Components

**Wire Protocol** (`wire.hpp`): 16-byte fixed header + variable payload
- Magic: `0x534F4E47` ("SONG")
- Message types: init, call, result, error, stream, stream_end, ping, shutdown
- Native endianness, no byte swapping

**Buffer** (`buffer.hpp`): Core data container with 4KB small-buffer optimization
- Move-only semantics (non-copyable)
- Encoding/decoding for primitives, strings, bytes, arrays

**IPC Flow**:
1. ServiceManager spawns service via fork/exec
2. Service sends init message on stdout, parent validates
3. Client calls: `[Header(call) + service_id + method_id + args]` → Service
4. Service responds: `[Header(result) + response_data]` → Client

### Key Classes

- `ServiceManager` - Registers services, manages lifecycle, provides connections
- `ServiceProcess` - Fork/exec, stdin/stdout pipe management, init handshake
- `ServiceRuntime` - Service-side main loop, dispatcher registration
- `ServiceConnection` - Client-side RPC proxy with sequence ID matching

### Service Implementation Pattern

```cpp
// Service side
void dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    switch (method_id) {
        case kMethod_foo: /* handle */ break;
    }
}
runtime.register_dispatcher(kService_Id, dispatcher);
runtime.run();  // Never returns

// Client side
mgr.register_service("name", "./executable", version);
auto conn = mgr.connect("name");  // Lazy-starts service
Buffer resp = conn.call(service_id, method_id, request);
```

## Project Status

**Phase 1 complete**: Core runtime, wire protocol, service lifecycle, examples.

**Phase 2 complete**: Full IDL compiler pipeline:
- Lexer with all token types
- Parser for full Song grammar (structs, enums, services, classes)
- Semantic resolver with type checking
- Code generator producing C++ headers (proxy, interface, dispatcher)

**Integration Test Suite** (`sing/`): 4 standalone projects demonstrating Song:
- Calculator - basic RPC, primitives, struct returns
- Stock Ticker - complex types, arrays of structs
- Chat - stateful services, message history, pagination
- Data Copy - binary data, chunked file transfer

**Test coverage**: 292 tests (216 unit + 76 integration)

Not yet implemented:
- Streaming support, property support
- Multi-dimensional arrays in codegen
- Optional types in codegen

## Pre-commit Hooks

Active hooks in `tooling/`:
- `new_line.sh` - Enforces trailing newlines
- `update_header_license.sh` - Adds/verifies MIT license headers
