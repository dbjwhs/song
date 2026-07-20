// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <thread>
#include <cerrno>
#include <csignal>
#include <unistd.h>

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

// Regression: the fd-based StreamWriter path must surface a broken pipe as a
// ServiceError, not a fatal SIGPIPE. With SIGPIPE ignored, writing a chunk to a
// pipe whose read end is closed fails the underlying write, which write()
// reports and StreamWriter turns into a ServiceError.
TEST(StreamWriterTest, FdWriteToBrokenPipeThrows) {
    struct sigaction prev{};
    struct sigaction ign{};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    ASSERT_EQ(sigaction(SIGPIPE, &ign, &prev), 0);

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    ::close(fds[0]);  // close the read end

    bool threw = false;
    {
        StreamWriter writer(fds[1], /*sequence_id=*/1);
        Buffer chunk;
        encode_i32(chunk, 42);
        try {
            writer.write(chunk);
        } catch (const ServiceError&) {
            threw = true;
        }
        // Destructor calls end(), which writes stream_end to the broken pipe but
        // deliberately ignores the failure, so it does not throw during unwind.
    }

    ::close(fds[1]);
    sigaction(SIGPIPE, &prev, nullptr);

    EXPECT_TRUE(threw) << "StreamWriter fd write to a broken pipe must throw ServiceError";
}

// Read one complete wire message (16-byte header + payload) from a raw fd.
static bool read_one_message(int fd, wire::Header& hdr_out, Buffer& payload_out) {
    std::byte hdr_buf[16];
    size_t off = 0;
    while (off < 16) {
        ssize_t n = ::read(fd, hdr_buf + off, 16 - off);
        if (n <= 0) {
            return false;
        }
        off += static_cast<size_t>(n);
    }
    Buffer hb;
    hb.write(hdr_buf, 16);
    hb.reset_read();
    hdr_out = wire::decode_header(hb);

    payload_out.reset();
    if (hdr_out.payload_size > 0) {
        std::vector<std::byte> pl(hdr_out.payload_size);
        size_t poff = 0;
        while (poff < hdr_out.payload_size) {
            ssize_t n = ::read(fd, pl.data() + poff, hdr_out.payload_size - poff);
            if (n <= 0) {
                return false;
            }
            poff += static_cast<size_t>(n);
        }
        payload_out.write(pl.data(), hdr_out.payload_size);
        payload_out.reset_read();
    }
    return true;
}

// The fd-based StreamWriter path (used by run()'s streaming dispatch) emits one
// MSG_STREAM per write() and a MSG_STREAM_END on end()/destruction, all carrying
// the sequence id. Only the Transport-based path was tested previously.
TEST(StreamWriterTest, FdModeWritesStreamThenEnd) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    {
        StreamWriter writer(fds[1], /*sequence_id=*/7);
        for (i32 value : {0, 100, 200}) {
            Buffer chunk;
            encode_i32(chunk, value);
            writer.write(chunk);
        }
        writer.end();
    }
    ::close(fds[1]);

    // Three stream chunks with the sequence id, in order.
    for (i32 expected : {0, 100, 200}) {
        wire::Header hdr{};
        Buffer payload;
        ASSERT_TRUE(read_one_message(fds[0], hdr, payload));
        EXPECT_EQ(hdr.type, wire::MsgType::stream);
        EXPECT_EQ(hdr.sequence_id, 7u);
        EXPECT_EQ(decode_i32(payload), expected);
    }

    // Then the stream-end marker (empty payload, same sequence id).
    wire::Header end_hdr{};
    Buffer end_payload;
    ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
    EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(end_hdr.sequence_id, 7u);
    EXPECT_EQ(end_hdr.payload_size, 0u);

    ::close(fds[0]);
}


// =============================================================================
// StreamWriter Move Semantics (fd mode) -- gap: stream-writer-move-semantics
// =============================================================================

