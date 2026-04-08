// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>

using namespace song;
using namespace song::wire;

// =============================================================================
// Capability Enum Tests
// =============================================================================

TEST(CapabilityTest, BitwiseOr) {
    auto caps = Capability::streaming | Capability::tls;
    EXPECT_EQ(static_cast<u32>(caps), 0x05u);
}

TEST(CapabilityTest, BitwiseAnd) {
    auto caps = Capability::streaming | Capability::tls | Capability::objects;
    auto masked = caps & Capability::tls;
    EXPECT_EQ(masked, Capability::tls);
}

TEST(CapabilityTest, BitwiseNot) {
    auto all = Capability::streaming | Capability::tls;
    auto without_tls = all & ~Capability::tls;
    EXPECT_EQ(without_tls, Capability::streaming);
}

TEST(CapabilityTest, HasCapability) {
    auto caps = Capability::streaming | Capability::objects | Capability::hmac;
    EXPECT_TRUE(has_capability(caps, Capability::streaming));
    EXPECT_TRUE(has_capability(caps, Capability::objects));
    EXPECT_TRUE(has_capability(caps, Capability::hmac));
    EXPECT_FALSE(has_capability(caps, Capability::tls));
    EXPECT_FALSE(has_capability(caps, Capability::compression));
    EXPECT_FALSE(has_capability(Capability::none, Capability::streaming));
}

TEST(CapabilityTest, NegotiationIsIntersection) {
    auto client = Capability::streaming | Capability::tls | Capability::objects;
    auto server = Capability::streaming | Capability::compression | Capability::objects;
    auto negotiated = client & server;
    EXPECT_TRUE(has_capability(negotiated, Capability::streaming));
    EXPECT_TRUE(has_capability(negotiated, Capability::objects));
    EXPECT_FALSE(has_capability(negotiated, Capability::tls));
    EXPECT_FALSE(has_capability(negotiated, Capability::compression));
}

TEST(CapabilityTest, ExtensionBits) {
    auto caps = Capability::ext_0 | Capability::ext_3 | Capability::ext_7;
    EXPECT_TRUE(has_capability(caps, Capability::ext_0));
    EXPECT_TRUE(has_capability(caps, Capability::ext_3));
    EXPECT_TRUE(has_capability(caps, Capability::ext_7));
    EXPECT_FALSE(has_capability(caps, Capability::ext_1));
    EXPECT_FALSE(has_capability(caps, Capability::vendor_0));
}

TEST(CapabilityTest, VendorBits) {
    auto caps = Capability::vendor_0 | Capability::vendor_7;
    EXPECT_EQ(static_cast<u32>(caps), 0x81000000u);
}

// =============================================================================
// Version Tests
// =============================================================================

TEST(VersionTest, MakeVersion) {
    EXPECT_EQ(make_version(1, 0), 0x0100);
    EXPECT_EQ(make_version(1, 1), 0x0101);
    EXPECT_EQ(make_version(2, 0), 0x0200);
    EXPECT_EQ(make_version(2, 3), 0x0203);
}

TEST(VersionTest, CurrentIsOneOne) {
    EXPECT_EQ(kCurrentVersion, make_version(1, 1));
    EXPECT_EQ(kVersionMajor, 1);
    EXPECT_EQ(kVersionMinor, 1);
}

TEST(VersionTest, FirstIsOneZero) {
    EXPECT_EQ(kFirstVersion, make_version(1, 0));
}

TEST(VersionTest, NegotiationMinRule) {
    // v1.1 client talking to v1.0 server -> negotiated = 1.0
    u16 client = make_version(1, 1);
    u16 server = make_version(1, 0);
    EXPECT_EQ(std::min(client, server), make_version(1, 0));

    // v1.1 client talking to v1.1 server -> negotiated = 1.1
    EXPECT_EQ(std::min(client, make_version(1, 1)), make_version(1, 1));
}

// =============================================================================
// InitMessage Capability Roundtrip
// =============================================================================

