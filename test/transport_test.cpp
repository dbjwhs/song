// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/transport.hpp>
#include <song/process.hpp>
#include <song/wire.hpp>
#include <thread>
#include <chrono>
#include <atomic>

using namespace song;

// =============================================================================
// PipeTransport Tests
// =============================================================================

TEST(PipeTransportTest, SendReceive) {
    // Create pipe pair
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    // Create transports (cross-connected)
    PipeTransport sender(std::move(write_pipe), std::move(read_pipe2));
    PipeTransport receiver(std::move(write_pipe2), std::move(read_pipe));

    // Create a test message
    Buffer args;
    encode_string(args, "Hello, pipes!");
    Buffer msg = wire::create_call_message(1, 1, 1, args);

    // Send from sender
    sender.send(msg);

    // Receive on receiver
    Buffer received;
    EXPECT_TRUE(receiver.receive(received, 1000));

    // Verify header
    auto hdr = wire::decode_header_validated(received);
    EXPECT_EQ(hdr.type, wire::MsgType::call);
    EXPECT_EQ(hdr.sequence_id, 1u);
}

// Regression: receive() must accumulate a header that arrives split across
// multiple writes, not throw a spurious "incomplete header" ProtocolError. A
// single pipe read can return fewer than 16 bytes; TcpTransport already loops,
// and this proves PipeTransport now does too.
TEST(PipeTransportTest, ReceiveAccumulatesSplitHeader) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();  // unused write side
    PipeTransport receiver(std::move(write_pipe2), std::move(read_pipe));

    Buffer args;
    encode_string(args, "split");
    Buffer msg = wire::create_call_message(7, 1, 1, args);
    ASSERT_GE(msg.size(), 16u);

    // Write the first 8 header bytes, pause, then the remaining header + payload.
    std::thread writer([&]() {
        const auto* bytes = reinterpret_cast<const char*>(msg.data());
        write_pipe.write(bytes, 8);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        write_pipe.write(bytes + 8, msg.size() - 8);
    });

    Buffer received;
    bool ok = receiver.receive(received, 1000);
    writer.join();

    EXPECT_TRUE(ok) << "receive() must accumulate a split header, not throw";
    if (ok) {
        auto hdr = wire::decode_header_validated(received);
        EXPECT_EQ(hdr.type, wire::MsgType::call);
        EXPECT_EQ(hdr.sequence_id, 7u);
        // Payload is a method call: service_id, method_id, then the string arg.
        auto [service_id, method_id] = wire::decode_method_call_header(received);
        EXPECT_EQ(service_id, 1u);
        EXPECT_EQ(method_id, 1u);
        EXPECT_EQ(decode_string(received), "split");
    }
}

TEST(PipeTransportTest, IsConnected) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    PipeTransport transport(std::move(write_pipe), std::move(read_pipe2));
    EXPECT_TRUE(transport.is_connected());

    transport.close();
    EXPECT_FALSE(transport.is_connected());
}

TEST(PipeTransportTest, TypeName) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    PipeTransport transport(std::move(write_pipe), std::move(read_pipe2));
    EXPECT_STREQ(transport.type_name(), "pipe");
}

TEST(PipeTransportTest, MoveConstruct) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    PipeTransport transport1(std::move(write_pipe), std::move(read_pipe2));
    EXPECT_TRUE(transport1.is_connected());

    PipeTransport transport2(std::move(transport1));
    EXPECT_TRUE(transport2.is_connected());
    EXPECT_FALSE(transport1.is_connected());
}

TEST(PipeTransportTest, MoveAssign) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    PipeTransport transport1(std::move(write_pipe), std::move(read_pipe2));

    auto [read_pipe3, write_pipe3] = Pipe::create_pair();
    auto [read_pipe4, write_pipe4] = Pipe::create_pair();
    PipeTransport transport2(std::move(write_pipe3), std::move(read_pipe4));

    transport2 = std::move(transport1);
    EXPECT_TRUE(transport2.is_connected());
    EXPECT_FALSE(transport1.is_connected());
}

// =============================================================================
// TcpListener Tests
// =============================================================================

