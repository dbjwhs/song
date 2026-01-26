// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <string>
#include <stdexcept>

namespace song {

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
