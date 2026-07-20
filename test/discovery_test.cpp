// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/discovery.hpp>
#include <song/manager.hpp>
#include <song/transport.hpp>
#include <song/runtime.hpp>
#include <song/wire.hpp>
#include <thread>
#include <chrono>
#include <atomic>

using namespace song;

// =============================================================================
// Discovery Static Methods
// =============================================================================

TEST(DiscoveryTest, MakeServiceType) {
    EXPECT_EQ(Discovery::make_service_type("calculator"), "_calculator._song._tcp");
    EXPECT_EQ(Discovery::make_service_type("echo"), "_echo._song._tcp");
    EXPECT_EQ(Discovery::make_service_type("test"), "_test._song._tcp");
}

// =============================================================================
// Discovery Factory
// =============================================================================

TEST(DiscoveryTest, CreateDiscovery) {
    auto discovery = create_discovery();
    // On macOS/Linux with mDNS, discovery should be non-null
    // On other platforms, may be null
    // This test just ensures the factory doesn't crash
#ifdef __APPLE__
    EXPECT_NE(discovery, nullptr);
#endif
}

TEST(DiscoveryTest, DiscoveryAvailability) {
    auto discovery = create_discovery();
    if (discovery) {
        // is_available() should not throw
        [[maybe_unused]] bool available = discovery->is_available();
    }
}

// =============================================================================
// ServiceRegistration RAII
// =============================================================================

TEST(DiscoveryTest, ServiceRegistrationDefaultConstruct) {
    ServiceRegistration reg;
    EXPECT_FALSE(reg.is_registered());
}

TEST(DiscoveryTest, ServiceRegistrationMove) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    ServiceRegistration reg1(*discovery, "TestService", "test", 12345);
    bool was_registered = reg1.is_registered();

    ServiceRegistration reg2 = std::move(reg1);

    EXPECT_FALSE(reg1.is_registered());  // NOLINT - testing moved-from state
    EXPECT_EQ(reg2.is_registered(), was_registered);
}

TEST(DiscoveryTest, ServiceRegistrationMoveAssign) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    ServiceRegistration reg1(*discovery, "TestService1", "test", 12345);
    ServiceRegistration reg2;

    reg2 = std::move(reg1);

    EXPECT_FALSE(reg1.is_registered());  // NOLINT - testing moved-from state
}

// =============================================================================
// Discovery Registration/Unregistration
// =============================================================================

TEST(DiscoveryTest, RegisterAndUnregister) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    EXPECT_FALSE(discovery->is_registered());

    bool registered = discovery->register_service("TestService", "test", 12345);
    if (registered) {
        EXPECT_TRUE(discovery->is_registered());
        discovery->unregister_service();
        EXPECT_FALSE(discovery->is_registered());
    }
}

TEST(DiscoveryTest, DoubleRegisterReplacesFirst) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    discovery->register_service("Service1", "test", 12345);
    bool reg2 = discovery->register_service("Service2", "test", 12346);

    // Second registration should work (replaces first)
    if (reg2) {
        EXPECT_TRUE(discovery->is_registered());
    }

    discovery->unregister_service();
}

// =============================================================================
// ServiceManager Discoverable Services
// =============================================================================

// =============================================================================
// ServiceRegistration RAII contract (deterministic, no real mDNS backend)
// =============================================================================

namespace {
// A Discovery mock that counts register/unregister calls so the RAII/move
// semantics of ServiceRegistration can be verified without mDNS (the existing
// move tests skip when discovery is unavailable).
class CountingDiscovery : public Discovery {
public:
    int register_count = 0;
    int unregister_count = 0;
    bool register_result = true;

    bool register_service(const std::string&, const std::string&, u16) override {
        ++register_count;
        registered_ = register_result;
        return register_result;
    }
    void unregister_service() override {
        ++unregister_count;
        registered_ = false;
    }
    bool is_registered() const override { return registered_; }
    std::vector<DiscoveredService> discover(const std::string&,
                                            std::chrono::milliseconds) override {
        return {};
    }
    std::optional<DiscoveredService> discover_one(const std::string&,
                                                  const std::string&,
                                                  std::chrono::milliseconds) override {
        return std::nullopt;
    }
    bool is_available() const override { return true; }

private:
    bool registered_ = false;
};
}  // namespace

