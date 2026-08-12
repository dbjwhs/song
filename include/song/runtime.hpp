// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "wire.hpp"
#include "object.hpp"
#include "stream.hpp"
#include "subscription.hpp"
#include <array>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
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
    std::unordered_map<u16, StreamDispatcher> stream_dispatchers_;
    std::vector<wire::MethodDescriptor> methods_;
    ObjectRegistry object_registry_;
    SubscriptionRegistry sub_registry_;
    u32 local_capabilities_ = 0;
    std::array<std::string, 8> extension_names_{};
    u8 next_extension_slot_ = 0;

public:
    /// Wraps each accepted TCP transport before it is served
    using TransportWrapper =
        std::function<std::unique_ptr<Transport>(std::unique_ptr<Transport>)>;

    /// Register a service dispatcher
    /// service_id: unique identifier for this service
    /// dispatcher: function that takes (method_id, request, response)
    void register_dispatcher(u16 service_id,
                           std::function<void(u16, Buffer&, Buffer&)> dispatcher);

    /// Set a wrapper applied to every transport accepted by run_tcp,
    /// run_tcp_multi, and run_tcp_discoverable, before any bytes are
    /// exchanged. This is how a service layers security onto its TCP
    /// serving loops, e.g. HMAC:
    ///   runtime.set_transport_wrapper([key](auto t) -> std::unique_ptr<Transport> {
    ///       return std::make_unique<SecureTransport>(std::move(t), SecurityConfig(key));
    ///   });
    /// The wrapper takes ownership and returns the transport to serve.
    void set_transport_wrapper(TransportWrapper wrapper) {
        transport_wrapper_ = std::move(wrapper);
    }

    /// Register a streaming dispatcher
    /// service_id: unique identifier for this service
    /// dispatcher: function that takes (method_id, request, stream_writer)
    void register_stream_dispatcher(u16 service_id, StreamDispatcher dispatcher);

    // -------------------------------------------------------------------------
    // Capability Management
    // -------------------------------------------------------------------------

    /// Set a capability flag (advertised to peers in init message)
    void set_capability(wire::Capability cap);

    /// Clear a capability flag
    void clear_capability(wire::Capability cap);

    /// Get current local capabilities (auto-detected + manually set)
    u32 capabilities() const;

    /// Register a named dynamic extension (claims one of ext_0..ext_7)
    /// @return The Capability bit assigned
    /// @throws std::runtime_error if all 8 extension slots are full
    wire::Capability register_extension(const std::string& name);

    /// Check if a named extension is registered locally
    bool has_extension(const std::string& name) const;

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

    // -------------------------------------------------------------------------
    // Introspection
    // -------------------------------------------------------------------------

    /// Get number of registered services (dispatchers)
    size_t service_count() const;

    /// Get total number of registered methods
    size_t method_count() const;

    /// Get list of registered service IDs
    std::vector<u16> get_service_ids() const;

    /// Get list of registered methods (service_id, method_id pairs)
    const std::vector<wire::MethodDescriptor>& get_methods() const;

    /// Check if a specific service is registered
    bool has_service(u16 service_id) const;

    /// Check if a specific method is registered
    bool has_method(u16 service_id, u16 method_id) const;

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
    /// Returns (deregistering from mDNS) on SIGTERM/SIGINT, so a service that
    /// restarts does not leave a stale record advertising a dead port.
    void run_tcp_discoverable(u16 port,
                             const std::string& name,
                             const std::string& type);

    /// Run TCP service loop with mDNS registration using existing listener
    /// @param listener Existing TcpListener (must already be listening)
    /// @param name Service instance name
    /// @param type Service type
    /// Returns (deregistering from mDNS) on SIGTERM/SIGINT.
    void run_tcp_discoverable(TcpListener& listener,
                             const std::string& name,
                             const std::string& type);

    /// Multi-client TCP service loop (thread-per-client)
    /// Accepts multiple concurrent clients, each in its own thread.
    /// Property notifications fan out to all subscribed clients.
    /// @param port Port to listen on (0 for OS-assigned)
    [[noreturn]] void run_tcp_multi(u16 port);

    /// Multi-client TCP service loop with existing listener
    [[noreturn]] void run_tcp_multi(TcpListener& listener);

    /// Get the subscription registry (for external notification triggers)
    SubscriptionRegistry& subscriptions() { return sub_registry_; }

    /// Property subscription key: (object_id, property_id)
    using SubscriptionKey = std::pair<i32, u16>;
    using SubscriptionSet = std::set<SubscriptionKey>;

    /// Send init confirmation to a Transport-based client
    void send_init_confirmation_transport(Transport& transport);

    /// Handle a single message from a client (dispatch to registered handlers)
    void handle_message(const wire::Header& hdr, Buffer& payload,
                       Transport& transport,
                       std::unordered_set<i32>& tracked_objects);

    /// Handle a single message with property subscription tracking
    void handle_message(const wire::Header& hdr, Buffer& payload,
                       Transport& transport,
                       std::unordered_set<i32>& tracked_objects,
                       SubscriptionSet& subscriptions);

private:
    /// Release all objects tracked by a connection (prevents leaks on crash)
    void release_connection_objects(std::unordered_set<i32>& tracked);

    TransportWrapper transport_wrapper_;

    /// Apply the transport wrapper (identity when unset). Defined in the
    /// .cpp: Transport is incomplete here.
    std::unique_ptr<Transport> wrap_transport(std::unique_ptr<Transport> t);

    void send_init_confirmation_fd(int fd);
    void handle_message_fd(const wire::Header& hdr, Buffer& payload,
                          int write_fd,
                          std::unordered_set<i32>& tracked_objects);
    void client_loop(Transport& transport);
};

} // namespace song
