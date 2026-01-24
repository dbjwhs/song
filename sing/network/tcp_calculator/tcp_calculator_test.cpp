// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include "calculator.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/wait.h>

using namespace song;
using namespace song::calculator;

// Helper to get service path
static std::string get_service_path() {
    std::filesystem::path base = std::filesystem::current_path();

    // Try a few common locations relative to build directory
    std::vector<std::filesystem::path> candidates = {
        base / "sing" / "network" / "tcp_calculator" / "sing_network_tcp_calculator_service",
        base / "sing_network_tcp_calculator_service",
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    return (base / "sing" / "network" / "tcp_calculator" / "sing_network_tcp_calculator_service").string();
}

class TcpCalculatorTest : public ::testing::Test {
protected:
    static constexpr u16 kTestPort = 18765;  // Use a high port to avoid conflicts

    void SetUp() override {
        std::string path = get_service_path();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "TCP Calculator service not found at " << path;
        }

        // Fork and exec the TCP service
        server_pid_ = fork();
        if (server_pid_ == 0) {
            // Child process: exec the service with port argument
            std::string port_str = std::to_string(kTestPort);
            execl(path.c_str(), path.c_str(), port_str.c_str(), nullptr);
            _exit(1);  // exec failed
        }

        // Parent: wait for server to start listening
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Register the remote service and connect
        mgr_.register_remote_service("tcp_calc", "127.0.0.1", kTestPort, 1);
        conn_ = std::make_unique<ServiceConnection>(mgr_.connect("tcp_calc"));
    }

    void TearDown() override {
        conn_.reset();

        // Kill the server process
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
            int status;
            waitpid(server_pid_, &status, 0);
        }
    }

    pid_t server_pid_ = -1;
    ServiceManager mgr_;
    std::unique_ptr<ServiceConnection> conn_;
};

// =============================================================================
// Basic TCP Communication Tests
// =============================================================================

TEST_F(TcpCalculatorTest, BasicAdd) {
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.add(2, 3), 5);
}

TEST_F(TcpCalculatorTest, BasicSubtract) {
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.subtract(10, 4), 6);
}

TEST_F(TcpCalculatorTest, BasicMultiply) {
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.multiply(6, 7), 42);
}

TEST_F(TcpCalculatorTest, BasicDivide) {
    CalculatorProxy calc(*conn_);
    auto result = calc.divide(17, 5);
    EXPECT_EQ(result.quotient, 3);
    EXPECT_EQ(result.remainder, 2);
}

TEST_F(TcpCalculatorTest, Factorial) {
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.factorial(10), 3628800);
}

TEST_F(TcpCalculatorTest, Sum) {
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.sum({1, 2, 3, 4, 5}), 15);
}

// =============================================================================
// TCP-Specific Tests
// =============================================================================

TEST_F(TcpCalculatorTest, MultipleSequentialCalls) {
    CalculatorProxy calc(*conn_);

    // Verify connection remains stable across multiple calls
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(calc.add(i, i), i * 2);
    }
}

TEST_F(TcpCalculatorTest, MixedOperations) {
    CalculatorProxy calc(*conn_);

    // Mix of different operations
    EXPECT_EQ(calc.add(10, 20), 30);
    EXPECT_EQ(calc.multiply(5, 5), 25);
    EXPECT_EQ(calc.subtract(100, 50), 50);
    auto div = calc.divide(100, 7);
    EXPECT_EQ(div.quotient, 14);
    EXPECT_EQ(div.remainder, 2);
    EXPECT_EQ(calc.factorial(5), 120);
    EXPECT_EQ(calc.sum({10, 20, 30}), 60);
}

TEST_F(TcpCalculatorTest, LargeArray) {
    CalculatorProxy calc(*conn_);

    // Test with a larger array over TCP
    std::vector<i32> values;
    for (i32 i = 1; i <= 1000; ++i) {
        values.push_back(i);
    }
    // Sum of 1 to 1000 = 500500
    EXPECT_EQ(calc.sum(values), 500500);
}