TEST(TcpListenerTest, ListenOnEphemeralPort) {
    TcpListener listener;
    listener.listen(0);  // Ephemeral port

    EXPECT_TRUE(listener.is_listening());
    EXPECT_GT(listener.bound_port(), 0);
    EXPECT_GE(listener.bound_port(), 1024);

    listener.close();
    EXPECT_FALSE(listener.is_listening());
}

TEST(TcpListenerTest, AcceptTimeout) {
    TcpListener listener;
    listener.listen(0);

    auto start = std::chrono::steady_clock::now();
    auto conn = listener.accept(100);  // 100ms timeout
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(conn, nullptr);  // No connection
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 90);
}

TEST(TcpListenerTest, MoveConstruct) {
    TcpListener listener1;
    listener1.listen(0);
    u16 port = listener1.bound_port();

    TcpListener listener2(std::move(listener1));
    EXPECT_TRUE(listener2.is_listening());
    EXPECT_EQ(listener2.bound_port(), port);
    EXPECT_FALSE(listener1.is_listening());
}

TEST(TcpListenerTest, MoveAssign) {
    TcpListener listener1;
    listener1.listen(0);
    u16 port = listener1.bound_port();

    TcpListener listener2;
    listener2 = std::move(listener1);
    EXPECT_TRUE(listener2.is_listening());
    EXPECT_EQ(listener2.bound_port(), port);
    EXPECT_FALSE(listener1.is_listening());
}

// =============================================================================
// TcpTransport Tests
// =============================================================================

TEST(TcpTransportTest, ConnectToListener) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // Connect in a thread
    std::atomic<bool> connected{false};
    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);
        connected = true;
        EXPECT_TRUE(client.is_connected());
        EXPECT_EQ(client.peer_port(), port);
    });

    // Accept connection
    auto server_conn = listener.accept(1000);
    EXPECT_NE(server_conn, nullptr);
    EXPECT_TRUE(server_conn->is_connected());

    client_thread.join();
    EXPECT_TRUE(connected);
}

// Coverage: sending on a TcpTransport whose peer has closed must throw
// ServiceError, never terminate the process with SIGPIPE. The socket path is
// already protected (MSG_NOSIGNAL on send, SO_NOSIGPIPE on the socket); this
// guards that protection against regression. Reaching the assertion proves no
// signal killed the process.
TEST(TcpTransportTest, SendToClosedPeerThrowsNotSignal) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    TcpTransport client;
    std::thread client_thread([&]() {
        client.connect("127.0.0.1", port, 1000);
    });
    auto server = listener.accept(1000);
    ASSERT_NE(server, nullptr);
    client_thread.join();

    // Drop the peer, then let the OS propagate the close.
    server->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Buffer args;
    encode_string(args, "after close");
    Buffer msg = wire::create_call_message(1, 1, 1, args);

    // The first send may still be buffered locally; a subsequent one must fail
    // once the reset is seen. It must surface as an exception, not a signal.
    bool threw = false;
    try {
        for (int ndx = 0; ndx < 1000 && !threw; ++ndx) {
            client.send(msg);
        }
    } catch (const ServiceError&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "send() to a closed TCP peer must throw, not raise SIGPIPE";
}

TEST(TcpTransportTest, SendReceive) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // Client thread
    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);

        // Create and send a message
        Buffer args;
        encode_string(args, "Hello, TCP!");
        Buffer msg = wire::create_call_message(42, 1, 2, args);
        client.send(msg);

        // Receive response
        Buffer response;
        EXPECT_TRUE(client.receive(response, 1000));
        auto hdr = wire::decode_header_validated(response);
        EXPECT_EQ(hdr.type, wire::MsgType::result);
        EXPECT_EQ(hdr.sequence_id, 42u);
    });

    // Server side
    auto server = listener.accept(1000);
    ASSERT_NE(server, nullptr);

    // Receive message
    Buffer received;
    EXPECT_TRUE(server->receive(received, 1000));
    auto hdr = wire::decode_header_validated(received);
    EXPECT_EQ(hdr.type, wire::MsgType::call);
    EXPECT_EQ(hdr.sequence_id, 42u);

    // Send response
    Buffer result_payload;
    encode_string(result_payload, "Response");
    Buffer result_msg = wire::create_result_message(42, result_payload);
    server->send(result_msg);

    client_thread.join();
}

