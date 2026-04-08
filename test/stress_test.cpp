// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/manager.hpp>
#include <song/object.hpp>
#include <song/registry.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <song/buffer.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
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
// Buffer Stress Tests
// =============================================================================

TEST(StressTest, BufferRapidAllocFree) {
    // Rapid allocation and deallocation to test for memory leaks
    // and small-buffer optimization under pressure
    for (int ndx = 0; ndx < 10000; ++ndx) {
        Buffer buf;
        encode_i32(buf, ndx);
        encode_string(buf, "test string payload");
        encode_f64(buf, 3.14159 * ndx);

        buf.reset_read();
        ASSERT_EQ(decode_i32(buf), ndx);
        ASSERT_EQ(decode_string(buf), "test string payload");
        ASSERT_NEAR(decode_f64(buf), 3.14159 * ndx, 0.001);
    }
}

TEST(StressTest, BufferLargePayload) {
    // Test buffer behavior near small-buffer optimization boundary (4KB)
    // and beyond into heap allocation
    std::vector<size_t> sizes = {100, 1000, 4000, 4096, 4097, 8192, 65536};

    for (size_t target_size : sizes) {
        Buffer buf;
        std::string payload(target_size, 'X');
        encode_string(buf, payload);

        buf.reset_read();
        std::string decoded = decode_string(buf);
        ASSERT_EQ(decoded.size(), target_size)
            << "Failed for payload size " << target_size;
        ASSERT_EQ(decoded, payload);
    }
}

TEST(StressTest, BufferMoveSemantics) {
    // Stress-test move semantics to catch use-after-move bugs
    for (int ndx = 0; ndx < 1000; ++ndx) {
        Buffer src;
        encode_i64(src, static_cast<int64_t>(ndx) * 1000000);
        encode_string(src, "move test");

        Buffer dst = std::move(src);

        dst.reset_read();
        ASSERT_EQ(decode_i64(dst), static_cast<int64_t>(ndx) * 1000000);
        ASSERT_EQ(decode_string(dst), "move test");
    }
}

// =============================================================================
// Wire Protocol Stress Tests
// =============================================================================

TEST(StressTest, WireHeaderRoundtrip) {
    // Encode/decode thousands of headers to test for subtle corruption
    for (uint32_t seq = 0; seq < 10000; ++seq) {
        Buffer buf;
        wire::Header hdr{};
        hdr.magic = wire::kMagic;
        hdr.flags = wire::MsgFlags::none;
        hdr.type = wire::MsgType::call;
        hdr.reserved = 0;
        hdr.payload_size = seq * 100;
        hdr.sequence_id = seq;

        wire::encode_header(buf, hdr);
        buf.reset_read();

        wire::Header decoded = wire::decode_header(buf);
        ASSERT_EQ(decoded.magic, wire::kMagic);
        ASSERT_EQ(decoded.sequence_id, seq);
        ASSERT_EQ(decoded.payload_size, seq * 100);
    }
}

TEST(StressTest, WireMessageCreation) {
    // Create many complete messages to test for allocation pressure
    for (int ndx = 0; ndx < 5000; ++ndx) {
        Buffer args;
        encode_i32(args, ndx);
        encode_i32(args, ndx * 2);

        Buffer msg = wire::create_call_message(
            static_cast<uint32_t>(ndx),  // sequence
            1,                            // service_id
            1,                            // method_id
            args
        );

        ASSERT_GT(msg.size(), sizeof(wire::Header));
    }
}

// =============================================================================
// ServiceManager Concurrent Access Tests
// =============================================================================