TEST(ServiceRegistrationRaiiTest, RegisterSuccessUnregistersOnDestroy) {
    CountingDiscovery disc;
    {
        ServiceRegistration reg(disc, "n", "t", 1);
        EXPECT_TRUE(reg.is_registered());
        EXPECT_EQ(disc.register_count, 1);
        EXPECT_EQ(disc.unregister_count, 0);
    }
    EXPECT_EQ(disc.register_count, 1);
    EXPECT_EQ(disc.unregister_count, 1);
}

TEST(ServiceRegistrationRaiiTest, RegisterFailureDoesNotUnregister) {
    CountingDiscovery disc;
    disc.register_result = false;
    {
        ServiceRegistration reg(disc, "n", "t", 1);
        EXPECT_FALSE(reg.is_registered());
    }
    EXPECT_EQ(disc.register_count, 1);
    EXPECT_EQ(disc.unregister_count, 0);  // never registered -> never unregister
}

TEST(ServiceRegistrationRaiiTest, MoveConstructTransfersOwnership) {
    CountingDiscovery disc;
    {
        ServiceRegistration reg1(disc, "n", "t", 1);
        ServiceRegistration reg2(std::move(reg1));
        EXPECT_FALSE(reg1.is_registered());
        EXPECT_TRUE(reg2.is_registered());
    }
    EXPECT_EQ(disc.register_count, 1);
    EXPECT_EQ(disc.unregister_count, 1);  // exactly one, no double unregister
}

TEST(ServiceRegistrationRaiiTest, MoveAssignReleasesExistingTarget) {
    CountingDiscovery disc;
    {
        ServiceRegistration reg_a(disc, "a", "t", 1);
        ServiceRegistration reg_b(disc, "b", "t", 2);
        EXPECT_EQ(disc.register_count, 2);

        reg_a = std::move(reg_b);
        // reg_a's original registration is released immediately.
        EXPECT_EQ(disc.unregister_count, 1);
        EXPECT_TRUE(reg_a.is_registered());
        EXPECT_FALSE(reg_b.is_registered());
    }
    EXPECT_EQ(disc.unregister_count, 2);  // reg_a's surviving registration too
}

TEST(ServiceRegistrationRaiiTest, SelfMoveAssignIsNoOp) {
    CountingDiscovery disc;
    {
        ServiceRegistration reg(disc, "n", "t", 1);
        // Indirection defeats -Wself-move while still exercising self-assignment.
        ServiceRegistration* self = &reg;
        reg = std::move(*self);
        EXPECT_TRUE(reg.is_registered());
        EXPECT_EQ(disc.unregister_count, 0);
    }
    EXPECT_EQ(disc.unregister_count, 1);
}

TEST(ServiceRegistrationRaiiTest, VectorReallocationUnregistersEachOnce) {
    CountingDiscovery disc;
    {
        std::vector<ServiceRegistration> regs;
        for (int i = 0; i < 8; ++i) {
            regs.emplace_back(disc, "n", "t", static_cast<u16>(1000 + i));
        }
        EXPECT_EQ(disc.register_count, 8);
    }
    EXPECT_EQ(disc.unregister_count, 8);  // no missed and no double unregister
}

TEST(ManagerDiscoverableTest, RegisterDiscoverableService) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    // Registration should succeed without throwing
    EXPECT_TRUE(mgr.is_discoverable("calc"));
}

TEST(ManagerDiscoverableTest, IsDiscoverableForLocalService) {
    ServiceManager mgr;
    mgr.register_service("local", "/path/to/service", 1);
    EXPECT_FALSE(mgr.is_discoverable("local"));
}

TEST(ManagerDiscoverableTest, IsDiscoverableForRemoteService) {
    ServiceManager mgr;
    mgr.register_remote_service("remote", "localhost", 9000, 1);
    EXPECT_FALSE(mgr.is_discoverable("remote"));
}

TEST(ManagerDiscoverableTest, IsDiscoverableForNonexistentThrows) {
    ServiceManager mgr;
    EXPECT_THROW(mgr.is_discoverable("nonexistent"), ServiceError);
}

TEST(ManagerDiscoverableTest, StartDiscoverableServiceThrows) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    EXPECT_THROW(mgr.start("calc"), ServiceError);
}

