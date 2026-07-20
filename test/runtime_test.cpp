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
#include <song/object.hpp>
#include <song/stream.hpp>
#include <thread>
#include <chrono>

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

// Minimal remotable object with one i32 property, for object-ownership tests.
class OwnedTestObject : public Object {
    i32 value_ = 0;
public:
    void prop_get(u16, Buffer& resp) override { encode_i32(resp, value_); }
    void prop_set(u16, Buffer& req, Buffer& resp) override {
        value_ = decode_i32(req);
        encode_i32(resp, value_);
    }
    void dispatch(u16, Buffer&, Buffer&) override {}
};

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

    // Deliver a prop_subscribe for (object_id, 1) through handle_message.
    void subscribe(i32 object_id) {
        Buffer msg = wire::create_property_subscribe_message(1, object_id, 1);
        msg.reset_read();
        wire::Header hdr = wire::decode_header(msg);  // cursor now at the body
        handle(hdr, msg);
    }
};

// prop_subscribe must stop growing the per-connection set and the shared registry
// once the cap is reached, while remaining idempotent for already-tracked keys.
TEST_F(RuntimeDispatchTest, PropSubscribeCapBoundsPerConnectionGrowth) {
    runtime_.set_max_subscriptions_per_connection(3);

    for (i32 obj = 0; obj < 10; ++obj) {
        ASSERT_NO_THROW(subscribe(obj));  // distinct object -> distinct key
    }

    // Only the first 3 distinct subscriptions are retained; the rest are dropped.
    EXPECT_EQ(subs_.size(), 3u);
    EXPECT_EQ(runtime_.subscriptions().total_subscriptions(), 3u);

    // Re-subscribing to an already-tracked key stays idempotent past the cap.
    ASSERT_NO_THROW(subscribe(0));
    EXPECT_EQ(subs_.size(), 3u);
    EXPECT_EQ(runtime_.subscriptions().total_subscriptions(), 3u);
}

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


// =============================================================================
// Object protocol: create/release tracking and property access
//
// These drive the runtime's object-protocol branches (create, release,
// prop_get, prop_set) directly through handle_message with a MockTransport,
// exercising connection object-tracking and the object_not_found /
// property_error reply paths that no other test covers.
// =============================================================================

namespace {

// Minimal remotable object used to exercise the runtime's object-protocol
// dispatch. Property kProp_throws makes prop_get/prop_set raise so the
// property_error branch can be observed.
class RuntimeDispatchObject : public Object {
    i32 value_ = 0;

public:
    static constexpr u16 kProp_value = 1;
    static constexpr u16 kProp_throws = 2;

    explicit RuntimeDispatchObject(i32 v = 0) : value_(v) {}

    void prop_get(u16 prop_id, Buffer& resp) override {
        if (prop_id == kProp_throws) {
            throw std::runtime_error("prop_get nope");
        }
        encode_i32(resp, value_);
    }

    void prop_set(u16 prop_id, Buffer& req, Buffer& resp) override {
        if (prop_id == kProp_throws) {
            throw std::runtime_error("prop_set nope");
        }
        value_ = decode_i32(req);
        encode_i32(resp, value_);
    }

    void dispatch(u16, Buffer&, Buffer&) override {}
};

// Shared type_id for the runtime object-protocol tests.
constexpr u32 kRuntimeObjType = 7;

// Decode an ObjectRef carried in a result message captured by MockTransport.
wire::ObjectRef result_object_ref(const std::vector<std::byte>& bytes) {
    Buffer b;
    b.write(bytes.data(), bytes.size());
    b.reset_read();
    wire::decode_header(b);
    return wire::decode_object_ref(b);
}

// Decode a single i32 carried in a result message payload.
i32 result_i32(const std::vector<std::byte>& bytes) {
    Buffer b;
    b.write(bytes.data(), bytes.size());
    b.reset_read();
    wire::decode_header(b);
    return decode_i32(b);
}

// Decode the human-readable text of an error message (skips the u16 code).
std::string error_text(const std::vector<std::byte>& bytes) {
    Buffer b;
    b.write(bytes.data(), bytes.size());
    b.reset_read();
    wire::decode_header(b);
    decode_u16(b);
    return decode_string(b);
}

} // namespace

