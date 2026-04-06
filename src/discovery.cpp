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

#if defined(SONG_HAS_AVAHI)

// =============================================================================
// Linux Implementation using Avahi
// =============================================================================

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <mutex>
#include <atomic>

namespace {

// Poll the Avahi event loop until a condition is met or timeout expires
template<typename Predicate>
void avahi_poll_until(AvahiSimplePoll* poll, Predicate pred,
                      std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        int ms = std::max(static_cast<int>(remaining.count()), 0);
        if (ms <= 0) break;
        avahi_simple_poll_iterate(poll, std::min(ms, 100));
    }
}

std::string resolve_hostname(const std::string& hostname) {
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

} // anonymous namespace

// Browse context for collecting discovered service names
struct AvahiBrowseContext {
    std::vector<std::string> names;
    std::string domain;
    std::mutex mutex;
};

// Resolve context for a single service resolution
struct AvahiResolveContext {
    std::string host;
    u16 port = 0;
    bool resolved = false;
    AvahiSimplePoll* poll = nullptr;
};

static void avahi_browse_callback(
    [[maybe_unused]] AvahiServiceBrowser* b,
    [[maybe_unused]] AvahiIfIndex interface,
    [[maybe_unused]] AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char* name,
    [[maybe_unused]] const char* type,
    const char* domain,
    [[maybe_unused]] AvahiLookupResultFlags flags,
    void* userdata) {

    auto* ctx = static_cast<AvahiBrowseContext*>(userdata);
    if (event == AVAHI_BROWSER_NEW) {
        std::lock_guard lock(ctx->mutex);
        ctx->names.push_back(name);
        if (domain) ctx->domain = domain;
    }
}

static void avahi_resolve_callback(
    [[maybe_unused]] AvahiServiceResolver* r,
    [[maybe_unused]] AvahiIfIndex interface,
    [[maybe_unused]] AvahiProtocol protocol,
    AvahiResolverEvent event,
    [[maybe_unused]] const char* name,
    [[maybe_unused]] const char* type,
    [[maybe_unused]] const char* domain,
    const char* host_name,
    [[maybe_unused]] const AvahiAddress* address,
    uint16_t port,
    [[maybe_unused]] AvahiStringList* txt,
    [[maybe_unused]] AvahiLookupResultFlags flags,
    void* userdata) {

    auto* ctx = static_cast<AvahiResolveContext*>(userdata);
    if (event == AVAHI_RESOLVER_FOUND) {
        ctx->host = host_name ? host_name : "";
        ctx->port = port;
        ctx->resolved = true;
    }
}

static void avahi_entry_group_callback(
    [[maybe_unused]] AvahiEntryGroup* g,
    AvahiEntryGroupState state,
    void* userdata) {
    auto* registered = static_cast<std::atomic<bool>*>(userdata);
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        *registered = true;
    }
}

class AvahiDiscovery : public Discovery {
    AvahiSimplePoll* poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;
    std::mutex mutex_;
    std::atomic<bool> registered_{false};

public:
    AvahiDiscovery() {
        poll_ = avahi_simple_poll_new();
        if (!poll_) return;

        int error = 0;
        client_ = avahi_client_new(avahi_simple_poll_get(poll_),
                                    static_cast<AvahiClientFlags>(0),
                                    nullptr, nullptr, &error);
    }

    ~AvahiDiscovery() override {
        unregister_service();
        if (client_) avahi_client_free(client_);
        if (poll_) avahi_simple_poll_free(poll_);
    }

