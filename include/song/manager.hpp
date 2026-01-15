// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "process.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

namespace song {

/// Service manager
/// Manages the lifecycle of service processes
class ServiceManager {
    struct ServiceEntry {
        std::string name;
        std::string executable;
        u32 version;
        std::unique_ptr<ServiceProcess> process;
        bool auto_restart = false;
    };

    std::vector<ServiceEntry> services_;

public:
    /// Register a service (does not start it)
    void register_service(std::string_view name,
                         std::string_view executable,
                         u32 version = 1);

    /// Start a service process
    ServiceProcess* start(std::string_view name);

    /// Stop a service process
    void stop(std::string_view name);

    /// Restart a service with the same executable
    void restart(std::string_view name);

    /// Replace a service with a new executable (hot swap)
    void replace(std::string_view name, std::string_view new_executable);

    /// Get connection to a service (starts if not running)
    ServiceConnection connect(std::string_view name);

    /// Check if service is alive
    bool is_alive(std::string_view name) const;

    /// Enable/disable auto-restart on crash
    void set_auto_restart(std::string_view name, bool enable);

private:
    ServiceEntry* find_service(std::string_view name);
    const ServiceEntry* find_service(std::string_view name) const;
};

} // namespace song
