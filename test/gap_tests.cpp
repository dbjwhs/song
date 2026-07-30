// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <thread>
#include <atomic>
#include <filesystem>

using namespace song;

// =============================================================================
// Common test helpers
// =============================================================================

// Simple echo server: sends init, handles calls by echoing payload back
static void run_echo_server(TcpListener& listener, u16 server_version,
                            u32 server_caps, int max_clients = 1) {
    for (int c = 0; c < max_clients; ++c) {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            // Send init with specified version and capabilities
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, server_version, server_caps);
            client->send(init_msg);

            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;

                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::init_ack) continue;  // Skip ack

                if (hdr.type == wire::MsgType::call) {
                    [[maybe_unused]] auto [sid, mid] = wire::decode_method_call_header(msg);
                    // Echo: copy remaining payload as result
                    Buffer response;
                    if (msg.size() > msg.read_pos()) {
                        response.write(msg.data() + msg.read_pos(),
                                       msg.size() - msg.read_pos());
                    }
                    Buffer result = wire::create_result_message(hdr.sequence_id, response);
                    client->send(result);
                }
            }
        } catch (...) {}
    }
}

// =============================================================================
// C1: Version Negotiation Cross-Compatibility
// =============================================================================

TEST(VersionCrossCompatTest, V11ClientWithV10Server) {
    // Server advertises v1.0, client is v1.1
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        run_echo_server(listener, wire::make_version(1, 0), 0);
    });

    // v1.1 client connects
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    // Should negotiate down to v1.0
    EXPECT_EQ(conn.negotiated_version(), wire::make_version(1, 0));
    // Peer caps should be 0 (v1.0 server sends caps=0)
    EXPECT_EQ(conn.peer_capabilities(), 0u);

    // RPC should still work
    Buffer args;
    encode_i32(args, 42);
    Buffer result = conn.call(1, 1, args);
    EXPECT_EQ(decode_i32(result), 42);

    // Send shutdown
    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);

    listener.close();
    server.join();
}

TEST(VersionCrossCompatTest, V11ClientWithV11Server) {
    // Both sides v1.1 with capabilities
    u32 server_caps = static_cast<u32>(
        wire::Capability::streaming | wire::Capability::objects);
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener, server_caps]() {
        run_echo_server(listener, wire::kCurrentVersion, server_caps);
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));

    // Client sets its own capabilities before handshake
    u32 client_caps = static_cast<u32>(
        wire::Capability::streaming | wire::Capability::tls);
    conn.set_local_capabilities(client_caps);
    conn.init_handshake();

    // Should negotiate to v1.1
    EXPECT_EQ(conn.negotiated_version(), wire::make_version(1, 1));
    // Peer sent streaming + objects
    EXPECT_EQ(conn.peer_capabilities(), server_caps);
    // Negotiated = intersection: only streaming
    EXPECT_TRUE(conn.has_capability(wire::Capability::streaming));
    EXPECT_FALSE(conn.has_capability(wire::Capability::objects));
    EXPECT_FALSE(conn.has_capability(wire::Capability::tls));

    // RPC works
    Buffer args;
    encode_string(args, "hello");
    Buffer result = conn.call(1, 1, args);
    EXPECT_EQ(decode_string(result), "hello");

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();
}

