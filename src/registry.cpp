// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/registry.hpp"
#include "song/wire.hpp"
#include "song/error.hpp"
#include <algorithm>

namespace song {

// =============================================================================
// ServiceInfo Encoding/Decoding
// =============================================================================

void ServiceInfo::encode(Buffer& buf) const {
    encode_string(buf, name);
    encode_string(buf, host);
    encode_u16(buf, port);
}

ServiceInfo ServiceInfo::decode(Buffer& buf) {
    ServiceInfo info;
    info.name = decode_string(buf);
    info.host = decode_string(buf);
    info.port = decode_u16(buf);
    return info;
}

// =============================================================================
// RegistryClient Implementation
// =============================================================================

RegistryClient::RegistryClient(const std::string& host, u16 port, int timeout_ms)
    : timeout_ms_(timeout_ms)
    , registry_host_(host)
    , registry_port_(port) {
    connect(host, port, timeout_ms);
}

bool RegistryClient::connect(const std::string& host, u16 port, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    registry_host_ = host;
    registry_port_ = port;
    timeout_ms_ = timeout_ms;

    auto tcp = std::make_unique<TcpTransport>();
    try {
        tcp->connect(host, port, timeout_ms);
        transport_ = std::move(tcp);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RegistryClient::is_connected() const {
    return transport_ && transport_->is_connected();
}

void RegistryClient::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transport_) {
        transport_->close();
        transport_.reset();
    }
}

bool RegistryClient::ensure_connected() {
    if (is_connected()) {
        return true;
    }
    if (registry_host_.empty() || registry_port_ == 0) {
        return false;
    }
    // Try to reconnect
    auto tcp = std::make_unique<TcpTransport>();
    try {
        tcp->connect(registry_host_, registry_port_, timeout_ms_);
        transport_ = std::move(tcp);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RegistryClient::call(u16 method_id, const Buffer& request, Buffer& response) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ensure_connected()) {
        return false;
    }

    // Create call message
    u32 seq = ++sequence_id_;
    Buffer call_msg = wire::create_call_message(seq, kService_Registry, method_id, request);

    try {
        // Send request
        transport_->send(call_msg);

        // Receive response
        Buffer resp_msg;
        if (!transport_->receive(resp_msg, timeout_ms_)) {
            return false;
        }

        // Decode header
        auto hdr = wire::decode_header(resp_msg);

        if (hdr.sequence_id != seq) {
            return false;  // Wrong sequence
        }

        if (hdr.type == wire::MsgType::error) {
            return false;  // Error response
        }

        if (hdr.type != wire::MsgType::result) {
            return false;  // Unexpected message type
        }

        // Copy payload to response buffer
        response.reset();
        if (hdr.payload_size > 0) {
            response.write(resp_msg.data() + 16, hdr.payload_size);
            response.reset_read();
        }

        return true;
    } catch (const std::exception&) {
        // Connection error - close and retry next time
        transport_->close();
        transport_.reset();
        return false;
    }
}

bool RegistryClient::register_service(const ServiceInfo& info) {
    Buffer request;
    info.encode(request);

    Buffer response;
    if (!call(kMethod_Register, request, response)) {
        return false;
    }

    try {
        return decode_u8(response) != 0;
    } catch (const std::exception&) {
        return false;  // Malformed or short response from the registry.
    }
}

bool RegistryClient::unregister_service(const std::string& name) {
    Buffer request;
    encode_string(request, name);

    Buffer response;
    if (!call(kMethod_Unregister, request, response)) {
        return false;
    }

    try {
        return decode_u8(response) != 0;
    } catch (const std::exception&) {
        return false;  // Malformed or short response from the registry.
    }
}

ServiceInfo RegistryClient::discover(const std::string& name) {
    Buffer request;
    encode_string(request, name);

    Buffer response;
    if (!call(kMethod_Discover, request, response)) {
        return {};
    }

    try {
        return ServiceInfo::decode(response);
    } catch (const std::exception&) {
        return {};  // Truncated/malformed response -> invalid ServiceInfo.
    }
}

std::vector<ServiceInfo> RegistryClient::list_all() {
    Buffer request;  // Empty

    Buffer response;
    if (!call(kMethod_ListAll, request, response)) {
        return {};
    }

    try {
        u32 count = decode_u32(response);
        std::vector<ServiceInfo> services;
        // Bound the reservation by what the payload could actually contain. Each
        // ServiceInfo is at least 10 bytes (two 4-byte length prefixes + a 2-byte
        // port), so a hostile or corrupt count cannot drive a huge allocation.
        size_t max_possible = response.remaining() / 10 + 1;
        services.reserve(std::min<size_t>(count, max_possible));

        for (u32 i = 0; i < count; ++i) {
            services.push_back(ServiceInfo::decode(response));
        }
        return services;
    } catch (const std::exception&) {
        return {};  // Truncated or malformed ListAll response.
    }
}

bool RegistryClient::heartbeat(const std::string& name) {
    Buffer request;
    encode_string(request, name);

    Buffer response;
    if (!call(kMethod_Heartbeat, request, response)) {
        return false;
    }

    try {
        return decode_u8(response) != 0;
    } catch (const std::exception&) {
        return false;  // Malformed or short response from the registry.
    }
}

// =============================================================================
// Registry Dispatcher
// =============================================================================

void dispatch_Registry(IRegistry& impl, u16 method_id, Buffer& request, Buffer& response) {
    switch (method_id) {
        case kMethod_Register: {
            ServiceInfo info = ServiceInfo::decode(request);
            bool result = impl.register_service(info);
            encode_u8(response, result ? 1 : 0);
            break;
        }
        case kMethod_Unregister: {
            std::string name = decode_string(request);
            bool result = impl.unregister_service(name);
            encode_u8(response, result ? 1 : 0);
            break;
        }
        case kMethod_Discover: {
            std::string name = decode_string(request);
            ServiceInfo info = impl.discover(name);
            info.encode(response);
            break;
        }
        case kMethod_ListAll: {
            auto services = impl.list_all();
            encode_u32(response, static_cast<u32>(services.size()));
            for (const auto& info : services) {
                info.encode(response);
            }
            break;
        }
        case kMethod_Heartbeat: {
            std::string name = decode_string(request);
            bool result = impl.heartbeat(name);
            encode_u8(response, result ? 1 : 0);
            break;
        }
        default:
            throw ProtocolError("Unknown registry method: " + std::to_string(method_id));
    }
}

// =============================================================================
// MemoryRegistry Implementation
// =============================================================================

bool MemoryRegistry::register_service(const ServiceInfo& info) {
    if (!info.is_valid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (services_.count(info.name) > 0) {
        return false;  // Already registered
    }

    Entry entry;
    entry.info = info;
    entry.last_heartbeat = std::chrono::steady_clock::now();

    services_[info.name] = entry;
    return true;
}

bool MemoryRegistry::unregister_service(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.erase(name) > 0;
}

ServiceInfo MemoryRegistry::discover(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it == services_.end()) {
        return {};  // Not found
    }

    // Check if stale
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.last_heartbeat > expiry_time_) {
        // Service is stale, remove it
        services_.erase(it);
        return {};
    }

    return it->second.info;
}

std::vector<ServiceInfo> MemoryRegistry::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<ServiceInfo> result;
    result.reserve(services_.size());

    for (auto it = services_.begin(); it != services_.end(); ) {
        if (now - it->second.last_heartbeat > expiry_time_) {
            // Remove stale entry
            it = services_.erase(it);
        } else {
            result.push_back(it->second.info);
            ++it;
        }
    }

    return result;
}

bool MemoryRegistry::heartbeat(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;  // Not registered
    }

    it->second.last_heartbeat = std::chrono::steady_clock::now();
    return true;
}

void MemoryRegistry::cleanup_stale() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    for (auto it = services_.begin(); it != services_.end(); ) {
        if (now - it->second.last_heartbeat > expiry_time_) {
            it = services_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t MemoryRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}

} // namespace song
