// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/manager.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>

using namespace song;

// Helper to get path to test service executables
static std::string get_test_service_path(const std::string& name) {
    std::filesystem::path base = std::filesystem::current_path();

    std::vector<std::filesystem::path> candidates = {
        base / "examples" / name,
        base / ".." / "examples" / name,
        base.parent_path() / "examples" / name
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    return (base / "examples" / name).string();
}

// =============================================================================
// ServiceManager Basic Operations
// =============================================================================

TEST(ManagerTest, RegisterService) {
    ServiceManager mgr;
    mgr.register_service("echo", "/path/to/echo", 1);
    // Registration should succeed without throwing
}

TEST(ManagerTest, StartAndStop) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found at " << echo_path;
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    ServiceProcess* proc = mgr.start("echo");
    EXPECT_NE(proc, nullptr);
    EXPECT_TRUE(mgr.is_alive("echo"));

    mgr.stop("echo");
    EXPECT_FALSE(mgr.is_alive("echo"));
}

TEST(ManagerTest, StartNonexistentServiceThrows) {
    ServiceManager mgr;
    EXPECT_THROW(mgr.start("nonexistent"), ServiceError);
}

TEST(ManagerTest, StopNonexistentServiceThrows) {
    ServiceManager mgr;
    EXPECT_THROW(mgr.stop("nonexistent"), ServiceError);
}

TEST(ManagerTest, StartAlreadyRunning) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    ServiceProcess* proc1 = mgr.start("echo");
    ServiceProcess* proc2 = mgr.start("echo");  // Should return same process

    EXPECT_EQ(proc1, proc2);

    mgr.stop("echo");
}

// =============================================================================
// ServiceManager Connect
// =============================================================================

TEST(ManagerTest, Connect) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    ServiceConnection conn = mgr.connect("echo");

    Buffer args;
    encode_string(args, "Manager Test");
    Buffer result = conn.call(1, 1, args);
    std::string response = decode_string(result);
    EXPECT_EQ(response, "Manager Test");

    mgr.stop("echo");
}

TEST(ManagerTest, ConnectAutoStarts) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    EXPECT_FALSE(mgr.is_alive("echo"));

    ServiceConnection conn = mgr.connect("echo");  // Should auto-start

    EXPECT_TRUE(mgr.is_alive("echo"));

    mgr.stop("echo");
}

// =============================================================================
// ServiceManager Restart
// =============================================================================

TEST(ManagerTest, Restart) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    mgr.start("echo");
    EXPECT_TRUE(mgr.is_alive("echo"));

    mgr.restart("echo");
    EXPECT_TRUE(mgr.is_alive("echo"));

    mgr.stop("echo");
}

// =============================================================================
// ServiceManager Replace
// =============================================================================

TEST(ManagerTest, Replace) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    mgr.start("echo");

    // Replace with same executable (just testing the mechanism)
    mgr.replace("echo", echo_path);
    EXPECT_TRUE(mgr.is_alive("echo"));

    // Verify still works
    ServiceConnection conn = mgr.connect("echo");
    Buffer args;
    encode_string(args, "After Replace");
    Buffer result = conn.call(1, 1, args);
    EXPECT_EQ(decode_string(result), "After Replace");

    mgr.stop("echo");
}

// =============================================================================
// ServiceManager Auto-Restart
// =============================================================================

TEST(ManagerTest, AutoRestartConfiguration) {
    ServiceManager mgr;
    mgr.register_service("test", "/path/to/test", 1);

    mgr.set_auto_restart("test", true);
    mgr.set_max_restarts("test", 10);

    EXPECT_EQ(mgr.restart_count("test"), 0);
}