TEST(VersionCrossCompatTest, V10ClientSimulation) {
    // Simulate a v1.0 client (sends no init_ack, caps=0) talking to v1.1 server
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    u32 server_caps = static_cast<u32>(wire::Capability::streaming);

    std::thread server([&listener, server_caps]() {
        run_echo_server(listener, wire::kCurrentVersion, server_caps);
    });

    // Manual v1.0-style client: connect, receive init, do NOT send init_ack
    TcpTransport tcp;
    tcp.connect("127.0.0.1", port, 5000);

    // Receive server init
    Buffer init_msg;
    ASSERT_TRUE(tcp.receive(init_msg, 5000));
    init_msg.reset_read();
    auto hdr = wire::decode_header_validated(init_msg);
    EXPECT_EQ(hdr.type, wire::MsgType::init);
    auto init = wire::decode_init(init_msg);
    EXPECT_EQ(init.current_version, wire::kCurrentVersion);
    EXPECT_EQ(init.capabilities, server_caps);

    // Do NOT send init_ack -- this is what a v1.0 client would do
    // Just send a call directly
    Buffer args;
    encode_i32(args, 99);
    Buffer call_msg = wire::create_call_message(1, 1, 1, args);
    tcp.send(call_msg);

    // Should get result back
    Buffer result;
    ASSERT_TRUE(tcp.receive(result, 5000));
    result.reset_read();
    auto resp_hdr = wire::decode_header_validated(result);
    EXPECT_EQ(resp_hdr.type, wire::MsgType::result);

    tcp.close();
    listener.close();
    server.join();
}

TEST(VersionCrossCompatTest, MajorVersionMismatchRejects) {
    // Simulate a v2.0 server -- v1.1 client should reject
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        try {
            auto client = listener.accept(5000);
            if (!client) return;
            // Send init claiming v2.0 with first_version=2.0
            Buffer init_msg = wire::create_init_message(
                wire::make_version(2, 0), wire::make_version(2, 0), 0);
            client->send(init_msg);
            // Client will reject and disconnect
            Buffer msg;
            client->receive(msg, 2000);
        } catch (...) {
            // Expected: client disconnects
        }
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));

    EXPECT_THROW(conn.init_handshake(), VersionMismatchError);

    listener.close();
    server.join();
}

// =============================================================================
// C2: TLS + ServiceRuntime Integration
// =============================================================================

#if defined(SONG_HAS_TLS)

static std::string test_cert_dir() {
    std::vector<std::string> candidates = {
        "test/certs", "../test/certs", "../../test/certs",
#ifdef SONG_SOURCE_DIR
        SONG_SOURCE_DIR "/test/certs",
#endif
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c + "/ca_cert.pem")) return c;
    }
    return "test/certs";
}

static void tls_echo_dispatcher(u16, Buffer& request, Buffer& response) {
    std::string s = decode_string(request);
    encode_string(response, "tls:" + s);
}

static TlsTransport make_test_tls_client(u16 port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    TlsConfig cli_config(
        test_cert_dir() + "/client_cert.pem",
        test_cert_dir() + "/client_key.pem",
        test_cert_dir() + "/ca_cert.pem");
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsTransport transport(sock, std::move(cli_config), "127.0.0.1", port);
    transport.handshake();
    return transport;
}

TEST(TlsIntegrationTest, RpcOverTlsWithDispatcher) {
    if (!std::filesystem::exists(test_cert_dir() + "/ca_cert.pem")) {
        GTEST_SKIP() << "Test certs not found";
    }

    TlsConfig srv_config(
        test_cert_dir() + "/server_cert.pem",
        test_cert_dir() + "/server_key.pem",
        test_cert_dir() + "/ca_cert.pem");
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener tls_listener;
    tls_listener.listen(srv_config, 0);
    u16 port = tls_listener.bound_port();

    std::thread server([&tls_listener]() {
        auto conn = tls_listener.accept(5000);
        if (!conn) return;
        try {
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, 0);
            conn->send(init_msg);

            for (;;) {
                Buffer msg;
                if (!conn->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::init_ack) continue;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::call) {
                    [[maybe_unused]] auto [sid, mid] = wire::decode_method_call_header(msg);
                    Buffer response;
                    tls_echo_dispatcher(mid, msg, response);
                    Buffer result = wire::create_result_message(hdr.sequence_id, response);
                    conn->send(result);
                }
            }
        } catch (...) {}
    });

    auto tls_client = make_test_tls_client(port);
    ServiceConnection conn(&tls_client);
    conn.init_handshake();

    // Multiple RPC calls over TLS
    for (int ndx = 0; ndx < 5; ++ndx) {
        Buffer args;
        encode_string(args, std::to_string(ndx));
        Buffer result = conn.call(1, 1, args);
        EXPECT_EQ(decode_string(result), "tls:" + std::to_string(ndx));
    }

    Buffer shutdown = wire::create_shutdown_message();
    tls_client.send(shutdown);
    tls_listener.close();
    server.join();
}

