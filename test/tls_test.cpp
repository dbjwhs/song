// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>

#if defined(SONG_HAS_TLS)

#include <song/security.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <thread>
#include <filesystem>
#include <netinet/tcp.h>

using namespace song;

// =============================================================================
// Test certificate paths
// =============================================================================

static std::string cert_dir() {
    std::vector<std::string> candidates = {
        "test/certs",
        "../test/certs",
        "../../test/certs",
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c + "/ca_cert.pem")) {
            return c;
        }
    }
    return "/Users/dbjones/ng/dbjwhs/song/test/certs";
}

static std::string ca_cert()     { return cert_dir() + "/ca_cert.pem"; }
static std::string server_cert() { return cert_dir() + "/server_cert.pem"; }
static std::string server_key()  { return cert_dir() + "/server_key.pem"; }
static std::string client_cert() { return cert_dir() + "/client_cert.pem"; }
static std::string client_key()  { return cert_dir() + "/client_key.pem"; }
static std::string wrong_ca()    { return cert_dir() + "/wrong_ca_cert.pem"; }

// =============================================================================
// TlsConfig Tests
// =============================================================================

TEST(TlsConfigTest, CertificateMode) {
    TlsConfig config(server_cert(), server_key(), ca_cert());
    EXPECT_EQ(config.mode(), TlsConfig::Mode::certificate);
    EXPECT_EQ(config.verify_mode(), TlsConfig::VerifyMode::required);
    EXPECT_FALSE(config.is_server());
}

TEST(TlsConfigTest, PskMode) {
    TlsConfig config("my-secret-key", "song-identity", TlsConfig::Mode::psk);
    EXPECT_EQ(config.mode(), TlsConfig::Mode::psk);
    EXPECT_EQ(config.psk(), "my-secret-key");
    EXPECT_EQ(config.psk_identity(), "song-identity");
}

TEST(TlsConfigTest, MoveOnly) {
    TlsConfig config("my-secret-key", "identity", TlsConfig::Mode::psk);
    TlsConfig moved = std::move(config);
    EXPECT_EQ(moved.psk(), "my-secret-key");
    EXPECT_TRUE(config.psk().empty());  // NOLINT: testing moved-from
}

TEST(TlsConfigTest, SetServerMode) {
    TlsConfig config(server_cert(), server_key(), ca_cert());
    EXPECT_FALSE(config.is_server());
    config.set_server(true);
    EXPECT_TRUE(config.is_server());
}

TEST(TlsConfigTest, SetVerifyMode) {
    TlsConfig config(server_cert(), server_key(), ca_cert());
    config.set_verify_mode(TlsConfig::VerifyMode::none);
    EXPECT_EQ(config.verify_mode(), TlsConfig::VerifyMode::none);
}

// =============================================================================
// Helper: make a client TLS connection to a known port
// =============================================================================

static TlsTransport make_tls_client(u16 port, TlsConfig config) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) throw ServiceError("socket() failed");

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        throw ServiceError("connect() failed");
    }

    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    TlsTransport transport(sock, std::move(config), "127.0.0.1", port);
    transport.handshake();
    return transport;
}

// =============================================================================
// TLS Certificate Transport Tests
// =============================================================================

class TlsCertTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(ca_cert())) {
            GTEST_SKIP() << "Test certificates not found. Run test/certs/generate.sh";
        }
    }
};

TEST_F(TlsCertTest, BasicRoundTrip) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        Buffer msg;
        if (conn->receive(msg, 5000)) {
            conn->send(msg);
        }
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    EXPECT_TRUE(client.is_connected());
    EXPECT_STREQ(client.type_name(), "tls");

    // Send a call message
    Buffer args;
    encode_i32(args, 42);
    Buffer call_msg = wire::create_call_message(1, 100, 200, args);
    client.send(call_msg);

    // Receive echo
    Buffer response;
    ASSERT_TRUE(client.receive(response, 5000));

    response.reset_read();
    auto hdr = wire::decode_header(response);
    EXPECT_TRUE(wire::has_flag(hdr.flags, wire::MsgFlags::encrypted));
    EXPECT_EQ(hdr.type, wire::MsgType::call);
    EXPECT_EQ(hdr.sequence_id, 1u);

    client.close();
    listener.close();
    server.join();
}

TEST_F(TlsCertTest, MultipleMessages) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        for (int i = 0; i < 5; ++i) {
            Buffer msg;
            if (!conn->receive(msg, 5000)) break;
            conn->send(msg);
        }
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    for (int i = 0; i < 5; ++i) {
        Buffer args;
        encode_i32(args, i * 10);
        Buffer msg = wire::create_call_message(static_cast<u32>(i + 1), 1, 1, args);
        client.send(msg);

        Buffer resp;
        ASSERT_TRUE(client.receive(resp, 5000));
        resp.reset_read();
        auto hdr = wire::decode_header(resp);
        EXPECT_EQ(hdr.sequence_id, static_cast<u32>(i + 1));
        EXPECT_TRUE(wire::has_flag(hdr.flags, wire::MsgFlags::encrypted));
    }

    client.close();
    listener.close();
    server.join();
}

