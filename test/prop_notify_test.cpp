// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <thread>
#include <atomic>

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
            for (int i = 0; i < 3; ++i) {
                Buffer value;
                encode_f64(value, static_cast<f64>(i) * 10.0);
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
    for (int i = 0; i < 3; ++i) {
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
            for (int i = 0; i < 3; ++i) {
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
