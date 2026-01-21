// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "transport.hpp"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace song {

/// Service information for registry
struct ServiceInfo {
    std::string name;    // Service name (e.g., "calculator")
    std::string host;    // Host address (IP or hostname)
    u16 port{0};         // Service port

    // Encode to buffer
    void encode(Buffer& buf) const;

    // Decode from buffer
    static ServiceInfo decode(Buffer& buf);

    bool is_valid() const { return !name.empty() && !host.empty() && port != 0; }
};

/// Service/Method IDs for Registry service
constexpr u16 kService_Registry = 0xFFFE;  // Reserved service ID
constexpr u16 kMethod_Register = 1;
constexpr u16 kMethod_Unregister = 2;
constexpr u16 kMethod_Discover = 3;
constexpr u16 kMethod_ListAll = 4;
constexpr u16 kMethod_Heartbeat = 5;

/// Registry client - connects to a remote registry service
class RegistryClient {
    std::unique_ptr<Transport> transport_;
    std::mutex mutex_;
    u32 sequence_id_{0};
    int timeout_ms_;

    // Cached registry address for reconnection
    std::string registry_host_;
    u16 registry_port_{0};

public:
    /// Create a disconnected client
    RegistryClient() = default;

    /// Create client connected to registry at host:port
    /// @param host Registry host address
    /// @param port Registry port
    /// @param timeout_ms Connection and operation timeout
    RegistryClient(const std::string& host, u16 port, int timeout_ms = 5000);

    ~RegistryClient() = default;

    // Non-copyable, non-movable (due to mutex)
    RegistryClient(const RegistryClient&) = delete;
    RegistryClient& operator=(const RegistryClient&) = delete;
    RegistryClient(RegistryClient&&) = delete;
    RegistryClient& operator=(RegistryClient&&) = delete;

    /// Connect to registry at host:port
    /// @param host Registry host address
    /// @param port Registry port
    /// @param timeout_ms Connection timeout
    /// @return true if connected
    bool connect(const std::string& host, u16 port, int timeout_ms = 5000);

    /// Check if connected to registry
    bool is_connected() const;

    /// Close connection
    void close();

    /// Register a service with the registry
    /// @param info Service information
    /// @return true if registered successfully
    bool register_service(const ServiceInfo& info);

    /// Unregister a service from the registry
    /// @param name Service name to unregister
    /// @return true if unregistered successfully
    bool unregister_service(const std::string& name);

    /// Discover a service by name
    /// @param name Service name to find
    /// @return Service info if found, empty ServiceInfo if not
    ServiceInfo discover(const std::string& name);

    /// List all registered services
    /// @return Vector of all service infos
    std::vector<ServiceInfo> list_all();

    /// Send heartbeat for a service
    /// @param name Service name
    /// @return true if service is still registered
    bool heartbeat(const std::string& name);

private:
    /// Call a registry method
    /// @param method_id Method to call
    /// @param request Request buffer
    /// @param response Response buffer (output)
    /// @return true if call succeeded
    bool call(u16 method_id, const Buffer& request, Buffer& response);

    /// Try to reconnect if disconnected
    bool ensure_connected();
};

/// Registry server interface (implemented by registry service)
class IRegistry {
public:
    virtual ~IRegistry() = default;

    /// Register a service
    virtual bool register_service(const ServiceInfo& info) = 0;

    /// Unregister a service by name
    virtual bool unregister_service(const std::string& name) = 0;

    /// Discover a service by name
    virtual ServiceInfo discover(const std::string& name) = 0;

    /// List all registered services
    virtual std::vector<ServiceInfo> list_all() = 0;

    /// Heartbeat for a service (keep-alive)
    virtual bool heartbeat(const std::string& name) = 0;
};

/// Dispatcher for registry service
void dispatch_Registry(IRegistry& impl, u16 method_id, Buffer& request, Buffer& response);

/// In-memory registry implementation (for registry service)
class MemoryRegistry : public IRegistry {
    struct Entry {
        ServiceInfo info;
        std::chrono::steady_clock::time_point last_heartbeat;
    };

    std::unordered_map<std::string, Entry> services_;
    mutable std::mutex mutex_;

    // How long before a service is considered stale (no heartbeat)
    std::chrono::seconds expiry_time_{60};

public:
    MemoryRegistry() = default;
    explicit MemoryRegistry(std::chrono::seconds expiry_time)
        : expiry_time_(expiry_time) {}

    bool register_service(const ServiceInfo& info) override;
    bool unregister_service(const std::string& name) override;
    ServiceInfo discover(const std::string& name) override;
    std::vector<ServiceInfo> list_all() override;
    bool heartbeat(const std::string& name) override;

    /// Remove stale services (called periodically)
    void cleanup_stale();

    /// Get number of registered services
    size_t size() const;

    /// Set expiry time for services
    void set_expiry_time(std::chrono::seconds seconds) { expiry_time_ = seconds; }
};

} // namespace song
