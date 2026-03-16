// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/pipe.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdexcept>

namespace song {

Pipe::~Pipe() {
    close();
}

Pipe::Pipe(Pipe&& other) noexcept
    : read_fd_(other.read_fd_), write_fd_(other.write_fd_) {
    other.read_fd_ = -1;
    other.write_fd_ = -1;
}

Pipe& Pipe::operator=(Pipe&& other) noexcept {
    if (this != &other) {
        close();
        read_fd_ = other.read_fd_;
        write_fd_ = other.write_fd_;
        other.read_fd_ = -1;
        other.write_fd_ = -1;
    }
    return *this;
}

std::pair<Pipe, Pipe> Pipe::create_pair() {
    int fds[2];
    if (::pipe(fds) == -1) {
        throw std::runtime_error("Failed to create pipe");
    }

    // Create read and write pipes
    Pipe read_pipe(fds[0], -1);
    Pipe write_pipe(-1, fds[1]);

    return {std::move(read_pipe), std::move(write_pipe)};
}

ssize_t Pipe::read(void* buf, size_t len) {
    if (read_fd_ < 0) {
        errno = EBADF;
        return -1;
    }
    ssize_t n;
    do {
        n = ::read(read_fd_, buf, len);
    } while (n < 0 && errno == EINTR);
    return n;
}

ssize_t Pipe::write(const void* buf, size_t len) {
    if (write_fd_ < 0) {
        errno = EBADF;
        return -1;
    }
    ssize_t n;
    do {
        n = ::write(write_fd_, buf, len);
    } while (n < 0 && errno == EINTR);
    return n;
}

ssize_t Pipe::read_timeout(void* buf, size_t len, int timeout_ms) {
    if (read_fd_ < 0) {
        errno = EBADF;
        return -1;
    }

    struct pollfd pfd;
    pfd.fd = read_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret;
    do {
        ret = poll(&pfd, 1, timeout_ms);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        return -1;  // Error
    } else if (ret == 0) {
        errno = ETIMEDOUT;
        return -1;  // Timeout
    }

    ssize_t n;
    do {
        n = ::read(read_fd_, buf, len);
    } while (n < 0 && errno == EINTR);
    return n;
}

void Pipe::close() {
    close_read();
    close_write();
}

void Pipe::close_read() {
    if (read_fd_ >= 0) {
        ::close(read_fd_);
        read_fd_ = -1;
    }
}

void Pipe::close_write() {
    if (write_fd_ >= 0) {
        ::close(write_fd_);
        write_fd_ = -1;
    }
}

} // namespace song
