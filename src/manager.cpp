// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/manager.hpp"
#include <algorithm>

namespace song {

void ServiceManager::register_service(std::string_view name,
                                     std::string_view executable,
                                     u32 version) {
    ServiceEntry entry;
    entry.name = name;
    entry.executable = executable;
    entry.version = version;
    services_.push_back(std::move(entry));
}

ServiceProcess* ServiceManager::start(std::string_view name) {
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

    // TODO: Version negotiation handshake

    return entry->process.get();
}

void ServiceManager::stop(std::string_view name) {
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
    stop(name);
    start(name);
}

void ServiceManager::replace(std::string_view name, std::string_view new_executable) {
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }

    stop(name);
    entry->executable = new_executable;
    start(name);
}

ServiceConnection ServiceManager::connect(std::string_view name) {
    auto* proc = start(name);  // Starts if not running
    return ServiceConnection(proc);
}

bool ServiceManager::is_alive(std::string_view name) const {
    const auto* entry = find_service(name);
    if (!entry || !entry->process) {
        return false;
    }
    return entry->process->alive();
}

void ServiceManager::set_auto_restart(std::string_view name, bool enable) {
    auto* entry = find_service(name);
    if (!entry) {
        throw ServiceError("Service not found: " + std::string(name));
    }
    entry->auto_restart = enable;
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
