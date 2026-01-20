# Song: Services Over Native Gateways

**Song** (**S**ervices **O**ver **N**ative **G**ateways) is a modern C++ framework for building high-performance, type-safe service architectures with process isolation and binary wire protocols.

## Features

- **Fast**: Zero-copy where possible, minimal allocations, cache-friendly data layouts
- **Type Safe**: Compile-time verification of message structures and service contracts
- **Process Isolation**: Services run as separate processes, communicating via pipes
- **Minimal Footprint**: Suitable for embedded Linux, IoT devices, resource-constrained systems
- **Clean Architecture**: Modern C++20, no external dependencies beyond POSIX
- **Full IDL Compiler**: Complete pipeline from `.song` files to C++ code

## Architecture

```
+------------------+     +------------------+
|   Application    |     |   Application    |
+------------------+     +------------------+
        |                        |
        v                        v
+------------------+     +------------------+
|  Song Runtime    |<--->|  Song Runtime    |
|  (Host Process)  | pipe|  (Service Proc)  |
+------------------+     +------------------+
        |                        |
        v                        v
+------------------+     +------------------+
|  Generated Code  |     |  Generated Code  |
|  (from .song)    |     |  (from .song)    |
+------------------+     +------------------+
```

## Components

| Component | Purpose |
|-----------|---------|
| **songc** | IDL compiler: `.song` files → C++ headers with proxy, interface, and dispatcher |
| **libsong** | Runtime library: wire protocol, serialization, process management |
| **ServiceManager** | Service lifecycle: fork/exec, auto-restart, connection pooling |

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j

# Run all tests (292 tests)
ctest --output-on-failure
```

## Quick Start

### 1. Define a Service (calculator.song)

```song
namespace calculator;

struct DivResult {
    i32 quotient;
    i32 remainder;
}

service Calculator {
    add(i32 a, i32 b) -> i32;
    subtract(i32 a, i32 b) -> i32;
    multiply(i32 a, i32 b) -> i32;
    divide(i32 a, i32 b) -> DivResult;
}
```

### 2. Generate C++ Code

```bash
./songc calculator.song
# Generates: calculator.hpp
```

### 3. Implement the Service

```cpp
#include "calculator.hpp"

using namespace song::calculator;

class CalculatorImpl : public ICalculator {
public:
    i32 add(i32 a, i32 b) override { return a + b; }
    i32 subtract(i32 a, i32 b) override { return a - b; }
    i32 multiply(i32 a, i32 b) override { return a * b; }
    DivResult divide(i32 a, i32 b) override {
        return {a / b, a % b};
    }
};

static CalculatorImpl g_calc;

void calc_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_Calculator(g_calc, method_id, request, response);
}

int main() {
    ServiceRuntime runtime;
    runtime.register_dispatcher(kService_Calculator, calc_dispatcher);
    runtime.run();
}
```

### 4. Write a Client

```cpp
#include "calculator.hpp"

using namespace song::calculator;

int main() {
    ServiceManager mgr;
    mgr.register_service("calc", "./calculator_service", 1);

    auto conn = mgr.connect("calc");
    CalculatorProxy calc(conn);

    std::cout << "5 + 3 = " << calc.add(5, 3) << "\n";
    std::cout << "10 - 4 = " << calc.subtract(10, 4) << "\n";

    auto div = calc.divide(17, 5);
    std::cout << "17 / 5 = " << div.quotient << " r " << div.remainder << "\n";
}
```

## Integration Test Suite (Sing)

The `sing/` folder contains four complete example projects demonstrating Song's capabilities. Each project includes a `.song` IDL file, generated code, server implementation, and comprehensive integration tests.

See [sing/README.md](sing/README.md) for detailed documentation.

### Projects

| Project | Tests | Description |
|---------|-------|-------------|
| [Calculator](sing/calculator/) | 13 | Basic arithmetic RPC, struct returns, error handling |
| [Stock Ticker](sing/stockticker/) | 15 | Complex structs, arrays of structs, batch queries |
| [Chat](sing/chat/) | 23 | Stateful server, message history, pagination |
| [Data Copy](sing/datacopy/) | 25 | Binary data, chunked file transfer, CRUD operations |

### Running Integration Tests

```bash
cd build

# Run all integration tests
ctest -R sing_

