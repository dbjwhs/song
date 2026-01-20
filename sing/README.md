# Sing - Song Integration Tests

Sing is the integration test suite for the Song high-performance service framework. Each project is a standalone application that demonstrates Song's capabilities and validates correctness through real process-to-process communication.

## Projects

### 1. Calculator (`calculator/`)

A basic arithmetic service demonstrating fundamental Song RPC patterns.

**Features:**
- Basic operations: add, subtract, multiply, divide
- Struct return types (quotient + remainder for division)
- Error handling for division by zero

**What Is Covered:**
- Service definition and code generation
- Client proxy usage
- Server interface implementation
- Request/response encoding/decoding
- Process spawning and IPC
- Error propagation

---

### 2. Stock Ticker (`stockticker/`)

A financial data service demonstrating request/response patterns for market data.

**Features:**
- Get current price for a symbol
- Get batch quotes for multiple symbols
- Price history queries
- Market status checks

**What Is Covered:**
- Complex struct types (Quote with multiple fields)
- Array parameters and return types
- Optional fields (for unavailable data)
- Multiple service methods
- Timestamp handling

---

### 3. Chat (`chat/`)

A simple messaging service demonstrating stateful server interactions.

**Features:**
- Send messages to server
- Retrieve message history
- Clear history
- Message metadata (timestamp, sender)

**What Is Covered:**
- Stateful service implementation
- String and array handling
- Message ordering and history
- Multiple sequential calls
- Server-side state management

---

### 4. Data Copy (`datacopy/`)

A file transfer service demonstrating binary data handling.

**Features:**
- Send named file chunks to server
- Server acknowledges receipt with checksum
- Retrieve stored files
- List stored files
- Delete files

**What Is Covered:**
- Binary data (bytes) handling
- Chunked data transfer
- Checksum validation
- CRUD-like operations
- Large payload handling

---

## Building

Each project can be built standalone or as part of the main Song build:

```bash
# Build all sing projects
cd song/build
cmake ..
make sing_all

# Or build individual projects
make sing_calculator
make sing_stockticker
make sing_chat
make sing_datacopy
```

## Running Tests

```bash
# Run all sing tests
ctest -R sing_

# Or run individual test suites
./sing/calculator/calculator_test
./sing/stockticker/stockticker_test
./sing/chat/chat_test
./sing/datacopy/datacopy_test
```

## Project Structure

Each project follows the same structure:

```
<project>/
├── <project>.song      # IDL definition
├── <project>.hpp       # Generated header (by songc)
├── <project>_service.cpp   # Server implementation
├── <project>_test.cpp      # Integration tests
└── CMakeLists.txt      # Build configuration
```

## Requirements

- Song runtime library
- Song compiler (songc)
- GoogleTest (fetched automatically)
- C++20 compiler