// Move-construction transfers stream ownership: the moved-from writer is marked
// ended (emits nothing on destruction) while the target keeps the sequence id
// and remains active. The reader must see exactly one chunk + one stream_end.
TEST(StreamWriterTest, FdMoveCtorTransfersOwnership) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    {
        StreamWriter a(fds[1], 5);
        Buffer chunk;
        encode_i32(chunk, 42);
        a.write(chunk);

        StreamWriter b(std::move(a));
        EXPECT_TRUE(a.ended());
        EXPECT_FALSE(b.ended());
        EXPECT_EQ(b.sequence_id(), 5u);
        b.end();
        EXPECT_TRUE(b.ended());
    }  // a and b destruct; both ended_, so neither emits anything
    ::close(fds[1]);

    wire::Header hdr{};
    Buffer payload;
    ASSERT_TRUE(read_one_message(fds[0], hdr, payload));
    EXPECT_EQ(hdr.type, wire::MsgType::stream);
    EXPECT_EQ(hdr.sequence_id, 5u);
    EXPECT_EQ(decode_i32(payload), 42);

    wire::Header end_hdr{};
    Buffer end_payload;
    ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
    EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(end_hdr.sequence_id, 5u);

    // Write end closed and both messages consumed -> EOF, nothing more emitted.
    wire::Header extra_hdr{};
    Buffer extra_payload;
    EXPECT_FALSE(read_one_message(fds[0], extra_hdr, extra_payload));
    ::close(fds[0]);
}

// A moved-from StreamWriter has ended_ == true, so write() must reject with a
// ServiceError before touching the fd.
TEST(StreamWriterTest, MovedFromWriterRejectsWrite) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    {
        StreamWriter a(fds[1], 3);
        StreamWriter b(std::move(a));
        EXPECT_TRUE(a.ended());

        Buffer chunk;
        encode_i32(chunk, 1);
        EXPECT_THROW(a.write(chunk), ServiceError);

        b.end();  // b carries a's sequence id (3)
    }
    ::close(fds[1]);

    // Only b's stream_end(seq 3) should appear; the moved-from a emits nothing.
    wire::Header hdr{};
    Buffer payload;
    ASSERT_TRUE(read_one_message(fds[0], hdr, payload));
    EXPECT_EQ(hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(hdr.sequence_id, 3u);

    wire::Header extra_hdr{};
    Buffer extra_payload;
    EXPECT_FALSE(read_one_message(fds[0], extra_hdr, extra_payload));
    ::close(fds[0]);
}

// Self move-assignment is a no-op (guarded by this != &other): the writer keeps
// its state, emits no premature stream_end, and stays usable.
TEST(StreamWriterTest, SelfMoveAssignmentIsSafe) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    {
        StreamWriter w(fds[1], 9);
        // Pointer indirection avoids a compile-time -Wself-move diagnostic while
        // still exercising the runtime self-assignment guard.
        StreamWriter* self = &w;
        w = std::move(*self);
        EXPECT_FALSE(w.ended());
        EXPECT_EQ(w.sequence_id(), 9u);

        Buffer chunk;
        encode_i32(chunk, 77);
        w.write(chunk);  // still usable after self-move
        w.end();
    }
    ::close(fds[1]);

    wire::Header hdr{};
    Buffer payload;
    ASSERT_TRUE(read_one_message(fds[0], hdr, payload));
    EXPECT_EQ(hdr.type, wire::MsgType::stream);
    EXPECT_EQ(hdr.sequence_id, 9u);
    EXPECT_EQ(decode_i32(payload), 77);

    wire::Header end_hdr{};
    Buffer end_payload;
    ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
    EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(end_hdr.sequence_id, 9u);

    wire::Header extra_hdr{};
    Buffer extra_payload;
    EXPECT_FALSE(read_one_message(fds[0], extra_hdr, extra_payload));
    ::close(fds[0]);
}

