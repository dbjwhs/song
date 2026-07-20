// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/object.hpp>
#include <song/wire.hpp>
#include <song/buffer.hpp>
#include <song/subscription.hpp>
#include <song/transport.hpp>

namespace song::test {

// =============================================================================
// Test Object Implementation
// =============================================================================

class TestObject : public Object {
    i32 value_ = 0;
    std::string name_ = "test";

public:
    explicit TestObject(i32 initial_value = 0) : value_(initial_value) {}
    TestObject(i32 value, const std::string& name) : value_(value), name_(name) {}

    i32 value() const { return value_; }
    void set_value(i32 v) { value_ = v; }

    const std::string& name() const { return name_; }
    void set_name(const std::string& n) { name_ = n; }

    // Property IDs
    static constexpr u16 kProp_value = 1;
    static constexpr u16 kProp_name = 2;

    // Method IDs
    static constexpr u16 kMethod_increment = 1;
    static constexpr u16 kMethod_add = 2;

    void prop_get(u16 prop_id, Buffer& resp) override {
        switch (prop_id) {
            case kProp_value:
                encode_i32(resp, value_);
                break;
            case kProp_name:
                encode_string(resp, name_);
                break;
            default:
                throw ServiceError("Unknown property ID");
        }
    }

    void prop_set(u16 prop_id, Buffer& req, Buffer& resp) override {
        switch (prop_id) {
            case kProp_value:
                value_ = decode_i32(req);
                encode_i32(resp, value_);
                break;
            case kProp_name:
                name_ = decode_string(req);
                encode_string(resp, name_);
                break;
            default:
                throw ServiceError("Unknown property ID");
        }
    }

