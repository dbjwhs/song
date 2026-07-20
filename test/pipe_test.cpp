// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/pipe.hpp>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

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

// =============================================================================
// read_timeout: EOF (writer closed) vs Timeout
// =============================================================================

// When the writer end is fully closed, poll() reports POLLHUP and the
// subsequent ::read returns 0 (EOF). read_timeout must surface that 0 promptly
// and must NOT report it as a timeout (errno left at ETIMEDOUT).
TEST(PipeTest, ReadTimeoutReturnsZeroOnEof) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    write_pipe.close();  // writer fully closed -> read side sees EOF

    char buffer[10];
    errno = 0;
    auto start = std::chrono::steady_clock::now();
    ssize_t n = read_pipe.read_timeout(buffer, 10, 1000);  // generous timeout
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(n, 0);  // EOF, not -1
    EXPECT_NE(errno, ETIMEDOUT);

    // Must return on EOF immediately, not after the full 1000ms timeout.
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 200);
}

// Buffered data must be drained first, then EOF reported, through the poll path.
TEST(PipeTest, ReadTimeoutDrainsThenEof) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    write_pipe.write("OK", 2);
    write_pipe.close();  // close after writing so data is drained before EOF

    char buffer[10] = {0};
    ssize_t n = read_pipe.read_timeout(buffer, 2, 1000);
    EXPECT_EQ(n, 2);
    EXPECT_STREQ(buffer, "OK");

    ssize_t n2 = read_pipe.read_timeout(buffer, 2, 1000);
    EXPECT_EQ(n2, 0);  // EOF after the buffered bytes are consumed
}

// =============================================================================
// read_timeout: Boundary timeout values (0 and negative)
// =============================================================================

// A zero timeout is a non-blocking probe: with no data and an open writer it
// must return -1/ETIMEDOUT essentially immediately.
TEST(PipeTest, ReadTimeoutZeroNonBlockingEmpty) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();  // writer stays open

    char buffer[10];
    errno = 0;
    auto start = std::chrono::steady_clock::now();
    ssize_t n = read_pipe.read_timeout(buffer, 10, 0);
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, ETIMEDOUT);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 100);  // must not block
}

// A zero timeout with data already available takes the data-ready fast path.
TEST(PipeTest, ReadTimeoutZeroNonBlockingDataReady) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    write_pipe.write("abc", 3);

    char buffer[10] = {0};
    ssize_t n = read_pipe.read_timeout(buffer, 3, 0);
    EXPECT_EQ(n, 3);
    EXPECT_STREQ(buffer, "abc");
}

// A negative timeout is passed straight through to poll(), meaning block
// indefinitely. With a writer supplying data shortly it must block until the
// data arrives and then return it (documents the infinite-wait contract).
TEST(PipeTest, ReadTimeoutNegativeBlocksUntilData) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    std::thread writer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        write_pipe.write("Z", 1);
    });

    char buffer[10] = {0};
    ssize_t n = read_pipe.read_timeout(buffer, 1, -1);  // negative => block

    writer.join();

    EXPECT_EQ(n, 1);
    EXPECT_STREQ(buffer, "Z");
}

// =============================================================================
// Move-Assignment: target's existing fds are released
// =============================================================================

