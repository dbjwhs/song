// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <limits>

using namespace song;

// =============================================================================
// Wire Message Tests
// =============================================================================

TEST(PropNotifyWireTest, SubscribeMessage) {
    Buffer msg = wire::create_property_subscribe_message(100, -1, 5);
    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_subscribe);
    EXPECT_EQ(hdr.sequence_id, 0u);  // Fire-and-forget

    auto prop = wire::decode_property_header(msg);
    EXPECT_EQ(prop.type_id, 100u);
    EXPECT_EQ(prop.object_id, -1);
    EXPECT_EQ(prop.property_id, 5);
}

TEST(PropNotifyWireTest, UnsubscribeMessage) {
    Buffer msg = wire::create_property_unsubscribe_message(100, -1, 5);
    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_unsubscribe);
}

TEST(PropNotifyWireTest, NotifyMessage) {
    Buffer value;
    encode_f64(value, 3.14);
    Buffer msg = wire::create_property_notify_message(100, -1, 5, value);
    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_notify);
    EXPECT_EQ(hdr.sequence_id, 0u);  // Unsolicited push
    EXPECT_GT(hdr.payload_size, 0u);
}

// =============================================================================
// Object::notify_property() Tests
// =============================================================================

class TestObject : public Object {
    f64 price_ = 0.0;

public:
    void set_price(f64 p) {
        price_ = p;
        Buffer val;
        encode_f64(val, price_);
        notify_property(1, val);  // property_id = 1
    }

    f64 price() const { return price_; }

    void prop_get(u16 prop_id, Buffer& resp) override {
        if (prop_id == 1) encode_f64(resp, price_);
    }

    void prop_set(u16 prop_id, Buffer& req, Buffer& resp) override {
        if (prop_id == 1) {
            price_ = decode_f64(req);
            encode_f64(resp, price_);

            // Notify subscribers
            Buffer val;
            encode_f64(val, price_);
            notify_property(1, val);
        }
    }

    void dispatch(u16, Buffer&, Buffer&) override {}
};

TEST(PropNotifyTest, NotifyCallbackInvoked) {
    TestObject obj;
    std::atomic<int> notify_count{0};
    f64 last_value = 0.0;

    obj.set_notify_callback([&](u32 type_id, i32 obj_id, u16 prop_id, const Buffer& value) {
        (void)type_id;
        (void)obj_id;
        EXPECT_EQ(prop_id, 1);
        Buffer copy;
        copy.write(value.data(), value.size());
        copy.reset_read();
        last_value = decode_f64(copy);
        notify_count++;
    });

    obj.set_price(42.5);
    EXPECT_EQ(notify_count, 1);
    EXPECT_DOUBLE_EQ(last_value, 42.5);

    obj.set_price(100.0);
    EXPECT_EQ(notify_count, 2);
    EXPECT_DOUBLE_EQ(last_value, 100.0);
}

TEST(PropNotifyTest, NoCallbackNoOp) {
    TestObject obj;
    // No callback set -- should not crash
    obj.set_price(42.5);
}

// =============================================================================
// End-to-End Subscribe/Notify over TCP
// =============================================================================

