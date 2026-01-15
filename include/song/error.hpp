// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <string>
#include <variant>
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

    // Application errors (1000+)
    // Reserved for service-specific errors
};

/// Error class for Song framework
class Error {
    ErrorCode code_;
    std::string message_;

public:
    Error(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
};

/// Result type for operations that can fail
template<typename T>
class Result {
    std::variant<T, Error> value_;

public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : value_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(value_); }

    T& value() {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value of error result");
        }
        return std::get<T>(value_);
    }

    const T& value() const {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value of error result");
        }
        return std::get<T>(value_);
    }

    const Error& error() const {
        if (ok()) {
            throw std::runtime_error("Attempted to access error of success result");
        }
        return std::get<Error>(value_);
    }
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
