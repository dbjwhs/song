// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <sys/types.h>
#include <utility>

namespace song {

/// Pipe wrapper with RAII semantics
/// Handles Unix pipe file descriptors safely
class Pipe {
    int read_fd_ = -1;
    int write_fd_ = -1;

public:
    Pipe() = default;
    ~Pipe();

    // Non-copyable, movable
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&& other) noexcept;
    Pipe& operator=(Pipe&& other) noexcept;

    /// Create a pair of connected pipes
    /// Returns (read_pipe, write_pipe)
    static std::pair<Pipe, Pipe> create_pair();

    /// Create a pipe from existing file descriptors
    Pipe(int read_fd, int write_fd) : read_fd_(read_fd), write_fd_(write_fd) {}

    /// Read from pipe
    ssize_t read(void* buf, size_t len);

    /// Write to pipe
    ssize_t write(const void* buf, size_t len);

    /// Read with timeout (milliseconds)
    /// Returns -1 on timeout, sets errno to ETIMEDOUT
    ssize_t read_timeout(void* buf, size_t len, int timeout_ms);

    /// Get read file descriptor
    int read_fd() const { return read_fd_; }

    /// Get write file descriptor
    int write_fd() const { return write_fd_; }

    /// Check if pipe is valid
    bool valid() const { return read_fd_ >= 0 || write_fd_ >= 0; }

    /// Close the pipe
    void close();

    /// Close only the read end
    void close_read();

    /// Close only the write end
    void close_write();
};

} // namespace song