    void dispatch(u16 method_id, Buffer& req, Buffer& resp) override {
        switch (method_id) {
            case kMethod_increment: {
                ++value_;
                encode_i32(resp, value_);
                break;
            }
            case kMethod_add: {
                i32 delta = decode_i32(req);
                value_ += delta;
                encode_i32(resp, value_);
                break;
            }
            default:
                throw ServiceError("Unknown method ID");
        }
    }
};

// =============================================================================
// Object Registry Tests
// =============================================================================

class ObjectRegistryTest : public ::testing::Test {
protected:
    ObjectRegistry registry;
};

TEST_F(ObjectRegistryTest, RegisterObject) {
    auto* obj = new TestObject(42);
    i32 id = registry.register_object(obj);

    // IDs should be negative
    EXPECT_LT(id, 0);

    // Should be able to look it up
    EXPECT_EQ(registry.get(id), obj);
    EXPECT_TRUE(registry.contains(id));
    EXPECT_EQ(registry.size(), 1);
}

TEST_F(ObjectRegistryTest, MultipleObjects) {
    auto* obj1 = new TestObject(1);
    auto* obj2 = new TestObject(2);
    auto* obj3 = new TestObject(3);

    i32 id1 = registry.register_object(obj1);
    i32 id2 = registry.register_object(obj2);
    i32 id3 = registry.register_object(obj3);

    // All IDs should be unique and negative
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
    EXPECT_LT(id1, 0);
    EXPECT_LT(id2, 0);
    EXPECT_LT(id3, 0);

    // Should be able to look up each
    EXPECT_EQ(static_cast<TestObject*>(registry.get(id1))->value(), 1);
    EXPECT_EQ(static_cast<TestObject*>(registry.get(id2))->value(), 2);
    EXPECT_EQ(static_cast<TestObject*>(registry.get(id3))->value(), 3);

    EXPECT_EQ(registry.size(), 3);
}

TEST_F(ObjectRegistryTest, GetNonexistent) {
    EXPECT_EQ(registry.get(-999), nullptr);
    EXPECT_FALSE(registry.contains(-999));
}

TEST_F(ObjectRegistryTest, ReleaseObject) {
    auto* obj = new TestObject(42);
    i32 id = registry.register_object(obj);

    EXPECT_EQ(registry.size(), 1);
    registry.release(id);
    EXPECT_EQ(registry.size(), 0);
    EXPECT_EQ(registry.get(id), nullptr);
}

TEST_F(ObjectRegistryTest, AddRef) {
    auto* obj = new TestObject(42);
    i32 id = registry.register_object(obj);

    EXPECT_EQ(obj->ref_count(), 1);

    EXPECT_TRUE(registry.add_ref(id));
    EXPECT_EQ(obj->ref_count(), 2);

    // First release decrements but doesn't delete
    registry.release(id);
    EXPECT_EQ(obj->ref_count(), 1);
    EXPECT_TRUE(registry.contains(id));

    // Second release deletes
    registry.release(id);
    EXPECT_FALSE(registry.contains(id));
}

TEST_F(ObjectRegistryTest, ReleaseNonexistent) {
    // Should not crash
    registry.release(-999);
}

TEST_F(ObjectRegistryTest, Clear) {
    registry.register_object(new TestObject(1));
    registry.register_object(new TestObject(2));
    registry.register_object(new TestObject(3));

    EXPECT_EQ(registry.size(), 3);
    registry.clear();
    EXPECT_EQ(registry.size(), 0);
}

TEST_F(ObjectRegistryTest, RegisterFactory) {
    // Register factory for type_id = 1
    registry.register_factory(1, [](u16 constructor_id, Buffer& args) -> Object* {
        if (constructor_id == 0) {
            return new TestObject(0);
        } else if (constructor_id == 1) {
            i32 value = decode_i32(args);
            return new TestObject(value);
        }
        return nullptr;
    });

    // Create with default constructor
    Buffer empty;
    i32 id1 = registry.create_object(1, 0, empty);
    EXPECT_LT(id1, 0);
    EXPECT_EQ(static_cast<TestObject*>(registry.get(id1))->value(), 0);

    // Create with value constructor
    Buffer args;
    encode_i32(args, 42);
    args.reset_read();
    i32 id2 = registry.create_object(1, 1, args);
    EXPECT_LT(id2, 0);
    EXPECT_EQ(static_cast<TestObject*>(registry.get(id2))->value(), 42);
}

TEST_F(ObjectRegistryTest, CreateUnknownType) {
    Buffer empty;
    EXPECT_THROW(registry.create_object(999, 0, empty), ServiceError);
}

// A factory that returns null must raise ServiceError WITHOUT mutating registry
// state: the null check runs before next_id_-- and the objects_ insert, so a
// failed create must not consume an object id or leave a partial entry.
TEST_F(ObjectRegistryTest, CreateFactoryReturnsNull) {
    registry.register_factory(1, [](u16 constructor_id, Buffer& args) -> Object* {
        (void)args;
        if (constructor_id == 9) {
            return nullptr;
        }
        return new TestObject(0);
    });

    Buffer empty;
    i32 id1 = registry.create_object(1, 0, empty);
    EXPECT_EQ(id1, -1);
    EXPECT_EQ(registry.size(), 1u);

    try {
        registry.create_object(1, 9, empty);
        FAIL() << "expected ServiceError when the factory returns null";
    } catch (const ServiceError& e) {
        const std::string what = e.what();
        // Distinguish the factory-null message from the unknown-type message,
        // and confirm it names the type_id.
        EXPECT_NE(what.find("null"), std::string::npos);
        EXPECT_NE(what.find("1"), std::string::npos);
    }
    EXPECT_EQ(registry.size(), 1u);  // nothing inserted by the failed create

    // The next valid create gets id -2, proving next_id_ was not decremented by
    // the failed create.
    i32 id2 = registry.create_object(1, 0, empty);
    EXPECT_EQ(id2, -2);
    EXPECT_EQ(registry.size(), 2u);
}

// =============================================================================
// Object Tests
// =============================================================================

TEST(ObjectTest, PropertyGet) {
    TestObject obj(42);
    Buffer resp;

    obj.prop_get(TestObject::kProp_value, resp);
    resp.reset_read();
    EXPECT_EQ(decode_i32(resp), 42);
}

TEST(ObjectTest, PropertySet) {
    TestObject obj(0);
    Buffer req;
    encode_i32(req, 100);
    req.reset_read();

    Buffer resp;
    obj.prop_set(TestObject::kProp_value, req, resp);
    EXPECT_EQ(obj.value(), 100);

    resp.reset_read();
    EXPECT_EQ(decode_i32(resp), 100);
}

TEST(ObjectTest, PropertyGetString) {
    TestObject obj(0);
    obj.set_name("hello");

    Buffer resp;
    obj.prop_get(TestObject::kProp_name, resp);
    resp.reset_read();
    EXPECT_EQ(decode_string(resp), "hello");
}

TEST(ObjectTest, PropertySetString) {
    TestObject obj(0);

    Buffer req;
    encode_string(req, "world");
    req.reset_read();

    Buffer resp;
    obj.prop_set(TestObject::kProp_name, req, resp);
    EXPECT_EQ(obj.name(), "world");
}

TEST(ObjectTest, MethodDispatch) {
    TestObject obj(10);

    // Test increment
    Buffer empty_req;
    Buffer resp1;
    obj.dispatch(TestObject::kMethod_increment, empty_req, resp1);
    resp1.reset_read();
    EXPECT_EQ(decode_i32(resp1), 11);
    EXPECT_EQ(obj.value(), 11);

    // Test add
    Buffer add_req;
    encode_i32(add_req, 5);
    add_req.reset_read();

    Buffer resp2;
    obj.dispatch(TestObject::kMethod_add, add_req, resp2);
    resp2.reset_read();
    EXPECT_EQ(decode_i32(resp2), 16);
    EXPECT_EQ(obj.value(), 16);
}

TEST(ObjectTest, UnknownProperty) {
    TestObject obj(0);
    Buffer resp;
    EXPECT_THROW(obj.prop_get(999, resp), ServiceError);
}

TEST(ObjectTest, UnknownMethod) {
    TestObject obj(0);
    Buffer req, resp;
    EXPECT_THROW(obj.dispatch(999, req, resp), ServiceError);
}

// =============================================================================
// Wire Protocol Tests
// =============================================================================

TEST(ObjectWireTest, ObjectRefRoundTrip) {
    Buffer buf;
    wire::ObjectRef ref{42, -123};
    wire::encode_object_ref(buf, ref);

    buf.reset_read();
    wire::ObjectRef decoded = wire::decode_object_ref(buf);

    EXPECT_EQ(decoded.type_id, 42u);
    EXPECT_EQ(decoded.object_id, -123);
}

TEST(ObjectWireTest, NullObjectRef) {
    Buffer buf;
    wire::ObjectRef null_ref{1, 0};  // object_id = 0 means null
    wire::encode_object_ref(buf, null_ref);

    buf.reset_read();
    wire::ObjectRef decoded = wire::decode_object_ref(buf);

    EXPECT_EQ(decoded.object_id, 0);
}

TEST(ObjectWireTest, ObjectCreateHeader) {
    Buffer buf;
    wire::encode_object_create(buf, 100, 2, Buffer{});

    buf.reset_read();
    auto hdr = wire::decode_object_create_header(buf);

    EXPECT_EQ(hdr.type_id, 100u);
    EXPECT_EQ(hdr.constructor_id, 2);
}

TEST(ObjectWireTest, ObjectReleaseHeader) {
    Buffer buf;
    wire::encode_object_release(buf, 50, -42);

    buf.reset_read();
    auto hdr = wire::decode_object_release_header(buf);

    EXPECT_EQ(hdr.type_id, 50u);
    EXPECT_EQ(hdr.object_id, -42);
}

TEST(ObjectWireTest, PropertyGetHeader) {
    Buffer buf;
    wire::encode_property_get(buf, 10, -5, 3);

    buf.reset_read();
    auto hdr = wire::decode_property_header(buf);

    EXPECT_EQ(hdr.type_id, 10u);
    EXPECT_EQ(hdr.object_id, -5);
    EXPECT_EQ(hdr.property_id, 3);
}

TEST(ObjectWireTest, PropertySetHeader) {
    Buffer value;
    encode_i32(value, 999);

    Buffer buf;
    wire::encode_property_set(buf, 10, -5, 3, value);

    buf.reset_read();
    auto hdr = wire::decode_property_header(buf);

    EXPECT_EQ(hdr.type_id, 10u);
    EXPECT_EQ(hdr.object_id, -5);
    EXPECT_EQ(hdr.property_id, 3);

    // Value follows header
    EXPECT_EQ(decode_i32(buf), 999);
}

TEST(ObjectWireTest, ObjectMethodHeader) {
    Buffer args;
    encode_i32(args, 42);

    Buffer buf;
    wire::encode_object_method(buf, 20, -10, 5, args);

    buf.reset_read();
    auto hdr = wire::decode_object_method_header(buf);

    EXPECT_EQ(hdr.type_id, 20u);
    EXPECT_EQ(hdr.object_id, -10);
    EXPECT_EQ(hdr.method_id, 5);

    // Args follow header
    EXPECT_EQ(decode_i32(buf), 42);
}

// =============================================================================
// Message Creation Tests
// =============================================================================

TEST(ObjectMessageTest, CreateMessage) {
    Buffer args;
    encode_i32(args, 100);

    Buffer msg = wire::create_object_create_message(42, 1, 0, args);

    msg.reset_read();
    auto hdr = wire::decode_header_validated(msg);

    EXPECT_EQ(hdr.type, wire::MsgType::create);
    EXPECT_EQ(hdr.sequence_id, 42u);
}

TEST(ObjectMessageTest, ReleaseMessage) {
    Buffer msg = wire::create_object_release_message(10, -5);

    msg.reset_read();
    auto hdr = wire::decode_header_validated(msg);

    EXPECT_EQ(hdr.type, wire::MsgType::release);
    EXPECT_EQ(hdr.sequence_id, 0u);  // Fire-and-forget, no sequence
}

TEST(ObjectMessageTest, PropertyGetMessage) {
    Buffer msg = wire::create_property_get_message(100, 5, -10, 3);

    msg.reset_read();
    auto hdr = wire::decode_header_validated(msg);

    EXPECT_EQ(hdr.type, wire::MsgType::prop_get);
    EXPECT_EQ(hdr.sequence_id, 100u);
}

TEST(ObjectMessageTest, PropertySetMessage) {
    Buffer value;
    encode_i32(value, 42);

    Buffer msg = wire::create_property_set_message(200, 5, -10, 3, value);

    msg.reset_read();
    auto hdr = wire::decode_header_validated(msg);

    EXPECT_EQ(hdr.type, wire::MsgType::prop_set);
    EXPECT_EQ(hdr.sequence_id, 200u);
}

TEST(ObjectMessageTest, ObjectMethodMessage) {
    Buffer args;
    encode_string(args, "test");

    Buffer msg = wire::create_object_method_message(300, 5, -10, 7, args);

    msg.reset_read();
    auto hdr = wire::decode_header_validated(msg);

    EXPECT_EQ(hdr.type, wire::MsgType::call);  // Object methods use MSG_CALL type
    EXPECT_EQ(hdr.sequence_id, 300u);
}

} // namespace song::test

