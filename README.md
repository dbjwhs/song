# Song: Services Over Native Gateways

**Song** (**S**ervices **O**ver **N**ative **G**ateways) is a modern C++ framework for building high-performance, type-safe service architectures with process isolation and binary wire protocols.

## Features

- **Fast**: Zero-copy where possible, minimal allocations, cache-friendly data layouts
- **Type Safe**: Compile-time verification of message structures and service contracts
- **Process Isolation**: Services run as separate processes, communicating via pipes
- **Network Distribution**: TCP transport with zero-config mDNS service discovery
- **Security**: HMAC-SHA256 authentication for shared-secret protection
- **Minimal Footprint**: Suitable for embedded Linux, IoT devices, resource-constrained systems
- **Clean Architecture**: Modern C++20, no external dependencies beyond POSIX
- **Full IDL Compiler**: Complete pipeline from `.song` files to C++ code
- **Pluggable Logging**: Handler-based logging system with colored console output

## Architecture

```
+------------------+           +------------------+
|   Application    |           |   Application    |
+------------------+           +------------------+
        |                              |
        v                              v
+------------------+           +------------------+
|  Song Runtime    |<-- pipe ->|  Song Runtime    |  (local)
|  (Host Process)  |<-- TCP -->|  (Service Proc)  |  (remote)
+------------------+           +------------------+
        |                              |
        v                              v
+------------------+           +------------------+
|  Generated Code  |           |  Generated Code  |
|  (from .song)    |           |  (from .song)    |
+------------------+           +------------------+
```

## Components

| Component | Purpose |
|-----------|---------|
| **songc** | IDL compiler: `.song` files → C++ headers with proxy, interface, and dispatcher |
| **libsong** | Runtime library: wire protocol, serialization, process management, TCP transport, security |
| **ServiceManager** | Service lifecycle: fork/exec, auto-restart, TCP connections, mDNS discovery |

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j

# Run all tests (490 tests)
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
    runtime.run();  // Pipe mode (stdin/stdout)
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

## Network Distribution

Song supports three communication modes with a unified API:

### Local Services (Pipes)
```cpp
ServiceManager mgr;
mgr.register_service("calc", "./calculator_service", 1);
auto conn = mgr.connect("calc");  // Spawns process, uses pipes
```

### Remote Services (TCP with explicit address)
```cpp
ServiceManager mgr;
mgr.register_remote_service("calc", "192.168.1.50", 12345, 1);
auto conn = mgr.connect("calc");  // TCP connection to host:port
```

### Discoverable Services (TCP with mDNS)
```cpp
ServiceManager mgr;
mgr.register_discoverable_service("calc", "calculator", 1);
auto conn = mgr.connect("calc");  // Discovers via mDNS, then TCP
```

### Running a TCP Service

```cpp
// Service that listens on TCP (explicit port)
int main() {
    ServiceRuntime runtime;
    runtime.register_dispatcher(kService_Calculator, calc_dispatcher);
    runtime.run_tcp(12345);  // Listen on port 12345
}

// Service with mDNS registration (zero-config discovery)
int main() {
    ServiceRuntime runtime;
    runtime.register_dispatcher(kService_Calculator, calc_dispatcher);
    runtime.run_tcp_discoverable(0, "MyCalculator", "calculator");
    // Registers as "_calculator._song._tcp" on local network
    // Port 0 = ephemeral port (OS-assigned)
}
```

## Security

Song provides HMAC-SHA256 authentication for services communicating over untrusted networks.

### Shared Secret Authentication

```cpp
// Configure security with a shared key
SecurityConfig security("my-secret-key-32-bytes-long!!!!");

// Client: wrap connection in SecureTransport
auto tcp = std::make_unique<TcpTransport>();
tcp->connect("192.168.1.50", 12345, 5000);
SecureTransport secure(std::move(tcp), security);

// Server: wrap accepted connection
auto client_tcp = listener.accept();
SecureTransport secure_client(std::move(client_tcp), security);
```

### How It Works

- HMAC-SHA256 computed over each message (header + payload)
- Tag embedded in wire protocol (transparent to application)
- Constant-time comparison prevents timing attacks
- Mismatched keys throw `SecurityError`

### Platform Support

| Platform | Crypto Library |
|----------|---------------|
| macOS    | CommonCrypto  |
| Linux    | OpenSSL       |

## Cross-Subnet Discovery (Registry)

When mDNS can't reach services (different VLANs, cloud environments), use a registry service.

### Running the Registry

```bash
# Start registry on a known host (default port 9999)
./registry_service --port 9999
```

### Client Configuration

```cpp
ServiceManager mgr;

// Configure registry for cross-subnet discovery
mgr.set_registry("registry.example.com", 9999);

// Register a discoverable service (will use registry as fallback)
mgr.register_discoverable_service("calc", "calculator", 1);

// Connect - tries mDNS first, then registry
auto conn = mgr.connect("calc");
```

### Service Registration

Services can register with the registry programmatically:

```cpp
RegistryClient registry("registry.example.com", 9999);

ServiceInfo info;
info.name = "my-calculator";
info.host = "10.0.0.50";
info.port = 12345;

registry.register_service(info);

// Keep alive with periodic heartbeats
while (running) {
    registry.heartbeat("my-calculator");
    sleep(30);
}
```

## Integration Test Suite (Sing)

The `sing/` folder contains complete example projects demonstrating Song's capabilities. Each project includes a `.song` IDL file, generated code, server implementation, and comprehensive integration tests.

See [sing/README.md](sing/README.md) for detailed documentation.

### IPC Tests (Local Pipes)

