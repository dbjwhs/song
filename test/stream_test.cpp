// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <thread>

using namespace song;

// =============================================================================
// Stream Wire Message Tests
// =============================================================================

TEST(StreamWireTest, CreateStreamMessage) {
    Buffer chunk;
    encode_string(chunk, "hello");
    Buffer msg = wire::create_stream_message(42, chunk);

    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.magic, wire::kMagic);
    EXPECT_EQ(hdr.type, wire::MsgType::stream);
    EXPECT_EQ(hdr.sequence_id, 42u);
    EXPECT_EQ(hdr.payload_size, static_cast<u32>(chunk.size()));
}

TEST(StreamWireTest, CreateStreamEndMessage) {
    Buffer msg = wire::create_stream_end_message(99);

    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.magic, wire::kMagic);
    EXPECT_EQ(hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(hdr.sequence_id, 99u);
    EXPECT_EQ(hdr.payload_size, 0u);
}

TEST(StreamWireTest, EmptyChunk) {
    Buffer empty;
    Buffer msg = wire::create_stream_message(1, empty);

    msg.reset_read();
    auto hdr = wire::decode_header(msg);
    EXPECT_EQ(hdr.type, wire::MsgType::stream);
    EXPECT_EQ(hdr.payload_size, 0u);
}

// =============================================================================
// StreamWriter Unit Tests (using Transport directly)
// =============================================================================

TEST(StreamWriterTest, WriteAndEnd) {
    // Use a TCP loopback to test StreamWriter
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;

        // Create a stream writer and send 3 chunks
        StreamWriter writer(*conn, 42);
        for (int ndx = 0; ndx < 3; ++ndx) {
            Buffer chunk;
            encode_i32(chunk, ndx * 100);
            writer.write(chunk);
        }
        writer.end();
        EXPECT_TRUE(writer.ended());
    });

    // Client reads the stream messages
    TcpTransport client;
    client.connect("127.0.0.1", port, 5000);

    std::vector<i32> values;
    for (;;) {
        Buffer msg;
        ASSERT_TRUE(client.receive(msg, 5000));
        msg.reset_read();
        auto hdr = wire::decode_header(msg);

        if (hdr.type == wire::MsgType::stream_end) break;

        ASSERT_EQ(hdr.type, wire::MsgType::stream);
        EXPECT_EQ(hdr.sequence_id, 42u);
        values.push_back(decode_i32(msg));
    }

    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], 0);
    EXPECT_EQ(values[1], 100);
    EXPECT_EQ(values[2], 200);

    client.close();
    listener.close();
    server.join();
}

TEST(StreamWriterTest, AutoEndOnDestruction) {
    TcpListener listener;
    listener.listen(0);
    u16 port = listener.bound_port();

    std::thread server([&listener]() {
        auto conn = listener.accept(5000);
        if (!conn) return;

        {
            StreamWriter writer(*conn, 1);
            Buffer chunk;
            encode_string(chunk, "data");
            writer.write(chunk);
            // writer goes out of scope -- should auto-send stream_end
        }
    });

    TcpTransport client;
    client.connect("127.0.0.1", port, 5000);

    // Should get one stream + one stream_end
    Buffer msg1;
    ASSERT_TRUE(client.receive(msg1, 5000));
    msg1.reset_read();
    auto hdr1 = wire::decode_header(msg1);
    EXPECT_EQ(hdr1.type, wire::MsgType::stream);

    Buffer msg2;
    ASSERT_TRUE(client.receive(msg2, 5000));
    msg2.reset_read();
    auto hdr2 = wire::decode_header(msg2);
    EXPECT_EQ(hdr2.type, wire::MsgType::stream_end);

    client.close();
    listener.close();
    server.join();
}

// =============================================================================
// StreamReader Unit Tests
// =============================================================================

TEST(StreamReaderTest, BasicIteration) {
    StreamReader reader(1);

    Buffer c1;
    encode_i32(c1, 10);
    reader.add_chunk(std::move(c1));

    Buffer c2;
    encode_i32(c2, 20);
    reader.add_chunk(std::move(c2));
    reader.set_complete();

    EXPECT_EQ(reader.chunk_count(), 2u);
    EXPECT_TRUE(reader.complete());

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(decode_i32(reader.chunk()), 10);

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(decode_i32(reader.chunk()), 20);

    EXPECT_FALSE(reader.next());
}

TEST(StreamReaderTest, EmptyStream) {
    StreamReader reader(1);
    reader.set_complete();

    EXPECT_EQ(reader.chunk_count(), 0u);
    EXPECT_TRUE(reader.complete());
    EXPECT_FALSE(reader.next());
}

