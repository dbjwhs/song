// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/transport.hpp"
#include "song/wire.hpp"
#include "song/error.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <cstring>
#include <cerrno>

// MSG_NOSIGNAL prevents SIGPIPE on broken connections (Linux).
// macOS uses the SO_NOSIGPIPE socket option instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace song {

// =============================================================================
// PipeTransport Implementation
// =============================================================================

PipeTransport::PipeTransport(Pipe to_peer, Pipe from_peer)
    : to_peer_(std::move(to_peer))
    , from_peer_(std::move(from_peer)) {}

PipeTransport::PipeTransport(PipeTransport&& other) noexcept
    : to_peer_(std::move(other.to_peer_))
    , from_peer_(std::move(other.from_peer_)) {}

PipeTransport& PipeTransport::operator=(PipeTransport&& other) noexcept {
    if (this != &other) {
        to_peer_ = std::move(other.to_peer_);
        from_peer_ = std::move(other.from_peer_);
    }
    return *this;
}

void PipeTransport::send(const Buffer& msg) {
    if (!is_connected()) {
        throw ServiceError("PipeTransport: not connected");
    }

    size_t offset = 0;
    while (offset < msg.size()) {
        ssize_t written = to_peer_.write(
            reinterpret_cast<const char*>(msg.data()) + offset,
            msg.size() - offset
        );
        if (written < 0) {
            if (errno == EINTR) continue;
            throw ServiceError("PipeTransport: write failed: " + std::string(strerror(errno)));
        }
        offset += static_cast<size_t>(written);
    }
}

bool PipeTransport::receive(Buffer& msg, int timeout_ms) {
    if (!is_connected()) {
        return false;
    }

    msg.reset();

    // Read header (16 bytes)
    std::byte header_buf[16];
    ssize_t n = timeout_ms >= 0
        ? from_peer_.read_timeout(header_buf, 16, timeout_ms)
        : from_peer_.read(header_buf, 16);

    if (n == 0) {
        return false;  // EOF - peer disconnected
    }
    if (n < 0) {
        if (errno == ETIMEDOUT) {
            throw ServiceError("PipeTransport: read timeout");
        }
        throw ServiceError("PipeTransport: read failed: " + std::string(strerror(errno)));
    }
    if (n != 16) {
        throw ProtocolError("PipeTransport: incomplete header received");
    }

    msg.write(header_buf, 16);
    msg.reset_read();

    // Decode header to get payload size
    auto hdr = wire::decode_header(msg);

    // Read payload if present
    if (hdr.payload_size > 0) {
        if (hdr.payload_size > wire::kMaxPayloadSize) {
            throw ProtocolError("PipeTransport: payload size exceeds maximum");
        }

        std::vector<std::byte> payload_buf(hdr.payload_size);
        size_t offset = 0;
        while (offset < hdr.payload_size) {
            n = from_peer_.read(
                payload_buf.data() + offset,
                hdr.payload_size - offset
            );
            if (n < 0) {
                if (errno == EINTR) continue;
                throw ServiceError("PipeTransport: failed to read payload");
            }
            if (n == 0) {
                throw ServiceError("PipeTransport: pipe closed during payload read");
            }
            offset += static_cast<size_t>(n);
        }

        msg.write(payload_buf.data(), hdr.payload_size);
    }

    msg.reset_read();
    return true;
}

void PipeTransport::close() {
    to_peer_.close();
    from_peer_.close();
}

bool PipeTransport::is_connected() const {
    return to_peer_.valid() && from_peer_.valid();
}

// =============================================================================
// TcpTransport Implementation
// =============================================================================

TcpTransport::TcpTransport(int sock, const std::string& peer_addr, u16 peer_port)
    : sock_(sock)
    , peer_addr_(peer_addr)
    , peer_port_(peer_port) {}

TcpTransport::~TcpTransport() {
    close();
}

TcpTransport::TcpTransport(TcpTransport&& other) noexcept
    : sock_(other.sock_)
    , peer_addr_(std::move(other.peer_addr_))
    , peer_port_(other.peer_port_) {
    other.sock_ = -1;
    other.peer_port_ = 0;
}

TcpTransport& TcpTransport::operator=(TcpTransport&& other) noexcept {
    if (this != &other) {
        close();
        sock_ = other.sock_;
        peer_addr_ = std::move(other.peer_addr_);
        peer_port_ = other.peer_port_;
        other.sock_ = -1;
        other.peer_port_ = 0;
    }
    return *this;
}

