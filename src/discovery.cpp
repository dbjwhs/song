// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/discovery.hpp"

#ifdef __APPLE__
#include <dns_sd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#endif

namespace song {

#ifdef __APPLE__

// =============================================================================
// macOS Implementation using dns_sd (Bonjour)
// =============================================================================

// Context structs for callbacks
struct BrowseContext {
    std::vector<std::string> names;
    std::string type;
    std::string domain;
    std::mutex mutex;
    std::condition_variable cv;
};

struct ResolveContext {
    std::string host;
    u16 port = 0;
    bool resolved = false;
    std::mutex mutex;
    std::condition_variable cv;
};

// Static callbacks
static void DNSSD_API browse_callback(
    DNSServiceRef /* sdRef */,
    DNSServiceFlags flags,
    uint32_t /* interfaceIndex */,
    DNSServiceErrorType errorCode,
    const char* serviceName,
    const char* /* regtype */,
    const char* replyDomain,
    void* context) {
    if (errorCode != kDNSServiceErr_NoError) return;
    auto* ctx = static_cast<BrowseContext*>(context);
    std::lock_guard lock(ctx->mutex);
    if (flags & kDNSServiceFlagsAdd) {
        ctx->names.push_back(serviceName);
        ctx->domain = replyDomain;
    }
    ctx->cv.notify_all();
}

static void DNSSD_API resolve_callback(
    DNSServiceRef /* sdRef */,
    DNSServiceFlags /* flags */,
    uint32_t /* interfaceIndex */,
    DNSServiceErrorType errorCode,
    const char* /* fullname */,
    const char* hosttarget,
    uint16_t port,
    uint16_t /* txtLen */,
    const unsigned char* /* txtRecord */,
    void* context) {
    if (errorCode != kDNSServiceErr_NoError) return;
    auto* ctx = static_cast<ResolveContext*>(context);
    std::lock_guard lock(ctx->mutex);
    ctx->host = hosttarget;
    ctx->port = ntohs(port);
    ctx->resolved = true;
    ctx->cv.notify_all();
}

class DnssdDiscovery : public Discovery {
    DNSServiceRef register_ref_ = nullptr;
    std::mutex mutex_;
    std::atomic<bool> registered_{false};

public:
    ~DnssdDiscovery() override {
        unregister_service();
    }

    bool register_service(const std::string& name,
                         const std::string& type,
                         u16 port) override {
        std::lock_guard lock(mutex_);

        if (register_ref_) {
            // Already registered, unregister first
            DNSServiceRefDeallocate(register_ref_);
            register_ref_ = nullptr;
            registered_ = false;
        }

        std::string service_type = make_service_type(type);

        DNSServiceErrorType err = DNSServiceRegister(
            &register_ref_,
            0,                          // flags
            kDNSServiceInterfaceIndexAny,
            name.c_str(),               // service name
            service_type.c_str(),       // service type
            nullptr,                    // domain (default = .local)
            nullptr,                    // host (default = local machine)
            htons(port),                // port (network byte order)
            0,                          // txtLen
            nullptr,                    // txtRecord
            register_callback,          // callback
            this                        // context
        );

        if (err != kDNSServiceErr_NoError) {
            register_ref_ = nullptr;
            return false;
        }

        // Process the registration (wait for callback)
        int fd = DNSServiceRefSockFD(register_ref_);
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        // Wait up to 1 second for registration to complete
        if (poll(&pfd, 1, 1000) > 0) {
            DNSServiceProcessResult(register_ref_);
        }

        return registered_.load();
    }

    void unregister_service() override {
        std::lock_guard lock(mutex_);
        if (register_ref_) {
            DNSServiceRefDeallocate(register_ref_);
            register_ref_ = nullptr;
            registered_ = false;
        }
    }

    bool is_registered() const override {
        return registered_.load();
    }

    std::vector<DiscoveredService> discover(
        const std::string& type,
        std::chrono::milliseconds timeout) override {

        std::vector<DiscoveredService> results;
        std::string service_type = make_service_type(type);

        BrowseContext ctx;
        ctx.type = service_type;

        DNSServiceRef browse_ref = nullptr;
        DNSServiceErrorType err = DNSServiceBrowse(
            &browse_ref,
            0,
            kDNSServiceInterfaceIndexAny,
            service_type.c_str(),
            nullptr,  // domain
            browse_callback,
            &ctx
        );

        if (err != kDNSServiceErr_NoError || !browse_ref) {
            return results;
        }

        // Process browse results with timeout
        int fd = DNSServiceRefSockFD(browse_ref);
        auto start = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - start < timeout) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;

            auto remaining = timeout - (std::chrono::steady_clock::now() - start);
            int timeout_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

            if (timeout_ms <= 0) break;

            if (poll(&pfd, 1, std::min(timeout_ms, 100)) > 0) {
                DNSServiceProcessResult(browse_ref);
            }
        }