TEST(TcpTransportTest, LargeMessage) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    // Create large payload (100KB)
    std::vector<std::byte> large_data(100000);
    for (size_t ndx = 0; ndx < large_data.size(); ++ndx) {
        large_data[ndx] = static_cast<std::byte>(ndx & 0xFF);
    }

    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);

        // Send large message
        Buffer args;
        encode_bytes(args, large_data);
        Buffer msg = wire::create_call_message(1, 1, 1, args);
        client.send(msg);
    });

    auto server = listener.accept(1000);
    ASSERT_NE(server, nullptr);

    // Receive large message
    Buffer received;
    EXPECT_TRUE(server->receive(received, 5000));

    // Decode header first (advances read position)
    auto hdr = wire::decode_header_validated(received);
    EXPECT_EQ(hdr.type, wire::MsgType::call);

    // Skip service_id and method_id in the payload
    [[maybe_unused]] auto call_info = wire::decode_method_call_header(received);

    // Now decode the bytes payload
    auto decoded = decode_bytes(received);
    EXPECT_EQ(decoded.size(), large_data.size());
    EXPECT_EQ(decoded, large_data);

    client_thread.join();
}

TEST(TcpTransportTest, ConnectionRefused) {
    TcpTransport client;

    // Try to connect to a port that's not listening
    // Use a high ephemeral port that's unlikely to be in use
    EXPECT_THROW(client.connect("127.0.0.1", 59999, 100), ServiceError);
    EXPECT_FALSE(client.is_connected());
}

TEST(TcpTransportTest, ConnectionTimeout) {
    TcpTransport client;

    // Try to connect to a non-routable address (should timeout)
    // 10.255.255.1 is in a private range and typically not routable
    auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.connect("10.255.255.1", 12345, 200), ServiceError);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should have waited at least the timeout period
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 150);
    EXPECT_FALSE(client.is_connected());
}

TEST(TcpTransportTest, TypeName) {
    TcpTransport transport;
    EXPECT_STREQ(transport.type_name(), "tcp");
}

TEST(TcpTransportTest, MoveConstruct) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread client_thread([&]() {
        TcpTransport client1;
        client1.connect("127.0.0.1", port, 1000);
        EXPECT_TRUE(client1.is_connected());

        TcpTransport client2(std::move(client1));
        EXPECT_TRUE(client2.is_connected());
        EXPECT_FALSE(client1.is_connected());
    });

    auto server = listener.accept(1000);
    client_thread.join();
}

TEST(TcpTransportTest, MoveAssign) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread client_thread([&]() {
        TcpTransport client1;
        client1.connect("127.0.0.1", port, 1000);

        TcpTransport client2;
        client2 = std::move(client1);
        EXPECT_TRUE(client2.is_connected());
        EXPECT_FALSE(client1.is_connected());
    });

    auto server = listener.accept(1000);
    client_thread.join();
}

TEST(TcpTransportTest, CloseDisconnects) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);
        EXPECT_TRUE(client.is_connected());

        client.close();
        EXPECT_FALSE(client.is_connected());
    });

    auto server = listener.accept(1000);
    client_thread.join();
}

TEST(TcpTransportTest, DetectPeerDisconnect) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);
        // Immediately close
        client.close();
    });

    auto server = listener.accept(1000);
    ASSERT_NE(server, nullptr);

    client_thread.join();

    // Give the close a moment to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should detect disconnect on receive
    Buffer msg;
    EXPECT_FALSE(server->receive(msg, 100));
}

// =============================================================================
// Transport Interface Tests
// =============================================================================

TEST(TransportInterfaceTest, PipeTransportAsTransport) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    // Use through base class pointer
    std::unique_ptr<Transport> transport =
        std::make_unique<PipeTransport>(std::move(write_pipe), std::move(read_pipe2));

    EXPECT_TRUE(transport->is_connected());
    EXPECT_STREQ(transport->type_name(), "pipe");

    transport->close();
    EXPECT_FALSE(transport->is_connected());
}

