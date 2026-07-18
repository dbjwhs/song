// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/runtime.hpp>
#include <song/transport.hpp>
#include <song/process.hpp>
#include <song/wire.hpp>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace song;

namespace {

// Transport stub that records every message the runtime sends. receive() always
// reports "no data" because these tests drive handle_message() directly.
class MockTransport : public Transport {
public:
    std::vector<std::vector<std::byte>> sent;
    void send(const Buffer& msg) override {
        sent.emplace_back(msg.data(), msg.data() + msg.size());
    }
    bool receive(Buffer&, int) override { return false; }
    void close() override {}
    bool is_connected() const override { return true; }
    const char* type_name() const override { return "mock"; }
};

wire::Header make_header(wire::MsgType type, u32 seq, u32 payload_size) {
    wire::Header h{};
    h.magic = wire::kMagic;
    h.flags = wire::MsgFlags::none;
    h.type = type;
    h.reserved = 0;
    h.payload_size = payload_size;
    h.sequence_id = seq;
    return h;
}

struct Captured {
    wire::MsgType type;
    u32 seq;
    ErrorCode code;  // meaningful only when type == error
};

Captured decode_captured(const std::vector<std::byte>& bytes) {
    Buffer b;
    b.write(bytes.data(), bytes.size());
    b.reset_read();
    wire::Header hdr = wire::decode_header(b);
    ErrorCode code = ErrorCode::ok;
    if (hdr.type == wire::MsgType::error) {
        code = static_cast<ErrorCode>(decode_u16(b));
    }
    return {hdr.type, hdr.sequence_id, code};
}

// Locate a built example service executable (mirrors process_test.cpp).
std::string find_service(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path base = fs::current_path();
    for (const auto& p : {base / "examples" / name,
                          base / ".." / "examples" / name,
                          base.parent_path() / "examples" / name}) {
        if (fs::exists(p)) {
            return p.string();
        }
    }
    return (base / "examples" / name).string();
}

} // namespace

// =============================================================================
// Introspection Tests
// =============================================================================

class RuntimeIntrospectionTest : public ::testing::Test {
protected:
    ServiceRuntime runtime_;

    // Dummy dispatcher for testing
    static void dummy_dispatcher(u16, Buffer&, Buffer&) {}
};

TEST_F(RuntimeIntrospectionTest, InitiallyEmpty) {
    EXPECT_EQ(runtime_.service_count(), 0);
    EXPECT_EQ(runtime_.method_count(), 0);
    EXPECT_TRUE(runtime_.get_service_ids().empty());
    EXPECT_TRUE(runtime_.get_methods().empty());
}

TEST_F(RuntimeIntrospectionTest, ServiceCount) {
    runtime_.register_dispatcher(1, dummy_dispatcher);
    EXPECT_EQ(runtime_.service_count(), 1);

    runtime_.register_dispatcher(2, dummy_dispatcher);
    EXPECT_EQ(runtime_.service_count(), 2);

    runtime_.register_dispatcher(3, dummy_dispatcher);
    EXPECT_EQ(runtime_.service_count(), 3);
}

TEST_F(RuntimeIntrospectionTest, MethodCount) {
    runtime_.register_method(1, 1);
    EXPECT_EQ(runtime_.method_count(), 1);

    runtime_.register_method(1, 2);
    runtime_.register_method(1, 3);
    EXPECT_EQ(runtime_.method_count(), 3);

    runtime_.register_method(2, 1);
    EXPECT_EQ(runtime_.method_count(), 4);
}

TEST_F(RuntimeIntrospectionTest, GetServiceIds) {
    runtime_.register_dispatcher(10, dummy_dispatcher);
    runtime_.register_dispatcher(20, dummy_dispatcher);
    runtime_.register_dispatcher(30, dummy_dispatcher);

    auto ids = runtime_.get_service_ids();

    EXPECT_EQ(ids.size(), 3);
    // Order not guaranteed, check all are present
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 10) != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 20) != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 30) != ids.end());
}

