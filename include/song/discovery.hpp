// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>

namespace song {

/// Discovered service information
struct DiscoveredService {
    std::string name;       // Service instance name
    std::string host;       // Hostname or IP address
    u16 port;               // Port number
    std::string type;       // Service type (e.g., "_calculator._song._tcp")
    std::string domain;     // Domain (usually "local.")
};

/// Service discovery interface
/// Provides mDNS/DNS-SD service registration and discovery
class Discovery {
public:
    virtual ~Discovery() = default;

    /// Register a service for discovery
    /// @param name Service instance name (e.g., "MyCalculator")
    /// @param type Service type without domain (e.g., "calculator")
    /// @param port Port the service is listening on
    /// @return true if registration initiated successfully
    virtual bool register_service(const std::string& name,
                                 const std::string& type,
                                 u16 port) = 0;

    /// Unregister a previously registered service
    virtual void unregister_service() = 0;

    /// Check if a service is currently registered
    virtual bool is_registered() const = 0;

    /// Discover services of a given type
    /// @param type Service type to discover (e.g., "calculator")
    /// @param timeout Maximum time to wait for discovery
    /// @return List of discovered services
    virtual std::vector<DiscoveredService> discover(
        const std::string& type,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) = 0;

    /// Discover a single service by name and type
    /// @param name Service instance name
    /// @param type Service type
    /// @param timeout Maximum time to wait
    /// @return Discovered service, or empty optional if not found
    virtual std::optional<DiscoveredService> discover_one(
        const std::string& name,
        const std::string& type,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) = 0;

    /// Check if discovery is available on this platform
    virtual bool is_available() const = 0;

    /// Get the full service type string for a Song service
    /// @param type Short type name (e.g., "calculator")
    /// @return Full service type (e.g., "_calculator._song._tcp")
    static std::string make_service_type(const std::string& type) {
        return "_" + type + "._song._tcp";
    }
};

/// Create a platform-appropriate discovery implementation
/// Returns nullptr if discovery is not available on this platform
std::unique_ptr<Discovery> create_discovery();

/// RAII wrapper for service registration
/// Automatically unregisters when destroyed
class ServiceRegistration {
    Discovery* discovery_;
    bool registered_ = false;

public:
    ServiceRegistration() : discovery_(nullptr) {}

    ServiceRegistration(Discovery& discovery,
                       const std::string& name,
                       const std::string& type,
                       u16 port)
        : discovery_(&discovery) {
        registered_ = discovery_->register_service(name, type, port);
    }

    ~ServiceRegistration() {
        if (registered_ && discovery_) {
            discovery_->unregister_service();
        }
    }

    // Non-copyable
    ServiceRegistration(const ServiceRegistration&) = delete;
    ServiceRegistration& operator=(const ServiceRegistration&) = delete;

    // Movable
    ServiceRegistration(ServiceRegistration&& other) noexcept
        : discovery_(other.discovery_)
        , registered_(other.registered_) {
        other.discovery_ = nullptr;
        other.registered_ = false;
    }

    ServiceRegistration& operator=(ServiceRegistration&& other) noexcept {
        if (this != &other) {
            if (registered_ && discovery_) {
                discovery_->unregister_service();
            }
            discovery_ = other.discovery_;
            registered_ = other.registered_;
            other.discovery_ = nullptr;
            other.registered_ = false;
        }
        return *this;
    }

    bool is_registered() const { return registered_; }
};

} // namespace song
