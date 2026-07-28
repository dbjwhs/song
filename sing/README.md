# Sing - Song Integration Tests

Sing is the integration test suite for the Song high-performance service framework. Each project is a standalone application that demonstrates Song's capabilities and validates correctness through real process-to-process communication.

## Directory Structure

```
sing/
├── ipc/                    # Local pipe-based communication tests
│   ├── calculator/         # Basic arithmetic RPC
│   ├── stockticker/        # Complex types and arrays
│   ├── chat/               # Stateful server interactions
│   └── datacopy/           # Binary data and file transfer
│
├── network/                # TCP and network communication tests
│   ├── tcp_calculator/     # Calculator over TCP (explicit host:port)
│   ├── discovery/          # mDNS zero-config service discovery
│   ├── secure/             # HMAC-SHA256 authenticated communication
│   └── backup/             # Comprehensive backup agent (all features)
│
└── README.md
```

---

## IPC Tests (`ipc/`)

These tests use local pipe-based communication via `ServiceProcess::spawn()`.

### 1. Calculator (`ipc/calculator/`)

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

### 2. Stock Ticker (`ipc/stockticker/`)

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

### 3. Chat (`ipc/chat/`)

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

### 4. Data Copy (`ipc/datacopy/`)

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

## Network Tests (`network/`)

These tests use TCP communication for remote service access.

### 1. TCP Calculator (`network/tcp_calculator/`)

Same calculator service as IPC, but communicating over TCP.

**Features:**
- `register_remote_service()` for explicit TCP endpoints
- `run_tcp(port)` for TCP service listening
- Connection and RPC over TCP sockets

**What Is Covered:**
- TCP transport functionality
- ServiceManager remote service registration
- Network-based RPC calls
- Connection stability over multiple calls

---

### 2. Discovery (`network/discovery/`)

mDNS-based zero-config service discovery.

**Features:**
- `run_tcp_discoverable()` for mDNS service registration
- `register_discoverable_service()` for automatic discovery
- Service type format: `_<type>-song._tcp`

**What Is Covered:**
- mDNS service registration (macOS Bonjour)
- Service discovery and resolution
- Automatic connection to discovered services

**Note:** These tests are skipped on non-macOS platforms and may require mDNS to be available.

---

### 3. Secure (`network/secure/`)

HMAC-SHA256 authenticated communication.

**Features:**
- `SecureTransport` wrapper for any transport
- Shared secret authentication
- Message integrity verification

**What Is Covered:**
- HMAC tag computation and verification
- Authentication with matching keys
- Rejection with mismatched keys
- Secure RPC over TCP

---

### 4. Backup Agent (`network/backup/`)

Comprehensive backup agent exercising all Song features in one project.

**Features:**
- Filesystem-backed file listing, upload, download, deletion
- Streaming file transfer via `StreamWriter`/`StreamReader` (64KB chunks)
- HMAC-SHA256 authentication with `--key` flag
- Multi-client support via `run_tcp_multi()` and custom secure accept loop
- Property notifications for upload progress via `SubscriptionRegistry`
- mDNS discovery with `--discover` flag
- Incremental backup via `changes_since(timestamp)`
- Path traversal protection

**What Is Covered:**
- Generated proxy/interface/dispatcher from `backup.song` IDL
- Hand-written streaming dispatcher (dual service IDs)
- Secure multi-client loop using public `handle_message()` API
- `subscribe_property()` / `poll_notifications()` for progress fan-out
- End-to-end incremental backup workflow
- Cross-feature integration (streaming + security + multi-client)

**Usage:**
```bash
# Start the agent
./sing_network_backup_service --port 19000 --root /tmp/backup

# With HMAC auth
./sing_network_backup_service --port 19000 --root /tmp/backup --key "my-secret-key"

# With mDNS discovery
./sing_network_backup_service --port 0 --root /tmp/backup --discover

# Connect with client
./sing_network_backup_client localhost 19000
./sing_network_backup_client localhost 19000 --key "my-secret-key"
```

---

## Building

```bash
# Build all sing projects
cd song/build
cmake ..
make sing_all

# Or build categories
make sing_ipc_all       # All IPC tests
make sing_network_all   # All network tests

# Or build individual projects
make sing_ipc_calculator
make sing_network_tcp_calculator
make sing_network_secure
make sing_network_backup
```

## Running Tests

```bash
# Run all sing tests
ctest -R "Calculator\|Chat\|DataCopy\|Stock\|TcpCalculator\|Discovery\|Secure\|BackupAgent"

# Run IPC tests only
ctest -R "^(Calculator|Chat|DataCopy|StockTicker)Test\."

# Run network tests only
ctest -R "^(TcpCalculator|Discovery|SecureTransport|BackupAgent|SecureBackup|DiscoverableBackup)Test\."

# Run individual test suites
./sing/ipc/calculator/sing_ipc_calculator_test
./sing/network/tcp_calculator/sing_network_tcp_calculator_test
./sing/network/secure/sing_network_secure_test
./sing/network/backup/sing_network_backup_test
```

## Test Counts

| Category | Tests |
|----------|-------|
| IPC Calculator | 13 |
| IPC StockTicker | 15 |
| IPC Chat | 23 |
| IPC DataCopy | 25 |
| Network TCP Calculator | 9 |
| Network Discovery | 4 |
| Network Secure | 5 |
| Network Backup Agent | 44 |
| **Total** | **138** |

## Requirements

- Song runtime library
- Song compiler (songc)
- GoogleTest (fetched automatically)
- C++20 compiler
- macOS for mDNS discovery tests (others skipped on Linux)
