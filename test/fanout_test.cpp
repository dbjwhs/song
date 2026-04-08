// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <thread>
#include <atomic>
#include <chrono>

using namespace song;

// =============================================================================
// SubscriptionRegistry Unit Tests
// =============================================================================

TEST(SubscriptionRegistryTest, SubscribeAndNotify) {
    SubscriptionRegistry reg;
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // Server subscribes its side of the connection, then notifies
    std::thread server_thread([&listener, &reg]() {
        auto conn = listener.accept(5000);
        if (!conn) return;

        // Register server-side transport -- notify sends TO client
        reg.subscribe(1, -1, 5, conn.get());
        EXPECT_EQ(reg.subscriber_count(-1, 5), 1u);

        // Notify -- pushes MSG_PROP_NOTIFY through conn to client
        Buffer value;
        encode_f64(value, 42.5);
        reg.notify(100, -1, 5, value);

        // Hold connection open
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });

    TcpTransport client;
    client.connect("127.0.0.1", port, 5000);

    EXPECT_EQ(reg.total_subscriptions(), 0u);  // Not yet subscribed (server thread races)

    // Client receives the notification
    Buffer msg;
    ASSERT_TRUE(client.receive(msg, 5000));
    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_notify);

    client.close();
    listener.close();
    server_thread.join();
}

TEST(SubscriptionRegistryTest, UnsubscribeRemovesSubscriber) {
    SubscriptionRegistry reg;

    // These are pure data structure tests -- no actual transport needed
    // Use a dummy pointer (never dereferenced since we don't call notify)
    Transport* dummy = reinterpret_cast<Transport*>(0x1234);

    reg.subscribe(1, -1, 5, dummy);
    EXPECT_EQ(reg.subscriber_count(-1, 5), 1u);

    reg.unsubscribe(1, -1, 5);
    EXPECT_EQ(reg.subscriber_count(-1, 5), 0u);
    EXPECT_EQ(reg.total_subscriptions(), 0u);
}

TEST(SubscriptionRegistryTest, UnsubscribeAllOnDisconnect) {
    SubscriptionRegistry reg;
    Transport* dummy = reinterpret_cast<Transport*>(0x1234);

    reg.subscribe(1, -1, 1, dummy);
    reg.subscribe(1, -1, 2, dummy);
    reg.subscribe(1, -2, 1, dummy);
    EXPECT_EQ(reg.total_subscriptions(), 3u);

    reg.unsubscribe_all(1);
    EXPECT_EQ(reg.total_subscriptions(), 0u);
}

// =============================================================================
// Multi-Client Fan-Out E2E Tests
// =============================================================================

TEST(FanOutTest, ThreeClientsReceiveNotification) {
    // Server that accepts 3 clients and pushes a notification to all
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    SubscriptionRegistry reg;

    std::thread server([&listener, &reg]() {
        std::vector<std::unique_ptr<TcpTransport>> clients;

        // Accept 3 clients
        for (int ndx = 0; ndx < 3; ++ndx) {
            auto conn = listener.accept(5000);
            if (!conn) return;

            // Send init
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion, wire::kCurrentVersion,
                static_cast<u32>(wire::Capability::properties));
            conn->send(init_msg);
            clients.push_back(std::move(conn));
        }

        // Wait for subscribe messages from all 3
        for (int ndx = 0; ndx < 3; ++ndx) {
            // Consume init_ack + subscribe
            for (int attempt = 0; attempt < 3; ++attempt) {
                Buffer msg;
                if (!clients[ndx]->receive(msg, 5000)) break;
                auto hdr = wire::decode_header(msg);
                if (hdr.type == wire::MsgType::prop_subscribe) {
                    auto prop = wire::decode_property_header(msg);
                    auto sub_id = reinterpret_cast<SubscriptionRegistry::SubscriberId>(
                        clients[ndx].get());
                    reg.subscribe(sub_id, prop.object_id, prop.property_id,
                                  clients[ndx].get());
                    break;
                }
            }
        }

        // Push notification -- should fan out to all 3
        Buffer value;
        encode_f64(value, 99.9);
        reg.notify(100, -1, 1, value);

        // Keep connections alive briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Clean up
        for (auto& c : clients) {
            c->close();
        }
    });

    // 3 clients connect and subscribe to the same property
    std::atomic<int> notifications_received{0};
    std::vector<std::thread> client_threads;

    for (int ndx = 0; ndx < 3; ++ndx) {
        client_threads.emplace_back([port, &notifications_received]() {
            auto tcp = std::make_unique<TcpTransport>();
            tcp->connect("127.0.0.1", port, 5000);
            ServiceConnection conn(std::move(tcp));
            conn.init_handshake();

            // Subscribe to object -1, property 1
            conn.subscribe_property(100, -1, 1, [&](const Buffer&) {
                notifications_received++;
            });

            // Poll for notification
            conn.poll_notifications(5000);
        });
    }

    for (auto& t : client_threads) {
        t.join();
    }

    listener.close();
    server.join();

    EXPECT_EQ(notifications_received, 3);
}

