// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include "wire.hpp"
#include <sys/types.h>
#include <string>
#include <vector>
#include <memory>

namespace song {

// Forward declaration for Transport
class Transport;

/// Service process handle
/// Manages a spawned service process and its pipes
class ServiceProcess {
    pid_t pid_ = -1;
    Pipe to_service_;
    Pipe from_service_;
    bool reusable_ = true;
    u16 negotiated_version_ = 0;  // Negotiated protocol version (0 = not negotiated)
    std::vector<wire::MethodDescriptor> methods_;  // Methods supported by service

public:
    ServiceProcess() = default;
    ~ServiceProcess();

    // Non-copyable, movable
    ServiceProcess(const ServiceProcess&) = delete;
    ServiceProcess& operator=(const ServiceProcess&) = delete;
    ServiceProcess(ServiceProcess&& other) noexcept;
    ServiceProcess& operator=(ServiceProcess&& other) noexcept;

    /// Spawn a service process
    static ServiceProcess spawn(const char* executable);

    /// Perform init handshake with service
    void init_handshake();

    /// Check if process is alive
    bool alive() const;

    /// Check if process is available for reuse
    bool available() const;

    /// Mark as reusable/busy
    void set_reusable(bool r) { reusable_ = r; }

    /// Terminate the process (SIGTERM, then SIGKILL if needed)
    void terminate();

    /// Send a message to the service
    void send(const Buffer& msg);

    /// Receive a message from the service
    /// Returns false on EOF (service died)
    bool receive(Buffer& msg, int timeout_ms = -1);

    /// Get process ID
    pid_t pid() const { return pid_; }

    /// Get negotiated protocol version
    /// Returns 0 if not yet negotiated
    u16 negotiated_version() const { return negotiated_version_; }

    /// Get methods supported by the service
    const std::vector<wire::MethodDescriptor>& methods() const { return methods_; }

private:
    ServiceProcess(pid_t pid, Pipe to_service, Pipe from_service);
};

/// Connection to a service
/// Used by client proxy to make RPC calls
/// Supports both local (pipe-based) and remote (TCP-based) services
class ServiceConnection {
    ServiceProcess* proc_ = nullptr;  // For local services (owns the process)
    Transport* transport_ = nullptr;  // For remote services (borrows transport)
    std::unique_ptr<Transport> owned_transport_;  // Owns transport if created internally
    u32 next_seq_ = 1;
    std::vector<wire::MethodDescriptor> methods_;  // Cached method list

public:
    /// Default constructor
    ServiceConnection() = default;

    /// Destructor (defined in cpp due to unique_ptr<Transport>)
    ~ServiceConnection();

    /// Create connection to a local service process
    explicit ServiceConnection(ServiceProcess* proc);

    /// Create connection using an existing transport (borrows, does not own)
    explicit ServiceConnection(Transport* transport);

    /// Create connection using an owned transport
    explicit ServiceConnection(std::unique_ptr<Transport> transport);

    // Non-copyable, movable
    ServiceConnection(const ServiceConnection&) = delete;
    ServiceConnection& operator=(const ServiceConnection&) = delete;
    ServiceConnection(ServiceConnection&& other) noexcept;
    ServiceConnection& operator=(ServiceConnection&& other) noexcept;

    /// Make a synchronous call to a service method
    /// Returns the result buffer
    Buffer call(u16 service_id, u16 method_id, const Buffer& args);

    /// Make a one-way call (no response expected)
    void call_oneway(u16 service_id, u16 method_id, const Buffer& args);

    // =========================================================================
    // Object Operations (for class support)
    // =========================================================================

    /// Create a new object on the server
    /// @param type_id Class type identifier
    /// @param constructor_id Constructor to use (0 = default)
    /// @param args Serialized constructor arguments
    /// @return Object reference (type_id, object_id)
    wire::ObjectRef create_object(u32 type_id, u16 constructor_id, const Buffer& args);

    /// Release an object reference
    /// Fire-and-forget: server decrements reference count
    /// @param type_id Object type
    /// @param object_id Object instance ID
    void release_object(u32 type_id, i32 object_id);

    /// Get a property value from an object
    /// @param type_id Object type
    /// @param object_id Object instance ID
    /// @param property_id Property identifier
    /// @return Buffer containing serialized property value
    Buffer get_property(u32 type_id, i32 object_id, u16 property_id);

    /// Set a property value on an object
    /// @param type_id Object type
    /// @param object_id Object instance ID
    /// @param property_id Property identifier
    /// @param value Serialized new value
    /// @return Buffer containing actual stored value
    Buffer set_property(u32 type_id, i32 object_id, u16 property_id, const Buffer& value);

    /// Call a method on an object
    /// @param type_id Object type
    /// @param object_id Object instance ID
    /// @param method_id Method identifier
    /// @param args Serialized method arguments
    /// @return Buffer containing method result
    Buffer call_object(u32 type_id, i32 object_id, u16 method_id, const Buffer& args);

    /// Check if service supports a specific method
    /// Returns true if the method was declared in the service's init message
    bool supports(u16 service_id, u16 method_id) const;

    /// Get the service process (nullptr for remote connections)
    ServiceProcess* process() { return proc_; }

    /// Get the transport (never nullptr for valid connection)
    Transport* transport() { return transport_; }

    /// Check if this is a remote (TCP) connection
    bool is_remote() const { return proc_ == nullptr && transport_ != nullptr; }

    /// Perform init handshake (for remote connections)
    /// Local connections get this automatically from ServiceProcess
    void init_handshake();

    /// Get methods supported by the service
    const std::vector<wire::MethodDescriptor>& methods() const;
};

} // namespace song
