// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/registry.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <thread>

using namespace song;

// =============================================================================
// ServiceInfo Tests
// =============================================================================

TEST(ServiceInfoTest, EncodeDecodeRoundTrip) {
    ServiceInfo info;
    info.name = "calculator";
    info.host = "192.168.1.50";
    info.port = 12345;

    Buffer buf;
    info.encode(buf);
    buf.reset_read();

    ServiceInfo decoded = ServiceInfo::decode(buf);
    EXPECT_EQ(decoded.name, "calculator");
    EXPECT_EQ(decoded.host, "192.168.1.50");
    EXPECT_EQ(decoded.port, 12345u);
}

TEST(ServiceInfoTest, IsValidChecksAllFields) {
    ServiceInfo info;
    EXPECT_FALSE(info.is_valid());

    info.name = "test";
    EXPECT_FALSE(info.is_valid());

    info.host = "localhost";
    EXPECT_FALSE(info.is_valid());

    info.port = 8080;
    EXPECT_TRUE(info.is_valid());
}

TEST(ServiceInfoTest, EmptyNameInvalid) {
    ServiceInfo info;
    info.host = "localhost";
    info.port = 8080;
    EXPECT_FALSE(info.is_valid());
}

TEST(ServiceInfoTest, EmptyHostInvalid) {
    ServiceInfo info;
    info.name = "test";
    info.port = 8080;
    EXPECT_FALSE(info.is_valid());
}

TEST(ServiceInfoTest, ZeroPortInvalid) {
    ServiceInfo info;
    info.name = "test";
    info.host = "localhost";
    info.port = 0;
    EXPECT_FALSE(info.is_valid());
}

// =============================================================================
// MemoryRegistry Tests
// =============================================================================

TEST(MemoryRegistryTest, RegisterAndDiscover) {
    MemoryRegistry registry;

    ServiceInfo info;
    info.name = "test-service";
    info.host = "10.0.0.1";
    info.port = 9000;

    EXPECT_TRUE(registry.register_service(info));
    EXPECT_EQ(registry.size(), 1u);

    ServiceInfo found = registry.discover("test-service");
    EXPECT_EQ(found.name, "test-service");
    EXPECT_EQ(found.host, "10.0.0.1");
    EXPECT_EQ(found.port, 9000u);
}

TEST(MemoryRegistryTest, DiscoverNonexistent) {
    MemoryRegistry registry;
    ServiceInfo found = registry.discover("nonexistent");
    EXPECT_FALSE(found.is_valid());
}

TEST(MemoryRegistryTest, RegisterInvalid) {
    MemoryRegistry registry;

    ServiceInfo invalid;
    EXPECT_FALSE(registry.register_service(invalid));
    EXPECT_EQ(registry.size(), 0u);
}

TEST(MemoryRegistryTest, Unregister) {
    MemoryRegistry registry;

    ServiceInfo info;
    info.name = "test-service";
    info.host = "localhost";
    info.port = 8080;

    EXPECT_TRUE(registry.register_service(info));
    EXPECT_EQ(registry.size(), 1u);

    EXPECT_TRUE(registry.unregister_service("test-service"));
    EXPECT_EQ(registry.size(), 0u);

    // Unregister again should return false
    EXPECT_FALSE(registry.unregister_service("test-service"));
}

TEST(MemoryRegistryTest, Heartbeat) {
    MemoryRegistry registry;

    ServiceInfo info;
    info.name = "test-service";
    info.host = "localhost";
    info.port = 8080;

    registry.register_service(info);

    // Heartbeat should succeed
    EXPECT_TRUE(registry.heartbeat("test-service"));

    // Heartbeat for nonexistent should fail
    EXPECT_FALSE(registry.heartbeat("nonexistent"));
}

TEST(MemoryRegistryTest, ListAll) {
    MemoryRegistry registry;

    ServiceInfo info1 = {"service1", "host1", 1000};
    ServiceInfo info2 = {"service2", "host2", 2000};
    ServiceInfo info3 = {"service3", "host3", 3000};

    registry.register_service(info1);
    registry.register_service(info2);
    registry.register_service(info3);

    auto all = registry.list_all();
    EXPECT_EQ(all.size(), 3u);

    // Check all services are present (order not guaranteed)
    std::set<std::string> names;
    for (const auto& s : all) {
        names.insert(s.name);
    }
    EXPECT_TRUE(names.count("service1"));
    EXPECT_TRUE(names.count("service2"));
    EXPECT_TRUE(names.count("service3"));
}

