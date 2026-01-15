// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include "wire.hpp"
#include <sys/types.h>
#include <string>

namespace song {

/// Service process handle
/// Manages a spawned service process and its pipes
class ServiceProcess {
    pid_t pid_ = -1;
    Pipe to_service_;
    Pipe from_service_;
    bool reusable_ = true;

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

private:
    ServiceProcess(pid_t pid, Pipe to_service, Pipe from_service);
};

/// Connection to a service
/// Used by client proxy to make RPC calls
class ServiceConnection {
    ServiceProcess* proc_;
    u32 next_seq_ = 1;

public:
    explicit ServiceConnection(ServiceProcess* proc);

    /// Make a synchronous call to a service method
    /// Returns the result buffer
    Buffer call(u16 service_id, u16 method_id, const Buffer& args);

    /// Make a one-way call (no response expected)
    void call_oneway(u16 service_id, u16 method_id, const Buffer& args);

    /// Get the service process
    ServiceProcess* process() { return proc_; }
};

} // namespace song
