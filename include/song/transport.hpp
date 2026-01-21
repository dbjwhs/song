// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include <memory>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace song {

/// Abstract transport interface for Song message communication
/// Implementations provide the underlying mechanism (pipes, TCP, etc.)
class Transport {
public:
    virtual ~Transport() = default;

    /// Send a complete message
    /// @throws ServiceError on failure
    virtual void send(const Buffer& msg) = 0;

    /// Receive a complete message
    /// @param msg Buffer to receive into (reset before use)
    /// @param timeout_ms Timeout in milliseconds (-1 for blocking)
    /// @return true if message received, false on EOF/disconnect
    /// @throws ServiceError on read failure, ProtocolError on invalid data
    virtual bool receive(Buffer& msg, int timeout_ms = -1) = 0;

    /// Close the transport
    virtual void close() = 0;

    /// Check if transport is connected
    virtual bool is_connected() const = 0;

    /// Get transport type description (for logging)
    virtual const char* type_name() const = 0;
};

/// Pipe-based transport for local inter-process communication
class PipeTransport : public Transport {
    Pipe to_peer_;
    Pipe from_peer_;

public:
    /// Create from existing pipe pair
    /// @param to_peer Pipe to write to peer (takes ownership)
    /// @param from_peer Pipe to read from peer (takes ownership)
    PipeTransport(Pipe to_peer, Pipe from_peer);

    // Non-copyable, movable
    PipeTransport(const PipeTransport&) = delete;
    PipeTransport& operator=(const PipeTransport&) = delete;
    PipeTransport(PipeTransport&& other) noexcept;
    PipeTransport& operator=(PipeTransport&& other) noexcept;

    void send(const Buffer& msg) override;
    bool receive(Buffer& msg, int timeout_ms = -1) override;
    void close() override;
    bool is_connected() const override;
    const char* type_name() const override { return "pipe"; }

    /// Get read file descriptor (for select/poll)
    int read_fd() const { return from_peer_.read_fd(); }

    /// Get write file descriptor
    int write_fd() const { return to_peer_.write_fd(); }
};

/// TCP socket transport for network communication
class TcpTransport : public Transport {
    int sock_ = -1;
    std::string peer_addr_;
    u16 peer_port_ = 0;

public:
    TcpTransport() = default;

    /// Create from existing connected socket
    /// @param sock Connected socket file descriptor (takes ownership)
    /// @param peer_addr Peer address string (for logging)
    /// @param peer_port Peer port (for logging)
    TcpTransport(int sock, const std::string& peer_addr, u16 peer_port);

    ~TcpTransport() override;

    // Non-copyable, movable
    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;
    TcpTransport(TcpTransport&& other) noexcept;
    TcpTransport& operator=(TcpTransport&& other) noexcept;

    /// Connect to a remote host
    /// @param host Hostname or IP address
    /// @param port Port number
    /// @param timeout_ms Connection timeout (-1 for system default)
    /// @throws ServiceError on failure
    void connect(const std::string& host, u16 port, int timeout_ms = 5000);

    void send(const Buffer& msg) override;
    bool receive(Buffer& msg, int timeout_ms = -1) override;
    void close() override;
    bool is_connected() const override;
    const char* type_name() const override { return "tcp"; }

    /// Get socket file descriptor (for select/poll)
    int socket_fd() const { return sock_; }

    /// Get peer address
    const std::string& peer_address() const { return peer_addr_; }

    /// Get peer port
    u16 peer_port() const { return peer_port_; }
};

/// TCP listener for accepting incoming connections
class TcpListener {
    int sock_ = -1;
    u16 port_ = 0;

public:
    TcpListener() = default;
    ~TcpListener();

    // Non-copyable, movable
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;

    /// Bind to a port and start listening
    /// @param port Port to listen on (0 for OS-assigned ephemeral port)
    /// @param backlog Listen queue size
    /// @throws ServiceError on failure
    void listen(u16 port = 0, int backlog = 128);

    /// Accept an incoming connection
    /// @param timeout_ms Timeout in milliseconds (-1 for blocking)
    /// @return Connected TcpTransport, or nullptr on timeout
    /// @throws ServiceError on failure
    std::unique_ptr<TcpTransport> accept(int timeout_ms = -1);

    /// Close the listener
    void close();

    /// Check if listener is active
    bool is_listening() const { return sock_ >= 0; }

    /// Get the bound port (useful when port was 0)
    u16 bound_port() const { return port_; }

    /// Get socket file descriptor (for select/poll)
    int socket_fd() const { return sock_; }
};

} // namespace song
