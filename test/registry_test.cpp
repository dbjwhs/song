// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/registry.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

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

TEST(MemoryRegistryTest, DuplicateRegistrationRejected) {
    MemoryRegistry registry;

    ServiceInfo info1 = {"test", "host1", 1000};
    ServiceInfo info2 = {"test", "host2", 2000};

    EXPECT_TRUE(registry.register_service(info1));
    EXPECT_FALSE(registry.register_service(info2));

    EXPECT_EQ(registry.size(), 1u);

    // Original registration is preserved
    ServiceInfo found = registry.discover("test");
    EXPECT_EQ(found.host, "host1");
    EXPECT_EQ(found.port, 1000u);
}

TEST(MemoryRegistryTest, ReregisterAfterUnregister) {
    MemoryRegistry registry;

    ServiceInfo info1 = {"test", "host1", 1000};
    ServiceInfo info2 = {"test", "host2", 2000};

    EXPECT_TRUE(registry.register_service(info1));
    EXPECT_TRUE(registry.unregister_service("test"));
    EXPECT_TRUE(registry.register_service(info2));

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
    for (int ndx = 0; ndx < 3; ++ndx) {
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

// =============================================================================
// RegistryClient hardening: a malformed/hostile registry response must not
// crash the client (uncaught buffer underflow) or trigger a huge allocation.
// =============================================================================

// Runs a one-shot mock registry: accept a connection, read the client's request,
// and reply with a result message carrying the given (possibly hostile) payload.
static void serve_one_reply(TcpListener& listener, const Buffer& payload) {
    auto conn = listener.accept(5000);
    if (!conn) {
        return;
    }
    Buffer req;
    if (!conn->receive(req, 5000)) {
        return;
    }
    auto hdr = wire::decode_header(req);
    Buffer result = wire::create_result_message(hdr.sequence_id, payload);
    conn->send(result);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    conn->close();
}

TEST(RegistryClientHardeningTest, ListAllOversizedCountReturnsEmpty) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // ListAll result claiming 0xFFFFFFFF entries but carrying no element data.
    Buffer payload;
    encode_u32(payload, 0xFFFFFFFFu);
    std::thread server([&]() { serve_one_reply(listener, payload); });

    RegistryClient client("127.0.0.1", port, 2000);
    std::vector<ServiceInfo> services;
    EXPECT_NO_THROW(services = client.list_all());
    EXPECT_TRUE(services.empty());

    client.close();
    listener.close();
    server.join();
}

TEST(RegistryClientHardeningTest, RegisterEmptyReplyReturnsFalse) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    Buffer empty;  // result with a zero-length payload
    std::thread server([&]() { serve_one_reply(listener, empty); });

    RegistryClient client("127.0.0.1", port, 2000);
    bool ok = true;
    EXPECT_NO_THROW(ok = client.register_service({"svc", "127.0.0.1", 8080}));
    EXPECT_FALSE(ok);

    client.close();
    listener.close();
    server.join();
}

TEST(RegistryClientHardeningTest, DiscoverTruncatedReplyReturnsInvalid) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // Three bytes: far too short for a ServiceInfo (needs >= 10).
    Buffer payload;
    const unsigned char junk[3] = {1, 2, 3};
    payload.write(junk, sizeof(junk));
    std::thread server([&]() { serve_one_reply(listener, payload); });

    RegistryClient client("127.0.0.1", port, 2000);
    ServiceInfo info{"placeholder", "placeholder", 1};
    EXPECT_NO_THROW(info = client.discover("svc"));
    EXPECT_FALSE(info.is_valid());

    client.close();
    listener.close();
    server.join();
}

// =============================================================================
// Coverage appendix: dispatcher error paths, MemoryRegistry stale side effects,
// RegistryClient protocol-mismatch / lifecycle / reconnect branches.
// =============================================================================