// =============================================================================
// Coverage additions (appended)
// =============================================================================

namespace song::test {

namespace {

// Object subclass that bumps a caller-supplied counter in its destructor, used
// to prove clear()/~ObjectRegistry delete each object exactly once even when the
// reference count is greater than 1.
class CountingObject : public Object {
    int* destroyed_;

public:
    explicit CountingObject(int* destroyed) : destroyed_(destroyed) {}
    ~CountingObject() override { ++(*destroyed_); }

    void prop_get(u16, Buffer&) override {}
    void prop_set(u16, Buffer&, Buffer&) override {}
    void dispatch(u16, Buffer&, Buffer&) override {}
};

// Transport that records the most recent message sent to it, used to observe the
// fan-out that Object::notify_property drives through a SubscriptionRegistry.
class CapturingTransport : public Transport {
public:
    int send_count = 0;
    Buffer last;

    void send(const Buffer& msg) override {
        last = Buffer{};
        last.write(msg.data(), msg.size());
        ++send_count;
    }
    bool receive(Buffer&, int) override { return false; }
    void close() override {}
    bool is_connected() const override { return true; }
    const char* type_name() const override { return "capturing"; }
};

} // namespace

// -- type_id() is threaded through create_object and readable on the instance ---
TEST_F(ObjectRegistryTest, TypeIdSetByCreateObject) {
    registry.register_factory(7, [](u16, Buffer&) -> Object* {
        return new TestObject(0);
    });

    Buffer empty;
    i32 id = registry.create_object(7, 0, empty);
    Object* obj = registry.get(id);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->type_id(), 7u);
    EXPECT_FALSE(obj->is_null());
}

