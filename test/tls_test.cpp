// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>

#if defined(SONG_HAS_TLS)

#include <song/security.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <thread>
#include <atomic>
#include <chrono>
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
#ifdef SONG_SOURCE_DIR
        SONG_SOURCE_DIR "/test/certs",
#endif
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c + "/ca_cert.pem")) {
            return c;
        }
    }
    return "test/certs";  // Last resort -- will fail with clear error
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

// receive(msg, 0) must return promptly when nothing is pending -- matching
// TcpTransport -- rather than blocking indefinitely (a 0 mbedTLS read-timeout
// means "block forever"). A watchdog makes a regression fail rather than hang.
TEST_F(TlsCertTest, ReceiveZeroTimeoutDoesNotBlockForever) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::atomic<bool> accepted{false};
    std::thread server([&]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        accepted = true;
        // Keep the connection open but send nothing.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    for (int i = 0; i < 500 && !accepted.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // With no data pending, receive(0) must return promptly (throwing a read
    // timeout), not hang. Run it in a worker guarded by a watchdog.
    std::atomic<bool> returned{false};
    std::atomic<bool> threw_timeout{false};
    std::thread worker([&]() {
        try {
            Buffer resp;
            client.receive(resp, 0);
        } catch (const ServiceError&) {
            threw_timeout = true;
        } catch (...) {
        }
        returned = true;
    });

    for (int i = 0; i < 200 && !returned.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool completed = returned.load();
    if (completed) {
        worker.join();
    } else {
        worker.detach();  // blocked forever; leak so the assert can report
    }

    ASSERT_TRUE(completed) << "receive(msg, 0) blocked instead of returning promptly";
    EXPECT_TRUE(threw_timeout.load()) << "receive(msg, 0) with no data should throw a read timeout";

    client.close();
    listener.close();
    server.join();
}

// A peer that sends a header promising a payload but closes before delivering it
// must cause receive() to throw ServiceError, not hang or return a partial
// message.
TEST_F(TlsCertTest, ReceiveTruncatedPayloadThrows) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        // Header claims a 64-byte payload; send only the header, then close.
        wire::Header hdr{};
        hdr.magic = wire::kMagic;
        hdr.type = wire::MsgType::call;
        hdr.sequence_id = 1;
        hdr.payload_size = 64;
        Buffer header_only;
        wire::encode_header(header_only, hdr);
        conn->send(header_only);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    Buffer resp;
    EXPECT_THROW(client.receive(resp, 5000), ServiceError);

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
        for (int ndx = 0; ndx < 5; ++ndx) {
            Buffer msg;
            if (!conn->receive(msg, 5000)) break;
            conn->send(msg);
        }
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    for (int ndx = 0; ndx < 5; ++ndx) {
        Buffer args;
        encode_i32(args, ndx * 10);
        Buffer msg = wire::create_call_message(static_cast<u32>(ndx + 1), 1, 1, args);
        client.send(msg);

        Buffer resp;
        ASSERT_TRUE(client.receive(resp, 5000));
        resp.reset_read();
        auto hdr = wire::decode_header(resp);
        EXPECT_EQ(hdr.sequence_id, static_cast<u32>(ndx + 1));
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


// =============================================================================
// COVERAGE tests (medium-risk gaps): TLS lifecycle, EOF, timeout, move,
// oversized/unvalidated headers, PSK negative path.
// Wrapped in a guard so the block is correct whether appended inside or after
// the file's existing SONG_HAS_TLS region.
// =============================================================================
#if defined(SONG_HAS_TLS)

// gap tls-receive-clean-eof-returns-false: a peer that closes cleanly at a
// message boundary must make receive() return false (not throw), leave the
// transport not-connected, and cause a subsequent send() to throw.
TEST_F(TlsCertTest, CleanEofReturnsFalse) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        conn->close();  // clean close_notify, no application message
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    Buffer resp;
    EXPECT_FALSE(client.receive(resp, 5000));
    EXPECT_FALSE(client.is_connected());

    Buffer args;
    encode_i32(args, 1);
    Buffer msg = wire::create_call_message(1, 1, 1, args);
    EXPECT_THROW(client.send(msg), ServiceError);

    client.close();
    listener.close();
    server.join();
}

// gap tls-receive-clean-eof-returns-false (bullet 2): after a successful
// round-trip, the next receive() past the peer's clean close returns false.
TEST_F(TlsCertTest, EchoThenCleanEofReturnsFalse) {
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

    Buffer args;
    encode_i32(args, 42);
    Buffer call = wire::create_call_message(1, 1, 1, args);
    client.send(call);

    Buffer resp;
    ASSERT_TRUE(client.receive(resp, 5000));

    Buffer resp2;
    EXPECT_FALSE(client.receive(resp2, 5000));
    EXPECT_FALSE(client.is_connected());

    client.close();
    listener.close();
    server.join();
}

// gap tls-receive-read-timeout-path: a positive read timeout with no data must
// throw ServiceError('read timeout') promptly, and the timeout must not corrupt
// the SSL read state -- a later receive() still decodes a delayed message.
TEST_F(TlsCertTest, ReceiveReadTimeoutThenRecovers) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        // Idle long enough that the client's short receive times out first...
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        Buffer args;
        encode_i32(args, 123);
        Buffer msg = wire::create_call_message(11, 1, 1, args);
        conn->send(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    const auto start = std::chrono::steady_clock::now();
    bool threw = false;
    try {
        Buffer resp;
        client.receive(resp, 100);
    } catch (const ServiceError& e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("read timeout"), std::string::npos);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    EXPECT_TRUE(threw) << "receive(100) with no data should throw a read timeout";
    EXPECT_LT(elapsed, 2000) << "read timeout should surface promptly";

    // The delayed message must still arrive and decode after the earlier timeout.
    Buffer resp;
    ASSERT_TRUE(client.receive(resp, 3000));
    resp.reset_read();
    auto hdr = wire::decode_header(resp);
    EXPECT_EQ(hdr.sequence_id, 11u);

    client.close();
    listener.close();
    server.join();
}

// gap tls-oversize-payload-and-unvalidated-magic (bullet 1): a header whose
// payload_size exceeds kMaxPayloadSize must be rejected with ProtocolError
// before any payload is read.
TEST_F(TlsCertTest, OversizePayloadRejected) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        wire::Header hdr{};
        hdr.magic = wire::kMagic;
        hdr.type = wire::MsgType::call;
        hdr.sequence_id = 1;
        hdr.payload_size = static_cast<u32>(wire::kMaxPayloadSize + 1);
        Buffer header_only;
        wire::encode_header(header_only, hdr);
        conn->send(header_only);  // send only the header, no payload
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    bool threw = false;
    try {
        Buffer resp;
        client.receive(resp, 5000);
    } catch (const ProtocolError& e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("exceeds maximum"), std::string::npos);
    }
    EXPECT_TRUE(threw) << "oversized payload_size should be rejected with ProtocolError";

    client.close();
    listener.close();
    server.join();
}