| Project | Tests | Description |
|---------|-------|-------------|
| [Calculator](sing/ipc/calculator/) | 13 | Basic arithmetic RPC, struct returns, error handling |
| [Stock Ticker](sing/ipc/stockticker/) | 15 | Complex structs, arrays of structs, batch queries |
| [Chat](sing/ipc/chat/) | 23 | Stateful server, message history, pagination |
| [Data Copy](sing/ipc/datacopy/) | 25 | Binary data, chunked file transfer, CRUD operations |

### Network Tests (TCP)

| Project | Tests | Description |
|---------|-------|-------------|
| [TCP Calculator](sing/network/tcp_calculator/) | 9 | Calculator service over TCP sockets |
| [Discovery](sing/network/discovery/) | 4 | mDNS zero-config service discovery |
| [Secure](sing/network/secure/) | 5 | HMAC-SHA256 authenticated communication |

### Running Integration Tests

```bash
cd build

# Run all integration tests
ctest -R sing_

# Run individual test suites
./sing/ipc/calculator/sing_ipc_calculator_test
./sing/network/tcp_calculator/sing_network_tcp_calculator_test
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

### Phase 3: Network Distribution [COMPLETE]
- **TCP Transport**: TcpTransport and TcpListener for socket communication
- **Transport Abstraction**: Unified Transport interface for pipes and TCP
- **Remote Services**: `register_remote_service()` for explicit TCP endpoints
- **mDNS Discovery**: Zero-config service discovery using native Bonjour API (macOS)
- **Discoverable Services**: `register_discoverable_service()` for mDNS lookup
- **Service Type Format**: `_<type>._song._tcp` (e.g., `_calculator._song._tcp`)
- **HMAC Security**: SecureTransport wrapper with HMAC-SHA256 authentication
- **Platform Crypto**: CommonCrypto (macOS), OpenSSL (Linux)
- **Registry Service**: Cross-subnet service discovery via central registry
- **Registry Fallback**: ServiceManager tries mDNS first, then registry

### Phase 4: Class Support [COMPLETE]
- **Object Base Class**: Reference-counted objects with identity (negative IDs like DAG)
- **ObjectRegistry**: Server-side object lifecycle management
- **Object Creation**: `MSG_CREATE` message type with constructor dispatch
- **Object Release**: `MSG_RELEASE` for reference count decrement (fire-and-forget)
- **Property Access**: `MSG_PROP_GET` and `MSG_PROP_SET` for remote property access
- **Object Methods**: Method dispatch on object instances
- **ServiceConnection**: `create_object()`, `release_object()`, `get_property()`, `set_property()`, `call_object()`
- **ServiceRuntime**: Handles all object message types with factory registration
- **Class Code Generation**: Proxy classes (client), skeleton base classes (server) with DAG-style macro pattern

### Phase 5: Logging and Introspection [COMPLETE]
- **Pluggable Logging**: Handler-based system with Log::debug/info/warn/error/fatal
- **Built-in Handlers**: Colored console, null, callback
- **Source Location**: Automatic file/line/function capture
- **Runtime Introspection**: service_count(), method_count(), get_service_ids(), has_service()

### Test Coverage
- **396 unit tests**: buffer, wire, pipe, process, manager, transport, discovery, security, registry, object, logging, runtime, lexer, parser, resolver, codegen
- **94 integration tests**: IPC (calculator, stockticker, chat, datacopy) + Network (tcp_calculator, discovery, secure)
- **Total: 490 tests**

### Coming Soon
- Streaming support (bidirectional message streams)
- Property change notifications
- Linux Avahi support for mDNS discovery

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
│   ├── transport.hpp     # Transport interface (pipes, TCP)
│   ├── discovery.hpp     # mDNS service discovery
│   ├── security.hpp      # HMAC authentication
│   ├── registry.hpp      # Cross-subnet registry
│   ├── object.hpp        # Object base class and registry
│   ├── logging.hpp       # Pluggable logging system
│   ├── error.hpp         # Error types
│   └── song.hpp          # Main include
├── src/
│   ├── buffer.cpp
│   ├── pipe.cpp
│   ├── wire.cpp
│   ├── process.cpp
│   ├── manager.cpp
│   ├── runtime.cpp
│   ├── transport.cpp     # TCP/pipe transport implementations
│   ├── discovery.cpp     # mDNS implementation (macOS Bonjour)
│   ├── security.cpp      # HMAC (CommonCrypto/OpenSSL)
│   ├── registry.cpp      # Registry client/server
│   ├── object.cpp        # ObjectRegistry implementation
│   └── logging.cpp       # Logging implementation
├── compiler/
│   ├── ast.hpp           # AST node definitions
│   ├── lexer.hpp/cpp     # Tokenizer for Song IDL
│   ├── parser.hpp/cpp    # Recursive descent parser
│   ├── resolver.hpp/cpp  # Semantic analysis
│   ├── codegen.hpp/cpp   # C++ code generation
│   └── main.cpp          # songc entry point
├── test/                 # Unit tests (396 tests)
├── examples/
│   ├── echo/             # Echo service example
│   ├── calculator/       # Generated calculator example
│   ├── crash/            # Auto-restart test
│   └── registry/         # Registry service
└── sing/                 # Integration test suite (94 tests)
    ├── README.md         # Sing documentation
    ├── ipc/              # Local pipe-based tests
    │   ├── calculator/   # Basic RPC patterns
    │   ├── stockticker/  # Complex types, arrays
    │   ├── chat/         # Stateful services
    │   └── datacopy/     # Binary data handling
    └── network/          # TCP network tests
        ├── tcp_calculator/  # TCP transport
        ├── discovery/       # mDNS discovery
        └── secure/          # HMAC authentication
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

### Why TCP + mDNS?
- Transparent API (same code for local and remote)
- Zero configuration (mDNS "just works" on LAN)
- No heavy dependencies (native OS APIs only)

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
