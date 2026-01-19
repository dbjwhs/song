// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/process.hpp>
#include <song/wire.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdlib>

using namespace song;

// Helper to get path to test service executables
static std::string get_test_service_path(const std::string& name) {
    // Look in build/examples directory
    std::filesystem::path base = std::filesystem::current_path();

    // Try a few common locations
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

    // Fall back to assuming we're in build directory
    return (base / "examples" / name).string();
}

// =============================================================================
// ServiceProcess Spawn
// =============================================================================

TEST(ProcessTest, SpawnEchoService) {
    std::string echo_path = get_test_service_path("echo_service");

    // Skip test if echo_service not found
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found at " << echo_path;
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    EXPECT_GT(proc.pid(), 0);
    EXPECT_TRUE(proc.alive());
    EXPECT_EQ(proc.negotiated_version(), wire::kCurrentVersion);

    proc.terminate();
    EXPECT_FALSE(proc.alive());
}

TEST(ProcessTest, SpawnFailsForNonexistent) {
    EXPECT_THROW(
        ServiceProcess::spawn("/nonexistent/path/to/service"),
        ServiceError
    );
}

// =============================================================================
// ServiceProcess Communication
// =============================================================================

TEST(ProcessTest, SendAndReceive) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    // Create a call message
    Buffer args;
    encode_string(args, "Hello, Test!");

    Buffer call_msg = wire::create_call_message(1, 1, 1, args);  // seq=1, service=1, method=1 (echo)
    proc.send(call_msg);

    // Receive response
    Buffer response;
    bool received = proc.receive(response, 5000);
    EXPECT_TRUE(received);

    // Decode response
    auto hdr = wire::decode_header_validated(response);
    EXPECT_EQ(hdr.type, wire::MsgType::result);
    EXPECT_EQ(hdr.sequence_id, 1);

    // The payload should be our echoed string
    std::string result = decode_string(response);
    EXPECT_EQ(result, "Hello, Test!");

    proc.terminate();
}

// =============================================================================
// ServiceProcess Lifecycle
// =============================================================================

TEST(ProcessTest, Terminate) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    EXPECT_TRUE(proc.alive());

    proc.terminate();
    EXPECT_FALSE(proc.alive());
}

TEST(ProcessTest, MoveConstructor) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc1 = ServiceProcess::spawn(echo_path.c_str());
    pid_t original_pid = proc1.pid();

    ServiceProcess proc2(std::move(proc1));

    EXPECT_EQ(proc1.pid(), -1);
    EXPECT_EQ(proc2.pid(), original_pid);
    EXPECT_TRUE(proc2.alive());

    proc2.terminate();
}

TEST(ProcessTest, MoveAssignment) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc1 = ServiceProcess::spawn(echo_path.c_str());
    ServiceProcess proc2 = ServiceProcess::spawn(echo_path.c_str());

    pid_t pid1 = proc1.pid();

    proc2 = std::move(proc1);

    EXPECT_EQ(proc1.pid(), -1);
    EXPECT_EQ(proc2.pid(), pid1);

    proc2.terminate();
}

// =============================================================================
// Method List / Capability Exchange
// =============================================================================

TEST(ProcessTest, MethodList) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    const auto& methods = proc.methods();

    // Echo service registers 3 methods: echo(1), add(2), double_all(3)
    EXPECT_EQ(methods.size(), 3);

    // Verify method 1,1 exists
    bool found = false;
    for (const auto& m : methods) {
        if (m.service_id == 1 && m.method_id == 1) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);

    proc.terminate();
}

// =============================================================================
// ServiceConnection
// =============================================================================

TEST(ProcessTest, ConnectionCall) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    // Call echo method
    Buffer args;
    encode_string(args, "Connection Test");

    Buffer result = conn.call(1, 1, args);
    std::string response = decode_string(result);
    EXPECT_EQ(response, "Connection Test");

    proc.terminate();
}

TEST(ProcessTest, ConnectionSupports) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    // Echo service has methods 1,2,3 on service 1
    EXPECT_TRUE(conn.supports(1, 1));
    EXPECT_TRUE(conn.supports(1, 2));
    EXPECT_TRUE(conn.supports(1, 3));
    EXPECT_FALSE(conn.supports(1, 99));  // Non-existent method
    EXPECT_FALSE(conn.supports(99, 1));  // Non-existent service

    proc.terminate();
}

TEST(ProcessTest, ConnectionAddMethod) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    // Call add method (service=1, method=2)
    Buffer args;
    encode_i32(args, 40);
    encode_i32(args, 2);

    Buffer result = conn.call(1, 2, args);
    i32 sum = decode_i32(result);
    EXPECT_EQ(sum, 42);

    proc.terminate();
}

TEST(ProcessTest, ConnectionMultipleCalls) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    for (int i = 0; i < 10; ++i) {
        Buffer args;
        encode_i32(args, i);
        encode_i32(args, i);

        Buffer result = conn.call(1, 2, args);  // add method
        i32 sum = decode_i32(result);
        EXPECT_EQ(sum, i + i);
    }

    proc.terminate();
}