void TcpTransport::connect(const std::string& host, u16 port, int timeout_ms) {
    // Close existing connection if any
    close();

    // Resolve hostname
    struct addrinfo hints{};
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    std::string port_str = std::to_string(port);
    int err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (err != 0) {
        throw ServiceError("TcpTransport: failed to resolve host '" + host + "': " +
                          gai_strerror(err));
    }

    // Create socket
    sock_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock_ < 0) {
        freeaddrinfo(result);
        throw ServiceError("TcpTransport: failed to create socket: " +
                          std::string(strerror(errno)));
    }

    // Set non-blocking for connect with timeout
    int flags = fcntl(sock_, F_GETFL, 0);
    if (timeout_ms > 0 && flags >= 0) {
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    }

    // Connect
    int connect_result = ::connect(sock_, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (connect_result < 0) {
        if (errno == EINPROGRESS && timeout_ms > 0) {
            // Wait for connection with timeout
            struct pollfd pfd{};
            pfd.fd = sock_;
            pfd.events = POLLOUT;

            int poll_result;
            do {
                poll_result = poll(&pfd, 1, timeout_ms);
            } while (poll_result < 0 && errno == EINTR);

            if (poll_result == 0) {
                close();
                throw ServiceError("TcpTransport: connection timeout to " + host + ":" +
                                  std::to_string(port));
            }
            if (poll_result < 0) {
                close();
                throw ServiceError("TcpTransport: poll failed: " + std::string(strerror(errno)));
            }

            // Check for connection error
            int error = 0;
            socklen_t error_len = sizeof(error);
            getsockopt(sock_, SOL_SOCKET, SO_ERROR, &error, &error_len);
            if (error != 0) {
                close();
                throw ServiceError("TcpTransport: connection failed to " + host + ":" +
                                  std::to_string(port) + ": " + strerror(error));
            }
        } else {
            close();
            throw ServiceError("TcpTransport: connection failed to " + host + ":" +
                              std::to_string(port) + ": " + strerror(errno));
        }
    }

    // Restore blocking mode
    if (timeout_ms > 0 && flags >= 0) {
        fcntl(sock_, F_SETFL, flags);
    }

    // Disable Nagle's algorithm for lower latency
    int nodelay = 1;
    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

#ifdef SO_NOSIGPIPE
    // macOS: prevent SIGPIPE on broken connections (Linux uses MSG_NOSIGNAL per-send)
    int nosigpipe = 1;
    setsockopt(sock_, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif

    peer_addr_ = host;
    peer_port_ = port;
}

void TcpTransport::send(const Buffer& msg) {
    if (sock_ < 0) {
        throw ServiceError("TcpTransport: not connected");
    }

    size_t offset = 0;
    while (offset < msg.size()) {
        ssize_t written = ::send(sock_,
            reinterpret_cast<const char*>(msg.data()) + offset,
            msg.size() - offset,
            MSG_NOSIGNAL
        );
        if (written < 0) {
            if (errno == EINTR) continue;
            throw ServiceError("TcpTransport: send failed: " + std::string(strerror(errno)));
        }
        if (written == 0) {
            throw ServiceError("TcpTransport: connection closed");
        }
        offset += static_cast<size_t>(written);
    }
}

bool TcpTransport::receive(Buffer& msg, int timeout_ms) {
    if (sock_ < 0) {
        return false;
    }

    // Wait for data with timeout
    if (timeout_ms >= 0) {
        struct pollfd pfd{};
        pfd.fd = sock_;
        pfd.events = POLLIN;

        int poll_result;
        do {
            poll_result = poll(&pfd, 1, timeout_ms);
        } while (poll_result < 0 && errno == EINTR);

        if (poll_result == 0) {
            throw ServiceError("TcpTransport: read timeout");
        }
        if (poll_result < 0) {
            throw ServiceError("TcpTransport: poll failed: " + std::string(strerror(errno)));
        }
        // POLLHUP means peer closed, but data may still be available
        // Only return false if there's an error and no data to read
        if ((pfd.revents & POLLERR) && !(pfd.revents & POLLIN)) {
            return false;  // Error with no data
        }
        // If we only have POLLHUP but no POLLIN, try to read anyway
        // recv() will return 0 if there's no data (proper EOF detection)
    }

    msg.reset();

    // Read header (16 bytes)
    std::byte header_buf[16];
    size_t header_offset = 0;
    while (header_offset < 16) {
        ssize_t n = recv(sock_, header_buf + header_offset, 16 - header_offset, 0);
        if (n == 0) {
            return false;  // Connection closed
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            throw ServiceError("TcpTransport: recv failed: " + std::string(strerror(errno)));
        }
        header_offset += static_cast<size_t>(n);
    }

    msg.write(header_buf, 16);
    msg.reset_read();

    // Decode header to get payload size
    auto hdr = wire::decode_header(msg);

    // Read payload if present
    if (hdr.payload_size > 0) {
        if (hdr.payload_size > wire::kMaxPayloadSize) {
            throw ProtocolError("TcpTransport: payload size exceeds maximum");
        }

        std::vector<std::byte> payload_buf(hdr.payload_size);
        size_t offset = 0;
        while (offset < hdr.payload_size) {
            ssize_t n = recv(sock_, payload_buf.data() + offset,
                           hdr.payload_size - offset, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                throw ServiceError("TcpTransport: failed to read payload");
            }
            if (n == 0) {
                throw ServiceError("TcpTransport: connection closed during payload read");
            }
            offset += static_cast<size_t>(n);
        }

        msg.write(payload_buf.data(), hdr.payload_size);
    }

    msg.reset_read();
    return true;
}

void TcpTransport::close() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    peer_addr_.clear();
    peer_port_ = 0;
}