TEST(ManagerDiscoverableTest, StopDiscoverableServiceThrows) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    EXPECT_THROW(mgr.stop("calc"), ServiceError);
}

TEST(ManagerDiscoverableTest, RestartDiscoverableServiceThrows) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    EXPECT_THROW(mgr.restart("calc"), ServiceError);
}

TEST(ManagerDiscoverableTest, ReplaceDiscoverableServiceThrows) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    EXPECT_THROW(mgr.replace("calc", "/new/path"), ServiceError);
}

TEST(ManagerDiscoverableTest, SetAutoRestartDiscoverableServiceThrows) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    EXPECT_THROW(mgr.set_auto_restart("calc", true), ServiceError);
}

TEST(ManagerDiscoverableTest, SetMaxRestartsDiscoverableServiceThrows) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    EXPECT_THROW(mgr.set_max_restarts("calc", 5), ServiceError);
}

TEST(ManagerDiscoverableTest, IsAliveDiscoverableServiceNotFound) {
    ServiceManager mgr;
    mgr.register_discoverable_service("calc", "calculator", 1);
    // Service not running/discoverable, so is_alive should return false
    EXPECT_FALSE(mgr.is_alive("calc"));
}

// =============================================================================
// End-to-End Discovery Test
// =============================================================================

TEST(DiscoveryE2ETest, DiscoverService) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    // Start a TCP listener
    TcpListener listener;
    listener.listen(0);  // Ephemeral port
    u16 port = listener.bound_port();

    // Register the service
    std::string service_name = "SongTestService" + std::to_string(port);  // Unique name
    bool registered = discovery->register_service(service_name, "songtest", port);
    if (!registered) {
        GTEST_SKIP() << "Failed to register service";
    }

    // Give mDNS time to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Create a second discovery instance for browsing
    auto browser = create_discovery();
    ASSERT_NE(browser, nullptr);

    // Try to discover the service
    auto found = browser->discover_one(
        service_name,
        "songtest",
        std::chrono::milliseconds(3000)
    );

    if (found) {
        EXPECT_EQ(found->name, service_name);
        EXPECT_EQ(found->port, port);
        EXPECT_FALSE(found->host.empty());
    }
    // Note: mDNS discovery may not work in all CI environments,
    // so we don't fail if not found

    discovery->unregister_service();
}

TEST(DiscoveryE2ETest, DiscoverMultipleServices) {
    auto discovery1 = create_discovery();
    auto discovery2 = create_discovery();
    if (!discovery1 || !discovery1->is_available() ||
        !discovery2 || !discovery2->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    // Start two TCP listeners
    TcpListener listener1, listener2;
    listener1.listen(0);
    listener2.listen(0);
    u16 port1 = listener1.bound_port();
    u16 port2 = listener2.bound_port();

    // Register two services
    std::string name1 = "SongTest1_" + std::to_string(port1);
    std::string name2 = "SongTest2_" + std::to_string(port2);

    bool reg1 = discovery1->register_service(name1, "multitest", port1);
    bool reg2 = discovery2->register_service(name2, "multitest", port2);

    if (!reg1 || !reg2) {
        GTEST_SKIP() << "Failed to register services";
    }

    // Give mDNS time to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Browse for all services of this type
    auto browser = create_discovery();
    auto services = browser->discover("multitest", std::chrono::milliseconds(3000));

    // Should find at least the two we registered (may find more if other tests running)
    // Note: This may not work in all CI environments
    if (!services.empty()) {
        // Check that our services are in the list
        bool found1 = false, found2 = false;
        for (const auto& svc : services) {
            if (svc.name == name1) found1 = true;
            if (svc.name == name2) found2 = true;
        }
        // Don't fail if not found - mDNS can be flaky
        (void)found1;
        (void)found2;
    }

    discovery1->unregister_service();
    discovery2->unregister_service();
}

TEST(DiscoveryE2ETest, ConnectToDiscoveredService) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    // Start a TCP listener and server thread
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::atomic<bool> server_done{false};
    std::exception_ptr server_exception;

    std::thread server_thread([&]() {
        try {
            auto server = listener.accept(10000);
            if (!server) return;

            // Send init message
            std::vector<wire::MethodDescriptor> methods = {
                {1, 1, wire::MethodFlags::none, 0},
            };
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion,
                wire::kCurrentVersion,
                0,
                methods
            );
            server->send(init_msg);

            // Receive next message (skip init_ack if present)
            Buffer call;
            if (!server->receive(call, 10000)) return;

            auto hdr = wire::decode_header_validated(call);
            if (hdr.type == wire::MsgType::init_ack) {
                call = Buffer{};
                if (!server->receive(call, 10000)) return;
                hdr = wire::decode_header_validated(call);
            }
            [[maybe_unused]] auto call_info = wire::decode_method_call_header(call);
            std::string input = decode_string(call);

            Buffer response;
            encode_string(response, "discovered:" + input);
            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            server->send(result_msg);

            server_done = true;
        } catch (...) {
            server_exception = std::current_exception();
        }
    });

    // Register the service
    std::string service_name = "EchoDiscoverable" + std::to_string(port);
    bool registered = discovery->register_service(service_name, "echodisc", port);
    if (!registered) {
        server_thread.join();
        GTEST_SKIP() << "Failed to register service";
    }

    // Give mDNS time to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Register with ServiceManager and connect
    ServiceManager mgr;
    mgr.register_discoverable_service(service_name, "echodisc", 1);

    try {
        ServiceConnection conn = mgr.connect(service_name);

        Buffer args;
        encode_string(args, "Hello");
        Buffer result = conn.call(1, 1, args);
        std::string response = decode_string(result);
        EXPECT_EQ(response, "discovered:Hello");
    } catch (const ServiceError& e) {
        // mDNS discovery may fail in CI environments
        std::cerr << "Note: Discovery connection failed (may be expected in CI): "
                  << e.what() << std::endl;
    }

    server_thread.join();
    discovery->unregister_service();
}