#endif // SONG_HAS_TLS

// =============================================================================
// C3: Streaming + Security (HMAC)
// =============================================================================

TEST(StreamSecurityTest, StreamOverHmac) {
    std::string key = "stream-hmac-test-key-32-bytes!!";
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener, &key]() {
        auto tcp = listener.accept(5000);
        if (!tcp) return;
        SecurityConfig security(key);
        SecureTransport transport(std::move(tcp), std::move(security));

        StreamWriter writer(transport, 42);
        for (int ndx = 0; ndx < 3; ++ndx) {
            Buffer chunk;
            encode_i32(chunk, ndx * 10);
            writer.write(chunk);
        }
        writer.end();
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    SecurityConfig security(key);
    SecureTransport client(std::move(tcp), std::move(security));

    std::vector<i32> values;
    for (;;) {
        Buffer msg;
        ASSERT_TRUE(client.receive(msg, 5000));
        msg.reset_read();
        auto hdr = wire::decode_header(msg);
        if (hdr.type == wire::MsgType::stream_end) break;
        ASSERT_EQ(hdr.type, wire::MsgType::stream);
        values.push_back(decode_i32(msg));
    }

    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], 0);
    EXPECT_EQ(values[1], 10);
    EXPECT_EQ(values[2], 20);

    client.close();
    listener.close();
    server.join();
}

#if defined(SONG_HAS_TLS)

TEST(StreamSecurityTest, StreamOverTls) {
    if (!std::filesystem::exists(test_cert_dir() + "/ca_cert.pem")) {
        GTEST_SKIP() << "Test certs not found";
    }

    TlsConfig srv_config(
        test_cert_dir() + "/server_cert.pem",
        test_cert_dir() + "/server_key.pem",
        test_cert_dir() + "/ca_cert.pem");
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener tls_listener;
    tls_listener.listen(srv_config, 0);
    u16 port = tls_listener.bound_port();

    std::thread server([&tls_listener]() {
        auto conn = tls_listener.accept(5000);
        if (!conn) return;
        StreamWriter writer(*conn, 99);
        for (int ndx = 0; ndx < 5; ++ndx) {
            Buffer chunk;
            encode_string(chunk, "chunk-" + std::to_string(ndx));
            writer.write(chunk);
        }
        writer.end();
    });

    auto client = make_test_tls_client(port);

    std::vector<std::string> chunks;
    for (;;) {
        Buffer msg;
        ASSERT_TRUE(client.receive(msg, 5000));
        msg.reset_read();
        auto hdr = wire::decode_header(msg);
        if (hdr.type == wire::MsgType::stream_end) break;
        ASSERT_EQ(hdr.type, wire::MsgType::stream);
        EXPECT_TRUE(wire::has_flag(hdr.flags, wire::MsgFlags::encrypted));
        chunks.push_back(decode_string(msg));
    }

    ASSERT_EQ(chunks.size(), 5u);
    EXPECT_EQ(chunks[0], "chunk-0");
    EXPECT_EQ(chunks[4], "chunk-4");

    client.close();
    tls_listener.close();
    server.join();
}

#endif // SONG_HAS_TLS

// =============================================================================
// C4: Streaming Error Paths
// =============================================================================

