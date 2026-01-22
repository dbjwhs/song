// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "wire.hpp"
#include "object.hpp"
#include <unordered_map>
#include <functional>
#include <vector>
#include <string>

namespace song {

// Forward declarations
class Transport;
class TcpListener;
class Discovery;

/// Service runtime
/// Used by service implementations to handle incoming requests
/// Supports both pipe-based (stdin/stdout) and TCP connections
class ServiceRuntime {
    std::unordered_map<u16, std::function<void(u16, Buffer&, Buffer&)>> dispatchers_;
    std::vector<wire::MethodDescriptor> methods_;
    ObjectRegistry object_registry_;

public:
    /// Register a service dispatcher
    /// service_id: unique identifier for this service
    /// dispatcher: function that takes (method_id, request, response)
    void register_dispatcher(u16 service_id,
                           std::function<void(u16, Buffer&, Buffer&)> dispatcher);

    /// Register a method for capability exchange
    /// service_id: service this method belongs to
    /// method_id: method ID within the service
    /// flags: method flags (optional, streaming, oneway)
    void register_method(u16 service_id, u16 method_id,
                        wire::MethodFlags flags = wire::MethodFlags::none);

    /// Register an object factory for creating instances of a class type
    /// type_id: unique identifier for this class type
    /// factory: function that creates instances given constructor_id and args
    void register_factory(u32 type_id, ObjectFactory factory);

    /// Get the object registry (for advanced use cases)
    ObjectRegistry& objects() { return object_registry_; }

    /// Main service loop - reads from stdin, writes to stdout (pipe mode)
    /// Never returns (until shutdown message received)
    [[noreturn]] void run();

    /// TCP service loop - listens on port, accepts one client at a time
    /// @param port Port to listen on (0 for OS-assigned ephemeral port)
    /// Note: This method blocks and handles clients sequentially
    [[noreturn]] void run_tcp(u16 port);

    /// Run TCP service loop with an existing listener
    /// Useful for when you need to know the port before starting
    [[noreturn]] void run_tcp(TcpListener& listener);

    /// TCP service loop with mDNS registration
    /// Registers the service for local network discovery
    /// @param port Port to listen on (0 for OS-assigned ephemeral port)
    /// @param name Service instance name (e.g., "MyCalculator")
    /// @param type Service type (e.g., "calculator")
    /// Note: Uses ephemeral port when port=0 for automatic port allocation
    [[noreturn]] void run_tcp_discoverable(u16 port,
                                          const std::string& name,
                                          const std::string& type);

    /// Run TCP service loop with mDNS registration using existing listener
    /// @param listener Existing TcpListener (must already be listening)
    /// @param name Service instance name
    /// @param type Service type
    [[noreturn]] void run_tcp_discoverable(TcpListener& listener,
                                          const std::string& name,
                                          const std::string& type);

private:
    void send_init_confirmation_fd(int fd);
    void send_init_confirmation_transport(Transport& transport);
    void handle_message(const wire::Header& hdr, Buffer& payload,
                       Transport& transport);
    void handle_message_fd(const wire::Header& hdr, Buffer& payload, int write_fd);
    void client_loop(Transport& transport);
};

} // namespace song