// -- register_object leaves type_id at its default 0 (documents the contract) ---
TEST_F(ObjectRegistryTest, TypeIdZeroAfterRegisterObject) {
    i32 id = registry.register_object(new TestObject(0));
    Object* obj = registry.get(id);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->type_id(), 0u);
}

// -- The stored type_id is what fans out through notify_property ---------------
TEST_F(ObjectRegistryTest, TypeIdFansOutThroughSubscription) {
    registry.register_factory(7, [](u16, Buffer&) -> Object* {
        return new TestObject(0);
    });

    Buffer empty;
    i32 id = registry.create_object(7, 0, empty);
    Object* obj = registry.get(id);
    ASSERT_NE(obj, nullptr);

    SubscriptionRegistry subs;
    CapturingTransport sink;
    subs.subscribe(1, id, TestObject::kProp_value, &sink);
    obj->set_subscription_registry(&subs);

    Buffer val;
    encode_i32(val, 99);
    obj->notify_property(TestObject::kProp_value, val);

    ASSERT_EQ(sink.send_count, 1);
    sink.last.reset_read();
    auto hdr = wire::decode_header(sink.last);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_notify);

    auto prop = wire::decode_property_header(sink.last);
    EXPECT_EQ(prop.type_id, 7u);  // the type_id assigned at create time
    EXPECT_EQ(prop.object_id, id);
    EXPECT_EQ(prop.property_id, TestObject::kProp_value);
}