// gap tls-oversize-payload-and-unvalidated-magic (bullet 3): receive() uses the
// non-validating decode_header, so a wrong magic is surfaced to the caller
// rather than rejected at the transport. Documents/locks current behavior.
TEST_F(TlsCertTest, ReceiveDoesNotValidateMagic) {
    TlsConfig srv_config(server_cert(), server_key(), ca_cert());
    srv_config.set_verify_mode(TlsConfig::VerifyMode::none);

    TlsListener listener;
    listener.listen(srv_config, 0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;
        wire::Header hdr{};
        hdr.magic = 0xDEADBEEFu;  // deliberately wrong magic
        hdr.type = wire::MsgType::call;
        hdr.sequence_id = 5;
        hdr.payload_size = 0;
        Buffer header_only;
        wire::encode_header(header_only, hdr);
        conn->send(header_only);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        conn->close();
    });

    TlsConfig cli_config(client_cert(), client_key(), ca_cert());
    cli_config.set_verify_mode(TlsConfig::VerifyMode::none);
    auto client = make_tls_client(port, std::move(cli_config));

    Buffer resp;
    ASSERT_TRUE(client.receive(resp, 5000));
    resp.reset_read();
    auto hdr = wire::decode_header(resp);
    EXPECT_NE(hdr.magic, wire::kMagic);
    EXPECT_EQ(hdr.magic, 0xDEADBEEFu);
    EXPECT_EQ(hdr.sequence_id, 5u);

    client.close();
    listener.close();
    server.join();
}

