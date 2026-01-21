// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/manager.hpp"
#include "song/transport.hpp"
#include "song/discovery.hpp"
#include <algorithm>
#include <chrono>

namespace song {

ServiceManager::~ServiceManager() {
    stop_monitor();

    // Stop all services
    std::lock_guard lock(mutex_);
    for (auto& entry : services_) {
        if (entry.process) {
            entry.process->terminate();
        }
    }
}

void ServiceManager::register_service(std::string_view name,
                                     std::string_view executable,
                                     u32 version) {
    std::lock_guard lock(mutex_);
    ServiceEntry entry;
    entry.name = name;
    entry.executable = executable;
    entry.is_remote = false;
    entry.version = version;
    services_.push_back(std::move(entry));
}

void ServiceManager::register_remote_service(std::string_view name,
                                            std::string_view host,
                                            u16 port,
                                            u32 version) {
    std::lock_guard lock(mutex_);
    ServiceEntry entry;
    entry.name = name;
    entry.host = host;
    entry.port = port;
    entry.is_remote = true;
    entry.version = version;
    services_.push_back(std::move(entry));
}

bool ServiceManager::is_remote(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    return entry->is_remote;
}

void ServiceManager::register_discoverable_service(std::string_view name,
                                                   std::string_view type,
                                                   u32 version) {
    std::lock_guard lock(mutex_);
    ServiceEntry entry;
    entry.name = name;
    entry.service_type = type;
    entry.is_discoverable = true;
    entry.version = version;
    services_.push_back(std::move(entry));
}

bool ServiceManager::is_discoverable(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    return entry->is_discoverable;
}

ServiceProcess* ServiceManager::start(std::string_view name) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }

    if (entry->is_remote) {
        throw ServiceError("Cannot start remote service: " + std::string(name) +
                          " (use connect() instead)");
    }

    if (entry->is_discoverable) {
        throw ServiceError("Cannot start discoverable service: " + std::string(name) +
                          " (use connect() instead)");
    }

    if (entry->process && entry->process->alive()) {
        return entry->process.get();
    }

    // Spawn new process
    entry->process = std::make_unique<ServiceProcess>(
        ServiceProcess::spawn(entry->executable.c_str())
    );

    return entry->process.get();
}

void ServiceManager::stop(std::string_view name) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }

    if (entry->is_remote) {
        throw ServiceError("Cannot stop remote service: " + std::string(name));
    }

    if (entry->is_discoverable) {
        throw ServiceError("Cannot stop discoverable service: " + std::string(name));
    }

    if (entry->process) {
        entry->process->terminate();
        entry->process.reset();
    }
}

void ServiceManager::restart(std::string_view name) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }

    if (entry->is_remote) {
        throw ServiceError("Cannot restart remote service: " + std::string(name));
    }

    if (entry->is_discoverable) {
        throw ServiceError("Cannot restart discoverable service: " + std::string(name));
    }

    // Stop if running
    if (entry->process) {
        entry->process->terminate();
        entry->process.reset();
    }

    // Start new process
    entry->process = std::make_unique<ServiceProcess>(
        ServiceProcess::spawn(entry->executable.c_str())
    );
}

void ServiceManager::replace(std::string_view name, std::string_view new_executable) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }

    if (entry->is_remote) {
        throw ServiceError("Cannot replace remote service: " + std::string(name));
    }

    if (entry->is_discoverable) {
        throw ServiceError("Cannot replace discoverable service: " + std::string(name));
    }

    // Stop if running
    if (entry->process) {
        entry->process->terminate();
        entry->process.reset();
    }

    entry->executable = new_executable;

    // Start new process
    entry->process = std::make_unique<ServiceProcess>(
        ServiceProcess::spawn(entry->executable.c_str())
    );
}

ServiceConnection ServiceManager::connect(std::string_view name) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }

    if (entry->is_remote) {
        // Connect to remote service via TCP
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect(entry->host, entry->port, 5000);  // 5 second connect timeout

        ServiceConnection conn(std::move(tcp));
        conn.init_handshake();  // Perform protocol handshake
        return conn;
    }

    if (entry->is_discoverable) {
        std::string host;
        u16 port = 0;

        // First, try mDNS discovery
        auto discovery = create_discovery();
        bool mdns_available = discovery && discovery->is_available();

        if (mdns_available) {
            auto found = discovery->discover_one(
                std::string(name),
                entry->service_type,
                std::chrono::milliseconds(3000)
            );

            if (found) {
                host = found->host;
                port = found->port;
            }
        }

        // If mDNS failed or unavailable, try registry
        if (port == 0 && has_registry()) {
            // Temporarily release lock to avoid deadlock with registry_client()
            mutex_.unlock();
            auto* client = registry_client();
            mutex_.lock();

            if (client && client->is_connected()) {
                auto info = client->discover(std::string(name));
                if (info.is_valid()) {
                    host = info.host;
                    port = info.port;
                }
            }
        }

        if (port == 0) {
            if (!mdns_available && !has_registry()) {
                throw ServiceError("Service discovery not available: no mDNS and no registry configured");
            }
            throw ServiceError("Service not found: " + std::string(name));
        }

        // Connect to discovered service via TCP
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect(host, port, 5000);

        ServiceConnection conn(std::move(tcp));
        conn.init_handshake();
        return conn;
    }

    // Local service - ensure it's running
    if (!entry->process || !entry->process->alive()) {
        entry->process = std::make_unique<ServiceProcess>(
            ServiceProcess::spawn(entry->executable.c_str())
        );
    }

    return ServiceConnection(entry->process.get());
}

