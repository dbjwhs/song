// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/manager.hpp>
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
    for (int i = 0; i < 10000; ++i) {
        Buffer buf;
        encode_i32(buf, i);
        encode_string(buf, "test string payload");
        encode_f64(buf, 3.14159 * i);

        buf.reset_read();
        ASSERT_EQ(decode_i32(buf), i);
        ASSERT_EQ(decode_string(buf), "test string payload");
        ASSERT_NEAR(decode_f64(buf), 3.14159 * i, 0.001);
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
    for (int i = 0; i < 1000; ++i) {
        Buffer src;
        encode_i64(src, static_cast<int64_t>(i) * 1000000);
        encode_string(src, "move test");

        Buffer dst = std::move(src);

        dst.reset_read();
        ASSERT_EQ(decode_i64(dst), static_cast<int64_t>(i) * 1000000);
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
    for (int i = 0; i < 5000; ++i) {
        Buffer args;
        encode_i32(args, i);
        encode_i32(args, i * 2);

        Buffer msg = wire::create_call_message(
            static_cast<uint32_t>(i),  // sequence
            1,                          // service_id
            1,                          // method_id
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
            for (int i = 0; i < kServicesPerThread; ++i) {
                std::string name = "svc_t" + std::to_string(t) + "_" + std::to_string(i);
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

// =============================================================================
// Latency Measurement (informational, not pass/fail on timing)
// =============================================================================

TEST(StressTest, BufferEncodeDecode1000Roundtrips) {
    // Measure encode/decode throughput for a realistic message
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i) {
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
    for (int i = 0; i < 10; ++i) {
        Buffer args;
        encode_string(args, "warmup");
        conn.call(1, 1, args);
    }

    // Measure 1000 sequential RPC calls
    constexpr int kCalls = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kCalls; ++i) {
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