// Reuses RuntimeDispatchTest's runtime_/transport_/tracked_/subs_/handle() and
// adds object-protocol message builders on top.
class RuntimeObjectProtocolTest : public RuntimeDispatchTest {
protected:
    static constexpr u32 kType = kRuntimeObjType;

    void register_factory() {
        runtime_.register_factory(kType, [](u16 ctor, Buffer& args) -> Object* {
            if (ctor == 1) {
                return new RuntimeDispatchObject(decode_i32(args));
            }
            return new RuntimeDispatchObject(0);
        });
    }

    // Create an object through the runtime; return its assigned id.
    i32 create(u16 ctor, u32 seq) {
        Buffer args;
        Buffer payload;
        wire::encode_object_create(payload, kType, ctor, args);
        payload.reset_read();
        auto hdr = make_header(wire::MsgType::create, seq,
                               static_cast<u32>(payload.size()));
        handle(hdr, payload);
        return result_object_ref(transport_.sent.back()).object_id;
    }

    void send_release(i32 object_id, u32 seq) {
        Buffer payload;
        wire::encode_object_release(payload, kType, object_id);
        payload.reset_read();
        auto hdr = make_header(wire::MsgType::release, seq,
                               static_cast<u32>(payload.size()));
        handle(hdr, payload);
    }

    void send_prop_get(i32 object_id, u16 prop_id, u32 seq) {
        Buffer payload;
        wire::encode_property_get(payload, kType, object_id, prop_id);
        payload.reset_read();
        auto hdr = make_header(wire::MsgType::prop_get, seq,
                               static_cast<u32>(payload.size()));
        handle(hdr, payload);
    }

    void send_prop_set(i32 object_id, u16 prop_id, const Buffer& value, u32 seq) {
        Buffer payload;
        wire::encode_property_set(payload, kType, object_id, prop_id, value);
        payload.reset_read();
        auto hdr = make_header(wire::MsgType::prop_set, seq,
                               static_cast<u32>(payload.size()));
        handle(hdr, payload);
    }
};

TEST_F(RuntimeObjectProtocolTest, CreateTracksObjectAndReturnsRef) {
    register_factory();
    i32 id = create(0, 1);

    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::result);
    EXPECT_EQ(d.seq, 1u);

    wire::ObjectRef ref = result_object_ref(transport_.sent[0]);
    EXPECT_EQ(ref.type_id, kType);
    EXPECT_EQ(ref.object_id, id);

    EXPECT_LT(id, 0);
    EXPECT_EQ(tracked_.count(id), 1u);
    EXPECT_NE(runtime_.objects().get(id), nullptr);
    EXPECT_EQ(runtime_.objects().size(), 1u);
}

TEST_F(RuntimeObjectProtocolTest, ReleaseUntracksAndFreesObject) {
    register_factory();
    i32 id = create(0, 1);
    ASSERT_EQ(transport_.sent.size(), 1u);

    send_release(id, 0);

    // Release is fire-and-forget: no new message emitted.
    EXPECT_EQ(transport_.sent.size(), 1u);
    EXPECT_EQ(tracked_.count(id), 0u);
    EXPECT_EQ(runtime_.objects().get(id), nullptr);
    EXPECT_EQ(runtime_.objects().size(), 0u);
}