// Move-assigning into an active writer must end the target's current stream
// first (stream_end on the old sequence id / fd), then adopt the source stream.
TEST(StreamWriterTest, FdMoveAssignEndsPriorTargetStream) {
    int fds_a[2];
    int fds_b[2];
    ASSERT_EQ(::pipe(fds_a), 0);
    ASSERT_EQ(::pipe(fds_b), 0);
    {
        StreamWriter writer_a(fds_a[1], 5);
        Buffer a_chunk;
        encode_i32(a_chunk, 500);
        writer_a.write(a_chunk);

        StreamWriter writer_b(fds_b[1], 2);
        Buffer b_chunk;
        encode_i32(b_chunk, 200);
        writer_b.write(b_chunk);

        writer_b = std::move(writer_a);  // ends stream 2 on pipe B, adopts stream 5
        EXPECT_TRUE(writer_a.ended());
        EXPECT_FALSE(writer_b.ended());
        EXPECT_EQ(writer_b.sequence_id(), 5u);

        Buffer follow;
        encode_i32(follow, 501);
        writer_b.write(follow);  // now writes to pipe A, seq 5
        writer_b.end();
    }
    ::close(fds_a[1]);
    ::close(fds_b[1]);

    // Pipe B: original chunk(200, seq 2) then the forced stream_end(seq 2).
    {
        wire::Header hdr{};
        Buffer payload;
        ASSERT_TRUE(read_one_message(fds_b[0], hdr, payload));
        EXPECT_EQ(hdr.type, wire::MsgType::stream);
        EXPECT_EQ(hdr.sequence_id, 2u);
        EXPECT_EQ(decode_i32(payload), 200);

        wire::Header end_hdr{};
        Buffer end_payload;
        ASSERT_TRUE(read_one_message(fds_b[0], end_hdr, end_payload));
        EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
        EXPECT_EQ(end_hdr.sequence_id, 2u);

        wire::Header extra_hdr{};
        Buffer extra_payload;
        EXPECT_FALSE(read_one_message(fds_b[0], extra_hdr, extra_payload));
    }

    // Pipe A: writer_a's chunk(500) + writer_b's follow-up chunk(501) + end, all seq 5.
    {
        wire::Header hdr{};
        Buffer payload;
        ASSERT_TRUE(read_one_message(fds_a[0], hdr, payload));
        EXPECT_EQ(hdr.type, wire::MsgType::stream);
        EXPECT_EQ(hdr.sequence_id, 5u);
        EXPECT_EQ(decode_i32(payload), 500);

        wire::Header hdr2{};
        Buffer payload2;
        ASSERT_TRUE(read_one_message(fds_a[0], hdr2, payload2));
        EXPECT_EQ(hdr2.type, wire::MsgType::stream);
        EXPECT_EQ(hdr2.sequence_id, 5u);
        EXPECT_EQ(decode_i32(payload2), 501);

        wire::Header end_hdr{};
        Buffer end_payload;
        ASSERT_TRUE(read_one_message(fds_a[0], end_hdr, end_payload));
        EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
        EXPECT_EQ(end_hdr.sequence_id, 5u);

        wire::Header extra_hdr{};
        Buffer extra_payload;
        EXPECT_FALSE(read_one_message(fds_a[0], extra_hdr, extra_payload));
    }

    ::close(fds_a[0]);
    ::close(fds_b[0]);
}

// =============================================================================
// StreamWriter end()/write() contract -- gap: stream-writer-write-after-end
// =============================================================================

// After end(), further write() must throw ServiceError and emit nothing extra.
TEST(StreamWriterTest, FdWriteAfterEndThrows) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    {
        StreamWriter w(fds[1], 11);
        Buffer chunk;
        encode_i32(chunk, 55);
        w.write(chunk);
        w.end();
        EXPECT_TRUE(w.ended());

        Buffer chunk2;
        encode_i32(chunk2, 66);
        EXPECT_THROW(w.write(chunk2), ServiceError);
    }
    ::close(fds[1]);

    // Exactly one chunk followed by exactly one stream_end.
    wire::Header hdr{};
    Buffer payload;
    ASSERT_TRUE(read_one_message(fds[0], hdr, payload));
    EXPECT_EQ(hdr.type, wire::MsgType::stream);
    EXPECT_EQ(hdr.sequence_id, 11u);
    EXPECT_EQ(decode_i32(payload), 55);

    wire::Header end_hdr{};
    Buffer end_payload;
    ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
    EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(end_hdr.sequence_id, 11u);

    wire::Header extra_hdr{};
    Buffer extra_payload;
    EXPECT_FALSE(read_one_message(fds[0], extra_hdr, extra_payload));
    ::close(fds[0]);
}

