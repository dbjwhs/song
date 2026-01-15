# Song Project Status

## Current State: Phase 1 Complete - Foundation Established

**Last Updated:** 2026-01-14

## What is Song?

Song is a high-performance C++ service framework for building process-isolated microservices that communicate via Unix pipes. It provides zero-copy IPC, type-safe RPC, and automatic service lifecycle management.

## Project History

1. Started as "Bolt" based on design document in ../new-world/SONG_DESIGN.md
2. Renamed to "Song" with complete code transformation
3. Initial implementation completed and committed to GitHub: https://github.com/dbjwhs/song

## What's Been Implemented (Phase 1)

### Core Runtime (100% Complete)
- ✅ Buffer class with small-buffer optimization (4KB inline storage)
- ✅ Pipe class with RAII for Unix pipe management
- ✅ Wire protocol with 16-byte fixed headers (magic: 0x534F4E47 "SONG")
- ✅ ServiceProcess for spawning and managing child processes
- ✅ ServiceManager for managing multiple services
- ✅ ServiceRuntime for service-side request handling
- ✅ Init handshake protocol for service startup

### Compiler Stubs (Structure Only)
- ✅ Lexer stub (compiler/lexer.cpp)
- ✅ Parser stub (compiler/parser.cpp)
- ✅ Resolver stub (compiler/resolver.cpp)
- ✅ Codegen stub (compiler/codegen.cpp)
- ⚠️  These are placeholder files - not yet implemented

### Examples
- ✅ Echo service and client (examples/echo/)
- ✅ Full working demonstration of service spawn, RPC call, and response

### Build System
- ✅ CMake configuration with C++20
- ✅ Optimization flags (-O3 -march=native -flto)
- ✅ Test framework structure (test/CMakeLists.txt)

### Code Quality
- ✅ MIT License headers on all files (Copyright 2026 dbjwhs)
- ✅ Pre-commit hooks for enforcing standards:
  - tooling/new_line.sh - ensures trailing newlines
  - tooling/update_header_license.sh - adds/verifies license headers
- ✅ .claude/preferences - no emoji rule configured

## Project Structure

```
song/
├── include/song/           # Public API headers
│   ├── buffer.hpp         # Buffer with small-buffer optimization
│   ├── pipe.hpp           # RAII pipe wrapper
│   ├── wire.hpp           # Wire protocol definitions
│   ├── process.hpp        # ServiceProcess (spawn & manage)
│   ├── manager.hpp        # ServiceManager (multi-service)
│   ├── runtime.hpp        # ServiceRuntime (service-side)
│   ├── types.hpp          # Type definitions (u8, u16, u32, u64)
│   ├── error.hpp          # Error codes
│   └── song.hpp           # Convenience header
├── src/                    # Implementation files
├── compiler/               # Compiler stubs (not implemented)
├── examples/echo/          # Working echo example
├── test/                   # Test structure (empty)
├── tooling/                # Pre-commit hook scripts
└── .claude/                # Claude Code configuration
```

## Testing Status

- Echo example builds and runs successfully
- No formal test suite yet (Phase 2 task)

## What's Next: The Punch List

See ../new-world/SONG_DESIGN.md Section 19 for the complete 55+ task punch list.

**High Priority Next Steps:**
1. Implement actual tests in test/ directory
2. Add string encoder/decoder to wire protocol
3. Implement list/vector encoding
4. Add proper error handling and logging
5. Begin compiler implementation (lexer first)

## Key Technical Details

**Magic Number:** 0x534F4E47 ("SONG")
**Wire Protocol:** 16-byte fixed header + variable payload
**IPC Mechanism:** Unix pipes (stdin/stdout redirection)
**Service Model:** Fork/exec with init handshake
**Language:** C++20 with modern idioms (RAII, move semantics)

## Design Document

The authoritative design document is at: **../new-world/SONG_DESIGN.md**

This document contains:
- Complete architecture description
- Wire protocol specification
- Service lifecycle details
- Implementation punch list with 55+ bite-sized tasks
- Current implementation status with checkboxes

## Repository

GitHub: https://github.com/dbjwhs/song
Branch: main
Clean working tree: Yes
All changes committed and pushed: Yes

## Notes for Future Sessions

- Pre-commit hooks are active - they run automatically on commit
- All code must have MIT License headers (enforced by hook)
- No emojis in code or commits (per .claude/preferences)
- Compiler stubs exist but are not functional yet
- Focus should be on completing punch list tasks from design doc
- Test infrastructure exists but has no tests yet
