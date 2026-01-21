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
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

namespace song {

/// Service manager
/// Manages the lifecycle of service processes and remote service connections
class ServiceManager {
    struct ServiceEntry {
        std::string name;
        std::string executable;  // For local services
        std::string host;        // For remote services
        u16 port = 0;            // For remote services
        std::string service_type; // For discoverable services (mDNS type)
        bool is_remote = false;  // True for TCP-based remote services
        bool is_discoverable = false;  // True for mDNS-discoverable services
        u32 version;
        std::unique_ptr<ServiceProcess> process;
        bool auto_restart = false;
        int restart_count = 0;      // Number of times restarted
        int max_restarts = 5;       // Max restarts before giving up (0 = unlimited)
    };

    std::vector<ServiceEntry> services_;
    mutable std::mutex mutex_;

    // Background monitor thread
    std::thread monitor_thread_;
    std::atomic<bool> monitor_running_{false};
    int monitor_interval_ms_ = 1000;  // Check every second

    // Callback for restart events
    std::function<void(const std::string& name, int restart_count)> on_restart_;

public:
    ServiceManager() = default;
    ~ServiceManager();

    /// Register a local service (does not start it)
    /// The service will be spawned as a child process when needed
    void register_service(std::string_view name,
                         std::string_view executable,
                         u32 version = 1);

    /// Register a remote service (TCP-based)
    /// The service is expected to be running and listening on host:port
    void register_remote_service(std::string_view name,
                                std::string_view host,
                                u16 port,
                                u32 version = 1);

    /// Register a discoverable service (mDNS-based)
    /// The service will be discovered via mDNS when connecting
    /// @param name Service name (used for registration lookup)
    /// @param type Service type for mDNS (e.g., "calculator")
    void register_discoverable_service(std::string_view name,
                                       std::string_view type,
                                       u32 version = 1);

    /// Check if a service is registered as remote
    bool is_remote(std::string_view name) const;

    /// Check if a service is registered as discoverable
    bool is_discoverable(std::string_view name) const;

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

    /// Set maximum restart attempts (0 = unlimited)
    void set_max_restarts(std::string_view name, int max_restarts);

    /// Get restart count for a service
    int restart_count(std::string_view name) const;

    /// Reset restart count (e.g., after successful operation)
    void reset_restart_count(std::string_view name);

    /// Start background monitor thread
    void start_monitor(int interval_ms = 1000);

    /// Stop background monitor thread
    void stop_monitor();

    /// Check if monitor is running
    bool monitor_running() const { return monitor_running_; }

    /// Set callback for restart events
    void set_restart_callback(std::function<void(const std::string& name, int restart_count)> callback);

    /// Manually check all services and restart if needed (called by monitor thread)
    void check_and_restart();

private:
    ServiceEntry* find_service(std::string_view name);
    const ServiceEntry* find_service(std::string_view name) const;

    void monitor_loop();
};

} // namespace song