    bool register_service(const std::string& name,
                         const std::string& type,
                         u16 port) override {
        std::lock_guard lock(mutex_);
        if (!client_) return false;

        if (group_) {
            avahi_entry_group_reset(group_);
            registered_ = false;
        } else {
            group_ = avahi_entry_group_new(client_, avahi_entry_group_callback,
                                            &registered_);
            if (!group_) return false;
        }

        std::string service_type = make_service_type(type);
        int ret = avahi_entry_group_add_service(
            group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            static_cast<AvahiPublishFlags>(0),
            name.c_str(), service_type.c_str(),
            nullptr, nullptr, port, nullptr);

        if (ret < 0) return false;

        ret = avahi_entry_group_commit(group_);
        if (ret < 0) return false;

        // Wait for registration to complete
        avahi_poll_until(poll_, [this]() { return registered_.load(); },
                         std::chrono::milliseconds(1000));

        return registered_.load();
    }

    void unregister_service() override {
        std::lock_guard lock(mutex_);
        if (group_) {
            avahi_entry_group_reset(group_);
            avahi_entry_group_free(group_);
            group_ = nullptr;
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
        if (!client_) return results;

        std::string service_type = make_service_type(type);

        AvahiBrowseContext ctx;
        AvahiServiceBrowser* browser = avahi_service_browser_new(
            client_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            service_type.c_str(), nullptr,
            static_cast<AvahiLookupFlags>(0),
            avahi_browse_callback, &ctx);

        if (!browser) return results;

        // Browse with timeout
        avahi_poll_until(poll_,
            [&ctx]() {
                std::lock_guard lock(ctx.mutex);
                return !ctx.names.empty();
            },
            timeout);

        // Keep browsing briefly to collect more results
        avahi_poll_until(poll_, []() { return false; },
                         std::chrono::milliseconds(200));

        avahi_service_browser_free(browser);

        // Resolve each discovered service
        std::lock_guard lock(ctx.mutex);
        for (const auto& name : ctx.names) {
            auto resolved = resolve_service(name, service_type,
                                            ctx.domain.empty() ? "local" : ctx.domain,
                                            timeout);
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
        return resolve_service(name, service_type, "local", timeout);
    }

    bool is_available() const override {
        return client_ != nullptr;
    }

private:
    std::optional<DiscoveredService> resolve_service(
        const std::string& name,
        const std::string& type,
        const std::string& domain,
        std::chrono::milliseconds timeout) {

        if (!client_) return std::nullopt;

        AvahiResolveContext ctx;
        ctx.poll = poll_;

        AvahiServiceResolver* resolver = avahi_service_resolver_new(
            client_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            name.c_str(), type.c_str(), domain.c_str(),
            AVAHI_PROTO_UNSPEC,
            static_cast<AvahiLookupFlags>(0),
            avahi_resolve_callback, &ctx);

        if (!resolver) return std::nullopt;

        avahi_poll_until(poll_, [&ctx]() { return ctx.resolved; }, timeout);

        avahi_service_resolver_free(resolver);

        if (!ctx.resolved) return std::nullopt;

        std::string ip = resolve_hostname(ctx.host);
        if (ip.empty()) ip = ctx.host;

        DiscoveredService service;
        service.name = name;
        service.host = ip;
        service.port = ctx.port;
        service.type = type;
        service.domain = domain;
        return service;
    }
};

std::unique_ptr<Discovery> create_discovery() {
    return std::make_unique<AvahiDiscovery>();
}

#else // !SONG_HAS_AVAHI

// =============================================================================
// Linux without Avahi -- stub returns unavailable
// =============================================================================

class AvahiStubDiscovery : public Discovery {
public:
    bool register_service(const std::string&, const std::string&, u16) override {
        return false;
    }
    void unregister_service() override {}
    bool is_registered() const override { return false; }
    std::vector<DiscoveredService> discover(
        const std::string&, std::chrono::milliseconds) override { return {}; }
    std::optional<DiscoveredService> discover_one(
        const std::string&, const std::string&, std::chrono::milliseconds) override {
        return std::nullopt;
    }
    bool is_available() const override { return false; }
};

std::unique_ptr<Discovery> create_discovery() {
    return std::make_unique<AvahiStubDiscovery>();
}

#endif // SONG_HAS_AVAHI

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
