// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/security.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <thread>

using namespace song;

// =============================================================================
// HMAC Tests
// =============================================================================

TEST(SecurityTest, ComputeHmacBasic) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;
    encode_u8(msg, 0x01);
    encode_u8(msg, 0x02);
    encode_u8(msg, 0x03);
    encode_u8(msg, 0x04);

    HmacTag tag = compute_hmac(key, msg);

    // Tag should be non-zero
    bool all_zero = true;
    for (auto b : tag) {
        if (b != std::byte{0}) {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE(all_zero);
}

TEST(SecurityTest, ComputeHmacDeterministic) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;
    encode_string(msg, "Hello, World!");

    HmacTag tag1 = compute_hmac(key, msg);
    HmacTag tag2 = compute_hmac(key, msg);

    EXPECT_EQ(tag1, tag2);
}

TEST(SecurityTest, ComputeHmacDifferentKeys) {
    std::string key1 = "test-secret-key-32-bytes-long!!!";
    std::string key2 = "different-key-32-bytes-long!!!!!";
    Buffer msg;
    encode_string(msg, "Hello, World!");

    HmacTag tag1 = compute_hmac(key1, msg);
    HmacTag tag2 = compute_hmac(key2, msg);

    EXPECT_NE(tag1, tag2);
}

TEST(SecurityTest, ComputeHmacDifferentMessages) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg1, msg2;
    encode_string(msg1, "Hello");
    encode_string(msg2, "World");

    HmacTag tag1 = compute_hmac(key, msg1);
    HmacTag tag2 = compute_hmac(key, msg2);

    EXPECT_NE(tag1, tag2);
}

TEST(SecurityTest, VerifyHmacSuccess) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;
    encode_string(msg, "Test message for HMAC verification");

    HmacTag tag = compute_hmac(key, msg);
    EXPECT_TRUE(verify_hmac(key, msg, tag));
}

TEST(SecurityTest, VerifyHmacWrongKey) {
    std::string key1 = "test-secret-key-32-bytes-long!!!";
    std::string key2 = "different-key-32-bytes-long!!!!!";
    Buffer msg;
    encode_string(msg, "Test message");

    HmacTag tag = compute_hmac(key1, msg);
    EXPECT_FALSE(verify_hmac(key2, msg, tag));
}

TEST(SecurityTest, VerifyHmacTamperedMessage) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;
    encode_string(msg, "Original message");

    HmacTag tag = compute_hmac(key, msg);

    // Tamper with the message
    Buffer tampered;
    encode_string(tampered, "Tampered message");

    EXPECT_FALSE(verify_hmac(key, tampered, tag));
}

TEST(SecurityTest, VerifyHmacTamperedTag) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;
    encode_string(msg, "Test message");

    HmacTag tag = compute_hmac(key, msg);

    // Tamper with the tag
    tag[0] = static_cast<std::byte>(static_cast<uint8_t>(tag[0]) ^ 0xFF);

    EXPECT_FALSE(verify_hmac(key, msg, tag));
}

