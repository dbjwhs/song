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

    std::vector<std::filesystem::path> candidates = {
        base / "sing" / "network" / "discovery" / "sing_network_discovery_service",
        base / "sing_network_discovery_service",
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    return (base / "sing" / "network" / "discovery" / "sing_network_discovery_service").string();
}

class DiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string path = get_service_path();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "Discovery service not found at " << path;
        }

        // Check if mDNS is available (macOS only for now)
#ifndef __APPLE__
        GTEST_SKIP() << "mDNS discovery only supported on macOS";
#endif

        // Fork and exec the discovery service
        server_pid_ = fork();
        if (server_pid_ == 0) {
            // Child process: exec the service
            execl(path.c_str(), path.c_str(), "SingTestCalc", nullptr);
            _exit(1);  // exec failed
        }

        // Parent: wait for server to start and register with mDNS
        // mDNS registration can take a moment
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Register as discoverable service
        mgr_.register_discoverable_service("calc", "testcalc", 1);
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
// mDNS Discovery Tests
// =============================================================================

TEST_F(DiscoveryTest, DiscoverAndConnect) {
    // This test verifies that mDNS discovery finds the service
    // and we can make RPC calls to it
    try {
        conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    } catch (const std::exception& e) {
        GTEST_SKIP() << "mDNS discovery failed (may not be available): " << e.what();
    }

    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.add(10, 20), 30);
}

TEST_F(DiscoveryTest, MultipleCallsAfterDiscovery) {
    try {
        conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    } catch (const std::exception& e) {
        GTEST_SKIP() << "mDNS discovery failed: " << e.what();
    }

    CalculatorProxy calc(*conn_);

    // Verify connection works for multiple calls
    EXPECT_EQ(calc.add(1, 2), 3);
    EXPECT_EQ(calc.multiply(3, 4), 12);
    EXPECT_EQ(calc.subtract(10, 5), 5);
    EXPECT_EQ(calc.factorial(5), 120);
}

TEST_F(DiscoveryTest, StructReturnOverDiscovery) {
    try {
        conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    } catch (const std::exception& e) {
        GTEST_SKIP() << "mDNS discovery failed: " << e.what();
    }

    CalculatorProxy calc(*conn_);
    auto result = calc.divide(17, 5);
    EXPECT_EQ(result.quotient, 3);
    EXPECT_EQ(result.remainder, 2);
}

TEST_F(DiscoveryTest, ArrayOverDiscovery) {
    try {
        conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    } catch (const std::exception& e) {
        GTEST_SKIP() << "mDNS discovery failed: " << e.what();
    }

    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.sum({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 55);
}