TEST(TransportInterfaceTest, TcpTransportAsTransport) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread client_thread([&]() {
        auto client = std::make_unique<TcpTransport>();
        client->connect("127.0.0.1", port, 1000);

        // Use through base class pointer
        std::unique_ptr<Transport> transport = std::move(client);

        EXPECT_TRUE(transport->is_connected());
        EXPECT_STREQ(transport->type_name(), "tcp");

        transport->close();
        EXPECT_FALSE(transport->is_connected());
    });

    auto server = listener.accept(1000);
    client_thread.join();
}

// =============================================================================
// ServiceConnection over TCP Tests
// =============================================================================

TEST(TcpServiceConnectionTest, ConnectAndCallViaTcp) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::atomic<bool> server_ready{false};
    std::exception_ptr server_exception;

    // Server thread - simulates a Song service
    std::thread server_thread([&]() {
        try {
            auto server = listener.accept(5000);
            if (!server) {
                throw std::runtime_error("Server accept timed out");
            }

            // Send init message (like ServiceRuntime does)
            std::vector<wire::MethodDescriptor> methods = {
                {1, 1, wire::MethodFlags::none, 0},  // service 1, method 1 (echo)
            };
            Buffer init_msg = wire::create_init_message(
                wire::kFirstVersion,
                wire::kCurrentVersion,
                0,
                methods
            );
            server->send(init_msg);
            server_ready = true;

            // Receive next message (skip init_ack if present)
            Buffer call;
            if (!server->receive(call, 5000)) {
                throw std::runtime_error("Server receive timed out");
            }
            auto hdr = wire::decode_header_validated(call);
            if (hdr.type == wire::MsgType::init_ack) {
                call = Buffer{};
                if (!server->receive(call, 5000)) {
                    throw std::runtime_error("Server receive timed out after init_ack");
                }
                hdr = wire::decode_header_validated(call);
            }
            if (hdr.type != wire::MsgType::call) {
                throw std::runtime_error("Expected call message");
            }

            [[maybe_unused]] auto [service_id, method_id] = wire::decode_method_call_header(call);

            // Echo back the string
            std::string input = decode_string(call);
            Buffer response;
            encode_string(response, input);
            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            server->send(result_msg);
        } catch (...) {
            server_exception = std::current_exception();
        }
    });

    try {
        // Client - use ServiceConnection over TCP
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect("127.0.0.1", port, 5000);

        ServiceConnection conn(std::move(tcp));
        conn.init_handshake();

        // Verify methods were received
        EXPECT_TRUE(conn.supports(1, 1));

        // Make a call
        Buffer args;
        encode_string(args, "Hello via TCP!");
        Buffer result = conn.call(1, 1, args);
        std::string response = decode_string(result);
        EXPECT_EQ(response, "Hello via TCP!");
    } catch (const std::exception& e) {
        FAIL() << "Client exception: " << e.what();
    }

    server_thread.join();

    if (server_exception) {
        try {
            std::rethrow_exception(server_exception);
        } catch (const std::exception& e) {
            FAIL() << "Server exception: " << e.what();
        }
    }
}

TEST(TcpServiceConnectionTest, MultipleCallsViaTcp) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::atomic<int> call_count{0};

    std::thread server_thread([&]() {
        auto server = listener.accept(1000);
        ASSERT_NE(server, nullptr);

        // Send init message
        std::vector<wire::MethodDescriptor> methods;
        Buffer init_msg = wire::create_init_message(
            wire::kFirstVersion,
            wire::kCurrentVersion,
            0,
            methods
        );
        server->send(init_msg);

        // Skip init_ack if present (v1.1 client)
        {
            Buffer ack;
            if (server->receive(ack, 500)) {
                auto ack_hdr = wire::decode_header_validated(ack);
                if (ack_hdr.type != wire::MsgType::init_ack) {
                    // Not an ack -- this is the first call, handle below
                    // (won't happen in practice since client sends ack first)
                }
            }
        }

        // Handle multiple calls
        for (int ndx = 0; ndx < 3; ++ndx) {
            Buffer call;
            if (!server->receive(call, 1000)) break;

            auto hdr = wire::decode_header_validated(call);
            if (hdr.type != wire::MsgType::call) break;

            [[maybe_unused]] auto call_info = wire::decode_method_call_header(call);
            i32 input = decode_i32(call);

            // Return input * 2
            Buffer response;
            encode_i32(response, input * 2);
            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            server->send(result_msg);
            call_count++;
        }
    });

    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port, 1000);

    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    // Make 3 calls
    for (int ndx = 1; ndx <= 3; ++ndx) {
        Buffer args;
        encode_i32(args, ndx);
        Buffer result = conn.call(1, 1, args);
        i32 response = decode_i32(result);
        EXPECT_EQ(response, ndx * 2);
    }

    server_thread.join();
    EXPECT_EQ(call_count, 3);
}

