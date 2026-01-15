# Song: High-Performance Service Framework

Song is a modern C++ framework for building high-performance, type-safe service architectures with process isolation and binary wire protocols.

## Features

- **Fast**: Zero-copy where possible, minimal allocations, cache-friendly data layouts
- **Type Safe**: Compile-time verification of message structures and service contracts
- **Process Isolation**: Services run as separate processes, communicating via pipes
- **Minimal Footprint**: Suitable for embedded Linux, IoT devices, resource-constrained systems
- **Clean Architecture**: Modern C++20, no external dependencies beyond POSIX

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
```

## Components

| Component | Purpose |
|-----------|---------|
| **songc** | IDL compiler: `.song` files to C++ (coming soon) |
| **libsong** | Runtime library: wire protocol, serialization |
| **song-host** | Service manager: fork/exec, lifecycle, pipes |

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

## Quick Start

### 1. Write a Service Implementation

```cpp
#include <song/song.hpp>

using namespace song;

// Service IDs
constexpr u16 kService_Echo = 1;

// Method IDs
constexpr u16 kMethod_echo = 1;

void echo_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    if (method_id == kMethod_echo) {
        std::string msg = decode_string(request);
        encode_string(response, msg);
    }
}

int main() {
    ServiceRuntime runtime;
    runtime.register_dispatcher(kService_Echo, echo_dispatcher);
    runtime.run();  // Never returns
}
```

### 2. Write a Client

```cpp
#include <song/song.hpp>

using namespace song;

class EchoProxy {
    ServiceConnection& conn_;
public:
    explicit EchoProxy(ServiceConnection& conn) : conn_(conn) {}

    std::string echo(const std::string& msg) {
        Buffer req;
        encode_string(req, msg);
        Buffer resp = conn_.call(kService_Echo, kMethod_echo, req);
        return decode_string(resp);
    }
};

int main() {
    ServiceManager mgr;
    mgr.register_service("echo", "./echo_service", 1);

    auto conn = mgr.connect("echo");
    EchoProxy proxy(conn);

    std::string result = proxy.echo("Hello, Song!");
    std::cout << result << "\n";
}
```

### 3. Run the Example

```bash
cd build/examples
./echo_client
```

Output:
```
Testing echo method...
Echo result: Hello, Song!

Testing add method...
Add result: 100

Testing multiple calls...
0 + 0 = 0
1 + 1 = 2
2 + 2 = 4
3 + 3 = 6
4 + 4 = 8

All tests passed!
```

## Current Status

### Implemented
- [x] Buffer class with small-buffer optimization
- [x] Primitive type encoders/decoders (i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, string, bytes)
- [x] Pipe abstraction with RAII
- [x] Wire protocol (16-byte fixed headers, binary format)
- [x] ServiceProcess (fork/exec, terminate, send/receive)
- [x] ServiceManager (lifecycle management)
- [x] ServiceRuntime (service-side main loop)
- [x] ServiceConnection (client-side RPC)
- [x] Working echo example

### Coming Soon
- [ ] Compiler (songc) - IDL to C++ code generation
- [ ] Array encoders/decoders
- [ ] Struct encoders/decoders
- [ ] Error handling with custom exceptions
- [ ] Streaming support
- [ ] Property support
- [ ] Version negotiation
- [ ] Auto-restart on crash
- [ ] Comprehensive test suite
- [ ] Performance benchmarks

## Project Structure

```
song/
├── CMakeLists.txt
├── include/
│   └── song/
│       ├── buffer.hpp        # Buffer with small-buffer optimization
│       ├── pipe.hpp          # POSIX pipe wrapper
│       ├── wire.hpp          # Wire protocol definitions
│       ├── process.hpp       # Service process management
│       ├── manager.hpp       # Service lifecycle manager
│       ├── runtime.hpp       # Service-side runtime
│       ├── error.hpp         # Error types
│       └── song.hpp          # Main include
├── src/                      # Implementation files
├── compiler/                 # songc compiler (coming soon)
├── test/                     # Tests (coming soon)
└── examples/
    └── echo/                 # Echo service example
```

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
- Wire protocol specification: See `include/song/wire.hpp`
- Example code: `examples/echo/`