TEST(SecurityTest, HmacEmptyMessage) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;  // Empty

    HmacTag tag = compute_hmac(key, msg);

    // Even empty message should produce a valid HMAC
    bool all_zero = true;
    for (auto b : tag) {
        if (b != std::byte{0}) {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE(all_zero);
    EXPECT_TRUE(verify_hmac(key, msg, tag));
}

TEST(SecurityTest, HmacLargeMessage) {
    std::string key = "test-secret-key-32-bytes-long!!!";
    Buffer msg;

    // Create a large message (1MB)
    std::string large_data(1024 * 1024, 'X');
    encode_string(msg, large_data);

    HmacTag tag = compute_hmac(key, msg);
    EXPECT_TRUE(verify_hmac(key, msg, tag));
}

// =============================================================================
// SecurityConfig Tests
// =============================================================================

TEST(SecurityConfigTest, DefaultDisabled) {
    SecurityConfig config;
    EXPECT_FALSE(config.is_enabled());
    EXPECT_EQ(config.level(), SecurityLevel::none);
}

TEST(SecurityConfigTest, WithKey) {
    SecurityConfig config("my-secret-key");
    EXPECT_TRUE(config.is_enabled());
    EXPECT_EQ(config.level(), SecurityLevel::shared_secret);
    EXPECT_EQ(config.key(), "my-secret-key");
}

TEST(SecurityConfigTest, SetKey) {
    SecurityConfig config;
    EXPECT_FALSE(config.is_enabled());

    config.set_key("new-secret-key");
    EXPECT_TRUE(config.is_enabled());
    EXPECT_EQ(config.level(), SecurityLevel::shared_secret);
}

TEST(SecurityConfigTest, Disable) {
    SecurityConfig config("my-secret-key");
    EXPECT_TRUE(config.is_enabled());

    config.disable();
    EXPECT_FALSE(config.is_enabled());
    EXPECT_EQ(config.level(), SecurityLevel::none);
}

TEST(SecurityConfigTest, EmptyKeyDisabled) {
    SecurityConfig config;
    config.set_key("");
    // Empty key shouldn't enable security
    EXPECT_FALSE(config.is_enabled());
}

// =============================================================================
// SecureTransport Tests (using TCP loopback)
// =============================================================================

TEST(SecureTransportTest, SendReceiveWithSecurity) {
    // Start listener
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::string shared_key = "test-shared-secret-key-32bytes!!";

    std::thread server_thread([&]() {
        auto server_tcp = listener.accept(5000);
        ASSERT_NE(server_tcp, nullptr);

        SecurityConfig config(shared_key);
        SecureTransport server(std::move(server_tcp), std::move(config));

        // Receive message
        Buffer msg;
        ASSERT_TRUE(server.receive(msg, 5000));

        // Verify it's a valid message with expected content
        auto hdr = wire::decode_header(msg);
        EXPECT_EQ(hdr.magic, wire::kMagic);

        // Send response
        Buffer response = wire::create_result_message(hdr.sequence_id, Buffer{});
        server.send(response);
    });

    // Client side
    auto client_tcp = std::make_unique<TcpTransport>();
    client_tcp->connect("127.0.0.1", port, 5000);

    SecurityConfig config(shared_key);
    SecureTransport client(std::move(client_tcp), std::move(config));

    // Send a call message
    Buffer args;
    encode_string(args, "test");
    Buffer call = wire::create_call_message(1, 1, 1, args);
    client.send(call);

    // Receive response
    Buffer response;
    ASSERT_TRUE(client.receive(response, 5000));

    server_thread.join();
}

TEST(SecureTransportTest, MismatchedKeysFails) {
    // Start listener
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server_thread([&]() {
        auto server_tcp = listener.accept(5000);
        ASSERT_NE(server_tcp, nullptr);

        // Server uses different key
        SecurityConfig config("server-key-different-32-bytes!!");
        SecureTransport server(std::move(server_tcp), std::move(config));

        // Receive should fail due to HMAC mismatch
        Buffer msg;
        EXPECT_THROW(server.receive(msg, 5000), SecurityError);
    });

    // Client uses different key
    auto client_tcp = std::make_unique<TcpTransport>();
    client_tcp->connect("127.0.0.1", port, 5000);

    SecurityConfig config("client-key-different-32-bytes!!");
    SecureTransport client(std::move(client_tcp), std::move(config));

    // Send a message
    Buffer args;
    encode_string(args, "test");
    Buffer call = wire::create_call_message(1, 1, 1, args);
    client.send(call);

    server_thread.join();
}

TEST(SecureTransportTest, NoSecurityPassthrough) {
    // Start listener
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server_thread([&]() {
        auto server_tcp = listener.accept(5000);
        ASSERT_NE(server_tcp, nullptr);

        // No security
        SecurityConfig config;
        EXPECT_FALSE(config.is_enabled());
        SecureTransport server(std::move(server_tcp), std::move(config));

        // Receive message (should work without HMAC)
        Buffer msg;
        ASSERT_TRUE(server.receive(msg, 5000));

        auto hdr = wire::decode_header(msg);
        EXPECT_EQ(hdr.magic, wire::kMagic);
    });

    // Client also with no security
    auto client_tcp = std::make_unique<TcpTransport>();
    client_tcp->connect("127.0.0.1", port, 5000);

    SecurityConfig config;
    SecureTransport client(std::move(client_tcp), std::move(config));

    // Send a call message
    Buffer args;
    Buffer call = wire::create_call_message(1, 1, 1, args);
    client.send(call);

    server_thread.join();
}

TEST(SecureTransportTest, TypeName) {
    auto tcp = std::make_unique<TcpTransport>();
    SecurityConfig config("key");
    SecureTransport secure(std::move(tcp), std::move(config));

    std::string type_name = secure.type_name();
    EXPECT_TRUE(type_name.find("secure") != std::string::npos);
    EXPECT_TRUE(type_name.find("tcp") != std::string::npos);
}

TEST(SecureTransportTest, IsConnected) {
    auto tcp = std::make_unique<TcpTransport>();
    SecurityConfig config;
    SecureTransport secure(std::move(tcp), std::move(config));

    // Not connected since we didn't connect the underlying TCP
    EXPECT_FALSE(secure.is_connected());
}

TEST(SecureTransportTest, InnerAccess) {
    auto tcp = std::make_unique<TcpTransport>();
    TcpTransport* raw_ptr = tcp.get();

    SecurityConfig config;
    SecureTransport secure(std::move(tcp), std::move(config));

    EXPECT_EQ(secure.inner(), raw_ptr);
}

TEST(SecureTransportTest, MultipleMessages) {
    // Start listener
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::string shared_key = "test-shared-secret-key-32bytes!!";

    std::thread server_thread([&]() {
        auto server_tcp = listener.accept(5000);
        ASSERT_NE(server_tcp, nullptr);

        SecurityConfig config(shared_key);
        SecureTransport server(std::move(server_tcp), std::move(config));

        // Receive and respond to 5 messages
        for (int i = 0; i < 5; ++i) {
            Buffer msg;
            ASSERT_TRUE(server.receive(msg, 5000));

            auto hdr = wire::decode_header(msg);
            EXPECT_EQ(hdr.magic, wire::kMagic);
            EXPECT_EQ(hdr.sequence_id, static_cast<u32>(i + 1));

            Buffer response = wire::create_result_message(hdr.sequence_id, Buffer{});
            server.send(response);
        }
    });

    // Client side
    auto client_tcp = std::make_unique<TcpTransport>();
    client_tcp->connect("127.0.0.1", port, 5000);

    SecurityConfig config(shared_key);
    SecureTransport client(std::move(client_tcp), std::move(config));

    // Send 5 messages
    for (int i = 0; i < 5; ++i) {
        Buffer args;
        encode_i32(args, i);
        Buffer call = wire::create_call_message(static_cast<u32>(i + 1), 1, 1, args);
        client.send(call);

        Buffer response;
        ASSERT_TRUE(client.receive(response, 5000));
        auto hdr = wire::decode_header(response);
        EXPECT_EQ(hdr.sequence_id, static_cast<u32>(i + 1));
    }

    server_thread.join();
}

// =============================================================================
// SecureTransport with Pipes
// =============================================================================

TEST(SecureTransportPipeTest, SendReceiveWithPipes) {
    // Create pipe pairs for bidirectional communication
    auto [client_to_server_read, client_to_server_write] = Pipe::create_pair();
    auto [server_to_client_read, server_to_client_write] = Pipe::create_pair();

    std::string shared_key = "pipe-shared-secret-key-32bytes!!";

    std::thread server_thread([&,
        read = std::move(client_to_server_read),
        write = std::move(server_to_client_write)]() mutable {

        auto pipe_transport = std::make_unique<PipeTransport>(
            std::move(write), std::move(read)
        );

        SecurityConfig config(shared_key);
        SecureTransport server(std::move(pipe_transport), std::move(config));

        // Receive message
        Buffer msg;
        ASSERT_TRUE(server.receive(msg, 5000));

        auto hdr = wire::decode_header(msg);
        EXPECT_EQ(hdr.magic, wire::kMagic);

        // Send response
        Buffer response = wire::create_result_message(hdr.sequence_id, Buffer{});
        server.send(response);
    });

    // Client side
    auto pipe_transport = std::make_unique<PipeTransport>(
        std::move(client_to_server_write), std::move(server_to_client_read)
    );

    SecurityConfig config(shared_key);
    SecureTransport client(std::move(pipe_transport), std::move(config));

    // Send a call message
    Buffer args;
    encode_string(args, "pipe test");
    Buffer call = wire::create_call_message(42, 1, 1, args);
    client.send(call);

    // Receive response
    Buffer response;
    ASSERT_TRUE(client.receive(response, 5000));

    auto hdr = wire::decode_header(response);
    EXPECT_EQ(hdr.sequence_id, 42u);

    server_thread.join();
}