// =============================================================================
// End-to-End Streaming via TCP (ServiceRuntime + ServiceConnection)
// =============================================================================

static constexpr u16 kStreamService = 500;
static constexpr u16 kMethod_count_to = 1;
static constexpr u16 kMethod_echo_chunks = 2;

// Streaming dispatcher: counts from 0 to N, sending each as a stream chunk
void stream_dispatcher(u16 method_id, Buffer& request, StreamWriter& writer) {
    if (method_id == kMethod_count_to) {
        i32 n = decode_i32(request);
        for (i32 i = 0; i < n; ++i) {
            Buffer chunk;
            encode_i32(chunk, i);
            writer.write(chunk);
        }
    } else if (method_id == kMethod_echo_chunks) {
        // Read a string and echo it back as individual character chunks
        std::string s = decode_string(request);
        for (char c : s) {
            Buffer chunk;
            encode_string(chunk, std::string(1, c));
            writer.write(chunk);
        }
    }
    // writer auto-ends on return
}

class StreamE2ETest : public ::testing::Test {
protected:
    ServiceRuntime runtime_;
    TcpListener listener_;
    std::thread server_thread_;
    u16 port_ = 0;

    void SetUp() override {
        runtime_.register_stream_dispatcher(kStreamService, stream_dispatcher);
        runtime_.register_method(kStreamService, kMethod_count_to,
                                 wire::MethodFlags::streaming);
        runtime_.register_method(kStreamService, kMethod_echo_chunks,
                                 wire::MethodFlags::streaming);

        listener_.listen(0);
        port_ = listener_.bound_port();

        server_thread_ = std::thread([this]() {
            // Handle one client connection
            auto client = listener_.accept(5000);
            if (!client) return;
            try {
                // Manually run a simple client loop with streaming support
                // Send init
                Buffer init_msg = wire::create_init_message(
                    wire::kFirstVersion, wire::kCurrentVersion, 0);
                client->send(init_msg);

                // Handle messages
                for (;;) {
                    Buffer msg;
                    if (!client->receive(msg, 5000)) break;

                    auto hdr = wire::decode_header(msg);
                    if (hdr.magic != wire::kMagic) break;
                    if (hdr.type == wire::MsgType::shutdown) break;

                    if (hdr.type == wire::MsgType::call) {
                        auto [sid, mid] = wire::decode_method_call_header(msg);

                        try {
                            StreamWriter writer(*client, hdr.sequence_id);
                            stream_dispatcher(mid, msg, writer);
                        } catch (const std::exception& e) {
                            Buffer err = wire::create_error_message(
                                hdr.sequence_id, ErrorCode::unknown_method, e.what());
                            client->send(err);
                        }
                    }
                }
            } catch (...) {}
        });
    }

    void TearDown() override {
        listener_.close();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
};

TEST_F(StreamE2ETest, CountToFive) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port_, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_i32(args, 5);
    StreamReader reader = conn.call_streaming(kStreamService, kMethod_count_to, args);

    EXPECT_TRUE(reader.complete());
    EXPECT_EQ(reader.chunk_count(), 5u);

    for (i32 i = 0; i < 5; ++i) {
        ASSERT_TRUE(reader.next());
        EXPECT_EQ(decode_i32(reader.chunk()), i);
    }
    EXPECT_FALSE(reader.next());
}

TEST_F(StreamE2ETest, EchoChunks) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port_, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_string(args, "ABC");
    StreamReader reader = conn.call_streaming(kStreamService, kMethod_echo_chunks, args);

    EXPECT_EQ(reader.chunk_count(), 3u);

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(decode_string(reader.chunk()), "A");
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(decode_string(reader.chunk()), "B");
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(decode_string(reader.chunk()), "C");
}

TEST_F(StreamE2ETest, EmptyStream) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port_, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_i32(args, 0);  // count to 0 = empty stream
    StreamReader reader = conn.call_streaming(kStreamService, kMethod_count_to, args);

    EXPECT_TRUE(reader.complete());
    EXPECT_EQ(reader.chunk_count(), 0u);
    EXPECT_FALSE(reader.next());
}

TEST_F(StreamE2ETest, LargeStream) {
    auto tcp = std::make_unique<TcpTransport>();
    tcp->connect("127.0.0.1", port_, 5000);
    ServiceConnection conn(std::move(tcp));
    conn.init_handshake();

    Buffer args;
    encode_i32(args, 100);
    StreamReader reader = conn.call_streaming(kStreamService, kMethod_count_to, args);

    EXPECT_EQ(reader.chunk_count(), 100u);

    for (i32 i = 0; i < 100; ++i) {
        ASSERT_TRUE(reader.next());
        EXPECT_EQ(decode_i32(reader.chunk()), i);
    }
}