// =============================================================================
// Receive framing error paths (oversized payload, mid-payload EOF)
// =============================================================================

// A header claiming a payload above the maximum is rejected at the transport
// layer before any large allocation, on both pipe and TCP.
TEST(PipeTransportTest, ReceiveOversizedPayloadRejected) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();  // unused write side
    PipeTransport receiver(std::move(write_pipe2), std::move(read_pipe));

    wire::Header hdr{};
    hdr.magic = wire::kMagic;
    hdr.type = wire::MsgType::call;
    hdr.sequence_id = 1;
    hdr.payload_size = static_cast<u32>(wire::kMaxPayloadSize + 1);
    Buffer header_only;
    wire::encode_header(header_only, hdr);
    write_pipe.write(header_only.data(), header_only.size());

    Buffer msg;
    EXPECT_THROW(receiver.receive(msg, 1000), ProtocolError);
}

// A header whose declared payload never fully arrives (peer closes mid-payload)
// throws ServiceError rather than hanging or returning a partial message.
TEST(PipeTransportTest, ReceiveMidPayloadEofThrows) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();  // unused write side
    PipeTransport receiver(std::move(write_pipe2), std::move(read_pipe));

    wire::Header hdr{};
    hdr.magic = wire::kMagic;
    hdr.type = wire::MsgType::call;
    hdr.sequence_id = 1;
    hdr.payload_size = 100;
    Buffer header_buf;
    wire::encode_header(header_buf, hdr);
    write_pipe.write(header_buf.data(), header_buf.size());
    std::vector<char> partial(40, 'x');
    write_pipe.write(partial.data(), partial.size());
    write_pipe.close_write();  // EOF mid-payload

    Buffer msg;
    EXPECT_THROW(receiver.receive(msg, 1000), ServiceError);
}

TEST(TcpTransportTest, ReceiveOversizedPayloadRejected) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::atomic<bool> threw{false};
    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);
        Buffer msg;
        try {
            client.receive(msg, 2000);
        } catch (const ProtocolError&) {
            threw = true;
        } catch (...) {
        }
    });

    auto server = listener.accept(2000);
    ASSERT_NE(server, nullptr);

    wire::Header hdr{};
    hdr.magic = wire::kMagic;
    hdr.type = wire::MsgType::call;
    hdr.sequence_id = 1;
    hdr.payload_size = static_cast<u32>(wire::kMaxPayloadSize + 1);
    Buffer header_only;
    wire::encode_header(header_only, hdr);
    server->send(header_only);

    client_thread.join();
    EXPECT_TRUE(threw);

    server->close();
    listener.close();
}

TEST(TcpTransportTest, ReceiveMidPayloadEofThrows) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::atomic<bool> threw{false};
    std::thread client_thread([&]() {
        TcpTransport client;
        client.connect("127.0.0.1", port, 1000);
        Buffer msg;
        try {
            client.receive(msg, 2000);
        } catch (const ServiceError&) {
            threw = true;
        } catch (...) {
        }
    });

    auto server = listener.accept(2000);
    ASSERT_NE(server, nullptr);

    // Header claims a 100-byte payload; send only 40 bytes then close.
    wire::Header hdr{};
    hdr.magic = wire::kMagic;
    hdr.type = wire::MsgType::call;
    hdr.sequence_id = 1;
    hdr.payload_size = 100;
    Buffer buf;
    wire::encode_header(buf, hdr);
    std::vector<char> partial(40, 'x');
    buf.write(partial.data(), partial.size());
    server->send(buf);
    server->close();  // EOF mid-payload

    client_thread.join();
    EXPECT_TRUE(threw);

    listener.close();
}
