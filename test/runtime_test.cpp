// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/runtime.hpp>

#include <algorithm>  // std::find; not transitively included by GCC 15 libstdc++

using namespace song;

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
