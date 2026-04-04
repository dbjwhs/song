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