TEST_F(RuntimeObjectProtocolTest, ReleaseOneKeepsOthers) {
    register_factory();
    i32 id1 = create(0, 1);
    i32 id2 = create(0, 2);
    ASSERT_NE(id1, id2);
    EXPECT_EQ(runtime_.objects().size(), 2u);

    send_release(id1, 0);

    EXPECT_EQ(tracked_.count(id1), 0u);
    EXPECT_EQ(tracked_.count(id2), 1u);
    EXPECT_EQ(runtime_.objects().get(id1), nullptr);
    EXPECT_NE(runtime_.objects().get(id2), nullptr);
    EXPECT_EQ(runtime_.objects().size(), 1u);
}

TEST_F(RuntimeObjectProtocolTest, ReleaseUnknownIdIsSilentNoop) {
    // A well-formed release for an id that was never created must be a no-op:
    // no reply (fire-and-forget) and no throw.
    ASSERT_NO_THROW(send_release(-4242, 0));
    EXPECT_TRUE(transport_.sent.empty());
    EXPECT_EQ(runtime_.objects().size(), 0u);
}

TEST_F(RuntimeObjectProtocolTest, PropGetMissingObjectRepliesObjectNotFound) {
    send_prop_get(-999, RuntimeDispatchObject::kProp_value, 3);

    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.code, ErrorCode::object_not_found);
    EXPECT_NE(error_text(transport_.sent[0]).find("Object not found"),
              std::string::npos);
}

TEST_F(RuntimeObjectProtocolTest, PropSetMissingObjectRepliesObjectNotFound) {
    Buffer value;
    encode_i32(value, 5);
    send_prop_set(-999, RuntimeDispatchObject::kProp_value, value, 4);

    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.code, ErrorCode::object_not_found);
}

TEST_F(RuntimeObjectProtocolTest, PropGetAccessorThrowRepliesPropertyError) {
    register_factory();
    i32 id = create(0, 1);
    transport_.sent.clear();  // isolate the prop_get reply

    send_prop_get(id, RuntimeDispatchObject::kProp_throws, 5);

    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.code, ErrorCode::property_error);
    EXPECT_NE(error_text(transport_.sent[0]).find("prop_get nope"),
              std::string::npos);
}

TEST_F(RuntimeObjectProtocolTest, PropSetAccessorThrowRepliesPropertyError) {
    register_factory();
    i32 id = create(0, 1);
    transport_.sent.clear();

    Buffer value;
    encode_i32(value, 5);
    send_prop_set(id, RuntimeDispatchObject::kProp_throws, value, 6);

    ASSERT_EQ(transport_.sent.size(), 1u);
    auto d = decode_captured(transport_.sent[0]);
    EXPECT_EQ(d.type, wire::MsgType::error);
    EXPECT_EQ(d.code, ErrorCode::property_error);
    EXPECT_NE(error_text(transport_.sent[0]).find("prop_set nope"),
              std::string::npos);
}

TEST_F(RuntimeObjectProtocolTest, PropGetSetHappyPathRoundTrip) {
    register_factory();
    i32 id = create(0, 1);  // value starts at 0
    transport_.sent.clear();

    // Initial value is 0.
    send_prop_get(id, RuntimeDispatchObject::kProp_value, 2);
    ASSERT_EQ(transport_.sent.size(), 1u);
    EXPECT_EQ(decode_captured(transport_.sent[0]).type, wire::MsgType::result);
    EXPECT_EQ(result_i32(transport_.sent[0]), 0);

    // Set to 77; prop_set echoes the stored value.
    Buffer value;
    encode_i32(value, 77);
    send_prop_set(id, RuntimeDispatchObject::kProp_value, value, 3);
    ASSERT_EQ(transport_.sent.size(), 2u);
    EXPECT_EQ(decode_captured(transport_.sent[1]).type, wire::MsgType::result);
    EXPECT_EQ(result_i32(transport_.sent[1]), 77);

    // A subsequent get reflects the new value.
    send_prop_get(id, RuntimeDispatchObject::kProp_value, 4);
    ASSERT_EQ(transport_.sent.size(), 3u);
    EXPECT_EQ(result_i32(transport_.sent[2]), 77);
}

