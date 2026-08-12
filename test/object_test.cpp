// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/object.hpp>
#include <song/wire.hpp>
#include <song/buffer.hpp>

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

    // Object method frames carry their own MsgType so the server routes them
    // to Object::dispatch -- NOT MsgType::call, which decodes as a service
    // call header and mis-dispatches (the bug this type distinction fixes).
    EXPECT_EQ(hdr.type, wire::MsgType::object_call);
    EXPECT_EQ(hdr.sequence_id, 300u);

    auto obj_hdr = wire::decode_object_method_header(msg);
    EXPECT_EQ(obj_hdr.type_id, 5u);
    EXPECT_EQ(obj_hdr.object_id, -10);
    EXPECT_EQ(obj_hdr.method_id, 7u);
    EXPECT_EQ(decode_string(msg), "test");
}

} // namespace song::test