TEST_F(RuntimeIntrospectionTest, GetMethods) {
    runtime_.register_method(1, 10);
    runtime_.register_method(1, 20);
    runtime_.register_method(2, 10);

    const auto& methods = runtime_.get_methods();

    EXPECT_EQ(methods.size(), 3);
    EXPECT_EQ(methods[0].service_id, 1);
    EXPECT_EQ(methods[0].method_id, 10);
    EXPECT_EQ(methods[1].service_id, 1);
    EXPECT_EQ(methods[1].method_id, 20);
    EXPECT_EQ(methods[2].service_id, 2);
    EXPECT_EQ(methods[2].method_id, 10);
}

TEST_F(RuntimeIntrospectionTest, HasService) {
    EXPECT_FALSE(runtime_.has_service(1));

    runtime_.register_dispatcher(1, dummy_dispatcher);

    EXPECT_TRUE(runtime_.has_service(1));
    EXPECT_FALSE(runtime_.has_service(2));
}

TEST_F(RuntimeIntrospectionTest, HasMethod) {
    EXPECT_FALSE(runtime_.has_method(1, 1));

    runtime_.register_method(1, 1);
    runtime_.register_method(1, 2);
    runtime_.register_method(2, 1);

    EXPECT_TRUE(runtime_.has_method(1, 1));
    EXPECT_TRUE(runtime_.has_method(1, 2));
    EXPECT_TRUE(runtime_.has_method(2, 1));
    EXPECT_FALSE(runtime_.has_method(2, 2));
    EXPECT_FALSE(runtime_.has_method(3, 1));
}

TEST_F(RuntimeIntrospectionTest, ReplacingDispatcherDoesNotIncrementCount) {
    runtime_.register_dispatcher(1, dummy_dispatcher);
    EXPECT_EQ(runtime_.service_count(), 1);

    // Register again with same ID
    runtime_.register_dispatcher(1, dummy_dispatcher);
    EXPECT_EQ(runtime_.service_count(), 1);
}

TEST_F(RuntimeIntrospectionTest, MethodsAccumulateForSameService) {
    runtime_.register_method(1, 1);
    runtime_.register_method(1, 2);
    runtime_.register_method(1, 3);

    EXPECT_EQ(runtime_.method_count(), 3);

    // Methods for same service are all stored
    const auto& methods = runtime_.get_methods();
    for (const auto& m : methods) {
        EXPECT_EQ(m.service_id, 1);
    }
}

// =============================================================================
// Message Dispatch: malformed-input and error-path handling
//
// handle_message()/handle_message_fd() must never let a decode exception escape
// into the service loop: run() and client_loop() install no handler, so an
// escaping throw would std::terminate the service (a remote denial of service).
// A malformed request/response message must produce a decode-error reply; a
// malformed fire-and-forget message must be dropped silently so the reply is not
// mistaken for the response to the client's next request.
// =============================================================================

class RuntimeDispatchTest : public ::testing::Test {
protected:
    ServiceRuntime runtime_;
    MockTransport transport_;
    std::unordered_set<i32> tracked_;
    ServiceRuntime::SubscriptionSet subs_;

    void handle(const wire::Header& hdr, Buffer& payload) {
        runtime_.handle_message(hdr, payload, transport_, tracked_, subs_);
    }
};

TEST_F(RuntimeDispatchTest, TruncatedCallRepliesDecodeErrorNotTerminate) {
    // Payload holds one u16; the method-call header needs two.
    Buffer payload;
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 42, 2);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.seq, 42u);
    EXPECT_EQ(d.code, ErrorCode::decode_error);
}

TEST_F(RuntimeDispatchTest, TruncatedCreateRepliesDecodeError) {
    // ObjectCreateHeader needs 8 bytes; supply 4.
    Buffer payload;
    encode_u32(payload, 7);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::create, 7, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_EQ(transport_.sent.size(), 1u);
    EXPECT_EQ(decode_captured(transport_.sent[0]).code, ErrorCode::decode_error);
}