// =============================================================================
// capabilities(): auto-detection of the objects bit via register_factory
// =============================================================================

TEST(RuntimeCapabilityObjectsTest, ObjectsBitAbsentByDefault) {
    ServiceRuntime runtime;
    EXPECT_FALSE(wire::has_capability(
        static_cast<wire::Capability>(runtime.capabilities()),
        wire::Capability::objects));
}

TEST(RuntimeCapabilityObjectsTest, RegisterFactorySetsObjectsBit) {
    ServiceRuntime runtime;
    runtime.register_factory(kRuntimeObjType, [](u16, Buffer&) -> Object* {
        return new RuntimeDispatchObject(0);
    });
    auto caps = static_cast<wire::Capability>(runtime.capabilities());
    EXPECT_TRUE(wire::has_capability(caps, wire::Capability::objects));
    EXPECT_FALSE(wire::has_capability(caps, wire::Capability::streaming));
}

TEST(RuntimeCapabilityObjectsTest, FactoryAndStreamSetBothBits) {
    ServiceRuntime runtime;
    runtime.register_factory(kRuntimeObjType, [](u16, Buffer&) -> Object* {
        return new RuntimeDispatchObject(0);
    });
    runtime.register_stream_dispatcher(1, [](u16, Buffer&, StreamWriter&) {});
    auto caps = static_cast<wire::Capability>(runtime.capabilities());
    EXPECT_TRUE(wire::has_capability(caps, wire::Capability::objects));
    EXPECT_TRUE(wire::has_capability(caps, wire::Capability::streaming));
}

TEST(RuntimeCapabilityObjectsTest, AutoDetectOrsWithManualCapability) {
    ServiceRuntime runtime;
    runtime.register_factory(kRuntimeObjType, [](u16, Buffer&) -> Object* {
        return new RuntimeDispatchObject(0);
    });
    runtime.set_capability(wire::Capability::hmac);
    auto caps = static_cast<wire::Capability>(runtime.capabilities());
    EXPECT_TRUE(wire::has_capability(caps, wire::Capability::objects));
    EXPECT_TRUE(wire::has_capability(caps, wire::Capability::hmac));
}

TEST(RuntimeCapabilityObjectsTest, InitMessageAdvertisesObjectsBit) {
    ServiceRuntime runtime;
    runtime.register_factory(kRuntimeObjType, [](u16, Buffer&) -> Object* {
        return new RuntimeDispatchObject(0);
    });

    MockTransport transport;
    runtime.send_init_confirmation_transport(transport);
    ASSERT_EQ(transport.sent.size(), 1u);

    Buffer b;
    b.write(transport.sent[0].data(), transport.sent[0].size());
    b.reset_read();
    wire::Header hdr = wire::decode_header(b);
    EXPECT_EQ(hdr.type, wire::MsgType::init);
    wire::InitMessage init = wire::decode_init(b);
    EXPECT_TRUE(wire::has_capability(
        static_cast<wire::Capability>(init.capabilities),
        wire::Capability::objects));
}

// =============================================================================
// Stream vs regular dispatcher precedence for the same service_id
// =============================================================================

TEST_F(RuntimeDispatchTest, StreamDispatcherShadowsRegularForSameService) {
    bool regular_called = false;
    runtime_.register_dispatcher(5, [&regular_called](u16, Buffer&, Buffer&) {
        regular_called = true;
    });
    runtime_.register_stream_dispatcher(5, [](u16, Buffer&, StreamWriter& w) {
        Buffer chunk;
        encode_i32(chunk, 1);
        w.write(chunk);
    });

    Buffer payload;
    encode_u16(payload, 5);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 20, 4);
    handle(hdr, payload);

    // The stream path handled the call; the regular dispatcher never ran.
    EXPECT_FALSE(regular_called);
    bool saw_stream_end = false;
    for (const auto& m : transport_.sent) {
        if (decode_captured(m).type == wire::MsgType::stream_end) {
            saw_stream_end = true;
        }
    }
    EXPECT_TRUE(saw_stream_end);
}