TEST(StressTest, ManagerConcurrentRegistration) {
    // Multiple threads registering services simultaneously
    ServiceManager mgr;
    constexpr int kThreads = 8;
    constexpr int kServicesPerThread = 100;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&mgr, &success_count, t]() {
            for (int ndx = 0; ndx < kServicesPerThread; ++ndx) {
                std::string name = "svc_t" + std::to_string(t) + "_" + std::to_string(ndx);
                try {
                    mgr.register_service(name, "/dummy/path", 1);
                    ++success_count;
                } catch (...) {
                    // Registration failures are acceptable under contention
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All unique names should have registered successfully
    EXPECT_EQ(success_count.load(), kThreads * kServicesPerThread);
}

TEST(StressTest, ManagerConcurrentStartStop) {
    // Multiple threads starting, stopping, and querying the same service
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found at " << echo_path;
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);

    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 20;
    std::atomic<int> start_count{0};
    std::atomic<int> stop_count{0};
    std::atomic<int> connect_count{0};
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int ndx = 0; ndx < kOpsPerThread; ++ndx) {
                try {
                    int op = (t * kOpsPerThread + ndx) % 3;
                    if (op == 0) {
                        mgr.start("echo");
                        ++start_count;
                    } else if (op == 1) {
                        mgr.stop("echo");
                        ++stop_count;
                    } else {
                        try {
                            auto conn = mgr.connect("echo");
                            ++connect_count;
                        } catch (const ServiceError&) {
                            // Service may not be running; that's fine
                        }
                    }
                } catch (const std::exception&) {
                    ++errors;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crashes or deadlocks is the primary assertion.
    // Some errors are expected (stopping already-stopped, etc.)
    EXPECT_EQ(start_count.load() + stop_count.load() + connect_count.load() + errors.load(),
              kThreads * kOpsPerThread);

    // Clean up
    try { mgr.stop("echo"); } catch (...) {}
}

TEST(StressTest, RegistryConcurrentOps) {
    // Concurrent register/unregister/discover on MemoryRegistry
    MemoryRegistry registry;
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 200;
    std::atomic<int> registered{0};
    std::atomic<int> discovered{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int ndx = 0; ndx < kOpsPerThread; ++ndx) {
                std::string name = "svc_" + std::to_string(t) + "_" + std::to_string(ndx);
                int op = ndx % 3;
                if (op == 0) {
                    ServiceInfo info{name, "localhost", static_cast<u16>(8000 + t * 1000 + ndx)};
                    if (registry.register_service(info)) {
                        ++registered;
                    }
                } else if (op == 1) {
                    registry.unregister_service(name);
                } else {
                    auto info = registry.discover(name);
                    if (info.is_valid()) {
                        ++discovered;
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crashes or data corruption
    EXPECT_GT(registered.load(), 0);
}

// =============================================================================
// Latency Measurement (informational, not pass/fail on timing)
// =============================================================================

TEST(StressTest, BufferEncodeDecode1000Roundtrips) {
    // Measure encode/decode throughput for a realistic message
    auto start = std::chrono::high_resolution_clock::now();

    for (int ndx = 0; ndx < 1000; ++ndx) {
        Buffer buf;
        encode_i32(buf, 42);
        encode_i64(buf, 123456789LL);
        encode_f64(buf, 3.14159);
        encode_string(buf, "Hello, Song!");

        buf.reset_read();
        decode_i32(buf);
        decode_i64(buf);
        decode_f64(buf);
        decode_string(buf);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "[  PERF  ] 1000 encode/decode roundtrips: " << us << " us"
              << " (" << (us / 1000.0) << " us/op)" << std::endl;

    // Sanity check: should complete in well under 100ms
    EXPECT_LT(us, 100000);
}

TEST(StressTest, PipeRPCLatency) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found at " << echo_path;
    }

    ServiceManager mgr;
    mgr.register_service("echo", echo_path, 1);
    ServiceConnection conn = mgr.connect("echo");

    // Warm up
    for (int ndx = 0; ndx < 10; ++ndx) {
        Buffer args;
        encode_string(args, "warmup");
        conn.call(1, 1, args);
    }

    // Measure 1000 sequential RPC calls
    constexpr int kCalls = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int ndx = 0; ndx < kCalls; ++ndx) {
        Buffer args;
        encode_string(args, "ping");
        conn.call(1, 1, args);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "[  PERF  ] " << kCalls << " pipe RPC calls: " << us << " us total"
              << " (" << (us / static_cast<double>(kCalls)) << " us/call)" << std::endl;

    mgr.stop("echo");
}

// =============================================================================
// ObjectRegistry Concurrent Lifecycle Tests
// =============================================================================

namespace {

/// Minimal concrete Object subclass used as a dummy entry in stress tests.
class DummyObject final : public song::Object {
public:
    void prop_get(song::u16, song::Buffer&) override {}
    void prop_set(song::u16, song::Buffer&, song::Buffer&) override {}
    void dispatch(song::u16, song::Buffer&, song::Buffer&) override {}
};

} // namespace

TEST(StressTest, RegistryConcurrentObjectLifecycle) {
    // 8 threads each create, verify, add_ref/release, then finally release
    // 100 independent objects. The registry must reach size 0 at the end.
    song::ObjectRegistry registry;

    constexpr u32 kTypeId = 1;
    registry.register_factory(kTypeId, [](song::u16, song::Buffer&) -> song::Object* {
        return new DummyObject{};
    });

    constexpr int kThreads = 8;
    constexpr int kItersPerThread = 100;
    std::atomic<int> total_created{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int ndx = 0; ndx < kItersPerThread; ++ndx) {
                song::Buffer args;

                // Create the object (ref_count starts at 1)
                song::i32 id = registry.create_object(kTypeId, 0, args);
                ++total_created;

                // Verify the object exists immediately after creation
                ASSERT_NE(registry.get(id), nullptr);

                // Add an extra reference then release it — net effect: zero change
                ASSERT_TRUE(registry.add_ref(id));
                registry.release(id);

                // Final release — should bring ref_count to 0 and remove the object
                registry.release(id);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(total_created.load(), kThreads * kItersPerThread);
    EXPECT_EQ(registry.size(), 0u);
}

// =============================================================================
// Service Crash During RPC
// =============================================================================

TEST(StressTest, CrashDuringRPCRecovers) {
    // crash_service aborts after 2 messages. Verify:
    // 1. Client gets an error (not a hang) when service crashes
    // 2. Auto-restart kicks in and the next call succeeds
    std::string crash_path = get_test_service_path("crash_service");
    if (!std::filesystem::exists(crash_path)) {
        GTEST_SKIP() << "crash_service not found at " << crash_path;
    }

    ServiceManager mgr;
    mgr.register_service("crash", crash_path, 1);
    mgr.set_auto_restart("crash", true);
    mgr.set_max_restarts("crash", 3);
    mgr.start_monitor(200);

    // First connection: send 2 messages, service crashes on 2nd
    {
        auto conn = mgr.connect("crash");

        // First call should succeed
        Buffer args;
        encode_string(args, "msg1");
        Buffer resp = conn.call(1, 1, args);
        EXPECT_EQ(decode_string(resp), "ok");

        // Second call triggers crash — client should get an error, not hang
        Buffer args2;
        encode_string(args2, "msg2");
        EXPECT_THROW(conn.call(1, 1, args2), std::exception);
    }

    // Wait for monitor to detect crash and restart
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Service should have been restarted
    EXPECT_GE(mgr.restart_count("crash"), 1);

    // New connection after restart should work
    {
        auto conn = mgr.connect("crash");
        Buffer args;
        encode_string(args, "after_restart");
        Buffer resp = conn.call(1, 1, args);
        EXPECT_EQ(decode_string(resp), "ok");
    }

    mgr.stop_monitor();
    try { mgr.stop("crash"); } catch (...) {}
}

// =============================================================================
// Multi-Client Concurrent RPC
// =============================================================================

TEST(StressTest, MultiClientConcurrentRPC) {
    // Each client gets its own service process (pipe IPC is single-client).
    // Verify correct response routing and no cross-client interference.
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found at " << echo_path;
    }

    constexpr int kClients = 4;
    constexpr int kCallsPerClient = 50;
    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};
    std::vector<std::thread> threads;

    for (int c = 0; c < kClients; ++c) {
        threads.emplace_back([&, c]() {
            try {
                // Each client gets its own ServiceManager and process instance
                ServiceManager mgr;
                std::string svc_name = "echo_" + std::to_string(c);
                mgr.register_service(svc_name, echo_path, 1);
                auto conn = mgr.connect(svc_name);

                for (int ndx = 0; ndx < kCallsPerClient; ++ndx) {
                    Buffer args;
                    std::string payload = "client" + std::to_string(c) + "_msg" + std::to_string(ndx);
                    encode_string(args, payload);

                    Buffer resp = conn.call(1, 1, args);
                    std::string result = decode_string(resp);
                    EXPECT_EQ(result, payload);
                    ++success_count;
                }

                mgr.stop(svc_name);
            } catch (const std::exception&) {
                ++error_count;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), kClients * kCallsPerClient);
    EXPECT_EQ(error_count.load(), 0);
}

// =============================================================================
// Crash Recovery Stress — max_restarts enforcement
// =============================================================================

TEST(StressTest, MaxRestartsEnforced) {
    // crash_service aborts after 2 messages. Set max_restarts = 2, then
    // repeatedly crash until the limit is hit. Verify the manager stops
    // restarting beyond the configured limit and no zombie processes remain.
    std::string crash_path = get_test_service_path("crash_service");
    if (!std::filesystem::exists(crash_path)) {
        GTEST_SKIP() << "crash_service not found at " << crash_path;
    }

    ServiceManager mgr;
    mgr.register_service("crash_stress", crash_path, 1);
    mgr.set_auto_restart("crash_stress", true);
    mgr.set_max_restarts("crash_stress", 2);
    mgr.start_monitor(100);

    // Crash the service repeatedly
    for (int round = 0; round < 4; ++round) {
        try {
            auto conn = mgr.connect("crash_stress");

            // First call succeeds
            Buffer args;
            encode_string(args, "msg1");
            conn.call(1, 1, args);

            // Second call triggers crash
            Buffer args2;
            encode_string(args2, "msg2");
            try { conn.call(1, 1, args2); } catch (...) {}
        } catch (...) {
            // Connection may fail if max_restarts exhausted
        }

        // Wait for monitor cycle
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // max_restarts = 2 means at most 2 restarts should have occurred
    EXPECT_LE(mgr.restart_count("crash_stress"), 2);

    mgr.stop_monitor();
    try { mgr.stop("crash_stress"); } catch (...) {}
}