TEST(MemoryRegistryTest, OverwriteExisting) {
    MemoryRegistry registry;

    ServiceInfo info1 = {"test", "host1", 1000};
    ServiceInfo info2 = {"test", "host2", 2000};

    registry.register_service(info1);
    registry.register_service(info2);

    EXPECT_EQ(registry.size(), 1u);

    ServiceInfo found = registry.discover("test");
    EXPECT_EQ(found.host, "host2");
    EXPECT_EQ(found.port, 2000u);
}

TEST(MemoryRegistryTest, StaleServiceExpires) {
    // Set very short expiry for testing
    MemoryRegistry registry(std::chrono::seconds(1));

    ServiceInfo info = {"test", "localhost", 8080};
    registry.register_service(info);

    EXPECT_TRUE(registry.discover("test").is_valid());

    // Wait for expiry
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Should be gone now
    EXPECT_FALSE(registry.discover("test").is_valid());
}

TEST(MemoryRegistryTest, HeartbeatKeepsAlive) {
    // Set short expiry for testing
    MemoryRegistry registry(std::chrono::seconds(2));

    ServiceInfo info = {"test", "localhost", 8080};
    registry.register_service(info);

    // Send heartbeats to keep alive
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        EXPECT_TRUE(registry.heartbeat("test"));
    }

    // Should still be discoverable
    EXPECT_TRUE(registry.discover("test").is_valid());
}

TEST(MemoryRegistryTest, CleanupStale) {
    MemoryRegistry registry(std::chrono::seconds(1));

    ServiceInfo info1 = {"service1", "host1", 1000};
    ServiceInfo info2 = {"service2", "host2", 2000};

    registry.register_service(info1);
    registry.register_service(info2);
    EXPECT_EQ(registry.size(), 2u);

    // Let one expire but heartbeat the other
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    registry.heartbeat("service1");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    registry.cleanup_stale();

    EXPECT_TRUE(registry.discover("service1").is_valid());
    EXPECT_FALSE(registry.discover("service2").is_valid());
}

// =============================================================================
// Registry Dispatcher Tests
// =============================================================================

TEST(RegistryDispatcherTest, RegisterMethod) {
    MemoryRegistry impl;

    Buffer request;
    ServiceInfo info = {"test", "localhost", 8080};
    info.encode(request);
    request.reset_read();

    Buffer response;
    dispatch_Registry(impl, kMethod_Register, request, response);
    response.reset_read();

    u8 result = decode_u8(response);
    EXPECT_EQ(result, 1u);

    EXPECT_TRUE(impl.discover("test").is_valid());
}

TEST(RegistryDispatcherTest, DiscoverMethod) {
    MemoryRegistry impl;
    impl.register_service({"test", "localhost", 8080});

    Buffer request;
    encode_string(request, "test");
    request.reset_read();

    Buffer response;
    dispatch_Registry(impl, kMethod_Discover, request, response);
    response.reset_read();

    ServiceInfo found = ServiceInfo::decode(response);
    EXPECT_EQ(found.name, "test");
    EXPECT_EQ(found.host, "localhost");
    EXPECT_EQ(found.port, 8080u);
}

TEST(RegistryDispatcherTest, ListAllMethod) {
    MemoryRegistry impl;
    impl.register_service({"s1", "h1", 1000});
    impl.register_service({"s2", "h2", 2000});

    Buffer request;
    Buffer response;
    dispatch_Registry(impl, kMethod_ListAll, request, response);
    response.reset_read();

    u32 count = decode_u32(response);
    EXPECT_EQ(count, 2u);
}

TEST(RegistryDispatcherTest, UnregisterMethod) {
    MemoryRegistry impl;
    impl.register_service({"test", "localhost", 8080});

    Buffer request;
    encode_string(request, "test");
    request.reset_read();

    Buffer response;
    dispatch_Registry(impl, kMethod_Unregister, request, response);
    response.reset_read();

    u8 result = decode_u8(response);
    EXPECT_EQ(result, 1u);

    EXPECT_FALSE(impl.discover("test").is_valid());
}