# Run individual test suites
./sing/calculator/sing_calculator_test
./sing/stockticker/sing_stockticker_test
./sing/chat/sing_chat_test
./sing/datacopy/sing_datacopy_test
```

### Example: Running Calculator Tests

```bash
cd build
./sing/calculator/sing_calculator_test

# Output:
[==========] Running 13 tests from 1 test suite.
[ RUN      ] CalculatorTest.Add
[calculator_service] Starting...
[       OK ] CalculatorTest.Add (108 ms)
...
[  PASSED  ] 13 tests.
```

## Current Status

### Phase 1: Core Runtime [COMPLETE]
- Buffer class with 4KB small-buffer optimization
- Primitive type encoders/decoders (i8-i64, u8-u64, f32, f64, string, bytes)
- Array encoders/decoders with nested support
- Wire protocol (16-byte fixed headers)
- ServiceProcess (fork/exec, pipes, init handshake)
- ServiceManager (lifecycle, auto-restart, pooling)
- ServiceRuntime (service-side main loop)
- ServiceConnection (client-side RPC with sequence matching)
- Version negotiation and capability exchange

### Phase 2: IDL Compiler [COMPLETE]
- Lexer for Song IDL (all token types, doc comments)
- Recursive descent parser (structs, enums, services, classes)
- Semantic resolver (type checking, symbol tables)
- Code generator (proxy classes, interfaces, dispatchers)

### Test Coverage
- **216 unit tests**: buffer, wire, pipe, process, manager, lexer, parser, resolver, codegen
- **76 integration tests**: calculator, stockticker, chat, datacopy
- **Total: 292 tests**

### Coming Soon
- Streaming support (bidirectional message streams)
- Property support (get/set with change notifications)
- Multi-dimensional arrays in codegen
- Optional types in codegen

## Project Structure

```
song/
├── CMakeLists.txt
├── include/song/
│   ├── buffer.hpp        # Buffer with small-buffer optimization
│   ├── pipe.hpp          # POSIX pipe wrapper
│   ├── wire.hpp          # Wire protocol definitions
│   ├── process.hpp       # Service process management
│   ├── manager.hpp       # Service lifecycle manager
│   ├── runtime.hpp       # Service-side runtime
│   ├── error.hpp         # Error types
│   └── song.hpp          # Main include
├── src/                  # Implementation files
├── compiler/
│   ├── ast.hpp           # AST node definitions
│   ├── lexer.hpp/cpp     # Tokenizer for Song IDL
│   ├── parser.hpp/cpp    # Recursive descent parser
│   ├── resolver.hpp/cpp  # Semantic analysis
│   ├── codegen.hpp/cpp   # C++ code generation
│   └── main.cpp          # songc entry point
├── test/                 # Unit tests (216 tests)
├── examples/
│   ├── echo/             # Echo service example
│   ├── calculator/       # Generated calculator example
│   └── crash/            # Auto-restart test
└── sing/                 # Integration test suite (76 tests)
    ├── README.md         # Sing documentation
    ├── calculator/       # Basic RPC patterns
    ├── stockticker/      # Complex types, arrays
    ├── chat/             # Stateful services
    └── datacopy/         # Binary data handling
```

## Code Quality

- **NO WARNINGS ALLOWED**: Code compiles with `-Wall -Wextra -Werror`
- MIT License headers on all source files (enforced by pre-commit hook)
- Trailing newlines enforced (pre-commit hook)

## Performance Characteristics

- **Message overhead**: 16 bytes per message (fixed header)
- **Small buffer optimization**: First 4KB inline (no heap allocation)
- **Zero-copy**: Large buffers can be passed by reference
- **Latency**: Sub-microsecond for simple messages on localhost

## Design Principles

### Why Pipes?
- Simpler than sockets (no address management)
- Faster for local communication
- Debugger-friendly (can attach to service process)

### Why Process Isolation?
- Crash containment (service crash doesn't kill host)
- Hot replacement (swap service binary without restart)
- Security (services can run with reduced privileges)
- Memory isolation (service memory leaks don't affect host)

### Why Native Endianness?
- Speed (no byte swapping)
- Target clarity (embedded Linux is little-endian)
- Simplicity (removes entire class of bugs)

## References

- Design document: `../new-world/SONG_DESIGN.md`
- Wire protocol: `include/song/wire.hpp`
- IDL grammar: `compiler/parser.cpp`
- Integration tests: `sing/README.md`

## License

MIT License - Copyright (c) 2026 dbjwhs