namespace {

// Accept one connection, read the client's request, and reply with a message
// built from the request's sequence id by `make_reply`. Then linger briefly so
// the client can read the reply, and close.
template <typename MakeReply>
void serve_one_crafted(TcpListener& listener, MakeReply make_reply) {
    auto conn = listener.accept(5000);
    if (!conn) {
        return;
    }
    Buffer req;
    if (!conn->receive(req, 5000)) {
        return;
    }
    auto hdr = wire::decode_header(req);
    Buffer reply = make_reply(hdr.sequence_id);
    conn->send(reply);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    conn->close();
}

// Accept one connection, read the client's request, but never reply. Linger long
// enough for the client to hit its (short) receive timeout, then close.
void serve_no_reply(TcpListener& listener) {
    auto conn = listener.accept(5000);
    if (!conn) {
        return;
    }
    Buffer req;
    conn->receive(req, 5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    conn->close();
}

// A minimal, reconnection-capable registry server. Unlike a one-shot server it
// returns to accept() whenever a connection ends (EOF or idle timeout), so a
// client that close()s and issues another op is served on a fresh connection.
void serve_registry_loop(TcpListener& listener, MemoryRegistry& reg,
                         std::atomic<bool>& running) {
    while (running) {
        std::unique_ptr<TcpTransport> conn;
        try {
            conn = listener.accept(100);
        } catch (...) {
            continue;
        }
        if (!conn) {
            continue;
        }
        while (running && conn->is_connected()) {
            Buffer msg;
            bool got = false;
            try {
                got = conn->receive(msg, 100);
            } catch (...) {
                break;  // idle timeout or read error -> end this connection
            }
            if (!got) {
                break;  // EOF: peer closed -> go back to accept()
            }
            try {
                auto hdr = wire::decode_header(msg);
                if (hdr.type != wire::MsgType::call) {
                    continue;
                }
                auto [service_id, method_id] = wire::decode_method_call_header(msg);
                if (service_id != kService_Registry) {
                    continue;
                }
                Buffer response_payload;
                dispatch_Registry(reg, method_id, msg, response_payload);
                Buffer response = wire::create_result_message(hdr.sequence_id, response_payload);
                conn->send(response);
            } catch (...) {
                break;
            }
        }
        conn->close();
    }
}

}  // namespace

// =============================================================================
// dispatch_Registry error paths (unknown method + malformed request)
// =============================================================================

TEST(RegistryDispatcherTest, UnknownMethodThrows) {
    MemoryRegistry impl;
    {
        Buffer request;
        Buffer response;
        EXPECT_THROW(dispatch_Registry(impl, 0, request, response), ProtocolError);
    }
    {
        Buffer request;
        Buffer response;
        EXPECT_THROW(dispatch_Registry(impl, 6, request, response), ProtocolError);
    }
    {
        Buffer request;
        Buffer response;
        EXPECT_THROW(dispatch_Registry(impl, 0xFFFF, request, response), ProtocolError);
    }
}

TEST(RegistryDispatcherTest, UnknownMethodMessageIncludesId) {
    MemoryRegistry impl;
    Buffer request;
    Buffer response;
    try {
        dispatch_Registry(impl, 6, request, response);
        FAIL() << "expected ProtocolError for unknown method";
    } catch (const ProtocolError& e) {
        EXPECT_NE(std::string(e.what()).find("6"), std::string::npos);
    }
}

TEST(RegistryDispatcherTest, RegisterMalformedRequestThrows) {
    MemoryRegistry impl;

    // Encode only the name, omitting host and port: decoding host underflows.
    Buffer request;
    encode_string(request, "only-name");
    request.reset_read();

    Buffer response;
    EXPECT_THROW(dispatch_Registry(impl, kMethod_Register, request, response),
                 std::runtime_error);
    EXPECT_EQ(impl.size(), 0u);  // nothing was registered
}

TEST(RegistryDispatcherTest, DiscoverOversizedStringLengthThrows) {
    MemoryRegistry impl;

    // String length prefix claims 1000 bytes but only 4 more bytes follow.
    Buffer request;
    encode_u32(request, 1000u);
    encode_u32(request, 0u);
    request.reset_read();

    Buffer response;
    EXPECT_THROW(dispatch_Registry(impl, kMethod_Discover, request, response),
                 std::runtime_error);
}

// =============================================================================
// MemoryRegistry stale-entry side effects (size() / list_all() / re-register)
// =============================================================================

TEST(MemoryRegistryTest, StaleEntryBlocksReregistration) {
    MemoryRegistry registry(std::chrono::seconds(1));

    EXPECT_TRUE(registry.register_service({"svc", "h1", 1000}));

    // Let the entry go stale WITHOUT any reap (no discover/list_all/cleanup_stale).
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // The stale-but-present entry still blocks a fresh registration of the name.
    EXPECT_FALSE(registry.register_service({"svc", "h2", 2000}));

    // discover() reaps the stale entry, so the name now reports absent even though
    // the re-registration was just rejected: the map is left empty.
    EXPECT_FALSE(registry.discover("svc").is_valid());
    EXPECT_EQ(registry.size(), 0u);
}

TEST(MemoryRegistryTest, SizeCountsStaleUntilListAllReaps) {
    MemoryRegistry registry(std::chrono::seconds(1));

    registry.register_service({"s1", "h1", 1000});
    registry.register_service({"s2", "h2", 2000});
    EXPECT_EQ(registry.size(), 2u);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // size() does not reap: stale entries are still counted.
    EXPECT_EQ(registry.size(), 2u);

    // list_all() evicts stale entries as a side effect.
    auto all = registry.list_all();
    EXPECT_TRUE(all.empty());
    EXPECT_EQ(registry.size(), 0u);
}

TEST(MemoryRegistryTest, ListAllKeepsHeartbeatedEvictsStale) {
    MemoryRegistry registry(std::chrono::seconds(1));

    registry.register_service({"alive", "h1", 1000});
    registry.register_service({"dead", "h2", 2000});

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(registry.heartbeat("alive"));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    auto all = registry.list_all();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].name, "alive");
    EXPECT_EQ(registry.size(), 1u);
    EXPECT_FALSE(registry.discover("dead").is_valid());
}