TEST(StreamErrorTest, ServiceThrowsMidStream) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, 0);
            client->send(init_msg);

            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::init_ack) continue;
                if (hdr.type == wire::MsgType::shutdown) break;

                if (hdr.type == wire::MsgType::call) {
                    [[maybe_unused]] auto [sid, mid] = wire::decode_method_call_header(msg);
                    // Send 2 chunks then error instead of stream_end
                    for (int ndx = 0; ndx < 2; ++ndx) {
                        Buffer chunk;
                        encode_i32(chunk, ndx);
                        Buffer stream_msg = wire::create_stream_message(hdr.sequence_id, chunk);
                        client->send(stream_msg);
                    }
                    Buffer err = wire::create_error_message(
                        hdr.sequence_id, ErrorCode::unknown_method, "mid-stream failure");
                    client->send(err);
                }
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_i32(args, 5);
    EXPECT_THROW(conn.call_streaming(1, 1, args), ServiceError);

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();
}

TEST(StreamErrorTest, EmptyStreamViaServiceConnection) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, 0);
            client->send(init_msg);

            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::init_ack) continue;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::call) {
                    Buffer end_msg = wire::create_stream_end_message(hdr.sequence_id);
                    client->send(end_msg);
                }
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    StreamReader reader = conn.call_streaming(1, 1, args);
    EXPECT_TRUE(reader.complete());
    EXPECT_EQ(reader.chunk_count(), 0u);

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();
}

// =============================================================================
// C5: init_ack Full Handshake Through ServiceRuntime
// =============================================================================

TEST(InitAckIntegrationTest, RuntimeReceivesClientCapabilities) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::atomic<u32> received_peer_caps{0};

    std::thread server([&listener, &received_peer_caps]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            u32 srv_caps = static_cast<u32>(
                wire::Capability::streaming | wire::Capability::hmac);
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, srv_caps);
            client->send(init_msg);

            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::shutdown) break;

                if (hdr.type == wire::MsgType::init_ack) {
                    auto peer_init = wire::decode_init(msg);
                    received_peer_caps = peer_init.capabilities;
                    continue;
                }

                if (hdr.type == wire::MsgType::call) {
                    [[maybe_unused]] auto [sid, mid] = wire::decode_method_call_header(msg);
                    Buffer response;
                    encode_string(response, "ok");
                    Buffer result = wire::create_result_message(hdr.sequence_id, response);
                    client->send(result);
                }
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));

    u32 client_caps = static_cast<u32>(
        wire::Capability::streaming | wire::Capability::tls | wire::Capability::ext_0);
    conn.set_local_capabilities(client_caps);
    conn.init_handshake();

    // negotiated = local & peer
    // Client has streaming+tls+ext_0, server has streaming+hmac
    // Intersection = streaming only
    EXPECT_TRUE(conn.has_capability(wire::Capability::streaming));
    EXPECT_FALSE(conn.has_capability(wire::Capability::hmac));  // server has it, client doesn't
    EXPECT_FALSE(conn.has_capability(wire::Capability::tls));   // client has it, server doesn't
    // Peer caps (raw) should include both server flags
    EXPECT_EQ(conn.peer_capabilities(),
        static_cast<u32>(wire::Capability::streaming | wire::Capability::hmac));

    // Make a call to ensure connection is working
    Buffer args;
    Buffer result = conn.call(1, 1, args);
    EXPECT_EQ(decode_string(result), "ok");

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();

    // Server should have received client's capabilities via init_ack
    EXPECT_EQ(received_peer_caps, client_caps);
}

// =============================================================================
// M1: ServiceRuntime.run_tcp() via handle_message
// =============================================================================

TEST(RuntimeTcpTest, DispatcherCalledThroughRuntime) {
    ServiceRuntime runtime;
    std::atomic<int> call_count{0};

    runtime.register_dispatcher(1, [&call_count](u16 method_id, Buffer& req, Buffer& resp) {
        call_count++;
        if (method_id == 1) {
            i32 val = decode_i32(req);
            encode_i32(resp, val * 3);
        }
    });
    runtime.register_method(1, 1);

    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener, &runtime]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            runtime.send_init_confirmation_transport(*client);
            std::unordered_set<i32> tracked;
            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.magic != wire::kMagic) break;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::init_ack) continue;
                runtime.handle_message(hdr, msg, *client, tracked);
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    for (int ndx = 0; ndx < 10; ++ndx) {
        Buffer args;
        encode_i32(args, ndx);
        Buffer result = conn.call(1, 1, args);
        EXPECT_EQ(decode_i32(result), ndx * 3);
    }

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();

    EXPECT_EQ(call_count, 10);
}