bool ServiceManager::is_alive(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto* entry = find_service(name);
    if (!entry) {
        return false;
    }

    if (entry->is_remote) {
        // For remote services, try a quick TCP connection test
        try {
            TcpTransport tcp;
            tcp.connect(entry->host, entry->port, 1000);  // 1 second timeout
            return true;
        } catch (...) {
            return false;
        }
    }

    if (entry->is_discoverable) {
        // For discoverable services, try mDNS discovery
        try {
            auto discovery = create_discovery();
            if (!discovery || !discovery->is_available()) {
                return false;
            }
            auto found = discovery->discover_one(
                std::string(name),
                entry->service_type,
                std::chrono::milliseconds(1000)
            );
            return found.has_value();
        } catch (...) {
            return false;
        }
    }

    // Local service
    if (!entry->process) {
        return false;
    }
    return entry->process->alive();
}

void ServiceManager::set_auto_restart(std::string_view name, bool enable) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    if (entry->is_remote) {
        throw ServiceError("Cannot set auto-restart for remote service: " + std::string(name));
    }
    if (entry->is_discoverable) {
        throw ServiceError("Cannot set auto-restart for discoverable service: " + std::string(name));
    }
    entry->auto_restart = enable;
}

void ServiceManager::set_max_restarts(std::string_view name, int max_restarts) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    if (entry->is_remote) {
        throw ServiceError("Cannot set max-restarts for remote service: " + std::string(name));
    }
    if (entry->is_discoverable) {
        throw ServiceError("Cannot set max-restarts for discoverable service: " + std::string(name));
    }
    entry->max_restarts = max_restarts;
}

int ServiceManager::restart_count(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    return entry->restart_count;
}

void ServiceManager::reset_restart_count(std::string_view name) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    entry->restart_count = 0;
}

void ServiceManager::start_monitor(int interval_ms) {
    if (monitor_running_) {
        return;  // Already running
    }

    monitor_interval_ms_ = interval_ms;
    monitor_running_ = true;
    monitor_thread_ = std::thread(&ServiceManager::monitor_loop, this);
}

void ServiceManager::stop_monitor() {
    if (!monitor_running_) {
        return;
    }

    monitor_running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void ServiceManager::set_restart_callback(
    std::function<void(const std::string& name, int restart_count)> callback) {
    std::lock_guard lock(mutex_);
    on_restart_ = std::move(callback);
}

void ServiceManager::check_and_restart() {
    std::lock_guard lock(mutex_);

    for (auto& entry : services_) {
        // Skip remote services (can't restart them)
        if (entry.is_remote) {
            continue;
        }

        // Skip discoverable services (can't restart them)
        if (entry.is_discoverable) {
            continue;
        }

        // Skip services without auto-restart
        if (!entry.auto_restart) {
            continue;
        }

        // Skip if no process was ever started
        if (!entry.process) {
            continue;
        }

        // Check if still alive
        if (entry.process->alive()) {
            continue;
        }

        // Check if we've exceeded max restarts
        if (entry.max_restarts > 0 && entry.restart_count >= entry.max_restarts) {
            // Already at max restarts, don't restart again
            continue;
        }

        // Service died - restart it
        entry.restart_count++;

        try {
            entry.process = std::make_unique<ServiceProcess>(
                ServiceProcess::spawn(entry.executable.c_str())
            );

            // Call restart callback if set
            if (on_restart_) {
                on_restart_(entry.name, entry.restart_count);
            }
        } catch (const std::exception&) {
            // Failed to restart - will try again on next check
            entry.process.reset();
        }
    }
}

void ServiceManager::monitor_loop() {
    while (monitor_running_) {
        check_and_restart();

        // Sleep for interval, but check running flag periodically
        auto end_time = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(monitor_interval_ms_);

        while (monitor_running_ && std::chrono::steady_clock::now() < end_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

ServiceManager::ServiceEntry* ServiceManager::find_service(std::string_view name) {
    auto it = std::find_if(services_.begin(), services_.end(),
        [name](const ServiceEntry& e) { return e.name == name; });
    return it != services_.end() ? &(*it) : nullptr;
}

const ServiceManager::ServiceEntry* ServiceManager::find_service(std::string_view name) const {
    auto it = std::find_if(services_.begin(), services_.end(),
        [name](const ServiceEntry& e) { return e.name == name; });
    return it != services_.end() ? &(*it) : nullptr;
}

// =============================================================================
// Registry Support
// =============================================================================

void ServiceManager::set_registry(std::string_view host, u16 port) {
    std::lock_guard lock(mutex_);
    registry_host_ = host;
    registry_port_ = port;
    // Reset client so it will reconnect with new address
    registry_client_.reset();
}

RegistryClient* ServiceManager::registry_client() {
    std::lock_guard lock(mutex_);

    if (!has_registry()) {
        return nullptr;
    }

    // Create client if needed
    if (!registry_client_) {
        registry_client_ = std::make_unique<RegistryClient>(registry_host_, registry_port_);
    }

    return registry_client_.get();
}

} // namespace song
