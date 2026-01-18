// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/manager.hpp"
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
    entry.version = version;
    services_.push_back(std::move(entry));
}

ServiceProcess* ServiceManager::start(std::string_view name) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
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
    auto* proc = start(name);  // Starts if not running
    return ServiceConnection(proc);
}

bool ServiceManager::is_alive(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto* entry = find_service(name);
    if (!entry || !entry->process) {
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
    entry->auto_restart = enable;
}

void ServiceManager::set_max_restarts(std::string_view name, int max_restarts) {
    std::lock_guard lock(mutex_);
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
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

} // namespace song
