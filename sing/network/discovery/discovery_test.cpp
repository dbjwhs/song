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
            GTEST_SKIP() << "Discovery service binary not built at " << path;
        }

        // Skip ONLY when this host genuinely has no mDNS/DNS-SD stack
        // (macOS Bonjour, or Linux with a running avahi-daemon). When it IS
        // available, the tests run for real and a failure is a failure --
        // not a skip. The old "#ifndef __APPLE__ skip" hid that Linux gained
        // Avahi support, and "catch -> skip" once hid a broken service type.
        auto discovery = create_discovery();
        if (!discovery || !discovery->is_available()) {
            GTEST_SKIP() << "mDNS/DNS-SD not available on this host";
        }

        // Fork and exec the discovery service. The instance name MUST match
        // the name connect() browses for -- connect("calc") calls
        // discover_one("calc", "testcalc"), so the service must register its
        // instance as "calc". (It previously registered "SingTestCalc" and
        // every test connected to "calc"; the name mismatch meant these tests
        // never actually connected -- masked because they skipped on failure.)
        server_pid_ = fork();
        if (server_pid_ == 0) {
            // Child process: exec the service
            execl(path.c_str(), path.c_str(), "calc", nullptr);
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

// mDNS is confirmed available in SetUp, so discover-and-connect must WORK.
// A throw here is a real failure (a broken service type, a registration that
// never lands), reported loudly -- not swallowed into a skip.
TEST_F(DiscoveryTest, DiscoverAndConnect) {
    conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.add(10, 20), 30);
}

TEST_F(DiscoveryTest, MultipleCallsAfterDiscovery) {
    conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.add(1, 2), 3);
    EXPECT_EQ(calc.multiply(3, 4), 12);
    EXPECT_EQ(calc.subtract(10, 5), 5);
    EXPECT_EQ(calc.factorial(5), 120);
}

TEST_F(DiscoveryTest, StructReturnOverDiscovery) {
    conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    CalculatorProxy calc(*conn_);
    auto result = calc.divide(17, 5);
    EXPECT_EQ(result.quotient, 3);
    EXPECT_EQ(result.remainder, 2);
}

TEST_F(DiscoveryTest, ArrayOverDiscovery) {
    conn_ = std::make_unique<ServiceConnection>(mgr_.connect("calc"));
    CalculatorProxy calc(*conn_);
    EXPECT_EQ(calc.sum({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 55);
}

// Regression for the mDNS staleness fix (runtime.cpp): a SIGTERM'd
// discoverable service now deregisters cleanly, so a same-name service that
// starts afterward is the one peers resolve -- not the dead prior instance on
// its now-closed port. This is savannah's node-restart path: savannahd --mdns
// re-registers the same node name with a NEW ephemeral port on every restart.
// If deregistration regresses, the second connect resolves the stale dead
// port and throws.
TEST(DiscoveryRestart, ResolvesLiveServiceAfterSameNameRestart) {
    auto discovery = create_discovery();
    if (!discovery || !discovery->is_available()) {
        GTEST_SKIP() << "mDNS/DNS-SD not available on this host";
    }
    std::string path = get_service_path();
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Discovery service binary not built at " << path;
    }

    auto start_service = [&]() -> pid_t {
        pid_t pid = fork();
        if (pid == 0) {
            execl(path.c_str(), path.c_str(), "calcrestart", nullptr);
            _exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        return pid;
    };
    auto stop_service = [](pid_t pid) {
        kill(pid, SIGTERM);   // clean deregister via runtime's signal handler
        int status;
        waitpid(pid, &status, 0);
    };

    // First instance: register, resolve, use.
    pid_t p1 = start_service();
    {
        ServiceManager mgr;
        mgr.register_discoverable_service("calcrestart", "testcalc", 1);
        auto conn = std::make_unique<ServiceConnection>(mgr.connect("calcrestart"));
        EXPECT_EQ(CalculatorProxy(*conn).add(1, 1), 2);
    }
    stop_service(p1);  // its record must be gone after this

    // Same-name restart on a fresh ephemeral port. The peer must resolve THIS
    // live instance, not the stale one -- a successful RPC proves it.
    pid_t p2 = start_service();
    {
        ServiceManager mgr;
        mgr.register_discoverable_service("calcrestart", "testcalc", 1);
        auto conn = std::make_unique<ServiceConnection>(mgr.connect("calcrestart"));
        EXPECT_EQ(CalculatorProxy(*conn).add(2, 3), 5);
    }
    stop_service(p2);
}