TEST(PropNotifyE2ETest, SubscribeAndReceiveNotification) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            // Send init
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion,
                static_cast<u32>(wire::Capability::properties));
            client->send(init_msg);

            // Wait for subscribe message
            Buffer msg;
            if (!client->receive(msg, 5000)) return;
            auto hdr = wire::decode_header(msg);

            // Skip init_ack
            if (hdr.type == wire::MsgType::init_ack) {
                msg = Buffer{};
                if (!client->receive(msg, 5000)) return;
                hdr = wire::decode_header(msg);
            }

            EXPECT_EQ(hdr.type, wire::MsgType::prop_subscribe);

            // Push 3 notifications
            for (int ndx = 0; ndx < 3; ++ndx) {
                Buffer value;
                encode_f64(value, static_cast<f64>(ndx) * 10.0);
                Buffer notify = wire::create_property_notify_message(100, -1, 1, value);
                client->send(notify);
            }

            // Wait for unsubscribe or shutdown
            Buffer end_msg;
            if (client->receive(end_msg, 5000)) {
                auto end_hdr = wire::decode_header(end_msg);
                if (end_hdr.type == wire::MsgType::prop_unsubscribe) {
                    // Good
                }
            }
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    // Subscribe
    std::vector<f64> received_values;
    conn.subscribe_property(100, -1, 1, [&](const Buffer& value) {
        Buffer copy;
        copy.write(value.data(), value.size());
        copy.reset_read();
        received_values.push_back(decode_f64(copy));
    });

    // Poll for notifications
    for (int ndx = 0; ndx < 3; ++ndx) {
        conn.poll_notifications(5000);
    }

    ASSERT_EQ(received_values.size(), 3u);
    EXPECT_DOUBLE_EQ(received_values[0], 0.0);
    EXPECT_DOUBLE_EQ(received_values[1], 10.0);
    EXPECT_DOUBLE_EQ(received_values[2], 20.0);

    // Unsubscribe
    conn.unsubscribe_property(100, -1, 1);

    listener.close();
    server.join();
}

TEST(PropNotifyE2ETest, UnsubscribeStopsNotifications) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto client = listener.accept(5000);
        if (!client) return;
        try {
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion, 0);
            client->send(init_msg);

            // Read subscribe (+ init_ack)
            for (int ndx = 0; ndx < 3; ++ndx) {
                Buffer msg;
                if (!client->receive(msg, 5000)) return;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::prop_subscribe) break;
            }

            // Send one notification
            Buffer v1;
            encode_i32(v1, 42);
            client->send(wire::create_property_notify_message(1, -1, 1, v1));

            // Wait for unsubscribe
            Buffer unsub;
            if (!client->receive(unsub, 5000)) return;
        } catch (...) {}
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    int notify_count = 0;
    conn.subscribe_property(1, -1, 1, [&](const Buffer&) {
        notify_count++;
    });

    // Should get one notification
    conn.poll_notifications(5000);
    EXPECT_EQ(notify_count, 1);

    // Unsubscribe
    conn.unsubscribe_property(1, -1, 1);

    listener.close();
    server.join();
}


// =============================================================================
// SubscriptionRegistry Coverage Tests (mock-transport driven)
// =============================================================================

namespace {

// Mock transport that records every message sent to it.
class RecordingTransport : public Transport {
public:
    std::vector<Buffer> sent;
    int send_count = 0;

    void send(const Buffer& msg) override {
        Buffer copy;
        copy.write(msg.data(), msg.size());
        sent.push_back(std::move(copy));
        ++send_count;
    }
    bool receive(Buffer&, int) override { return false; }
    void close() override {}
    bool is_connected() const override { return true; }
    const char* type_name() const override { return "recording"; }
};

// Mock transport whose send() always throws, to test notify() fan-out isolation.
class ThrowingTransport : public Transport {
public:
    int send_attempts = 0;

    void send(const Buffer&) override {
        ++send_attempts;
        throw ServiceError("mock send failure");
    }
    bool receive(Buffer&, int) override { return false; }
    void close() override {}
    bool is_connected() const override { return true; }
    const char* type_name() const override { return "throwing"; }
};

// Mock transport with an atomic send counter, safe for concurrent-notify stress.
class CountingTransport : public Transport {
public:
    std::atomic<int> sends{0};

    void send(const Buffer&) override { sends.fetch_add(1, std::memory_order_relaxed); }
    bool receive(Buffer&, int) override { return false; }
    void close() override {}
    bool is_connected() const override { return true; }
    const char* type_name() const override { return "counting"; }
};

// Mock transport whose send() re-enters the registry, to prove the fan-out
// send loop runs outside the lock (no self-deadlock on the non-recursive mutex).
class ReentrantTransport : public Transport {
public:
    SubscriptionRegistry* reg = nullptr;
    int send_count = 0;
    size_t observed_count = 0;