bool TcpTransport::is_connected() const {
    return sock_ >= 0;
}

// =============================================================================
// TcpListener Implementation
// =============================================================================

TcpListener::~TcpListener() {
    close();
}

TcpListener::TcpListener(TcpListener&& other) noexcept
    : sock_(other.sock_)
    , port_(other.port_) {
    other.sock_ = -1;
    other.port_ = 0;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
    if (this != &other) {
        close();
        sock_ = other.sock_;
        port_ = other.port_;
        other.sock_ = -1;
        other.port_ = 0;
    }
    return *this;
}

void TcpListener::listen(u16 port, int backlog) {
    // Close existing listener if any
    close();

    // Create socket
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ < 0) {
        throw ServiceError("TcpListener: failed to create socket: " +
                          std::string(strerror(errno)));
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Bind to port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close();
        throw ServiceError("TcpListener: failed to bind to port " + std::to_string(port) +
                          ": " + strerror(errno));
    }

    // Get actual port if we bound to 0
    if (port == 0) {
        socklen_t addr_len = sizeof(addr);
        getsockname(sock_, reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
        port_ = ntohs(addr.sin_port);
    } else {
        port_ = port;
    }

    // Start listening
    if (::listen(sock_, backlog) < 0) {
        close();
        throw ServiceError("TcpListener: failed to listen: " + std::string(strerror(errno)));
    }
}

std::unique_ptr<TcpTransport> TcpListener::accept(int timeout_ms) {
    if (sock_ < 0) {
        throw ServiceError("TcpListener: not listening");
    }

    // Wait for connection with timeout
    if (timeout_ms >= 0) {
        struct pollfd pfd{};
        pfd.fd = sock_;
        pfd.events = POLLIN;

        int poll_result;
        do {
            poll_result = poll(&pfd, 1, timeout_ms);
        } while (poll_result < 0 && errno == EINTR);

        if (poll_result == 0) {
            return nullptr;  // Timeout, no connection
        }
        if (poll_result < 0) {
            throw ServiceError("TcpListener: poll failed: " + std::string(strerror(errno)));
        }
    }

    // Accept connection
    struct sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = ::accept(sock_,
        reinterpret_cast<struct sockaddr*>(&client_addr),
        &client_addr_len);

    if (client_sock < 0) {
        throw ServiceError("TcpListener: accept failed: " + std::string(strerror(errno)));
    }

    // Disable Nagle's algorithm for lower latency
    int nodelay = 1;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

#ifdef SO_NOSIGPIPE
    int nosigpipe = 1;
    setsockopt(client_sock, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif

    // Get peer address for logging
    char addr_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_buf, sizeof(addr_buf));
    std::string peer_addr = addr_buf;
    u16 peer_port = ntohs(client_addr.sin_port);

    return std::make_unique<TcpTransport>(client_sock, peer_addr, peer_port);
}

void TcpListener::close() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    port_ = 0;
}

} // namespace song