// Regression: when a stream dispatcher throws, the StreamWriter destructor
// used to send stream_end during unwind BEFORE the catch sent the error
// reply. Clients stop reading at stream_end, so the failure arrived as a
// clean-looking (possibly truncated) stream and the error was never read.
// The runtime now aborts the writer and the error reply terminates the
// stream. The fd dispatch path carries the identical fix.
TEST(RuntimeTcpTest, ThrowingStreamDispatcherDeliversErrorNotCleanStream) {
    ServiceRuntime runtime;

    runtime.register_stream_dispatcher(
        7, [](u16, Buffer&, StreamWriter& writer) {
            Buffer chunk;
            encode_i32(chunk, 1);
            writer.write(chunk);  // one good chunk precedes the failure
            throw std::runtime_error("dispatcher exploded mid-stream");
        });

    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener, &runtime]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            runtime.send_init_confirmation_transport(*client);
            std::unordered_set<i32> tracked;
            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.magic != wire::kMagic) break;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::init_ack) continue;
                runtime.handle_message(hdr, msg, *client, tracked);
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    bool threw = false;
    try {
        conn.call_streaming(7, 1, args);
    } catch (const ServiceError& e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("dispatcher exploded"),
                  std::string::npos);
    }
    EXPECT_TRUE(threw) << "client saw a clean stream instead of the error";

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();
}

// The transport wrapper lets a service layer security onto the TCP serving
// loops (SecureTransport for HMAC). run_tcp_multi never returns, so runtime
// and listener are deliberately leaked and the server thread detached; the
// ephemeral port is abandoned when the test process exits.
TEST(RuntimeTcpTest, TransportWrapperAppliesToAcceptedClients) {
    static const std::string kKey = "0123456789abcdef0123456789abcdef";

    auto* runtime = new ServiceRuntime;
    runtime->register_dispatcher(1, [](u16, Buffer& req, Buffer& resp) {
        i32 v = decode_i32(req);
        encode_i32(resp, v + 1);
    });
    runtime->set_transport_wrapper(
        [](std::unique_ptr<Transport> t) -> std::unique_ptr<Transport> {
            return std::make_unique<SecureTransport>(std::move(t),
                                                     SecurityConfig(kKey));
        });

    auto* listener = new TcpListener;
    listener->listen(0);
    u16 port = listener->bound_port();
    std::thread([runtime, listener]() {
        runtime->run_tcp_multi(*listener);
    }).detach();

    // Keyed client round-trips through the wrapped transport.
    {
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect("127.0.0.1", port, 5000);
        auto sec = std::make_unique<SecureTransport>(std::move(tcp),
                                                     SecurityConfig(kKey));
        ServiceConnection conn(std::move(sec));
        conn.init_handshake();
        Buffer args;
        encode_i32(args, 41);
        Buffer result = conn.call(1, 1, args);
        EXPECT_EQ(decode_i32(result), 42);
    }

    // Keyless client cannot complete a call against the wrapped server.
    {
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect("127.0.0.1", port, 5000);
        ServiceConnection conn(std::move(tcp));
        bool rejected = false;
        try {
            conn.init_handshake();
            Buffer args;
            encode_i32(args, 1);
            conn.call(1, 1, args);
        } catch (const std::exception&) {
            rejected = true;
        }
        EXPECT_TRUE(rejected);
    }
}