// =============================================================================
// RegistryClient::call() protocol-mismatch branches (via a crafting mock server)
// =============================================================================

TEST(RegistryClientProtocolTest, SequenceMismatchReturnsFalse) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&]() {
        serve_one_crafted(listener, [](u32 seq) {
            Buffer payload;
            encode_u8(payload, 1);  // a "true" heartbeat body...
            return wire::create_result_message(seq + 1, payload);  // ...but wrong seq
        });
    });

    RegistryClient client("127.0.0.1", port, 2000);
    bool ok = true;
    EXPECT_NO_THROW(ok = client.heartbeat("svc"));
    EXPECT_FALSE(ok);

    client.close();
    listener.close();
    server.join();
}

TEST(RegistryClientProtocolTest, ErrorReplyReturnsFalse) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&]() {
        serve_one_crafted(listener, [](u32 seq) {
            return wire::create_error_message(seq, ErrorCode::unknown_service, "boom");
        });
    });

    RegistryClient client("127.0.0.1", port, 2000);
    bool ok = true;
    EXPECT_NO_THROW(ok = client.heartbeat("svc"));
    EXPECT_FALSE(ok);

    client.close();
    listener.close();
    server.join();
}

TEST(RegistryClientProtocolTest, WrongTypeReplyReturnsFalse) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&]() {
        serve_one_crafted(listener, [](u32 seq) {
            wire::Header hdr;
            hdr.magic = wire::kMagic;
            hdr.flags = wire::MsgFlags::none;
            hdr.type = wire::MsgType::ping;  // matching seq, but not a result
            hdr.reserved = 0;
            hdr.payload_size = 0;
            hdr.sequence_id = seq;
            Buffer msg;
            wire::encode_header(msg, hdr);
            return msg;
        });
    });

    RegistryClient client("127.0.0.1", port, 2000);
    bool ok = true;
    EXPECT_NO_THROW(ok = client.heartbeat("svc"));
    EXPECT_FALSE(ok);

    client.close();
    listener.close();
    server.join();
}

