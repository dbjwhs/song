// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <string>
#include <stdexcept>

namespace song {

// =============================================================================
// Error Model Contracts
// =============================================================================
//
// Song uses a mix of return values and exceptions with the following contracts:
//
// Transport layer (PipeTransport, TcpTransport):
//   - send():     throws on fatal I/O error (broken pipe, closed socket)
//   - receive():  returns false on timeout or clean disconnect (EOF)
//                 throws nothing — callers detect disconnect via false return
//   - close():    never throws (best-effort cleanup)
//
// SecureTransport (wraps Transport):
//   - send():     throws SecurityError if payload too large for HMAC
//   - receive():  returns false on timeout/disconnect (same as inner transport)
//                 throws SecurityError on HMAC verification failure (tampered data)
//
// TlsTransport (mbedTLS encrypted transport, requires SONG_HAS_TLS):
//   - constructor: throws SecurityError if PSA init, cert loading, or config fails
//   - handshake(): throws SecurityError on TLS negotiation failure
//   - send():     throws ServiceError on write failure; stamps MsgFlags::encrypted
//   - receive():  returns false on clean disconnect
//                 throws ServiceError on read failure or timeout
//   - close():    sends close_notify, then closes socket (best-effort)
//
// ServiceManager:
//   - connect():  throws ServiceError if service not registered or spawn fails
//   - start():    throws ServiceError if already running or spawn fails
//   - stop():     throws ServiceError if not registered; no-op if already stopped
//
// ServiceConnection:
//   - call():     throws std::runtime_error on I/O error or protocol error
//                 throws ProtocolError on wire-level decoding errors
//                 returns Buffer containing the response payload
//
// ServiceRuntime (service-side message loop):
//   - handle_message()/run(): never let a decode exception escape the loop.
//     A malformed or truncated request (call/create/prop_get/prop_set) is
//     answered with an ErrorCode::decode_error reply; a malformed fire-and-forget
//     message (release/subscribe/unsubscribe) is dropped silently so the reply
//     is not mistaken for the response to the client's next request. A dispatcher
//     that throws yields an error reply (unknown_method) instead of crashing.
//
// Process:
//   - Lifecycle errors throw ServiceError (spawn_failed, service_crashed)
//
// Buffer:
//   - encode_*:   throws std::runtime_error on capacity overflow
//   - decode_*:   throws std::runtime_error on buffer underflow (read past end)
//   - Strings/bytes: throws on size > kMaxStringSize/kMaxBytesSize
//
// ObjectRegistry:
//   - create_object(): throws std::runtime_error if factory not found
//   - get():           returns nullptr for unknown IDs (no throw)
//   - release():       no-op for unknown IDs (no throw)
//   - add_ref():       returns false for unknown IDs (no throw)
//
// =============================================================================

/// Error codes for Song framework
enum class ErrorCode : u16 {
    ok = 0,

    // Protocol errors (1-99)
    invalid_message = 1,
    unknown_service = 2,
    unknown_method = 3,
    version_mismatch = 4,
    magic_mismatch = 5,

    // Serialization errors (100-199)
    decode_error = 100,
    buffer_overflow = 101,
    buffer_underflow = 102,

    // Service errors (200-299)
    service_not_running = 200,
    service_crashed = 201,
    service_timeout = 202,
    spawn_failed = 203,

    // Object errors (300-399)
    object_not_found = 300,
    object_creation_failed = 301,
    property_error = 302,
    invalid_object_type = 303,

    // Application errors (1000+)
    // Reserved for service-specific errors
};

/// Exception types
class ProtocolError : public std::runtime_error {
public:
    explicit ProtocolError(const std::string& msg) : std::runtime_error(msg) {}
};

class ServiceError : public std::runtime_error {
public:
    explicit ServiceError(const std::string& msg) : std::runtime_error(msg) {}
};

class VersionMismatchError : public std::runtime_error {
public:
    explicit VersionMismatchError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace song