// gap tls-transport-move-and-post-move-state (bullet 1): move-construction
// transfers the live connection; the moved-from source is inert and does not
// double-close the socket.
TEST_F(TlsCertTest, MoveConstructTransfersConnection) {
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
    auto client_a = make_tls_client(port, std::move(cli_config));
    ASSERT_TRUE(client_a.is_connected());

    TlsTransport client_b = std::move(client_a);

    // Moved-from source is inert.
    EXPECT_FALSE(client_a.is_connected());  // NOLINT: testing moved-from
    EXPECT_EQ(client_a.peer_port(), 0);
    EXPECT_TRUE(client_a.peer_address().empty());

    // Destination owns the live connection and round-trips.
    EXPECT_TRUE(client_b.is_connected());
    EXPECT_EQ(client_b.peer_port(), port);

    Buffer args;
    encode_i32(args, 55);
    Buffer msg = wire::create_call_message(7, 1, 1, args);
    client_b.send(msg);

    Buffer resp;
    ASSERT_TRUE(client_b.receive(resp, 5000));
    resp.reset_read();
    auto hdr = wire::decode_header(resp);
    EXPECT_EQ(hdr.sequence_id, 7u);

    client_b.close();
    listener.close();
    server.join();
}

// gap tls-transport-move-and-post-move-state (bullet 3): every method on a
// moved-from TlsTransport (null impl_) must be safe -- send throws, receive
// returns false, handshake throws, accessors return defaults, no crash.
TEST_F(TlsCertTest, MovedFromTransportGuards) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);
    TlsConfig config(client_cert(), client_key(), ca_cert());
    config.set_verify_mode(TlsConfig::VerifyMode::none);
    TlsTransport src(sock, std::move(config), "127.0.0.1", 1234);

    TlsTransport dst = std::move(src);

    Buffer args;
    encode_i32(args, 1);
    Buffer msg = wire::create_call_message(1, 1, 1, args);
    EXPECT_THROW(src.send(msg), ServiceError);   // NOLINT: testing moved-from
    Buffer resp;
    EXPECT_FALSE(src.receive(resp, 0));
    EXPECT_THROW(src.handshake(), SecurityError);
    EXPECT_FALSE(src.is_connected());
    EXPECT_EQ(src.peer_port(), 0);
    EXPECT_TRUE(src.peer_address().empty());

    dst.close();  // dst owns the fd; single close, no leak/double-free
}

// gap tls-send-receive-lifecycle-guards (bullet 1): before handshake, the
// transport is not connected, send() throws and receive() returns false.
TEST_F(TlsCertTest, PreHandshakeGuards) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);
    TlsConfig config(client_cert(), client_key(), ca_cert());
    config.set_verify_mode(TlsConfig::VerifyMode::none);
    TlsTransport transport(sock, std::move(config), "127.0.0.1", 4321);

    EXPECT_FALSE(transport.is_connected());
    EXPECT_STREQ(transport.type_name(), "tls");
    EXPECT_EQ(transport.peer_port(), 4321);

    Buffer args;
    encode_i32(args, 1);
    Buffer msg = wire::create_call_message(1, 1, 1, args);
    EXPECT_THROW(transport.send(msg), ServiceError);

    Buffer resp;
    EXPECT_FALSE(transport.receive(resp, 0));

    transport.close();
}

// gap tls-send-receive-lifecycle-guards (bullets 2+3): after close(), the
// transport is not connected, send throws, receive returns false, a second
// close() is a safe no-op, and handshake() throws instead of touching the fd.
TEST_F(TlsCertTest, AfterCloseGuards) {
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

    Buffer args;
    encode_i32(args, 9);
    Buffer msg = wire::create_call_message(3, 1, 1, args);
    client.send(msg);
    Buffer resp;
    ASSERT_TRUE(client.receive(resp, 5000));

    client.close();
    EXPECT_FALSE(client.is_connected());

    Buffer args2;
    encode_i32(args2, 2);
    Buffer msg2 = wire::create_call_message(4, 1, 1, args2);
    EXPECT_THROW(client.send(msg2), ServiceError);
    Buffer resp2;
    EXPECT_FALSE(client.receive(resp2, 0));

    client.close();  // second close: safe no-op
    EXPECT_THROW(client.handshake(), SecurityError);

    listener.close();
    server.join();
}

// gap tls-psk-mismatch-handshake-failure (bullet 1): PSK mode with mismatched
// keys must fail the handshake with SecurityError on the client side.
TEST_F(TlsPskTest, PskKeyMismatchHandshakeFails) {
    const std::string identity = "song-test";
    TlsConfig srv_config("psk-key-alpha", identity, TlsConfig::Mode::psk);

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
            // Expected: server-side handshake fails on PSK mismatch.
        }
    });

    TlsConfig cli_config("psk-key-bravo", identity, TlsConfig::Mode::psk);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    TlsTransport client(sock, std::move(cli_config), "127.0.0.1", port);
    EXPECT_THROW(client.handshake(), SecurityError);

    client.close();  // unblock the server's handshake read
    listener.close();
    server.join();
}

#endif // SONG_HAS_TLS (coverage block)
