# Song Project Status

## Current State: Phase 2 Complete + Network Distribution Phase 2 Complete

**Last Updated:** 2026-01-20

## What is Song?

Song (Services Over Native Gateways) is a high-performance C++ service framework for building process-isolated microservices that communicate via Unix pipes. It provides zero-copy IPC, type-safe RPC, and automatic service lifecycle management.

## POC Demo Readiness

**Target:** End-to-end demo showing .song IDL → generated C++ → working service

| Milestone | Status | Description |
|-----------|--------|-------------|
| P2.1 Lexer | ✅ Complete | Tokenizes .song files |
| P2.2 Parser | ✅ Complete | Builds AST from tokens |
| P2.3 Semantic Resolver | ✅ Complete | Type checking, validation |
| P2.4 Code Generation | ✅ Complete | Emit C++ from validated AST |
| Phase 3.1 TCP Transport | ✅ Complete | TCP-based service communication |
| Phase 3.2 mDNS Discovery | ✅ Complete | Zero-config service discovery |
| **POC Ready** | **YES** | Write .song → run songc → working service |

**Demo now supports:**
1. Write a simple .song IDL file (e.g., Calculator service)
2. Run `./songc calculator.song` to generate C++ code
3. Implement service logic in generated stubs
4. Run client calling the service over pipes, TCP, or mDNS discovery

## Project History

1. Started as "Bolt" based on design document in ../new-world/SONG_DESIGN.md
2. Renamed to "Song" with complete code transformation
3. Initial implementation completed and committed to GitHub: https://github.com/dbjwhs/song
4. Phase 1 punch list completed (P1.1-P1.8)
5. Code review and cleanup performed (2026-01-18)
6. Comprehensive test suite added (2026-01-19) - 90 tests
7. Lexer implemented (2026-01-19) - P2.1, 32 tests
8. Parser implemented (2026-01-19) - P2.2, 40 tests
9. Resolver implemented (2026-01-19) - P2.3, 38 tests
10. Code generator implemented (2026-01-19) - P2.4, 16 tests
11. TCP Transport implemented (2026-01-20) - Phase 3.1, 32 tests
12. mDNS Discovery implemented (2026-01-20) - Phase 3.2, 22 tests

## What's Been Implemented

### Phase 1 - Runtime (Complete)
- Buffer class with small-buffer optimization (4KB inline storage)
- Pipe class with RAII for Unix pipe management
- Wire protocol with 16-byte fixed headers (magic: 0x534F4E47 "SONG")
- ServiceProcess for spawning and managing child processes
- ServiceManager for managing multiple services with auto-restart
- ServiceRuntime for service-side request handling
- Init handshake with version negotiation
- Method list capability exchange (supports() API)
- Array serialization (encode_array/decode_array)

### Network Distribution - Phase 3.1 TCP Transport (Complete)
- Transport interface (abstract base class)
- PipeTransport wrapping Unix pipe communication
- TcpTransport for TCP socket communication
- TcpListener for accepting TCP connections
- ServiceRuntime `run_tcp()` for TCP-based services
- ServiceManager `register_remote_service()` for explicit TCP endpoints

### Network Distribution - Phase 3.2 Local Discovery (Complete)
- Discovery interface for mDNS service discovery
- DnssdDiscovery implementation for macOS (Bonjour via dns_sd API)
- ServiceRegistration RAII wrapper for service registration
- ServiceRuntime `run_tcp_discoverable()` for mDNS-registered services
- ServiceManager `register_discoverable_service()` for mDNS discovery
- Service type format: `_<type>._song._tcp` (e.g., `_calculator._song._tcp`)

### Phase 2 - Compiler (In Progress)
- AST definitions (compiler/ast.hpp) ✅
- Lexer (compiler/lexer.hpp/.cpp) ✅
  - 37 token types (keywords, identifiers, literals, punctuation)
  - Doc comment preservation (`///`)
  - Line/column tracking for errors
- Parser (compiler/parser.hpp/.cpp) ✅
  - Recursive descent parser
  - All Song IDL constructs (namespace, struct, enum, flags, class, service, error)
  - Type parsing with arrays and optionals
  - Doc comments attached to AST nodes
- Semantic resolver - NOT YET IMPLEMENTED
- Code generator (partial - struct/enum only, needs parser integration)

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
│   ├── lexer.hpp/.cpp     # Tokenizer (37 token types)
│   ├── parser.hpp/.cpp    # Recursive descent parser
│   ├── codegen.hpp/.cpp   # Code generator
│   └── main.cpp           # songc entry point
├── examples/
│   ├── echo/              # Echo service example
│   └── crash/             # Auto-restart test
├── test/                   # Automated test suite (274 tests)
│   ├── buffer_test.cpp    # 31 tests
│   ├── wire_test.cpp      # 17 tests
│   ├── pipe_test.cpp      # 15 tests
│   ├── process_test.cpp   # 11 tests
│   ├── manager_test.cpp   # 16 tests + remote tests
│   ├── transport_test.cpp # TCP/pipe transport tests
│   ├── discovery_test.cpp # mDNS discovery tests
│   ├── lexer_test.cpp     # 32 tests
│   └── parser_test.cpp    # 40 tests
├── tooling/                # Pre-commit hook scripts
└── .claude/                # Claude Code configuration
```

## Testing Status

**Test Framework:** GoogleTest v1.14.0 (via CMake FetchContent)

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| BufferTest | 31 | SBO, move semantics, all primitive encode/decode round-trips, arrays, error handling |
| WireTest | 17 | Header encoding, init messages, method descriptors, message creation, version helpers |
| PipeTest | 15 | Basic I/O, closure semantics, move semantics, timeout functionality |
| ProcessTest | 11 | Spawn, communication, lifecycle, method list, ServiceConnection |
| ManagerTest | 27 | Start/stop, connect, restart, replace, auto-restart, remote services, discoverable services |
| TransportTest | 32 | Pipe transport, TCP transport, TcpListener, remote connections |
| DiscoveryTest | 22 | mDNS registration, discovery, ServiceRegistration RAII, ServiceManager integration |
| LexerTest | 32 | All token types, keywords, identifiers, integers, comments, doc comments, errors |
| ParserTest | 40 | All IDL constructs, types, inheritance, doc comments, error cases |
| ResolverTest | 38 | Symbol tables, type resolution, validation, namespace handling |
| CodegenTest | 16 | Struct/enum generation, service proxies, server interfaces, dispatchers |
| **Total** | **274** | **All passing** |

**Running tests:**
```bash
cd build && make -j8 && ./test/song_tests
```

## What's Next

### Network Distribution (Phase 3) - Remaining
- **Phase 3.3: Security (SharedSecret)**
  - Add HMAC to wire protocol header
  - Shared key in config
  - Validate on receive

- **Phase 3.4: Cross-Subnet (Registry)**
  - Implement Registry service in Song IDL
  - Services register with registry if configured
  - Clients try mDNS first, then registry

### Future Features
- Class support (DAG-style reference types with object IDs)
- Streaming support (`stream` keyword)
- Property support (get/set via RPC)
- Linux Avahi support for mDNS discovery
- Performance benchmarks

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
- Test suite uses GoogleTest - add tests to appropriate *_test.cpp file
- Follow cql patterns for compiler code (m_ prefix, std::optional, string_view)