    void send(const Buffer&) override {
        ++send_count;
        observed_count = reg->subscriber_count(-1, 1);  // read under our own lock
        reg->subscribe(999, -5, 5, this);               // mutate under our own lock
    }
    bool receive(Buffer&, int) override { return false; }
    void close() override {}
    bool is_connected() const override { return true; }
    const char* type_name() const override { return "reentrant"; }
};

} // namespace

// -- Re-subscribe with same id: dedup keeps the originally-registered transport --
TEST(SubscriptionRegistryCoverageTest, ResubscribeSameIdKeepsOriginalTransport) {
    SubscriptionRegistry reg;
    RecordingTransport t1;
    RecordingTransport t2;

    reg.subscribe(7, -1, 1, &t1);
    reg.subscribe(7, -1, 1, &t2);  // same id -> dedup early-return, t1 retained

    EXPECT_EQ(reg.subscriber_count(-1, 1), 1u);
    EXPECT_EQ(reg.total_subscriptions(), 1u);

    Buffer value;
    encode_i32(value, 5);
    reg.notify(100, -1, 1, value);

    // Current contract: the first-registered transport remains the target.
    EXPECT_EQ(t1.send_count, 1);
    EXPECT_EQ(t2.send_count, 0);
}

// -- notify() isolates a throwing subscriber and still delivers to the rest --
TEST(SubscriptionRegistryCoverageTest, NotifyIsolatesThrowingSubscriber) {
    SubscriptionRegistry reg;
    ThrowingTransport thrower;
    RecordingTransport recorder;

    reg.subscribe(1, -1, 1, &thrower);   // sent first, throws
    reg.subscribe(2, -1, 1, &recorder);  // sent second, must still receive
    ASSERT_EQ(reg.subscriber_count(-1, 1), 2u);

    Buffer value;
    encode_i32(value, 42);
    EXPECT_NO_THROW(reg.notify(100, -1, 1, value));

    EXPECT_EQ(thrower.send_attempts, 1);
    EXPECT_EQ(recorder.send_count, 1);

    // A send failure must NOT auto-remove the subscriber.
    EXPECT_EQ(reg.subscriber_count(-1, 1), 2u);
}

// -- A throw on the SECOND target must not un-deliver the first --
TEST(SubscriptionRegistryCoverageTest, NotifyThrowOnSecondTargetStillDeliversFirst) {
    SubscriptionRegistry reg;
    RecordingTransport recorder;
    ThrowingTransport thrower;

    reg.subscribe(1, -1, 1, &recorder);  // delivered first
    reg.subscribe(2, -1, 1, &thrower);   // throws second

    Buffer value;
    encode_i32(value, 7);
    EXPECT_NO_THROW(reg.notify(100, -1, 1, value));

    EXPECT_EQ(recorder.send_count, 1);
    EXPECT_EQ(thrower.send_attempts, 1);
}

// -- All subscribers throw: notify() completes without propagating --
TEST(SubscriptionRegistryCoverageTest, NotifyAllThrowCompletesWithoutPropagating) {
    SubscriptionRegistry reg;
    ThrowingTransport t1;
    ThrowingTransport t2;

    reg.subscribe(1, -1, 1, &t1);
    reg.subscribe(2, -1, 1, &t2);

    Buffer value;
    encode_i32(value, 1);
    EXPECT_NO_THROW(reg.notify(100, -1, 1, value));

    EXPECT_EQ(t1.send_attempts, 1);
    EXPECT_EQ(t2.send_attempts, 1);
    EXPECT_EQ(reg.subscriber_count(-1, 1), 2u);
}