TEST_F(RuntimeDispatchTest, TruncatedPropGetRepliesDecodeError) {
    // PropertyHeader needs 12 bytes; supply 4.
    Buffer payload;
    encode_u32(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::prop_get, 9, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_EQ(transport_.sent.size(), 1u);
    EXPECT_EQ(decode_captured(transport_.sent[0]).code, ErrorCode::decode_error);
}

TEST_F(RuntimeDispatchTest, TruncatedReleaseIsSilentFireAndForget) {
    // release carries no response; a decode failure must emit nothing, or the
    // reply would be read as the response to the client's next request.
    Buffer payload;
    encode_u32(payload, 1);  // ObjectReleaseHeader needs 8 bytes; supply 4
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::release, 0, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    EXPECT_TRUE(transport_.sent.empty());
}

TEST_F(RuntimeDispatchTest, UnknownServiceRepliesUnknownService) {
    // Well-formed call to a service that has no registered dispatcher.
    Buffer payload;
    encode_u16(payload, 99);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 5, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.code, ErrorCode::unknown_service);
}

TEST_F(RuntimeDispatchTest, DispatcherThrowRepliesError) {
    runtime_.register_dispatcher(1, [](u16, Buffer&, Buffer&) {
        throw std::runtime_error("boom");
    });
    Buffer payload;
    encode_u16(payload, 1);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 6, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.code, ErrorCode::unknown_method);
}

TEST_F(RuntimeDispatchTest, DispatcherNonStdExceptionDoesNotTerminate) {
    // A dispatcher throwing something that is not a std::exception must still be
    // contained by the outer catch(...) wrapper rather than escaping the loop.
    runtime_.register_dispatcher(1, [](u16, Buffer&, Buffer&) {
        throw 42;  // not derived from std::exception
    });
    Buffer payload;
    encode_u16(payload, 1);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 8, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_EQ(transport_.sent.size(), 1u);
    EXPECT_EQ(decode_captured(transport_.sent[0]).type, wire::MsgType::error);
}

TEST_F(RuntimeDispatchTest, StreamingDispatcherThrowRepliesError) {
    runtime_.register_stream_dispatcher(2, [](u16, Buffer&, StreamWriter&) {
        throw std::runtime_error("stream boom");
    });
    Buffer payload;
    encode_u16(payload, 2);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 11, 4);

    ASSERT_NO_THROW(handle(hdr, payload));
    ASSERT_FALSE(transport_.sent.empty());
    bool saw_error = false;
    for (const auto& m : transport_.sent) {
        if (decode_captured(m).type == wire::MsgType::error) {
            saw_error = true;
        }
    }
    EXPECT_TRUE(saw_error);
}

// Integration: drive the real pipe-mode run() loop (via a spawned service) with
// a malformed message and confirm the service survives and keeps serving. This
// covers the exact site (handle_message_fd, invoked from run() with no handler)
// where a decode exception previously escaped into std::terminate.
TEST(RuntimeFdDispatchTest, MalformedCallDoesNotCrashPipeService) {
    std::string echo = find_service("echo_service");
    if (!std::filesystem::exists(echo)) {
        GTEST_SKIP() << "echo_service not found at " << echo;
    }

    ServiceProcess proc = ServiceProcess::spawn(echo.c_str());

    // A CALL whose payload (2 bytes) is too short for the 4-byte method header.
    Buffer bad;
    wire::encode_header(bad, make_header(wire::MsgType::call, 55, 2));
    encode_u16(bad, 1);
    proc.send(bad);

    // The service must reply with a decode error rather than dying (EOF).
    Buffer resp;
    ASSERT_TRUE(proc.receive(resp, 3000)) << "service crashed on malformed input";
    auto rhdr = wire::decode_header_validated(resp);
    EXPECT_EQ(rhdr.type, wire::MsgType::error);
    EXPECT_EQ(static_cast<ErrorCode>(decode_u16(resp)), ErrorCode::decode_error);

    // And the loop survived: a subsequent well-formed call still works.
    Buffer args;
    encode_string(args, "still alive");
    proc.send(wire::create_call_message(56, 1, 1, args));
    Buffer resp2;
    ASSERT_TRUE(proc.receive(resp2, 3000));
    auto rhdr2 = wire::decode_header_validated(resp2);
    EXPECT_EQ(rhdr2.type, wire::MsgType::result);
    EXPECT_EQ(decode_string(resp2), "still alive");

    proc.terminate();
}