// Move-assigning into a live read pipe must close the target's previous read
// fd rather than leaking it.
TEST(PipeTest, MoveAssignmentClosesTargetReadFd) {
    auto [r1, w1] = Pipe::create_pair();
    auto [r2, w2] = Pipe::create_pair();

    int old_fd = r2.read_fd();
    ASSERT_GE(old_fd, 0);

    r2 = std::move(r1);

    // The fd that r2 previously owned must have been closed by operator=.
    errno = 0;
    EXPECT_EQ(fcntl(old_fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

// Same guarantee for the write side.
TEST(PipeTest, MoveAssignmentClosesTargetWriteFd) {
    auto [r1, w1] = Pipe::create_pair();
    auto [r2, w2] = Pipe::create_pair();

    int old_fd = w2.write_fd();
    ASSERT_GE(old_fd, 0);

    w2 = std::move(w1);

    errno = 0;
    EXPECT_EQ(fcntl(old_fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

// Repeatedly move-assigning fresh pipes into one long-lived Pipe must not leak
// fds: if the target's old fd were not closed on each assignment, the fd
// numbers would climb monotonically and eventually exhaust the fd table.
TEST(PipeTest, RepeatedMoveAssignmentDoesNotLeakFds) {
    Pipe target;
    int first_fd = -1;
    int max_fd = -1;
    for (int ndx = 0; ndx < 4000; ++ndx) {
        auto [r, w] = Pipe::create_pair();
        int rfd = r.read_fd();
        if (first_fd < 0) {
            first_fd = rfd;
        }
        if (rfd > max_fd) {
            max_fd = rfd;
        }
        target = std::move(r);
        // w destructs here, closing its write fd.
    }
    EXPECT_TRUE(target.valid());
    // With move-assign correctly closing the target's previous fd, each
    // iteration releases as many fds as it allocates, so the kernel keeps
    // recycling the same low fd numbers.
    EXPECT_LT(max_fd, first_fd + 50);
}

// =============================================================================
// FD_CLOEXEC
// =============================================================================

// create_pair() must set FD_CLOEXEC on both fds so they do not leak across
// the fork/exec used to spawn services.
TEST(PipeTest, CreatePairSetsCloexec) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    int rflags = fcntl(read_pipe.read_fd(), F_GETFD);
    ASSERT_NE(rflags, -1);
    EXPECT_NE(rflags & FD_CLOEXEC, 0);

    int wflags = fcntl(write_pipe.write_fd(), F_GETFD);
    ASSERT_NE(wflags, -1);
    EXPECT_NE(wflags & FD_CLOEXEC, 0);
}

// The public two-arg Pipe(read_fd, write_fd) constructor adopts fds verbatim
// and deliberately does NOT set FD_CLOEXEC. Document that contract distinction
// so callers do not assume the flag is applied for them.
TEST(PipeTest, TwoArgConstructorDoesNotSetCloexec) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    Pipe pipe(fds[0], fds[1]);  // adopts fds; Pipe dtor closes them

    int rflags = fcntl(pipe.read_fd(), F_GETFD);
    ASSERT_NE(rflags, -1);
    EXPECT_EQ(rflags & FD_CLOEXEC, 0);  // plain ::pipe() fds are not CLOEXEC

    int wflags = fcntl(pipe.write_fd(), F_GETFD);
    ASSERT_NE(wflags, -1);
    EXPECT_EQ(wflags & FD_CLOEXEC, 0);
}

// =============================================================================
// Partial-read and zero-length IO contract corners
// =============================================================================

// read() forwards a single ::read and does not loop to fill the buffer, so
// requesting more than is available yields a short count. Callers must loop.
TEST(PipeTest, ReadReturnsShortCountWhenLessAvailable) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    write_pipe.write("xyz", 3);

    char buffer[64] = {0};
    ssize_t n = read_pipe.read(buffer, 64);  // request far more than the 3 sent
    EXPECT_EQ(n, 3);
    EXPECT_STREQ(buffer, "xyz");
}

// read(buf, 0) returns 0 (zero requested) and must not consume buffered bytes.
// This documents the 0-return ambiguity relative to the EOF sentinel.
TEST(PipeTest, ZeroLengthReadReturnsZero) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    write_pipe.write("data", 4);

    char buffer[10] = {0};
    ssize_t n = read_pipe.read(buffer, 0);
    EXPECT_EQ(n, 0);

    // The 4 bytes remain buffered and readable.
    ssize_t n2 = read_pipe.read(buffer, 4);
    EXPECT_EQ(n2, 4);
    EXPECT_STREQ(buffer, "data");
}

// write(buf, 0) returns 0 and injects no bytes into the stream.
TEST(PipeTest, ZeroLengthWriteReturnsZero) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    ssize_t n = write_pipe.write("", 0);
    EXPECT_EQ(n, 0);

    // A subsequent real write is unaffected; no spurious bytes precede it.
    write_pipe.write("hi", 2);
    char buffer[10] = {0};
    ssize_t r = read_pipe.read(buffer, 2);
    EXPECT_EQ(r, 2);
    EXPECT_STREQ(buffer, "hi");
}

// A write larger than the pipe capacity blocks until a concurrent reader
// drains it, then returns the full length with the payload intact.
TEST(PipeTest, LargeWriteBlocksUntilReaderDrains) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();

    const size_t total = 256 * 1024;  // larger than the pipe buffer
    std::vector<char> payload(total, 'q');

    std::vector<char> received;
    received.reserve(total);
    std::thread reader([&]() {
        char buf[4096];
        size_t got = 0;
        while (got < total) {
            ssize_t r = read_pipe.read(buf, sizeof(buf));
            if (r <= 0) {
                break;
            }
            received.insert(received.end(), buf, buf + r);
            got += static_cast<size_t>(r);
        }
    });

    ssize_t written = write_pipe.write(payload.data(), payload.size());
    reader.join();

    EXPECT_EQ(written, static_cast<ssize_t>(total));
    EXPECT_EQ(received.size(), total);
    EXPECT_EQ(std::memcmp(received.data(), payload.data(), total), 0);
}

// =============================================================================
// Self Move-Assignment guard
// =============================================================================

// p = std::move(p) must be a no-op: the this!=&other guard prevents the object
// from closing its own fds. The pointer indirection defeats the compiler's
// self-move warning while still exercising the runtime guard.
TEST(PipeTest, SelfMoveAssignmentKeepsFdOpen) {
    auto [read_pipe, write_pipe] = Pipe::create_pair();
    int fd = read_pipe.read_fd();
    ASSERT_GE(fd, 0);

    Pipe* self = &read_pipe;
    read_pipe = std::move(*self);  // self-move via indirection

    EXPECT_TRUE(read_pipe.valid());
    EXPECT_EQ(read_pipe.read_fd(), fd);

    // The fd is still open, not accidentally closed.
    errno = 0;
    EXPECT_NE(fcntl(fd, F_GETFD), -1);

    // The pipe still works end-to-end after the self-move.
    write_pipe.write("Q", 1);
    char buffer[2] = {0};
    ssize_t n = read_pipe.read(buffer, 1);
    EXPECT_EQ(n, 1);
    EXPECT_STREQ(buffer, "Q");
}