TEST(RegistryDispatcherTest, HeartbeatMethod) {
    MemoryRegistry impl;
    impl.register_service({"test", "localhost", 8080});

    Buffer request;
    encode_string(request, "test");
    request.reset_read();

    Buffer response;
    dispatch_Registry(impl, kMethod_Heartbeat, request, response);
    response.reset_read();

    u8 result = decode_u8(response);
    EXPECT_EQ(result, 1u);
}

// =============================================================================
// RegistryClient End-to-End Tests
// =============================================================================

class RegistryE2ETest : public ::testing::Test {
protected:
    static constexpr u16 kTestPort = 19999;

    std::unique_ptr<TcpListener> listener_;
    std::thread server_thread_;
    std::atomic<bool> server_running_{false};
    MemoryRegistry registry_;

    void SetUp() override {
        // Start a simple registry server
        listener_ = std::make_unique<TcpListener>();
        listener_->listen(kTestPort);
        server_running_ = true;

        server_thread_ = std::thread([this]() {
            while (server_running_) {
                try {
                    auto transport = listener_->accept(100);
                    if (!transport) continue;

                    while (server_running_ && transport->is_connected()) {
                        Buffer msg;
                        try {
                            if (!transport->receive(msg, 100)) continue;
                        } catch (...) {
                            continue;
                        }

                        auto hdr = wire::decode_header(msg);
                        if (hdr.type != wire::MsgType::call) continue;

                        auto [service_id, method_id] = wire::decode_method_call_header(msg);
                        if (service_id != kService_Registry) continue;

                        Buffer response_payload;
                        dispatch_Registry(registry_, method_id, msg, response_payload);

                        Buffer response = wire::create_result_message(hdr.sequence_id, response_payload);
                        transport->send(response);
                    }
                } catch (...) {
                    // Ignore errors during shutdown
                }
            }
        });

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        server_running_ = false;
        listener_->close();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
};

TEST_F(RegistryE2ETest, ConnectAndRegister) {
    RegistryClient client("127.0.0.1", kTestPort);
    EXPECT_TRUE(client.is_connected());

    ServiceInfo info = {"my-service", "10.0.0.1", 5000};
    EXPECT_TRUE(client.register_service(info));

    ServiceInfo found = client.discover("my-service");
    EXPECT_EQ(found.name, "my-service");
    EXPECT_EQ(found.host, "10.0.0.1");
    EXPECT_EQ(found.port, 5000u);
}

TEST_F(RegistryE2ETest, RegisterMultiple) {
    RegistryClient client("127.0.0.1", kTestPort);

    client.register_service({"s1", "h1", 1000});
    client.register_service({"s2", "h2", 2000});
    client.register_service({"s3", "h3", 3000});

    auto all = client.list_all();
    EXPECT_EQ(all.size(), 3u);
}

TEST_F(RegistryE2ETest, DiscoverNonexistent) {
    RegistryClient client("127.0.0.1", kTestPort);

    ServiceInfo found = client.discover("nonexistent");
    EXPECT_FALSE(found.is_valid());
}

TEST_F(RegistryE2ETest, Unregister) {
    RegistryClient client("127.0.0.1", kTestPort);

    client.register_service({"test", "localhost", 8080});
    EXPECT_TRUE(client.unregister_service("test"));

    ServiceInfo found = client.discover("test");
    EXPECT_FALSE(found.is_valid());
}

TEST_F(RegistryE2ETest, Heartbeat) {
    RegistryClient client("127.0.0.1", kTestPort);

    client.register_service({"test", "localhost", 8080});
    EXPECT_TRUE(client.heartbeat("test"));
    EXPECT_FALSE(client.heartbeat("nonexistent"));
}

TEST_F(RegistryE2ETest, ConnectionFailure) {
    // Connect to a port that's not listening
    RegistryClient client("127.0.0.1", 19998, 1000);
    EXPECT_FALSE(client.is_connected());

    // Operations should fail gracefully
    EXPECT_FALSE(client.register_service({"test", "localhost", 8080}));
}