TEST(RegistryClientProtocolTest, NoReplyTimesOutReturnsFalse) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&]() { serve_no_reply(listener); });

    RegistryClient client("127.0.0.1", port, 200);  // short op timeout
    bool ok = true;
    auto start = std::chrono::steady_clock::now();
    EXPECT_NO_THROW(ok = client.heartbeat("svc"));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(ok);
    // Returns around the timeout, not after the server's much longer linger.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
              2000);

    client.close();
    listener.close();
    server.join();
}

// =============================================================================
// RegistryClient lifecycle: default construction, connect() return value, reuse
// =============================================================================

TEST(RegistryClientLifecycleTest, DefaultConstructedOpsFailGracefully) {
    RegistryClient client;  // no cached host/port
    EXPECT_FALSE(client.is_connected());

    bool reg = true;
    bool unreg = true;
    bool hb = true;
    ServiceInfo disc{"placeholder", "placeholder", 1};
    std::vector<ServiceInfo> all;

    EXPECT_NO_THROW(reg = client.register_service({"svc", "127.0.0.1", 8080}));
    EXPECT_NO_THROW(unreg = client.unregister_service("svc"));
    EXPECT_NO_THROW(hb = client.heartbeat("svc"));
    EXPECT_NO_THROW(disc = client.discover("svc"));
    EXPECT_NO_THROW(all = client.list_all());

    EXPECT_FALSE(reg);
    EXPECT_FALSE(unreg);
    EXPECT_FALSE(hb);
    EXPECT_FALSE(disc.is_valid());
    EXPECT_TRUE(all.empty());
}

TEST(RegistryClientLifecycleTest, ConnectRefusedReturnsFalse) {
    RegistryClient client;
    EXPECT_FALSE(client.connect("127.0.0.1", 19997, 500));
    EXPECT_FALSE(client.is_connected());
}

TEST(RegistryClientReconnectTest, CloseThenReuseReconnects) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    MemoryRegistry reg;
    std::atomic<bool> running{true};
    std::thread server([&]() { serve_registry_loop(listener, reg, running); });

    RegistryClient client("127.0.0.1", port);
    EXPECT_TRUE(client.is_connected());
    EXPECT_TRUE(client.register_service({"reuse-svc", "10.0.0.8", 6000}));

    client.close();
    EXPECT_FALSE(client.is_connected());

    // ensure_connected() should transparently reconnect using cached host/port,
    // and the shared server registry still knows "reuse-svc".
    EXPECT_TRUE(client.heartbeat("reuse-svc"));
    EXPECT_TRUE(client.is_connected());

    client.close();
    running = false;
    listener.close();
    server.join();
}

// =============================================================================
// RegistryClient explicit connect() success (fixture-backed live registry)
// =============================================================================

TEST_F(RegistryE2ETest, ExplicitConnectReturnsTrue) {
    RegistryClient client;  // default-constructed, then connect explicitly
    EXPECT_FALSE(client.is_connected());

    EXPECT_TRUE(client.connect("127.0.0.1", kTestPort));
    EXPECT_TRUE(client.is_connected());

    EXPECT_TRUE(client.register_service({"lc-svc", "10.0.0.9", 7000}));
    ServiceInfo found = client.discover("lc-svc");
    EXPECT_EQ(found.name, "lc-svc");
    EXPECT_EQ(found.host, "10.0.0.9");
    EXPECT_EQ(found.port, 7000u);
}