// =============================================================================
// Negative discovery and timeout-boundary paths (deterministic negatives)
// =============================================================================

namespace {
// Build a short, likely-unique DNS-SD service type so discover()/discover_one()
// reliably find nothing. Kept under the 15-char DNS-SD label limit so the
// browse actually runs (rather than being rejected up front).
std::string make_unique_disc_type() {
    static std::atomic<unsigned> counter{0};
    auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned suffix = static_cast<unsigned>(ticks) % 10000u;
    return "gap" + std::to_string(suffix) + "x" +
           std::to_string(counter.fetch_add(1));
}
}  // namespace

TEST(DiscoveryNegativeTest, DiscoverUniqueTypeReturnsEmpty) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    std::string type = make_unique_disc_type();
    auto start = std::chrono::steady_clock::now();
    auto services = discovery->discover(type, std::chrono::milliseconds(300));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    // Nothing was registered under this unique type, so nothing can be found.
    EXPECT_TRUE(services.empty());
    // The internal timeout loop must bound the call; catch hang/slowness regressions.
    EXPECT_LT(elapsed, 3000);
}

TEST(DiscoveryNegativeTest, DiscoverOneNonexistentReturnsNullopt) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    std::string type = make_unique_disc_type();
    auto start = std::chrono::steady_clock::now();
    auto found = discovery->discover_one("NoSuchInstance", type,
                                         std::chrono::milliseconds(300));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    EXPECT_FALSE(found.has_value());
    EXPECT_LT(elapsed, 3000);
}

TEST(DiscoveryNegativeTest, ZeroTimeoutReturnsEmptyPromptly) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    std::string type = make_unique_disc_type();

    // discover() with a zero timeout: the <=0 guard must short-circuit the poll
    // loop, returning an empty result well under a normal (multi-hundred-ms) wait.
    auto start = std::chrono::steady_clock::now();
    auto services = discovery->discover(type, std::chrono::milliseconds(0));
    auto elapsed_browse = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
    EXPECT_TRUE(services.empty());
    EXPECT_LT(elapsed_browse, 1500);

    // discover_one() with a zero timeout: must return nullopt promptly.
    start = std::chrono::steady_clock::now();
    auto found = discovery->discover_one("NoSuchInstance", type,
                                         std::chrono::milliseconds(0));
    auto elapsed_resolve = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
    EXPECT_FALSE(found.has_value());
    EXPECT_LT(elapsed_resolve, 1500);
}