TEST_F(RuntimeDispatchTest, StreamOnlyServiceIgnoredByIntrospection) {
    runtime_.register_stream_dispatcher(9, [](u16, Buffer&, StreamWriter&) {});

    // Introspection only counts regular dispatchers.
    EXPECT_FALSE(runtime_.has_service(9));
    EXPECT_EQ(runtime_.service_count(), 0u);

    // But a call still routes through the stream path.
    Buffer payload;
    encode_u16(payload, 9);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 21, 4);
    handle(hdr, payload);

    bool saw_stream_end = false;
    for (const auto& m : transport_.sent) {
        if (decode_captured(m).type == wire::MsgType::stream_end) {
            saw_stream_end = true;
        }
    }
    EXPECT_TRUE(saw_stream_end);
}

TEST_F(RuntimeDispatchTest, RegularOnlyServiceEmitsResultNotStream) {
    runtime_.register_dispatcher(5, [](u16, Buffer&, Buffer& resp) {
        encode_i32(resp, 99);
    });

    Buffer payload;
    encode_u16(payload, 5);
    encode_u16(payload, 1);
    payload.reset_read();
    auto hdr = make_header(wire::MsgType::call, 22, 4);
    handle(hdr, payload);

    ASSERT_EQ(transport_.sent.size(), 1u);
    EXPECT_EQ(decode_captured(transport_.sent[0]).type, wire::MsgType::result);
}

// =============================================================================
// run_tcp_multi concurrency cap
// =============================================================================

// The client cap must be at least 1; non-positive values are clamped.
TEST(RuntimeMultiClientTest, MaxConcurrentClientsClampedToAtLeastOne) {
    ServiceRuntime runtime;
    EXPECT_EQ(runtime.max_concurrent_clients(), 128);  // default
    runtime.set_max_concurrent_clients(4);
    EXPECT_EQ(runtime.max_concurrent_clients(), 4);
    runtime.set_max_concurrent_clients(0);
    EXPECT_EQ(runtime.max_concurrent_clients(), 1);
    runtime.set_max_concurrent_clients(-7);
    EXPECT_EQ(runtime.max_concurrent_clients(), 1);
}

// run_tcp_multi() must stop admitting once the cap is reached, and must free a
// slot when a client disconnects (proving finished workers self-reap). The
// runtime and listener are intentionally leaked so the never-returning accept
// loop can reference them for the life of the process without an exit-time
// use-after-free (the ASan CI job runs with detect_leaks=0).
TEST(RuntimeMultiClientTest, RunTcpMultiRejectsBeyondCapAndReusesFreedSlots) {
    auto* runtime = new ServiceRuntime();
    auto* listener = new TcpListener();
    runtime->set_max_concurrent_clients(2);
    listener->listen(0, 128, "127.0.0.1");
    const u16 port = listener->bound_port();

    std::thread([runtime, listener]() { runtime->run_tcp_multi(*listener); }).detach();

    // Server sends an init confirmation as the first thing client_loop() does, so
    // receiving it proves the connection was admitted (active count incremented).
    auto admitted = [port](TcpTransport& c) -> bool {
        try {
            c.connect("127.0.0.1", port, 2000);
            Buffer init;
            return c.receive(init, 2000);
        } catch (...) {
            return false;
        }
    };

    // Fill the cap of 2.
    TcpTransport c1, c2;
    ASSERT_TRUE(admitted(c1));
    ASSERT_TRUE(admitted(c2));

    // Third client is over the cap: the server accepts then immediately closes it
    // without serving, so no init arrives.
    TcpTransport c3;
    EXPECT_FALSE(admitted(c3)) << "connection beyond the cap must be dropped";

    // Disconnect one client; its worker exits and decrements the active count,
    // freeing a slot for a new client.
    c1.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    TcpTransport c4;
    EXPECT_TRUE(admitted(c4)) << "a freed slot must admit a new client";

    c2.close();
    c4.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let workers exit
}

