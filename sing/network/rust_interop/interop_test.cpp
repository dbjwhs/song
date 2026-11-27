// MIT License
// Copyright (c) 2026 dbjwhs

// C++ client against the Rust service runtime: the interop service of
// interop.song served by rust/examples/interop_service.rs, over TCP (with
// and without HMAC) and over pipes.
//
// The Rust binary is built by cargo, which the CMake build does not
// require: every test here skips when the binary is absent. ctest builds it
// first when cargo is installed (the rust_build fixture).

#include <gtest/gtest.h>
#include <song/song.hpp>
#include "interop.hpp"
#include "interop_samples.hpp"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace song;
using namespace song::interop;

namespace {

const char* kKey = "song-interop-test-secret-32bytes";

// The Rust service binary: $SONG_RUST_INTEROP_SERVICE, else where
// `cargo build --examples` puts it in this checkout.
std::string rust_service_path() {
    if (const char* env = std::getenv("SONG_RUST_INTEROP_SERVICE")) {
        return env;
    }
    return std::string(SONG_SOURCE_DIR) + "/rust/target/debug/examples/interop_service";
}

class RustInteropTest : public ::testing::Test {
protected:
    static constexpr u16 kTestPort = 18931;

    // Spawn the Rust service on kTestPort (with a key when one is given)
    // and wait until it accepts connections. False, with the test marked
    // skipped, when the binary is not built.
    void start(const char* key = nullptr) {
        std::string path = rust_service_path();
        if (!std::filesystem::exists(path)) {
            skipped_ = true;
            return;
        }

        server_pid_ = fork();
        if (server_pid_ == 0) {
            std::string port_str = std::to_string(kTestPort);
            execl(path.c_str(), path.c_str(), port_str.c_str(), key, nullptr);
            _exit(1);  // exec failed
        }

        for (int attempt = 0; attempt < 50; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            try {
                TcpTransport probe;
                probe.connect("127.0.0.1", kTestPort, 500);
                return;
            } catch (const std::exception&) {
                // Not listening yet, retry
            }
        }
        FAIL() << "Rust interop service did not start listening on port " << kTestPort;
    }

    void TearDown() override {
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
            int status;
            waitpid(server_pid_, &status, 0);
        }
    }

    // A handshaken connection, through SecureTransport when a key is given.
    static ServiceConnection connect(const char* key = nullptr) {
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect("127.0.0.1", kTestPort, 5000);
        std::unique_ptr<Transport> transport = std::move(tcp);
        if (key != nullptr) {
            transport = std::make_unique<SecureTransport>(std::move(transport), SecurityConfig(key));
        }
        ServiceConnection conn(std::move(transport));
        conn.init_handshake();
        return conn;
    }

    pid_t server_pid_ = -1;
    bool skipped_ = false;
};

#define START_OR_SKIP(...)                                                              \
    start(__VA_ARGS__);                                                                 \
    if (skipped_) {                                                                     \
        GTEST_SKIP() << "Rust interop service not built at " << rust_service_path()     \
                     << " (run `cargo build --examples` in rust/; needs cargo)";        \
    }                                                                                   \
    if (HasFatalFailure()) {                                                            \
        return;                                                                         \
    }

Batch make_batch() {
    Batch b;
    for (i32 ndx = 0; ndx < 4; ++ndx) {
        b.samples.push_back(make_sample(ndx, "batch"));
    }
    b.best = make_sample(7, "best");
    b.grid = {{1, 2, 3}, {}, {-4}};
    b.history = {Status::ok, Status::failed, Status::retry, Status::ok};
    return b;
}

