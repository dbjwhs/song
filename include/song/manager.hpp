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
/// Manages the lifecycle of service processes
class ServiceManager {
    struct ServiceEntry {
        std::string name;
        std::string executable;
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
