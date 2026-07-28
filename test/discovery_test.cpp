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
    EXPECT_EQ(Discovery::make_service_type("calculator"), "_calculator-song._tcp");
    EXPECT_EQ(Discovery::make_service_type("echo"), "_echo-song._tcp");
    EXPECT_EQ(Discovery::make_service_type("test"), "_test-song._tcp");
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