TEST(ManagerTest, AutoRestartOnCrash) {
    std::string crash_path = get_test_service_path("crash_service");
    if (!std::filesystem::exists(crash_path)) {
        GTEST_SKIP() << "crash_service not found at " << crash_path;
    }

    ServiceManager mgr;
    mgr.register_service("crash", crash_path, 1);
    mgr.set_auto_restart("crash", true);
    mgr.set_max_restarts("crash", 2);

    std::atomic<int> restart_count{0};
    mgr.set_restart_callback([&](const std::string& name, int count) {
        restart_count++;
    });

    mgr.start_monitor(50);  // Check every 50ms for faster detection
    mgr.start("crash");

    // Send message to trigger crash
    try {
        auto conn = mgr.connect("crash");
        Buffer args;
        encode_string(args, "trigger");
        conn.call(1, 1, args);  // First call succeeds
        conn.call(1, 1, args);  // Second call causes crash
    } catch (...) {
        // Expected - service crashed
    }

    // Poll for restart with timeout - more reliable than fixed sleep
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (restart_count.load() < 1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_GE(restart_count.load(), 1);
    EXPECT_TRUE(mgr.is_alive("crash"));

    mgr.stop_monitor();
    mgr.stop("crash");
}

TEST(ManagerTest, MaxRestartsRespected) {
    std::string crash_path = get_test_service_path("crash_service");
    if (!std::filesystem::exists(crash_path)) {
        GTEST_SKIP() << "crash_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("crash", crash_path, 1);
    mgr.set_auto_restart("crash", true);
    mgr.set_max_restarts("crash", 2);

    mgr.start_monitor(100);
    mgr.start("crash");

    // Trigger multiple crashes
    for (int i = 0; i < 4; ++i) {
        try {
            auto conn = mgr.connect("crash");
            Buffer args;
            encode_string(args, "x");
            conn.call(1, 1, args);
            conn.call(1, 1, args);  // Triggers crash
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Should not exceed max_restarts
    EXPECT_LE(mgr.restart_count("crash"), 2);

    mgr.stop_monitor();
}

TEST(ManagerTest, ResetRestartCount) {
    ServiceManager mgr;
    mgr.register_service("test", "/path/to/test", 1);

    // Manually we can't increment, but we can reset
    mgr.reset_restart_count("test");
    EXPECT_EQ(mgr.restart_count("test"), 0);
}

// =============================================================================
// ServiceManager Monitor
// =============================================================================

TEST(ManagerTest, MonitorStartStop) {
    ServiceManager mgr;

    EXPECT_FALSE(mgr.monitor_running());

    mgr.start_monitor(100);
    EXPECT_TRUE(mgr.monitor_running());

    mgr.stop_monitor();
    EXPECT_FALSE(mgr.monitor_running());
}

TEST(ManagerTest, MonitorIdempotent) {
    ServiceManager mgr;

    mgr.start_monitor(100);
    mgr.start_monitor(100);  // Second call should be no-op
    EXPECT_TRUE(mgr.monitor_running());

    mgr.stop_monitor();
    mgr.stop_monitor();  // Second call should be no-op
    EXPECT_FALSE(mgr.monitor_running());
}

// =============================================================================
// ServiceManager Multiple Services
// =============================================================================

TEST(ManagerTest, MultipleServices) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceManager mgr;
    mgr.register_service("echo1", echo_path, 1);
    mgr.register_service("echo2", echo_path, 1);
    mgr.register_service("echo3", echo_path, 1);

    mgr.start("echo1");
    mgr.start("echo2");
    mgr.start("echo3");

    EXPECT_TRUE(mgr.is_alive("echo1"));
    EXPECT_TRUE(mgr.is_alive("echo2"));
    EXPECT_TRUE(mgr.is_alive("echo3"));

    // All should work independently
    {
        auto conn = mgr.connect("echo1");
        Buffer args;
        encode_string(args, "one");
        Buffer result = conn.call(1, 1, args);
        EXPECT_EQ(decode_string(result), "one");
    }
    {
        auto conn = mgr.connect("echo2");
        Buffer args;
        encode_string(args, "two");
        Buffer result = conn.call(1, 1, args);
        EXPECT_EQ(decode_string(result), "two");
    }

    mgr.stop("echo1");
    mgr.stop("echo2");
    mgr.stop("echo3");
}