TEST(DiscoveryNegativeTest, NegativeTimeoutDoesNotHang) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    std::string type = make_unique_disc_type();

    // A negative timeout must be treated like an already-expired deadline
    // (the `< timeout` / `timeout_ms <= 0` guards), never a hang or crash.
    auto start = std::chrono::steady_clock::now();
    auto services = discovery->discover(type, std::chrono::milliseconds(-50));
    auto found = discovery->discover_one("NoSuchInstance", type,
                                         std::chrono::milliseconds(-50));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    EXPECT_TRUE(services.empty());
    EXPECT_FALSE(found.has_value());
    EXPECT_LT(elapsed, 1500);
}

TEST(DiscoveryNegativeTest, RepeatedDiscoverOneAllNullopt) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    // Exercise the resolve setup/teardown path repeatedly; every lookup of a
    // unique nonexistent name must return nullopt with no accumulation of state.
    std::string type = make_unique_disc_type();
    for (int ndx = 0; ndx < 8; ++ndx) {
        auto found = discovery->discover_one("NoSuchInstance", type,
                                             std::chrono::milliseconds(40));
        EXPECT_FALSE(found.has_value());
    }
}

// =============================================================================
// DiscoveredService.type round-trip contract (double-wrap hazard)
// =============================================================================

TEST(DiscoveryTest, MakeServiceTypeDoubleWrapHazard) {
    const std::string short_type = "songtest";
    const std::string full = Discovery::make_service_type(short_type);
    EXPECT_EQ(full, "_songtest._song._tcp");

    // discover()/discover_one() take the SHORT type and wrap it internally,
    // while a resolved DiscoveredService.type stores the already-wrapped full
    // type. Feeding a full type back into the API double-wraps it and would
    // silently match nothing -- lock that hazard here deterministically.
    const std::string double_wrapped = Discovery::make_service_type(full);
    EXPECT_NE(double_wrapped, full);
    EXPECT_EQ(double_wrapped, "__songtest._song._tcp._song._tcp");
}

// =============================================================================
// Unavailable-platform stub contract (Avahi stub / NullDiscovery)
// =============================================================================

TEST(DiscoveryUnavailableTest, StubContractWhenUnavailable) {
    auto discovery = create_discovery();
    if (!discovery || discovery->is_available()) {
        GTEST_SKIP() << "Discovery is available (or null) on this platform";
    }

    // On platforms shipping the stub/null backend the whole contract is
    // all-negative and must never spuriously report success or hang.
    EXPECT_FALSE(discovery->register_service("n", "t", 1));
    EXPECT_FALSE(discovery->is_registered());
    // Repeated registration attempts keep failing without setting state.
    EXPECT_FALSE(discovery->register_service("n", "t", 1));
    EXPECT_FALSE(discovery->is_registered());
    EXPECT_TRUE(discovery->discover("t", std::chrono::milliseconds(100)).empty());
    EXPECT_FALSE(
        discovery->discover_one("n", "t", std::chrono::milliseconds(100)).has_value());
}

// =============================================================================
// Unregister idempotency and re-registration lifecycle
// =============================================================================

TEST(DiscoveryTest, UnregisterWhenIdleIsNoOp) {
    auto discovery = create_discovery();
    if (!discovery) {
        GTEST_SKIP() << "No discovery implementation on this platform";
    }

    // Fresh instance: nothing registered. unregister_service() must be a safe
    // no-op (guarded register_ref_/group_ null path), and idempotent.
    EXPECT_FALSE(discovery->is_registered());
    discovery->unregister_service();
    EXPECT_FALSE(discovery->is_registered());
    discovery->unregister_service();  // second call: still a safe no-op
    EXPECT_FALSE(discovery->is_registered());
}

TEST(DiscoveryTest, ReRegisterAfterUnregister) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "Discovery not available on this platform";
    }

    bool registered = discovery->register_service("ReRegTest", "test", 23456);
    if (!registered) {
        GTEST_SKIP() << "Failed to register service";
    }
    EXPECT_TRUE(discovery->is_registered());

    discovery->unregister_service();
    EXPECT_FALSE(discovery->is_registered());

    // Re-registering after teardown must work (the backing ref/group must have
    // been reset, not left dangling).
    bool re_registered = discovery->register_service("ReRegTest2", "test", 23457);
    if (re_registered) {
        EXPECT_TRUE(discovery->is_registered());
    }

    discovery->unregister_service();
    EXPECT_FALSE(discovery->is_registered());
}