TEST(FanOutTest, UnsubscribedClientDoesNotReceive) {
    // Simpler approach: test SubscriptionRegistry directly
    // Subscribe two, unsubscribe one, notify, verify only one receives
    SubscriptionRegistry reg;

    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // Server accepts 2 connections, registers both, unregisters first, then notifies
    std::thread server([&listener, &reg]() {
        try {
            auto conn1 = listener.accept(5000);
            auto conn2 = listener.accept(5000);
            if (!conn1 || !conn2) return;

            auto id1 = reinterpret_cast<SubscriptionRegistry::SubscriberId>(conn1.get());
            auto id2 = reinterpret_cast<SubscriptionRegistry::SubscriberId>(conn2.get());

            reg.subscribe(id1, -1, 1, conn1.get());
            reg.subscribe(id2, -1, 1, conn2.get());
            EXPECT_EQ(reg.subscriber_count(-1, 1), 2u);

            // Unsubscribe first client
            reg.unsubscribe(id1, -1, 1);
            EXPECT_EQ(reg.subscriber_count(-1, 1), 1u);

            // Notify -- only conn2 should get it
            Buffer value;
            encode_i32(value, 42);
            reg.notify(100, -1, 1, value);

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            conn1->close();
            conn2->close();
        } catch (...) {}
    });

    // Client 1: connect but should NOT receive notification
    TcpTransport client1;
    client1.connect("127.0.0.1", port, 5000);

    // Client 2: connect and should receive notification
    TcpTransport client2;
    client2.connect("127.0.0.1", port, 5000);

    // Client 2 should get the notification
    Buffer msg;
    ASSERT_TRUE(client2.receive(msg, 5000));
    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::prop_notify);

    // Client 1 should NOT get anything (short timeout)
    Buffer msg1;
    try {
        bool got = client1.receive(msg1, 500);
        EXPECT_FALSE(got) << "Client1 should not receive after unsubscribe";
    } catch (...) {
        // Timeout or disconnect -- expected
    }

    client1.close();
    client2.close();
    listener.close();
    server.join();
}

TEST(FanOutTest, ServerInitiatedNotification) {
    // Server changes a property value internally, all subscribers get notified
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    SubscriptionRegistry reg;

    std::thread server([&listener, &reg]() {
        auto client = listener.accept(5000);
        if (!client) return;

        Buffer init_msg = wire::create_init_message(
            wire::kFirstVersion, wire::kCurrentVersion, 0);
        client->send(init_msg);

        // Read subscribe (skip init_ack)
        for (int attempt = 0; attempt < 3; ++attempt) {
            Buffer msg;
            if (!client->receive(msg, 5000)) break;
            auto hdr = wire::decode_header(msg);
            if (hdr.type == wire::MsgType::prop_subscribe) {
                auto prop = wire::decode_property_header(msg);
                auto sub_id = reinterpret_cast<SubscriptionRegistry::SubscriberId>(client.get());
                reg.subscribe(sub_id, prop.object_id, prop.property_id, client.get());
                break;
            }
        }

        // Server-initiated: push 5 rapid notifications (simulating a ticker)
        for (int ndx = 0; ndx < 5; ++ndx) {
            Buffer value;
            encode_f64(value, 100.0 + static_cast<f64>(ndx));
            reg.notify(1, -1, 1, value);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client->close();
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    std::vector<f64> received;
    conn.subscribe_property(1, -1, 1, [&](const Buffer& val) {
        Buffer copy;
        copy.write(val.data(), val.size());
        copy.reset_read();
        received.push_back(decode_f64(copy));
    });

    // Poll for all 5
    for (int ndx = 0; ndx < 5; ++ndx) {
        conn.poll_notifications(5000);
    }

    listener.close();
    server.join();

    ASSERT_EQ(received.size(), 5u);
    EXPECT_DOUBLE_EQ(received[0], 100.0);
    EXPECT_DOUBLE_EQ(received[4], 104.0);
}