// end() is idempotent: repeated calls send exactly one stream_end.
TEST(StreamWriterTest, FdEndIsIdempotent) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    {
        StreamWriter w(fds[1], 22);
        w.end();
        EXPECT_TRUE(w.ended());
        w.end();  // no-op
        w.end();  // no-op
    }
    ::close(fds[1]);

    wire::Header end_hdr{};
    Buffer end_payload;
    ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
    EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(end_hdr.sequence_id, 22u);
    EXPECT_EQ(end_hdr.payload_size, 0u);

    // Only ONE stream_end was ever written.
    wire::Header extra_hdr{};
    Buffer extra_payload;
    EXPECT_FALSE(read_one_message(fds[0], extra_hdr, extra_payload));
    ::close(fds[0]);
}

// =============================================================================
// StreamWriter payload boundaries -- gap: stream-writer-large-chunk-partial-write
// =============================================================================

// A single chunk larger than the OS pipe buffer forces write_all() to perform
// multiple partial ::write() calls; a concurrent drainer lets it complete, and
// the payload must arrive byte-for-byte with the exact declared size.
TEST(StreamWriterTest, FdLargeChunkRoundTrips) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    const size_t kPayloadLen = 1u << 20;  // 1 MB, well above any pipe buffer
    std::vector<std::byte> data(kPayloadLen);
    for (size_t ndx = 0; ndx < kPayloadLen; ++ndx) {
        data[ndx] = static_cast<std::byte>((ndx * 31u + 7u) & 0xFFu);
    }

    std::vector<std::byte> received;
    bool got_end = false;
    std::thread reader([&]() {
        wire::Header hdr{};
        Buffer payload;
        if (!read_one_message(fds[0], hdr, payload)) {
            return;
        }
        if (hdr.type != wire::MsgType::stream) {
            return;
        }
        received.assign(payload.data(), payload.data() + payload.size());

        wire::Header end_hdr{};
        Buffer end_payload;
        if (read_one_message(fds[0], end_hdr, end_payload) &&
            end_hdr.type == wire::MsgType::stream_end) {
            got_end = true;
        }
    });

    bool write_ok = true;
    try {
        StreamWriter w(fds[1], 3);
        Buffer chunk;
        chunk.write(data.data(), data.size());
        w.write(chunk);
        w.end();
    } catch (...) {
        write_ok = false;
    }
    ::close(fds[1]);  // always close so the reader observes EOF and never hangs
    reader.join();
    ::close(fds[0]);

    EXPECT_TRUE(write_ok);
    ASSERT_EQ(received.size(), kPayloadLen);
    EXPECT_TRUE(got_end);
    EXPECT_EQ(received, data);
}

// Chunks exactly at (4096) and one past (4097) the Buffer small-buffer cap must
// round-trip intact through the fd writer, catching any off-by-one at the
// inline/heap transition. Total bytes stay under the pipe buffer, so no drainer
// thread is needed.
TEST(StreamWriterTest, FdChunkSizeBoundaries) {
    for (size_t len : {size_t{4096}, size_t{4097}}) {
        int fds[2];
        ASSERT_EQ(::pipe(fds), 0);

        std::vector<std::byte> data(len);
        for (size_t ndx = 0; ndx < len; ++ndx) {
            data[ndx] = static_cast<std::byte>((ndx * 7u + 1u) & 0xFFu);
        }

        {
            StreamWriter w(fds[1], 4);
            Buffer chunk;
            chunk.write(data.data(), data.size());
            w.write(chunk);
            w.end();
        }
        ::close(fds[1]);

        wire::Header hdr{};
        Buffer payload;
        ASSERT_TRUE(read_one_message(fds[0], hdr, payload));
        EXPECT_EQ(hdr.type, wire::MsgType::stream);
        EXPECT_EQ(hdr.payload_size, static_cast<u32>(len));
        ASSERT_EQ(payload.size(), len);
        std::vector<std::byte> got(payload.data(), payload.data() + payload.size());
        EXPECT_EQ(got, data);

        wire::Header end_hdr{};
        Buffer end_payload;
        ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
        EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);

        ::close(fds[0]);
    }
}

