// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include "wire.hpp"
#include "stream.hpp"
#include <sys/types.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace song {

// Forward declaration for Transport
class Transport;

/// Service process handle
/// Manages a spawned service process and its pipes
class ServiceProcess {
    // Mutable so the const alive() can clear the pid the instant waitpid() reaps
    // the child. Otherwise a later terminate() would signal a stale -- possibly
    // OS-reused -- pid.
    mutable pid_t pid_ = -1;
    Pipe to_service_;
    Pipe from_service_;
    bool reusable_ = true;
    u16 negotiated_version_ = 0;       // Negotiated protocol version (0 = not negotiated)
    u32 peer_capabilities_ = 0;        // Raw capabilities the peer advertised
    u32 negotiated_capabilities_ = 0;  // Intersection of our and peer capabilities
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

    /// Get raw capabilities the peer advertised
    u32 peer_capabilities() const { return peer_capabilities_; }

    /// Get negotiated capabilities (intersection of ours and peer's)
    u32 negotiated_capabilities() const { return negotiated_capabilities_; }

    /// Check if a specific capability was negotiated
    bool has_capability(wire::Capability cap) const {
        return wire::has_capability(static_cast<wire::Capability>(negotiated_capabilities_), cap);
    }

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
    u16 negotiated_version_ = 0;       // For remote connections (local delegates to proc_)
    u32 peer_capabilities_ = 0;        // Raw capabilities the peer advertised
    u32 negotiated_capabilities_ = 0;  // Intersection of our and peer capabilities
    u32 local_capabilities_ = 0;       // Our capabilities (set before init_handshake)
    std::vector<wire::MethodDescriptor> methods_;  // Cached method list
    // Property notification callbacks: key = (object_id, property_id)
    std::map<std::pair<i32, u16>, std::function<void(const Buffer&)>> prop_callbacks_;
    // Bounds on what call_streaming() will buffer from an untrusted service.
    size_t max_stream_chunks_ = 1u << 20;               // 1,048,576 chunks
    size_t max_stream_bytes_ = 512u * 1024u * 1024u;    // 512 MB cumulative

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

    /// Make a streaming call to a service method
    /// Sends the call, then collects MSG_STREAM chunks until MSG_STREAM_END.
    /// Aborts with ServiceError if the stream exceeds the configured chunk-count
    /// or cumulative-byte cap, so an untrusted service cannot exhaust client
    /// memory by streaming without end.
    /// @return StreamReader containing all received chunks
    StreamReader call_streaming(u16 service_id, u16 method_id, const Buffer& args);

    /// Bound how much a single call_streaming() will buffer before aborting.
    /// Defaults: 1,048,576 chunks and 512 MB cumulative. Raise for legitimately
    /// large transfers; a value of 0 is treated as 1 (at least one chunk/byte).
    void set_max_stream_chunks(size_t limit) { max_stream_chunks_ = limit ? limit : 1; }
    void set_max_stream_bytes(size_t limit) { max_stream_bytes_ = limit ? limit : 1; }
    size_t max_stream_chunks() const { return max_stream_chunks_; }
    size_t max_stream_bytes() const { return max_stream_bytes_; }

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

    // =========================================================================
    // Property Notifications
    // =========================================================================

    /// Subscribe to property change notifications
    /// @param type_id Object type
    /// @param object_id Object instance ID
    /// @param property_id Property to watch
    /// @param callback Called when the property value changes
    void subscribe_property(u32 type_id, i32 object_id, u16 property_id,
                           std::function<void(const Buffer&)> callback);

    /// Unsubscribe from property change notifications
    void unsubscribe_property(u32 type_id, i32 object_id, u16 property_id);

    /// Dispatch any pending property notifications (call periodically or after receive)
    /// Checks for MSG_PROP_NOTIFY messages and invokes registered callbacks.
    /// @param timeout_ms How long to wait for notifications (-1 = non-blocking check)
    void poll_notifications(int timeout_ms = 0);

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

    /// Set local capabilities before calling init_handshake()
    /// These are sent to the peer in the init_ack message
    void set_local_capabilities(u32 caps) { local_capabilities_ = caps; }

    /// Get negotiated protocol version
    u16 negotiated_version() const;

    /// Get raw capabilities the peer advertised
    u32 peer_capabilities() const;

    /// Get negotiated capabilities (intersection of local and peer)
    u32 negotiated_capabilities() const;

    /// Check if a specific capability was negotiated
    bool has_capability(wire::Capability cap) const;

    /// Get methods supported by the service
    const std::vector<wire::MethodDescriptor>& methods() const;
};

} // namespace song
