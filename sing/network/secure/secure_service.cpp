// MIT License
// Copyright (c) 2026 dbjwhs

#include "calculator.hpp"
#include <song/transport.hpp>
#include <song/security.hpp>
#include <iostream>
#include <cstdlib>

using namespace song;
using namespace song::calculator;

// Shared secret key (in production, this would be loaded from config)
static const std::string kSharedKey = "song-test-secret-key-32-bytes!!";

// Calculator implementation
class CalculatorImpl : public ICalculator {
public:
    i32 add(i32 a, i32 b) override { return a + b; }
    i32 subtract(i32 a, i32 b) override { return a - b; }
    i32 multiply(i32 a, i32 b) override { return a * b; }

    DivResult divide(i32 a, i32 b) override {
        if (b == 0) throw std::runtime_error("Division by zero");
        return DivResult{a / b, a % b};
    }

    i64 factorial(i32 n) override {
        if (n < 0) throw std::runtime_error("Factorial of negative number");
        i64 result = 1;
        for (i32 i = 2; i <= n; ++i) result *= i;
        return result;
    }

    i64 sum(std::vector<i32> values) override {
        i64 result = 0;
        for (auto v : values) result += v;
        return result;
    }
};

static CalculatorImpl g_calculator;

void calculator_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_Calculator(g_calculator, method_id, request, response);
}

// Handle a single client connection with security
void handle_secure_client(std::unique_ptr<Transport> tcp_transport) {
    SecurityConfig security(kSharedKey);
    SecureTransport transport(std::move(tcp_transport), security);

    std::cerr << "[secure_service] Client connected with HMAC auth\n";

    // Simple message loop
    Buffer msg;
    while (transport.receive(msg, 5000)) {
        msg.reset_read();

        // Decode header
        auto header = wire::decode_header_validated(msg);

        if (header.type == wire::MsgType::call) {
            Buffer response;

            try {
                // Decode service_id and method_id from payload
                auto [service_id, method_id] = wire::decode_method_call_header(msg);

                // Find dispatcher (we only have one service)
                calculator_dispatcher(method_id, msg, response);

                // Send success response
                Buffer reply = wire::create_result_message(header.sequence_id, response);
                transport.send(reply);
            } catch (const std::exception& e) {
                // Send error response
                Buffer reply = wire::create_error_message(header.sequence_id,
                    ErrorCode::unknown_method, e.what());
                transport.send(reply);
            }
        }

        msg = Buffer{};  // Reset for next message
    }

    std::cerr << "[secure_service] Client disconnected\n";
}

int main(int argc, char* argv[]) {
    u16 port = 18888;
    if (argc > 1) {
        port = static_cast<u16>(std::atoi(argv[1]));
    }

    std::cerr << "[secure_service] Starting on port " << port << " with HMAC auth...\n";

    TcpListener listener;
    listener.listen(port);
    std::cerr << "[secure_service] Listening...\n";

    // Accept and handle clients
    while (true) {
        auto client = listener.accept();
        if (client) {
            handle_secure_client(std::move(client));
        }
    }

    return 0;
}
