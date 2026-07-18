// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/pipe.hpp>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <thread>
#include <chrono>

using namespace song;

// =============================================================================
// Pipe Basic Operations
// =============================================================================

TEST(PipeTest, DefaultConstruction) {
    Pipe pipe;
    EXPECT_FALSE(pipe.valid());
    EXPECT_EQ(pipe.read_fd(), -1);
    EXPECT_EQ(pipe.write_fd(), -1);
}

TEST(PipeTest, CreatePair) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    EXPECT_TRUE(read_pipe.valid());
    EXPECT_TRUE(write_pipe.valid());
    EXPECT_GE(read_pipe.read_fd(), 0);
    EXPECT_GE(write_pipe.write_fd(), 0);
}

TEST(PipeTest, WriteAndRead) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    const char* message = "Hello, Pipe!";
    ssize_t written = write_pipe.write(message, strlen(message));
    EXPECT_EQ(written, static_cast<ssize_t>(strlen(message)));

    char buffer[64] = {0};
    ssize_t read_bytes = read_pipe.read(buffer, strlen(message));
    EXPECT_EQ(read_bytes, static_cast<ssize_t>(strlen(message)));
    EXPECT_STREQ(buffer, message);
}

TEST(PipeTest, MultipleWrites) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    write_pipe.write("AAA", 3);
    write_pipe.write("BBB", 3);
    write_pipe.write("CCC", 3);

    char buffer[10] = {0};
    read_pipe.read(buffer, 9);
    EXPECT_STREQ(buffer, "AAABBBCCC");
}

// =============================================================================
// Pipe Closure
// =============================================================================

TEST(PipeTest, Close) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    EXPECT_TRUE(read_pipe.valid());
    read_pipe.close();
    EXPECT_FALSE(read_pipe.valid());
}

TEST(PipeTest, CloseRead) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    read_pipe.close_read();
    EXPECT_EQ(read_pipe.read_fd(), -1);
}

TEST(PipeTest, CloseWrite) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    write_pipe.close_write();
    EXPECT_EQ(write_pipe.write_fd(), -1);
}

TEST(PipeTest, ReadAfterWriterCloses) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    write_pipe.write("data", 4);
    write_pipe.close();  // Close writer

    char buffer[10] = {0};
    ssize_t n = read_pipe.read(buffer, 4);
    EXPECT_EQ(n, 4);
    EXPECT_STREQ(buffer, "data");

    // Next read should return EOF (0)
    n = read_pipe.read(buffer, 1);
    EXPECT_EQ(n, 0);
}

// =============================================================================
// Move Semantics
// =============================================================================

TEST(PipeTest, MoveConstructor) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    int original_read_fd = read_pipe.read_fd();

    Pipe moved_pipe(std::move(read_pipe));

    EXPECT_FALSE(read_pipe.valid());  // Original should be invalid
    EXPECT_TRUE(moved_pipe.valid());
    EXPECT_EQ(moved_pipe.read_fd(), original_read_fd);
}

TEST(PipeTest, MoveAssignment) {
    auto [read_pipe1, write_pipe1] = Pipe::create_pair();
    auto [read_pipe2, write_pipe2] = Pipe::create_pair();

    int fd_to_keep = read_pipe1.read_fd();

    read_pipe2 = std::move(read_pipe1);

    EXPECT_FALSE(read_pipe1.valid());
    EXPECT_EQ(read_pipe2.read_fd(), fd_to_keep);
}

// =============================================================================
// Timeout
// =============================================================================

TEST(PipeTest, ReadTimeoutExpires) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    char buffer[10];
    auto start = std::chrono::steady_clock::now();
    ssize_t n = read_pipe.read_timeout(buffer, 10, 100);  // 100ms timeout
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(n, -1);  // Should return -1 on timeout
    EXPECT_EQ(errno, ETIMEDOUT);

    // Should have waited approximately 100ms
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 90);
    EXPECT_LE(duration.count(), 200);
}

TEST(PipeTest, ReadTimeoutSucceeds) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    // Write data in a separate thread after a short delay
    std::thread writer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        write_pipe.write("OK", 2);
    });

    char buffer[10] = {0};
    ssize_t n = read_pipe.read_timeout(buffer, 2, 500);  // 500ms timeout

    writer.join();

    EXPECT_EQ(n, 2);
    EXPECT_STREQ(buffer, "OK");
}

// =============================================================================
// Error Handling
// =============================================================================

TEST(PipeTest, ReadFromInvalidPipe) {
    Pipe pipe;  // Invalid pipe

    char buffer[10];
    ssize_t n = pipe.read(buffer, 10);
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(PipeTest, WriteToInvalidPipe) {
    Pipe pipe;  // Invalid pipe

    ssize_t n = pipe.write("test", 4);
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(PipeTest, ReadTimeoutFromInvalidPipe) {
    Pipe pipe;  // Invalid pipe

    char buffer[10];
    ssize_t n = pipe.read_timeout(buffer, 10, 100);
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EBADF);
}

// Pipe is a thin fd wrapper and provides no SIGPIPE protection of its own; the
// caller (or, in practice, Song's spawn()/run() entry points) must ignore
// SIGPIPE. With SIGPIPE ignored, a write to a pipe whose read end is fully
// closed returns -1/EPIPE rather than terminating the process. This documents
// that contract and guards it against regression.
TEST(PipeTest, WriteToClosedReadEndReturnsEpipeWhenSigpipeIgnored) {
    struct sigaction prev{};
    struct sigaction ign{};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    ASSERT_EQ(sigaction(SIGPIPE, &ign, &prev), 0);

    auto [read_pipe, write_pipe] = Pipe::create_pair();
    read_pipe.close();  // fully close the read end

    const char data = 'x';
    errno = 0;
    ssize_t n = write_pipe.write(&data, 1);
    int saved_errno = errno;

    // Restore the previous disposition so other tests are unaffected.
    sigaction(SIGPIPE, &prev, nullptr);

    EXPECT_EQ(n, -1);
    EXPECT_EQ(saved_errno, EPIPE);
}