// The calls every transport must carry: each value kind, both stream
// outcomes, and a failing unary call.
void exercise(ServiceConnection& conn) {
    InteropProxy proxy(conn);
    SecondProxy second(conn);

    EXPECT_EQ(second.whoami("cpp"), "rust serving cpp") << "the peer is the Rust implementation";
    EXPECT_EQ(proxy.add(40, 2), 42);
    proxy.ping();

    Sample sample = make_sample(5, "five");
    EXPECT_TRUE(same_sample(proxy.echo_sample(sample), sample));
    EXPECT_TRUE(same_sample(proxy.echo_sample(Sample{}), Sample{}));

    Batch batch = make_batch();
    Batch echoed = proxy.echo_batch(batch);
    ASSERT_EQ(echoed.samples.size(), batch.samples.size());
    for (size_t ndx = 0; ndx < batch.samples.size(); ++ndx) {
        EXPECT_TRUE(same_sample(echoed.samples[ndx], batch.samples[ndx])) << "sample " << ndx;
    }
    EXPECT_TRUE(same_sample(echoed.best, batch.best));
    EXPECT_EQ(echoed.grid, batch.grid);
    EXPECT_EQ(echoed.history, batch.history);

    Access read_exec = static_cast<Access>(static_cast<u32>(Access::read) | static_cast<u32>(Access::exec));
    EXPECT_EQ(proxy.describe(Status::failed, read_exec, "job "), "job failed:5");
    EXPECT_EQ(proxy.describe(Status::ok, static_cast<Access>(0), ""), "ok:0");

    // Optional parameters and an optional return, present and absent.
    EXPECT_EQ(proxy.bump(i64{5}, std::string("abc")), std::optional<i64>(8));
    EXPECT_EQ(proxy.bump(i64{-2}, std::nullopt), std::optional<i64>(-2));
    EXPECT_EQ(proxy.bump(std::nullopt, std::string("ignored")), std::nullopt);

    // A failing unary call is a service error carrying the message, and the
    // connection goes on.
    try {
        proxy.fail("no such job");
        ADD_FAILURE() << "fail() returned";
    } catch (const ServiceError& e) {
        EXPECT_EQ(std::string(e.what()), "Service error (code 3): no such job");
    }
    EXPECT_EQ(proxy.add(1, 1), 2);

    // Streams: scalars and structs.
    std::vector<i32> counted;
    proxy.count_to(5, [&](i32& value) { counted.push_back(value); }, 5000);
    EXPECT_EQ(counted, (std::vector<i32>{1, 2, 3, 4, 5}));

    int empty_chunks = 0;
    proxy.count_to(0, [&](i32&) { ++empty_chunks; }, 5000);
    EXPECT_EQ(empty_chunks, 0);

    std::vector<Sample> samples;
    proxy.samples(6, "s", [&](Sample& value) { samples.push_back(value); }, 5000);
    ASSERT_EQ(samples.size(), 6u);
    for (i32 ndx = 1; ndx <= 6; ++ndx) {
        EXPECT_TRUE(same_sample(samples[ndx - 1], make_sample(ndx, "s"))) << "sample " << ndx;
    }

    // A stream that fails part way: the chunks written first arrive, then
    // the error ends the stream. A clean stream_end here would be the bug.
    std::vector<i32> before_failure;
    try {
        proxy.fail_after(3, "ran dry", [&](i32& value) { before_failure.push_back(value); }, 5000);
        ADD_FAILURE() << "fail_after() ended cleanly";
    } catch (const ServiceError& e) {
        EXPECT_EQ(std::string(e.what()), "Service error (code 3): ran dry");
    }
    EXPECT_EQ(before_failure, (std::vector<i32>{1, 2, 3}));
    EXPECT_EQ(proxy.add(2, 3), 5) << "the connection is usable after a failed stream";
}

} // namespace

// =============================================================================
// TCP
// =============================================================================

TEST_F(RustInteropTest, HandshakeAnnouncesMethodsAndStreaming) {
    START_OR_SKIP();
    ServiceConnection conn = connect();

    EXPECT_EQ(conn.negotiated_version(), wire::kCurrentVersion);
    EXPECT_NE(conn.peer_capabilities() & static_cast<u32>(wire::Capability::streaming), 0u);

    // The method list the Rust runtime announces: the same ids, with the
    // streaming flag on the stream methods of the _Stream service id.
    const auto& methods = conn.methods();
    ASSERT_EQ(methods.size(), 11u);
    EXPECT_EQ(methods[0].service_id, kService_Interop);
    EXPECT_EQ(methods[0].method_id, kMethod_Interop_add);
    EXPECT_EQ(methods[0].flags, wire::MethodFlags::none);
    EXPECT_EQ(methods[6].service_id, kService_Interop_Stream);
    EXPECT_EQ(methods[6].method_id, kMethod_Interop_count_to);
    EXPECT_EQ(methods[6].flags, wire::MethodFlags::streaming);
    EXPECT_EQ(methods[9].service_id, kService_Interop);
    EXPECT_EQ(methods[9].method_id, kMethod_Interop_bump);
    EXPECT_EQ(methods[10].service_id, kService_Second);
    EXPECT_EQ(methods[10].method_id, kMethod_Second_whoami);
}

TEST_F(RustInteropTest, UnaryAndStreamingCalls) {
    START_OR_SKIP();
    ServiceConnection conn = connect();
    exercise(conn);
}