// Incremental streaming: chunks must reach the handler as they arrive, not
// after stream_end, and the chunk timeout must be a real parameter. The old
// hardcoded 5000 ms killed any stream whose first chunk took longer (found
// by savannah: a real agent thinks past five seconds before its first
// token), and the eager StreamReader hid all latency until completion.
TEST(RuntimeTcpTest, IncrementalStreamingDeliversChunksAsTheyArrive) {
    using Clock = std::chrono::steady_clock;

    ServiceRuntime runtime;
    runtime.register_stream_dispatcher(
        9, [](u16, Buffer&, StreamWriter& writer) {
            for (i32 ndx = 0; ndx < 3; ++ndx) {
                Buffer chunk;
                encode_i32(chunk, ndx);
                writer.write(chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();
    std::thread server([&listener, &runtime]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            runtime.send_init_confirmation_transport(*client);
            std::unordered_set<i32> tracked;
            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.magic != wire::kMagic) break;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::init_ack) continue;
                runtime.handle_message(hdr, msg, *client, tracked);
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    std::vector<i32> values;
    std::vector<Clock::time_point> arrivals;
    conn.call_streaming(9, 1, args, [&](Buffer& chunk) {
        values.push_back(decode_i32(chunk));
        arrivals.push_back(Clock::now());
    }, /*chunk_timeout_ms=*/3000);
    auto done = Clock::now();

    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], 0);
    EXPECT_EQ(values[2], 2);
    // The first chunk must have arrived well before the stream finished
    // (server sleeps 200ms after each write; eager collection would put
    // every arrival at effectively the same instant as completion).
    auto lead = std::chrono::duration_cast<std::chrono::milliseconds>(
                    done - arrivals.front())
                    .count();
    EXPECT_GT(lead, 300);

    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();
}

TEST(RuntimeTcpTest, StreamChunkTimeoutIsHonored) {
    ServiceRuntime runtime;
    runtime.register_stream_dispatcher(
        9, [](u16, Buffer&, StreamWriter& writer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            Buffer chunk;
            encode_i32(chunk, 1);
            writer.write(chunk);
        });

    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();
    std::thread server([&listener, &runtime]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            runtime.send_init_confirmation_transport(*client);
            std::unordered_set<i32> tracked;
            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.magic != wire::kMagic) break;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::init_ack) continue;
                runtime.handle_message(hdr, msg, *client, tracked);
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    // 100ms budget against an 800ms-silent dispatcher: must throw.
    Buffer args;
    bool timed_out = false;
    try {
        conn.call_streaming(9, 1, args, [](Buffer&) {}, 100);
    } catch (const ServiceError&) {
        timed_out = true;
    }
    EXPECT_TRUE(timed_out);

    // Unblock and reap the server: the dispatcher finishes its delayed
    // write into the socket we still hold, then its loop reads our
    // shutdown and returns.
    Buffer shutdown = wire::create_shutdown_message();
    conn.transport()->send(shutdown);
    listener.close();
    server.join();
}

// =============================================================================
// M2: Concurrent / Interleaved Operations
// =============================================================================

TEST(ConcurrencyTest, SequentialClientsOnSamePort) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();
    std::atomic<int> clients_served{0};

    std::thread server([&listener, &clients_served]() {
        for (int c = 0; c < 3; ++c) {
            auto client = listener.accept(5000);
            if (!client) return;
            try {
                Buffer init_msg = wire::create_init_message(
                    wire::kFirstVersion, wire::kCurrentVersion, 0);
                client->send(init_msg);
                for (;;) {
                    Buffer msg;
                    if (!client->receive(msg, 2000)) break;
                    auto hdr = wire::decode_header(msg);
                    if (hdr.type == wire::MsgType::init_ack) continue;
                    if (hdr.type == wire::MsgType::shutdown) break;
                    if (hdr.type == wire::MsgType::call) {
                        [[maybe_unused]] auto [sid, mid] = wire::decode_method_call_header(msg);
                        i32 val = decode_i32(msg);
                        Buffer response;
                        encode_i32(response, val + c);
                        Buffer result = wire::create_result_message(hdr.sequence_id, response);
                        client->send(result);
                    }
                }
            } catch (...) {}
            clients_served++;
        }
    });

    for (int c = 0; c < 3; ++c) {
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect("127.0.0.1", port, 5000);
        ServiceConnection conn(std::move(tcp));
        conn.init_handshake();

        Buffer args;
        encode_i32(args, 10);
        Buffer result = conn.call(1, 1, args);
        EXPECT_EQ(decode_i32(result), 10 + c);

        Buffer shutdown = wire::create_shutdown_message();
        conn.transport()->send(shutdown);
    }

    listener.close();
    server.join();
    EXPECT_EQ(clients_served, 3);
}