// An empty chunk written mid-stream produces a real stream message with
// payload_size == 0, and does not disturb the following non-empty chunk.
TEST(StreamWriterTest, FdEmptyChunkThenNonEmpty) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    {
        StreamWriter w(fds[1], 8);
        Buffer empty;
        w.write(empty);
        Buffer chunk;
        encode_i32(chunk, 314);
        w.write(chunk);
        w.end();
    }
    ::close(fds[1]);

    wire::Header hdr1{};
    Buffer p1;
    ASSERT_TRUE(read_one_message(fds[0], hdr1, p1));
    EXPECT_EQ(hdr1.type, wire::MsgType::stream);
    EXPECT_EQ(hdr1.sequence_id, 8u);
    EXPECT_EQ(hdr1.payload_size, 0u);

    wire::Header hdr2{};
    Buffer p2;
    ASSERT_TRUE(read_one_message(fds[0], hdr2, p2));
    EXPECT_EQ(hdr2.type, wire::MsgType::stream);
    EXPECT_EQ(hdr2.sequence_id, 8u);
    EXPECT_EQ(decode_i32(p2), 314);

    wire::Header end_hdr{};
    Buffer end_payload;
    ASSERT_TRUE(read_one_message(fds[0], end_hdr, end_payload));
    EXPECT_EQ(end_hdr.type, wire::MsgType::stream_end);
    EXPECT_EQ(end_hdr.sequence_id, 8u);

    ::close(fds[0]);
}

// =============================================================================
// StreamReader iteration contract -- gap: stream-reader-iteration-contract
// =============================================================================

// chunk() on an empty reader throws on both the mutable and const overloads.
TEST(StreamReaderTest, ChunkOnEmptyThrows) {
    StreamReader reader(1);
    EXPECT_THROW(reader.chunk(), ServiceError);
    const StreamReader& cref = reader;
    EXPECT_THROW(cref.chunk(), ServiceError);
}

// Once the single chunk is consumed, next() is false and chunk() throws.
TEST(StreamReaderTest, ChunkAfterExhaustionThrows) {
    StreamReader reader(1);
    Buffer c;
    encode_i32(c, 7);
    reader.add_chunk(std::move(c));
    reader.set_complete();

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(decode_i32(reader.chunk()), 7);  // consumes the only chunk
    EXPECT_FALSE(reader.next());
    EXPECT_THROW(reader.chunk(), ServiceError);
}

// chunk() is what advances the cursor; next() is only advisory. Calling chunk()
// repeatedly without next() still walks the chunks in order.
TEST(StreamReaderTest, ChunkConsumesWithoutNext) {
    StreamReader reader(1);
    for (i32 value : {0, 1, 2}) {
        Buffer c;
        encode_i32(c, value);
        reader.add_chunk(std::move(c));
    }
    reader.set_complete();

    EXPECT_EQ(decode_i32(reader.chunk()), 0);
    EXPECT_EQ(decode_i32(reader.chunk()), 1);
    EXPECT_EQ(decode_i32(reader.chunk()), 2);
    EXPECT_FALSE(reader.next());
    EXPECT_THROW(reader.chunk(), ServiceError);
}

// next() does not consume: calling it twice before chunk() is harmless and the
// chunk is still delivered exactly once.
TEST(StreamReaderTest, NextIsIdempotentBeforeConsume) {
    StreamReader reader(1);
    Buffer c;
    encode_i32(c, 99);
    reader.add_chunk(std::move(c));
    reader.set_complete();

    EXPECT_TRUE(reader.next());
    EXPECT_TRUE(reader.next());  // idempotent; does not advance
    EXPECT_EQ(decode_i32(reader.chunk()), 99);
    EXPECT_FALSE(reader.next());
}