TEST_F(RustInteropTest, UnknownServiceAndMethod) {
    START_OR_SKIP();
    ServiceConnection conn = connect();
    Buffer empty;

    try {
        conn.call(77, 1, empty);
        ADD_FAILURE() << "call to an unknown service returned";
    } catch (const ServiceError& e) {
        EXPECT_EQ(std::string(e.what()), "Service error (code 2): Unknown service ID");
    }
    try {
        conn.call(kService_Interop, 99, empty);
        ADD_FAILURE() << "call to an unknown method returned";
    } catch (const ServiceError& e) {
        EXPECT_EQ(std::string(e.what()), "Service error (code 3): Unknown method ID: 99");
    }
    EXPECT_EQ(InteropProxy(conn).add(1, 2), 3);
}

TEST_F(RustInteropTest, SeveralClientsAtOnce) {
    START_OR_SKIP();
    ServiceConnection first = connect();
    ServiceConnection second = connect();
    EXPECT_EQ(InteropProxy(first).add(1, 2), 3);
    EXPECT_EQ(InteropProxy(second).add(3, 4), 7);
    EXPECT_EQ(InteropProxy(first).add(5, 6), 11);
}

// =============================================================================
// HMAC
// =============================================================================

TEST_F(RustInteropTest, HmacCarriesUnaryAndStreamingCalls) {
    START_OR_SKIP(kKey);
    ServiceConnection conn = connect(kKey);
    EXPECT_NE(conn.peer_capabilities() & static_cast<u32>(wire::Capability::hmac), 0u);
    exercise(conn);
}

TEST_F(RustInteropTest, HmacWrongKeyIsRefused) {
    START_OR_SKIP(kKey);
    // The client cannot verify the service's init message.
    EXPECT_THROW(connect("some-other-secret-of-32-bytes!!!"), std::exception);
    // And the service still serves a client that has the key.
    ServiceConnection good = connect(kKey);
    EXPECT_EQ(InteropProxy(good).add(1, 2), 3);
}

TEST_F(RustInteropTest, HmacKeylessClientCannotCall) {
    START_OR_SKIP(kKey);
    // The untagged call is refused by the service, which hangs up.
    try {
        ServiceConnection conn = connect();
        InteropProxy(conn).add(1, 2);
        ADD_FAILURE() << "an untagged call was answered";
    } catch (const std::exception&) {
    }
}

TEST_F(RustInteropTest, KeyedClientRefusesUnkeyedService) {
    START_OR_SKIP();
    EXPECT_THROW(connect(kKey), std::exception);
}

// =============================================================================
// Pipes
// =============================================================================

// The Rust service spawned as a local service: the C++ runtime's fork/exec,
// init handshake and calls over its stdin and stdout.
TEST(RustInteropPipeTest, UnaryAndStreamingCallsOverPipes) {
    std::string path = rust_service_path();
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Rust interop service not built at " << path
                     << " (run `cargo build --examples` in rust/; needs cargo)";
    }

    ServiceProcess proc = ServiceProcess::spawn(path.c_str());
    {
        ServiceConnection conn(&proc);
        EXPECT_EQ(conn.negotiated_version(), wire::kCurrentVersion);
        exercise(conn);
    }
    if (proc.alive()) {
        proc.terminate();
    }
}

// =============================================================================
// Generated array codecs: the element count is the peer's word
// =============================================================================

namespace {

std::string decode_failure(Buffer& buf, bool structs) {
    buf.reset_read();
    try {
        if (structs) {
            decode_array_Sample(buf);
        } else {
            decode_array_Status(buf);
        }
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

// Needs no service: the generated decoders for arrays of structs and enums
// refuse a count over the cap, as decode_array<T> does, before reserving.
TEST(InteropArrayLimitTest, ForgedCountIsRefused) {
    for (bool structs : {true, false}) {
        Buffer forged;
        encode_u32(forged, 0xFFFFFFFFu);
        EXPECT_EQ(decode_failure(forged, structs), "Array too large");

        Buffer over;
        encode_u32(over, static_cast<u32>(kMaxArrayCount + 1));
        EXPECT_EQ(decode_failure(over, structs), "Array too large");

        // At the cap the count is allowed, and the missing elements are
        // the error.
        Buffer at_cap;
        encode_u32(at_cap, static_cast<u32>(kMaxArrayCount));
        std::string what = decode_failure(at_cap, structs);
        EXPECT_FALSE(what.empty());
        EXPECT_NE(what, "Array too large");
    }
}

TEST(InteropArrayLimitTest, EncodingOverTheCapIsRefused) {
    Buffer buf;
    std::vector<Status> at_cap(kMaxArrayCount, Status::ok);
    EXPECT_NO_THROW(encode_array_Status(buf, at_cap));

    std::vector<Status> over(kMaxArrayCount + 1, Status::ok);
    Buffer buf2;
    EXPECT_THROW(encode_array_Status(buf2, over), std::runtime_error);
}