TEST(VersionTest, InitMessageCapabilitiesRoundtrip) {
    u32 caps = static_cast<u32>(Capability::streaming | Capability::objects);
    Buffer msg = create_init_message(kFirstVersion, kCurrentVersion, caps);

    msg.reset_read();
    auto hdr = decode_header(msg);
    EXPECT_EQ(hdr.type, MsgType::init);

    auto init = decode_init(msg);
    EXPECT_EQ(init.capabilities, caps);
    EXPECT_EQ(init.first_version, kFirstVersion);
    EXPECT_EQ(init.current_version, kCurrentVersion);
}

TEST(VersionTest, InitAckRoundtrip) {
    u32 caps = static_cast<u32>(Capability::tls | Capability::hmac);
    Buffer msg = create_init_ack_message(kFirstVersion, kCurrentVersion, caps);

    msg.reset_read();
    auto hdr = decode_header(msg);
    EXPECT_EQ(hdr.type, MsgType::init_ack);

    auto init = decode_init(msg);
    EXPECT_EQ(init.capabilities, caps);
}

TEST(VersionTest, ZeroCapabilitiesBackcompat) {
    // v1.0 peer sends capabilities = 0
    Buffer msg = create_init_message(make_version(1, 0), make_version(1, 0), 0);
    msg.reset_read();
    decode_header(msg);
    auto init = decode_init(msg);
    EXPECT_EQ(init.capabilities, 0u);
}

// =============================================================================
// ServiceRuntime Capability Tests
// =============================================================================

TEST(RuntimeCapabilityTest, AutoDetectStreaming) {
    ServiceRuntime runtime;
    // No dispatchers registered -- no capabilities
    EXPECT_EQ(runtime.capabilities(), 0u);

    // Register a stream dispatcher -- should auto-detect streaming
    runtime.register_stream_dispatcher(1, [](u16, Buffer&, StreamWriter&) {});
    EXPECT_TRUE(has_capability(
        static_cast<Capability>(runtime.capabilities()), Capability::streaming));
}

TEST(RuntimeCapabilityTest, ManualSetAndClear) {
    ServiceRuntime runtime;
    runtime.set_capability(Capability::compression);
    runtime.set_capability(Capability::hmac);

    EXPECT_TRUE(has_capability(
        static_cast<Capability>(runtime.capabilities()), Capability::compression));
    EXPECT_TRUE(has_capability(
        static_cast<Capability>(runtime.capabilities()), Capability::hmac));

    runtime.clear_capability(Capability::compression);
    EXPECT_FALSE(has_capability(
        static_cast<Capability>(runtime.capabilities()), Capability::compression));
    EXPECT_TRUE(has_capability(
        static_cast<Capability>(runtime.capabilities()), Capability::hmac));
}

TEST(RuntimeCapabilityTest, RegisterExtension) {
    ServiceRuntime runtime;

    auto bit = runtime.register_extension("hot-fix-1");
    EXPECT_EQ(bit, Capability::ext_0);
    EXPECT_TRUE(runtime.has_extension("hot-fix-1"));
    EXPECT_TRUE(has_capability(
        static_cast<Capability>(runtime.capabilities()), Capability::ext_0));

    auto bit2 = runtime.register_extension("hot-fix-2");
    EXPECT_EQ(bit2, Capability::ext_1);
    EXPECT_TRUE(runtime.has_extension("hot-fix-2"));
}

TEST(RuntimeCapabilityTest, RegisterExtensionDuplicate) {
    ServiceRuntime runtime;

    auto bit1 = runtime.register_extension("feature-x");
    auto bit2 = runtime.register_extension("feature-x");  // Same name
    EXPECT_EQ(bit1, bit2);  // Should return same bit
}

TEST(RuntimeCapabilityTest, RegisterExtensionOverflow) {
    ServiceRuntime runtime;
    for (int ndx = 0; ndx < 8; ++ndx) {
        runtime.register_extension("ext-" + std::to_string(ndx));
    }
    // 9th should throw
    EXPECT_THROW(runtime.register_extension("ext-8"), std::runtime_error);
}