TEST(ConcurrencyTest, InterleavedStreamAndRegularCalls) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, 0);
            client->send(init_msg);
            for (;;) {
                Buffer msg;
                if (!client->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::init_ack) continue;
                if (hdr.type == wire::MsgType::shutdown) break;
                if (hdr.type == wire::MsgType::call) {
                    auto [sid, mid] = wire::decode_method_call_header(msg);
                    if (mid == 1) {
                        i32 val = decode_i32(msg);
                        Buffer resp;
                        encode_i32(resp, val);
                        client->send(wire::create_result_message(hdr.sequence_id, resp));
                    } else if (mid == 2) {
                        i32 n = decode_i32(msg);
                        for (i32 i = 0; i < n; ++i) {
                            Buffer chunk;
                            encode_i32(chunk, i);
                            client->send(wire::create_stream_message(hdr.sequence_id, chunk));
                        }
                        client->send(wire::create_stream_end_message(hdr.sequence_id));
                    }
                }
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    // Regular call
    Buffer a1;
    encode_i32(a1, 42);
    Buffer r1 = conn.call(1, 1, a1);
    EXPECT_EQ(decode_i32(r1), 42);

    // Streaming call
    Buffer a2;
    encode_i32(a2, 3);
    StreamReader reader = conn.call_streaming(1, 2, a2);
    EXPECT_EQ(reader.chunk_count(), 3u);

    // Regular call after streaming
    Buffer a3;
    encode_i32(a3, 99);
    Buffer r3 = conn.call(1, 1, a3);
    EXPECT_EQ(decode_i32(r3), 99);

    conn.transport()->send(wire::create_shutdown_message());
    listener.close();
    server.join();
}

// =============================================================================
// M3: Crash / Disconnect Mid-Operation
// =============================================================================

TEST(CrashRecoveryTest, ServerDisconnectDuringCall) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        Buffer init_msg = wire::create_init_message(
            wire::kFirstVersion, wire::kCurrentVersion, 0);
        client->send(init_msg);
        // Read one message (possibly init_ack), then crash
        Buffer msg;
        client->receive(msg, 5000);
        auto hdr = wire::decode_header(msg);
        if (hdr.type == wire::MsgType::init_ack) {
            client->receive(msg, 5000);  // Read the actual call
        }
        client->close();  // Crash without responding
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_i32(args, 1);
    EXPECT_THROW(conn.call(1, 1, args), std::exception);

    listener.close();
    server.join();
}

TEST(CrashRecoveryTest, ServerDisconnectDuringStream) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, 0);
            client->send(init_msg);

            // Read init_ack + call
            Buffer msg;
            if (!client->receive(msg, 5000)) return;
            auto hdr = wire::decode_header(msg);
            if (hdr.type == wire::MsgType::init_ack) {
                if (!client->receive(msg, 5000)) return;
                hdr = wire::decode_header(msg);
            }

            // Send 2 chunks then crash
            for (int ndx = 0; ndx < 2; ++ndx) {
                Buffer chunk;
                encode_i32(chunk, ndx);
                client->send(wire::create_stream_message(hdr.sequence_id, chunk));
            }
            client->close();  // No stream_end
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_i32(args, 5);
    EXPECT_THROW(conn.call_streaming(1, 1, args), std::exception);

    listener.close();
    server.join();
}