TEST_F(TlsCertTest, LargePayload) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        Buffer msg;
        if (conn->receive(msg, 10000)) {
            conn->send(msg);
        }
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    // Send 64KB payload
    Buffer args;
    std::vector<u8> big_data(65536, 0xAB);
    encode_bytes(args, std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(big_data.data()), big_data.size()));
    Buffer msg = wire::create_call_message(99, 1, 1, args);
    client.send(msg);

    Buffer resp;
    ASSERT_TRUE(client.receive(resp, 10000));
    resp.reset_read();
    auto hdr = wire::decode_header(resp);
    EXPECT_EQ(hdr.sequence_id, 99u);

    client.close();
    listener.close();
    server.join();
}

// =============================================================================
// TLS PSK Mode Tests
// =============================================================================

class TlsPskTest : public ::testing::Test {};

TEST_F(TlsPskTest, BasicPskRoundTrip) {
    std::string psk = "song-test-psk-32-bytes-long!!!!!";
    std::string identity = "song-test";

    TlsConfig srv_config(psk, identity, TlsConfig::Mode::psk);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        Buffer msg;
        if (conn->receive(msg, 5000)) {
            conn->send(msg);
        }
        conn->close();
    });

    TlsConfig cli_config(psk, identity, TlsConfig::Mode::psk);
    auto client = make_tls_client(port, std::move(cli_config));

    Buffer args;
    encode_i32(args, 777);
    Buffer msg = wire::create_call_message(1, 1, 1, args);
    client.send(msg);

    Buffer resp;
    ASSERT_TRUE(client.receive(resp, 5000));
    resp.reset_read();
    auto hdr = wire::decode_header(resp);
    EXPECT_EQ(hdr.sequence_id, 1u);
    EXPECT_TRUE(wire::has_flag(hdr.flags, wire::MsgFlags::encrypted));

    client.close();
    listener.close();
    server.join();
}

// =============================================================================
// TLS Error/Failure Tests
// =============================================================================

TEST_F(TlsCertTest, WrongCaRejectsCert) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        try {
            auto conn = listener.accept(5000);
            if (conn) {
                Buffer msg;
                conn->receive(msg, 2000);
            }
        } catch (...) {
            // Expected: handshake will fail when client aborts
        }
    });

    // Client uses wrong CA -- verification should fail
    TlsConfig cli_config(client_cert(), client_key(), wrong_ca());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::required);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    TlsTransport client(sock, std::move(cli_config), "127.0.0.1", port);
    EXPECT_THROW(client.handshake(), SecurityError);

    // Close the client socket so the server's handshake unblocks
    client.close();

    listener.close();
    server.join();
}

TEST_F(TlsCertTest, InvalidCertPathThrows) {
    EXPECT_THROW({
        TlsConfig config("/nonexistent/cert.pem", "/nonexistent/key.pem", "/nonexistent/ca.pem");
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        TlsTransport transport(sock, std::move(config));
    }, SecurityError);
}

TEST_F(TlsCertTest, ListenerAcceptTimeout) {
    TlsConfig config(server_cert(), server_key(), ca_cert());
    config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(config, 0);
    EXPECT_TRUE(listener.is_listening());
    EXPECT_GT(listener.bound_port(), 0);

    // No one connecting -- should timeout
    auto conn = listener.accept(100);
    EXPECT_EQ(conn, nullptr);

    listener.close();
    EXPECT_FALSE(listener.is_listening());
}

// =============================================================================
// MsgFlags Tests
// =============================================================================

TEST(MsgFlagsTest, BitwiseOr) {
    auto flags = wire::MsgFlags::encrypted | wire::MsgFlags::compressed;
    EXPECT_EQ(static_cast<u8>(flags), 0x03);
}

TEST(MsgFlagsTest, BitwiseAnd) {
    auto flags = wire::MsgFlags::encrypted | wire::MsgFlags::compressed;
    EXPECT_EQ(flags & wire::MsgFlags::encrypted, wire::MsgFlags::encrypted);
}

TEST(MsgFlagsTest, HasFlag) {
    auto flags = wire::MsgFlags::encrypted;
    EXPECT_TRUE(wire::has_flag(flags, wire::MsgFlags::encrypted));
    EXPECT_FALSE(wire::has_flag(flags, wire::MsgFlags::compressed));
    EXPECT_FALSE(wire::has_flag(wire::MsgFlags::none, wire::MsgFlags::encrypted));
}

#endif // SONG_HAS_TLS
