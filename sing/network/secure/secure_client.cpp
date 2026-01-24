// MIT License
// Copyright (c) 2026 dbjwhs

// Secure Client - Demonstrates HMAC-SHA256 authenticated RPC
// Usage: secure_client [host] [port] [key]

#include <song/song.hpp>
#include <song/security.hpp>
#include "calculator.hpp"
#include <iostream>
#include <cstdlib>

using namespace song;
using namespace song::calculator;

// Must match the server's key
static const std::string kDefaultKey = "song-test-secret-key-32-bytes!!";

// Helper to make a secure RPC call manually (since we need SecureTransport)
Buffer make_secure_call(SecureTransport& transport, u16 service_id, u16 method_id,
                        const Buffer& payload, u32& seq_id) {
    Buffer call_msg = wire::create_call_message(seq_id++, service_id, method_id, payload);
    transport.send(call_msg);

    Buffer response;
    if (!transport.receive(response, 5000)) {
        throw std::runtime_error("Timeout waiting for response");
    }

    response.reset_read();
    auto header = wire::decode_header_validated(response);

    if (header.type == wire::MsgType::error) {
        [[maybe_unused]] u16 code = decode_u16(response);
        std::string msg = decode_string(response);
        throw std::runtime_error("RPC error: " + msg);
    }

    return response;
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    u16 port = 18888;
    std::string key = kDefaultKey;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<u16>(std::atoi(argv[2]));
    if (argc > 3) key = argv[3];

    std::cout << "Connecting to " << host << ":" << port << " with HMAC auth...\n";

    try {
        // Create TCP connection
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect(host, port, 5000);

        // Wrap with security layer
        SecurityConfig security(key);
        SecureTransport transport(std::move(tcp), security);

        std::cout << "Connected with HMAC-SHA256 authentication\n";
        std::cout << "\n=== Secure Calculator Demo ===\n\n";

        u32 seq_id = 1;

        // add(25, 75)
        {
            Buffer req;
            encode_i32(req, 25);
            encode_i32(req, 75);
            auto resp = make_secure_call(transport, kService_Calculator,
                                         kMethod_Calculator_add, req, seq_id);
            std::cout << "add(25, 75) = " << decode_i32(resp) << "\n";
        }

        // multiply(8, 8)
        {
            Buffer req;
            encode_i32(req, 8);
            encode_i32(req, 8);
            auto resp = make_secure_call(transport, kService_Calculator,
                                         kMethod_Calculator_multiply, req, seq_id);
            std::cout << "multiply(8, 8) = " << decode_i32(resp) << "\n";
        }

        // factorial(12)
        {
            Buffer req;
            encode_i32(req, 12);
            auto resp = make_secure_call(transport, kService_Calculator,
                                         kMethod_Calculator_factorial, req, seq_id);
            std::cout << "factorial(12) = " << decode_i64(resp) << "\n";
        }

        // divide(100, 7)
        {
            Buffer req;
            encode_i32(req, 100);
            encode_i32(req, 7);
            auto resp = make_secure_call(transport, kService_Calculator,
                                         kMethod_Calculator_divide, req, seq_id);
            auto result = decode_DivResult(resp);
            std::cout << "divide(100, 7) = " << result.quotient
                      << " remainder " << result.remainder << "\n";
        }

        std::cout << "\nAll calls authenticated successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