// =============================================================================
// Object ownership scoping (IDOR)
// =============================================================================

// prop_get/prop_set/release must be scoped to the connection that created the
// object. In run_tcp_multi all connections share one object_registry_ with
// predictable ids, so without scoping one peer could read, mutate, or free
// another peer's object. This drives handle_message with two independent
// per-connection tracked-object sets against the same runtime.
TEST(RuntimeObjectOwnershipTest, PropAccessAndReleaseScopedToOwner) {
    ServiceRuntime runtime;
    runtime.register_factory(42, [](u16, Buffer&) -> Object* {
        return new OwnedTestObject();
    });

    MockTransport ta, tb;
    std::unordered_set<i32> tracked_a, tracked_b;
    ServiceRuntime::SubscriptionSet subs_a, subs_b;

    auto run = [&](MockTransport& t, std::unordered_set<i32>& tracked,
                   ServiceRuntime::SubscriptionSet& subs, Buffer msg) {
        msg.reset_read();
        wire::Header hdr = wire::decode_header(msg);
        runtime.handle_message(hdr, msg, t, tracked, subs);
    };

    // Connection A creates an object of type 42.
    Buffer empty_args;
    run(ta, tracked_a, subs_a, wire::create_object_create_message(1, 42, 0, empty_args));
    ASSERT_EQ(ta.sent.size(), 1u);
    i32 obj_id = 0;
    {
        Buffer b;
        b.write(ta.sent[0].data(), ta.sent[0].size());
        b.reset_read();
        wire::Header h = wire::decode_header(b);
        ASSERT_EQ(h.type, wire::MsgType::result);
        obj_id = wire::decode_object_ref(b).object_id;
    }
    ASSERT_EQ(tracked_a.count(obj_id), 1u);
    ta.sent.clear();

    // A sets its own object's property -> result.
    Buffer val99;
    encode_i32(val99, 99);
    run(ta, tracked_a, subs_a, wire::create_property_set_message(2, 42, obj_id, 0, val99));
    ASSERT_EQ(ta.sent.size(), 1u);
    EXPECT_EQ(decode_captured(ta.sent[0]).type, wire::MsgType::result);
    ta.sent.clear();

    // B cannot read A's object -> object_not_found.
    run(tb, tracked_b, subs_b, wire::create_property_get_message(3, 42, obj_id, 0));
    ASSERT_EQ(tb.sent.size(), 1u);
    EXPECT_EQ(decode_captured(tb.sent[0]).type, wire::MsgType::error);
    EXPECT_EQ(decode_captured(tb.sent[0]).code, ErrorCode::object_not_found);
    tb.sent.clear();

    // B cannot mutate A's object -> object_not_found.
    Buffer val7;
    encode_i32(val7, 7);
    run(tb, tracked_b, subs_b, wire::create_property_set_message(4, 42, obj_id, 0, val7));
    ASSERT_EQ(tb.sent.size(), 1u);
    EXPECT_EQ(decode_captured(tb.sent[0]).code, ErrorCode::object_not_found);
    tb.sent.clear();

    // B's release of A's object is a no-op (fire-and-forget, no reply).
    run(tb, tracked_b, subs_b, wire::create_object_release_message(42, obj_id));
    EXPECT_TRUE(tb.sent.empty());

    // A's object still exists and retains 99 -- B's tampering was fully rejected.
    run(ta, tracked_a, subs_a, wire::create_property_get_message(5, 42, obj_id, 0));
    ASSERT_EQ(ta.sent.size(), 1u);
    {
        Buffer b;
        b.write(ta.sent[0].data(), ta.sent[0].size());
        b.reset_read();
        wire::Header h = wire::decode_header(b);
        ASSERT_EQ(h.type, wire::MsgType::result);
        EXPECT_EQ(decode_i32(b), 99);
    }
}