// -- notify_property prefers the subscription registry and skips the callback ---
TEST(ObjectNotifyPrecedenceTest, SubRegistryTakesPrecedenceOverCallback) {
    TestObject obj(0);
    SubscriptionRegistry subs;  // empty: no subscribers registered
    int callback_calls = 0;

    obj.set_notify_callback([&](u32, i32, u16, const Buffer&) {
        ++callback_calls;
    });
    obj.set_subscription_registry(&subs);

    Buffer val;
    encode_i32(val, 5);
    obj.notify_property(TestObject::kProp_value, val);

    // The sub_registry branch early-returns before the callback branch runs.
    EXPECT_EQ(callback_calls, 0);
}

// -- With no subscription registry, the single-client callback fires -----------
TEST(ObjectNotifyPrecedenceTest, CallbackFiresWhenNoSubRegistry) {
    TestObject obj(0);
    int callback_calls = 0;
    u16 seen_prop = 0;

    obj.set_notify_callback([&](u32, i32, u16 prop_id, const Buffer&) {
        ++callback_calls;
        seen_prop = prop_id;
    });

    Buffer val;
    encode_i32(val, 5);
    obj.notify_property(TestObject::kProp_value, val);

    EXPECT_EQ(callback_calls, 1);
    EXPECT_EQ(seen_prop, TestObject::kProp_value);
}

// -- register_object(nullptr) throws and consumes no id ------------------------
TEST_F(ObjectRegistryTest, RegisterNullThrows) {
    EXPECT_THROW(registry.register_object(nullptr), ServiceError);
    EXPECT_EQ(registry.size(), 0u);

    // next_id_ was not consumed by the failed register: first valid id is -1.
    i32 id = registry.register_object(new TestObject(1));
    EXPECT_EQ(id, -1);
}

// -- add_ref on a missing id returns false and touches nothing -----------------
TEST_F(ObjectRegistryTest, AddRefNonexistentReturnsFalse) {
    EXPECT_FALSE(registry.add_ref(-999));

    auto* obj = new TestObject(5);
    registry.register_object(obj);
    EXPECT_EQ(obj->ref_count(), 1);

    EXPECT_FALSE(registry.add_ref(-12345));  // a different, still-missing id
    EXPECT_EQ(obj->ref_count(), 1);          // the real object is unaffected
}

// -- add_ref on an id whose object was released returns false (no resurrect) ----
TEST_F(ObjectRegistryTest, AddRefAfterReleaseReturnsFalse) {
    auto* obj = new TestObject(5);
    i32 id = registry.register_object(obj);

    registry.release(id);  // ref_count 1 -> deleted and erased
    EXPECT_FALSE(registry.contains(id));
    EXPECT_FALSE(registry.add_ref(id));
}