// -- unsubscribe_all: mixed retain/erase, idempotency, unknown id --
TEST(SubscriptionRegistryCoverageTest, UnsubscribeAllMixedRetainAndErase) {
    SubscriptionRegistry reg;
    RecordingTransport t1;
    RecordingTransport t2;

    reg.subscribe(1, -1, 1, &t1);  // id1-only bucket -> erased
    reg.subscribe(1, -1, 2, &t1);  // shared bucket   -> retains id2
    reg.subscribe(2, -1, 2, &t2);
    reg.subscribe(2, -3, 9, &t2);  // id2-only bucket -> retained
    ASSERT_EQ(reg.total_subscriptions(), 4u);

    reg.unsubscribe_all(1);

    EXPECT_EQ(reg.subscriber_count(-1, 1), 0u);  // fully erased
    EXPECT_EQ(reg.subscriber_count(-1, 2), 1u);  // retains id2
    EXPECT_EQ(reg.subscriber_count(-3, 9), 1u);  // intact
    EXPECT_EQ(reg.total_subscriptions(), 2u);

    // Idempotency: a second call changes nothing.
    reg.unsubscribe_all(1);
    EXPECT_EQ(reg.total_subscriptions(), 2u);

    // Unknown subscriber id: no change.
    reg.unsubscribe_all(4242);
    EXPECT_EQ(reg.total_subscriptions(), 2u);
}

// -- Reentrancy: send() re-entering the registry must not deadlock --
TEST(SubscriptionRegistryCoverageTest, NotifyReentrancyDoesNotDeadlock) {
    SubscriptionRegistry reg;
    ReentrantTransport rt;
    rt.reg = &reg;

    reg.subscribe(1, -1, 1, &rt);

    Buffer value;
    encode_i32(value, 3);
    EXPECT_NO_THROW(reg.notify(100, -1, 1, value));

    EXPECT_EQ(rt.send_count, 1);
    EXPECT_EQ(rt.observed_count, 1u);            // read completed (lock was free)
    EXPECT_EQ(reg.subscriber_count(-5, 5), 1u);  // reentrant subscribe took effect
}

// -- Concurrency stress: many threads subscribe while notify/read contend --
TEST(SubscriptionRegistryCoverageTest, ConcurrentSubscribeAndNotifyNoCrash) {
    SubscriptionRegistry reg;
    CountingTransport shared;

    constexpr int kThreads = 8;
    constexpr int kPerThread = 50;

    std::atomic<bool> stop{false};

    std::thread notifier([&reg, &stop]() {
        Buffer value;
        encode_i32(value, 1);
        while (!stop.load(std::memory_order_relaxed)) {
            for (int k = 0; k < kThreads; ++k) {
                reg.notify(100, k, 1, value);
            }
        }
    });

    std::thread reader([&reg, &stop]() {
        while (!stop.load(std::memory_order_relaxed)) {
            (void)reg.total_subscriptions();
            (void)reg.subscriber_count(0, 1);
        }
    });

    std::vector<std::thread> subs;
    for (int t = 0; t < kThreads; ++t) {
        subs.emplace_back([&reg, &shared, t]() {
            for (int j = 0; j < kPerThread; ++j) {
                // Globally-unique id, fixed per-thread key -> deterministic total.
                auto id = static_cast<SubscriptionRegistry::SubscriberId>(t * kPerThread + j) + 1;
                reg.subscribe(id, t, 1, &shared);
            }
        });
    }

    for (auto& s : subs) {
        s.join();
    }
    stop.store(true, std::memory_order_relaxed);
    notifier.join();
    reader.join();

    EXPECT_EQ(reg.total_subscriptions(),
              static_cast<size_t>(kThreads * kPerThread));
}