        DNSServiceRefDeallocate(browse_ref);

        // Resolve each discovered service
        std::lock_guard lock(ctx.mutex);
        for (const auto& name : ctx.names) {
            auto resolved = resolve_service(name, ctx.type, ctx.domain, timeout);
            if (resolved) {
                results.push_back(std::move(*resolved));
            }
        }

        return results;
    }

    std::optional<DiscoveredService> discover_one(
        const std::string& name,
        const std::string& type,
        std::chrono::milliseconds timeout) override {

        std::string service_type = make_service_type(type);
        return resolve_service(name, service_type, "local.", timeout);
    }

    bool is_available() const override {
        return true;
    }

private:
    static void DNSSD_API register_callback(
        DNSServiceRef /* sdRef */,
        DNSServiceFlags /* flags */,
        DNSServiceErrorType errorCode,
        const char* /* name */,
        const char* /* regtype */,
        const char* /* domain */,
        void* context) {
        auto* self = static_cast<DnssdDiscovery*>(context);
        if (errorCode == kDNSServiceErr_NoError) {
            self->registered_ = true;
        }
    }

    std::optional<DiscoveredService> resolve_service(
        const std::string& name,
        const std::string& type,
        const std::string& domain,
        std::chrono::milliseconds timeout) {

        ResolveContext ctx;

        DNSServiceRef resolve_ref = nullptr;
        DNSServiceErrorType err = DNSServiceResolve(
            &resolve_ref,
            0,
            kDNSServiceInterfaceIndexAny,
            name.c_str(),
            type.c_str(),
            domain.c_str(),
            resolve_callback,
            &ctx
        );

        if (err != kDNSServiceErr_NoError || !resolve_ref) {
            return std::nullopt;
        }

        // Wait for resolution
        int fd = DNSServiceRefSockFD(resolve_ref);
        auto start = std::chrono::steady_clock::now();

        while (!ctx.resolved &&
               std::chrono::steady_clock::now() - start < timeout) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;

            auto remaining = timeout - (std::chrono::steady_clock::now() - start);
            int timeout_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

            if (timeout_ms <= 0) break;

            if (poll(&pfd, 1, std::min(timeout_ms, 100)) > 0) {
                DNSServiceProcessResult(resolve_ref);
            }
        }

        DNSServiceRefDeallocate(resolve_ref);

        if (!ctx.resolved) {
            return std::nullopt;
        }

        // Resolve hostname to IP address
        std::string ip = resolve_hostname(ctx.host);
        if (ip.empty()) {
            ip = ctx.host;  // Use hostname if resolution fails
        }

        DiscoveredService service;
        service.name = name;
        service.host = ip;
        service.port = ctx.port;
        service.type = type;
        service.domain = domain;
        return service;
    }

    static std::string resolve_hostname(const std::string& hostname) {
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* result = nullptr;
        if (getaddrinfo(hostname.c_str(), nullptr, &hints, &result) != 0) {
            return "";
        }

        std::string ip;
        if (result) {
            auto* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            char buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf))) {
                ip = buf;
            }
            freeaddrinfo(result);
        }
        return ip;
    }
};

std::unique_ptr<Discovery> create_discovery() {
    return std::make_unique<DnssdDiscovery>();
}

#elif defined(__linux__)

// =============================================================================
// Linux Stub (TODO: Implement with Avahi)
// =============================================================================

class AvahiDiscovery : public Discovery {
public:
    bool register_service(const std::string&, const std::string&, u16) override {
        return false;  // Not implemented yet
    }

    void unregister_service() override {}

    bool is_registered() const override {
        return false;
    }

    std::vector<DiscoveredService> discover(
        const std::string&, std::chrono::milliseconds) override {
        return {};  // Not implemented yet
    }

    std::optional<DiscoveredService> discover_one(
        const std::string&, const std::string&, std::chrono::milliseconds) override {
        return std::nullopt;  // Not implemented yet
    }

    bool is_available() const override {
        return false;  // Not available until Avahi integration is done
    }
};

std::unique_ptr<Discovery> create_discovery() {
    return std::make_unique<AvahiDiscovery>();
}

#else

// =============================================================================
// Unsupported Platform
// =============================================================================

class NullDiscovery : public Discovery {
public:
    bool register_service(const std::string&, const std::string&, u16) override {
        return false;
    }

    void unregister_service() override {}

    bool is_registered() const override {
        return false;
    }

    std::vector<DiscoveredService> discover(
        const std::string&, std::chrono::milliseconds) override {
        return {};
    }

    std::optional<DiscoveredService> discover_one(
        const std::string&, const std::string&, std::chrono::milliseconds) override {
        return std::nullopt;
    }

    bool is_available() const override {
        return false;
    }
};

std::unique_ptr<Discovery> create_discovery() {
    return std::make_unique<NullDiscovery>();
}

#endif

} // namespace song