// -- Released ids are not recycled: next allocation keeps decrementing ----------
TEST_F(ObjectRegistryTest, IdsNotReusedAfterRelease) {
    i32 id_a = registry.register_object(new TestObject(1));
    EXPECT_EQ(id_a, -1);

    registry.release(id_a);
    EXPECT_EQ(registry.get(-1), nullptr);

    auto* b = new TestObject(2);
    i32 id_b = registry.register_object(b);
    EXPECT_EQ(id_b, -2);  // -1 is not handed back out
    EXPECT_EQ(registry.get(-1), nullptr);
    EXPECT_EQ(registry.get(-2), b);
}

// -- Interleaved create/release still yields strictly decreasing ids -----------
TEST_F(ObjectRegistryTest, IdsMonotonicWithInterleavedReleases) {
    i32 id_a = registry.register_object(new TestObject(1));
    auto* b = new TestObject(2);
    i32 id_b = registry.register_object(b);
    EXPECT_EQ(id_a, -1);
    EXPECT_EQ(id_b, -2);

    registry.release(id_a);

    auto* c = new TestObject(3);
    i32 id_c = registry.register_object(c);
    EXPECT_EQ(id_c, -3);  // not the freed -1

    EXPECT_EQ(registry.get(-1), nullptr);
    EXPECT_EQ(registry.get(-2), b);
    EXPECT_EQ(registry.get(-3), c);
}

// -- Pre-registration identity vs post-registration identity -------------------
TEST_F(ObjectRegistryTest, PreRegistrationIdentity) {
    auto* obj = new TestObject(0);
    EXPECT_EQ(obj->object_id(), 0);
    EXPECT_TRUE(obj->is_null());
    EXPECT_EQ(obj->registry(), nullptr);

    i32 id = registry.register_object(obj);
    EXPECT_LT(obj->object_id(), 0);
    EXPECT_EQ(obj->object_id(), id);
    EXPECT_FALSE(obj->is_null());
    EXPECT_EQ(obj->registry(), &registry);
}

// -- has_factories() reflects whether any factory is registered ----------------
TEST_F(ObjectRegistryTest, HasFactoriesReflectsRegistration) {
    EXPECT_FALSE(registry.has_factories());
    registry.register_factory(1, [](u16, Buffer&) -> Object* {
        return new TestObject(0);
    });
    EXPECT_TRUE(registry.has_factories());
}

// -- register_factory for an existing type_id overwrites (does not ignore) -----
TEST_F(ObjectRegistryTest, RegisterFactoryOverwrites) {
    registry.register_factory(1, [](u16, Buffer&) -> Object* {
        return new TestObject(111);
    });
    registry.register_factory(1, [](u16, Buffer&) -> Object* {
        return new TestObject(222);
    });

    Buffer empty;
    i32 id = registry.create_object(1, 0, empty);
    ASSERT_NE(registry.get(id), nullptr);
    EXPECT_EQ(static_cast<TestObject*>(registry.get(id))->value(), 222);
}

// -- clear() force-deletes objects even with ref_count > 1, exactly once -------
TEST(ObjectRegistryLifetimeTest, ClearDeletesObjectsWithOutstandingRefs) {
    ObjectRegistry registry;
    int destroyed = 0;
    i32 id = registry.register_object(new CountingObject(&destroyed));

    ASSERT_TRUE(registry.add_ref(id));
    ASSERT_TRUE(registry.add_ref(id));
    EXPECT_EQ(registry.get(id)->ref_count(), 3);

    registry.clear();
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_EQ(destroyed, 1);  // deleted once despite ref_count 3, no double-free
}

// -- ~ObjectRegistry force-deletes every outstanding object exactly once --------
TEST(ObjectRegistryLifetimeTest, DestructorDeletesOutstandingRefs) {
    int destroyed = 0;
    {
        ObjectRegistry registry;
        i32 a = registry.register_object(new CountingObject(&destroyed));
        i32 b = registry.register_object(new CountingObject(&destroyed));
        ASSERT_TRUE(registry.add_ref(a));  // a: ref_count 2
        ASSERT_TRUE(registry.add_ref(b));  // b: ref_count 2
    }  // registry destructs -> clear()
    EXPECT_EQ(destroyed, 2);
}

} // namespace song::test