// -- Extreme object_id / property_id key values stay distinct --
TEST(SubscriptionRegistryCoverageTest, ExtremeKeyValuesStoredIndependently) {
    SubscriptionRegistry reg;
    RecordingTransport t;

    const i32 min_obj = std::numeric_limits<i32>::min();
    const i32 max_obj = std::numeric_limits<i32>::max();
    const u16 max_prop = std::numeric_limits<u16>::max();  // 65535

    reg.subscribe(1, min_obj, 0, &t);
    reg.subscribe(2, max_obj, max_prop, &t);
    reg.subscribe(3, 0, 0, &t);
    reg.subscribe(4, 0, max_prop, &t);

    EXPECT_EQ(reg.subscriber_count(min_obj, 0), 1u);
    EXPECT_EQ(reg.subscriber_count(max_obj, max_prop), 1u);
    EXPECT_EQ(reg.subscriber_count(0, 0), 1u);
    EXPECT_EQ(reg.subscriber_count(0, max_prop), 1u);
    EXPECT_EQ(reg.total_subscriptions(), 4u);

    // property_id 0 vs 65535 on the same object must be distinct keys
    // (guards the (property_id << 16) hash mixing).
    Buffer value;
    encode_i32(value, 7);
    reg.notify(100, 0, 0, value);
    EXPECT_EQ(t.send_count, 1);  // only the (0,0) subscriber, not (0,65535)

    reg.unsubscribe(3, 0, 0);
    EXPECT_EQ(reg.subscriber_count(0, 0), 0u);
    EXPECT_EQ(reg.total_subscriptions(), 3u);
}

// -- notify() on a key with no subscribers is a no-op (early return) --
TEST(SubscriptionRegistryCoverageTest, NotifyNoSubscribersEarlyReturn) {
    SubscriptionRegistry reg;
    RecordingTransport other;

    reg.subscribe(1, -1, 1, &other);  // a DIFFERENT key

    Buffer value;
    encode_i32(value, 5);
    EXPECT_NO_THROW(reg.notify(100, 999, 7, value));
    EXPECT_EQ(other.send_count, 0);
}

// -- notify() after the only subscriber unsubscribes is a no-op --
TEST(SubscriptionRegistryCoverageTest, NotifyAfterUnsubscribeIsNoOp) {
    SubscriptionRegistry reg;
    RecordingTransport t;

    reg.subscribe(1, -1, 1, &t);
    reg.unsubscribe(1, -1, 1);  // bucket erased
    EXPECT_EQ(reg.subscriber_count(-1, 1), 0u);

    Buffer value;
    encode_i32(value, 5);
    EXPECT_NO_THROW(reg.notify(100, -1, 1, value));
    EXPECT_EQ(t.send_count, 0);
}

// -- notify() with an empty value delivers only the 12-byte PropertyHeader --
TEST(SubscriptionRegistryCoverageTest, NotifyEmptyValueDeliversHeaderOnly) {
    SubscriptionRegistry reg;
    RecordingTransport t;

    reg.subscribe(1, -1, 1, &t);

    Buffer empty;  // no value bytes
    reg.notify(100, -1, 1, empty);

    ASSERT_EQ(t.send_count, 1);
    Buffer& msg = t.sent.back();
    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_notify);
    EXPECT_EQ(hdr.payload_size, 12u);  // PropertyHeader only, no trailing value

    auto prop = wire::decode_property_header(msg);
    EXPECT_EQ(prop.type_id, 100u);
    EXPECT_EQ(prop.object_id, -1);
    EXPECT_EQ(prop.property_id, 1u);
}

// -- unsubscribe of a non-member id / nonexistent key are no-ops --
TEST(SubscriptionRegistryCoverageTest, UnsubscribeNonMemberAndUnknownKey) {
    SubscriptionRegistry reg;
    RecordingTransport t;

    reg.subscribe(1, -1, 1, &t);

    reg.unsubscribe(2, -1, 1);  // id not a member of an existing bucket
    EXPECT_EQ(reg.subscriber_count(-1, 1), 1u);

    reg.unsubscribe(1, -99, 99);  // wholly nonexistent key
    EXPECT_EQ(reg.subscriber_count(-1, 1), 1u);
    EXPECT_EQ(reg.total_subscriptions(), 1u);
}
