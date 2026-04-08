// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <song/transport.hpp>
#include <song/security.hpp>
#include "calculator.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/wait.h>

using namespace song;
using namespace song::calculator;

// Must match the key in secure_service.cpp
static const std::string kCorrectKey = "song-test-secret-key-32-bytes!!";
static const std::string kWrongKey = "wrong-key-that-will-not-work-!!";

// Helper to get service path
static std::string get_service_path() {
    std::filesystem::path base = std::filesystem::current_path();

    std::vector<std::filesystem::path> candidates = {
        base / "sing" / "network" / "secure" / "sing_network_secure_service",
        base / "sing_network_secure_service",
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    return (base / "sing" / "network" / "secure" / "sing_network_secure_service").string();
}

class SecureTransportTest : public ::testing::Test {
protected:
    static constexpr u16 kTestPort = 18888;

    void SetUp() override {
        std::string path = get_service_path();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "Secure service not found at " << path;
        }

        // Fork and exec the secure service
        server_pid_ = fork();
        if (server_pid_ == 0) {
            std::string port_str = std::to_string(kTestPort);
            execl(path.c_str(), path.c_str(), port_str.c_str(), nullptr);
            _exit(1);
        }

        // Wait for server to start - retry connection to verify it's ready
        for (int attempt = 0; attempt < 10; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
            try {
                TcpTransport tcp;
                tcp.connect("127.0.0.1", kTestPort, 1000);
                return;  // Server is ready
            } catch (const std::exception&) {
                // Server not ready yet, retry
            }
        }
    }

    void TearDown() override {
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
            int status;
            waitpid(server_pid_, &status, 0);
        }
    }

    // Helper to make a call using secure transport
    Buffer make_secure_call(SecureTransport& transport, u16 service_id, u16 method_id,
                           const Buffer& request) {
        static u32 seq = 1;
        Buffer msg = wire::create_call_message(seq++, service_id, method_id, request);
        transport.send(msg);

        Buffer response;
        if (!transport.receive(response, 5000)) {
            throw std::runtime_error("Timeout waiting for response");
        }

        response.reset_read();
        auto header = wire::decode_header_validated(response);

        if (header.type == wire::MsgType::error) {
            std::string error_msg = decode_string(response);
            throw ServiceError(error_msg);
        }

        return response;
    }

    pid_t server_pid_ = -1;
};

// =============================================================================
// HMAC Authentication Tests
// =============================================================================

TEST_F(SecureTransportTest, SuccessfulAuthentication) {
    // Connect with correct key
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", kTestPort, 5000);

    SecurityConfig security(kCorrectKey);
    SecureTransport transport(std::move(tcp), std::move(security));

    // Make a call
    Buffer req;
    encode_i32(req, 10);
    encode_i32(req, 20);

    Buffer resp = make_secure_call(transport, kService_Calculator, kMethod_Calculator_add, req);
    EXPECT_EQ(decode_i32(resp), 30);
}

TEST_F(SecureTransportTest, MultipleCallsWithAuth) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", kTestPort, 5000);

    SecurityConfig security(kCorrectKey);
    SecureTransport transport(std::move(tcp), std::move(security));

    // Multiple calls over the same secure connection
    for (int ndx = 0; ndx < 10; ++ndx) {
        Buffer req;
        encode_i32(req, ndx);
        encode_i32(req, ndx);

        Buffer resp = make_secure_call(transport, kService_Calculator, kMethod_Calculator_add, req);
        EXPECT_EQ(decode_i32(resp), ndx * 2);
    }
}

TEST_F(SecureTransportTest, StructReturnWithAuth) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", kTestPort, 5000);

    SecurityConfig security(kCorrectKey);
    SecureTransport transport(std::move(tcp), std::move(security));

    Buffer req;
    encode_i32(req, 17);
    encode_i32(req, 5);

    Buffer resp = make_secure_call(transport, kService_Calculator, kMethod_Calculator_divide, req);
    auto result = decode_DivResult(resp);
    EXPECT_EQ(result.quotient, 3);
    EXPECT_EQ(result.remainder, 2);
}

TEST_F(SecureTransportTest, WrongKeyRejected) {
    // Connect with wrong key - server should reject our messages
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", kTestPort, 5000);

    SecurityConfig security(kWrongKey);
    SecureTransport transport(std::move(tcp), std::move(security));

    Buffer req;
    encode_i32(req, 10);
    encode_i32(req, 20);

    // The server will fail to verify our HMAC and should drop the connection
    // or our receive will time out
    EXPECT_THROW({
        make_secure_call(transport, kService_Calculator, kMethod_Calculator_add, req);
    }, std::exception);
}

TEST_F(SecureTransportTest, ArrayWithAuth) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", kTestPort, 5000);

    SecurityConfig security(kCorrectKey);
    SecureTransport transport(std::move(tcp), std::move(security));

    Buffer req;
    std::vector<i32> values = {1, 2, 3, 4, 5};
    encode_array<i32>(req, values);

    Buffer resp = make_secure_call(transport, kService_Calculator, kMethod_Calculator_sum, req);
    EXPECT_EQ(decode_i64(resp), 15);
}
