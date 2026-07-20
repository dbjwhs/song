// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/process.hpp>
#include <song/transport.hpp>
#include <song/wire.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <csignal>

using namespace song;

// Helper to get path to test service executables
static std::string get_test_service_path(const std::string& name) {
    // Look in build/examples directory
    std::filesystem::path base = std::filesystem::current_path();

    // Try a few common locations
    std::vector<std::filesystem::path> candidates = {
        base / "examples" / name,
        base / ".." / "examples" / name,
        base.parent_path() / "examples" / name
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    // Fall back to assuming we're in build directory
    return (base / "examples" / name).string();
}

// =============================================================================
// ServiceProcess Spawn
// =============================================================================

TEST(ProcessTest, SpawnEchoService) {
    std::string echo_path = get_test_service_path("echo_service");

    // Skip test if echo_service not found
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found at " << echo_path;
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    EXPECT_GT(proc.pid(), 0);
    EXPECT_TRUE(proc.alive());
    EXPECT_EQ(proc.negotiated_version(), wire::kCurrentVersion);

    proc.terminate();
    EXPECT_FALSE(proc.alive());
}

TEST(ProcessTest, SpawnFailsForNonexistent) {
    EXPECT_THROW(
        ServiceProcess::spawn("/nonexistent/path/to/service"),
        ServiceError
    );
}

// =============================================================================
// ServiceProcess Communication
// =============================================================================

TEST(ProcessTest, SendAndReceive) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    // Create a call message
    Buffer args;
    encode_string(args, "Hello, Test!");

    Buffer call_msg = wire::create_call_message(1, 1, 1, args);  // seq=1, service=1, method=1 (echo)
    proc.send(call_msg);

    // Receive response
    Buffer response;
    bool received = proc.receive(response, 5000);
    EXPECT_TRUE(received);

    // Decode response
    auto hdr = wire::decode_header_validated(response);
    EXPECT_EQ(hdr.type, wire::MsgType::result);
    EXPECT_EQ(hdr.sequence_id, 1);

    // The payload should be our echoed string
    std::string result = decode_string(response);
    EXPECT_EQ(result, "Hello, Test!");

    proc.terminate();
}

// =============================================================================
// ServiceProcess Lifecycle
// =============================================================================

TEST(ProcessTest, Terminate) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    EXPECT_TRUE(proc.alive());

    proc.terminate();
    EXPECT_FALSE(proc.alive());
}

TEST(ProcessTest, MoveConstructor) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc1 = ServiceProcess::spawn(echo_path.c_str());
    pid_t original_pid = proc1.pid();

    ServiceProcess proc2(std::move(proc1));

    EXPECT_EQ(proc1.pid(), -1);
    EXPECT_EQ(proc2.pid(), original_pid);
    EXPECT_TRUE(proc2.alive());

    proc2.terminate();
}

TEST(ProcessTest, MoveAssignment) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc1 = ServiceProcess::spawn(echo_path.c_str());
    ServiceProcess proc2 = ServiceProcess::spawn(echo_path.c_str());

    pid_t pid1 = proc1.pid();

    proc2 = std::move(proc1);

    EXPECT_EQ(proc1.pid(), -1);
    EXPECT_EQ(proc2.pid(), pid1);

    proc2.terminate();
}

// Regression: a move must preserve negotiated capabilities, not just pid/version.
// Before the fix, the move ctor and move-assign copied pid_, reusable_,
// negotiated_version_, and methods_ but silently dropped peer_capabilities_ and
// negotiated_capabilities_. Because ServiceManager builds every service via
// make_unique<ServiceProcess>(ServiceProcess::spawn(...)) - which move-constructs
// the heap object - any manager-started service reported zero capabilities
// regardless of what it advertised. echo_service advertises no capabilities, so
// the existing move tests could not observe the drop; caps_service does.
TEST(ProcessTest, MovePreservesCapabilities) {
    std::string caps_path = get_test_service_path("caps_service");
    if (!std::filesystem::exists(caps_path)) {
        GTEST_SKIP() << "caps_service not found at " << caps_path;
    }

    ServiceProcess proc1 = ServiceProcess::spawn(caps_path.c_str());

    const u32 peer = proc1.peer_capabilities();
    const u32 negotiated = proc1.negotiated_capabilities();
    const u16 version = proc1.negotiated_version();
    const size_t method_count = proc1.methods().size();
    ASSERT_NE(peer, 0u) << "fixture must advertise a non-zero capability set";
    EXPECT_TRUE(proc1.has_capability(wire::Capability::streaming));

    // Move-construct: capabilities (and version/methods) must survive.
    ServiceProcess proc2(std::move(proc1));
    EXPECT_EQ(proc2.peer_capabilities(), peer);
    EXPECT_EQ(proc2.negotiated_capabilities(), negotiated);
    EXPECT_EQ(proc2.negotiated_version(), version);
    EXPECT_EQ(proc2.methods().size(), method_count);
    EXPECT_TRUE(proc2.has_capability(wire::Capability::streaming));

    // The moved-from object must be reset so it no longer reports capabilities.
    EXPECT_EQ(proc1.peer_capabilities(), 0u);
    EXPECT_EQ(proc1.negotiated_capabilities(), 0u);

    // Move-assign onto an already-spawned process: capabilities must survive too.
    ServiceProcess proc3 = ServiceProcess::spawn(caps_path.c_str());
    proc3 = std::move(proc2);
    EXPECT_EQ(proc3.peer_capabilities(), peer);
    EXPECT_EQ(proc3.negotiated_capabilities(), negotiated);
    EXPECT_TRUE(proc3.has_capability(wire::Capability::streaming));
    EXPECT_EQ(proc2.peer_capabilities(), 0u);

    proc3.terminate();
}

// Regression: writing to a service that has died must throw ServiceError, not
// raise SIGPIPE and kill this (host) process. ServiceProcess::spawn() installs a
// SIGPIPE-ignore so Pipe::write surfaces the broken pipe as -1/EPIPE, which
// send() turns into a ServiceError. Reaching the assertion at all proves the
// process was not terminated by a signal.
TEST(ProcessTest, SendToDeadServiceThrowsNotSignal) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    // Kill the service outright; proc still believes pid_ > 0 (not yet reaped).
    ::kill(proc.pid(), SIGKILL);

    // Wait until the OS has reaped the child so its pipe read end is closed.
    // alive() performs the waitpid() reap.
    for (int ndx = 0; ndx < 200 && proc.alive(); ++ndx) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    Buffer args;
    encode_string(args, "hello");
    Buffer msg = wire::create_call_message(1, 1, 1, args);

    bool threw = false;
    try {
        // The read end is gone; a write must fail. Loop a few times in case the
        // very first write still lands in a not-yet-torn-down buffer.
        for (int ndx = 0; ndx < 100 && !threw; ++ndx) {
            proc.send(msg);
        }
    } catch (const ServiceError&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "send() to a dead service must throw, not raise SIGPIPE";

    proc.terminate();
}

// =============================================================================
// Method List / Capability Exchange
// =============================================================================

TEST(ProcessTest, MethodList) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());

    const auto& methods = proc.methods();

    // Echo service registers 3 methods: echo(1), add(2), double_all(3)
    EXPECT_EQ(methods.size(), 3);

    // Verify method 1,1 exists
    bool found = false;
    for (const auto& m : methods) {
        if (m.service_id == 1 && m.method_id == 1) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);

    proc.terminate();
}

// =============================================================================
// ServiceConnection
// =============================================================================

TEST(ProcessTest, ConnectionCall) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    // Call echo method
    Buffer args;
    encode_string(args, "Connection Test");

    Buffer result = conn.call(1, 1, args);
    std::string response = decode_string(result);
    EXPECT_EQ(response, "Connection Test");

    proc.terminate();
}

TEST(ProcessTest, ConnectionSupports) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    // Echo service has methods 1,2,3 on service 1
    EXPECT_TRUE(conn.supports(1, 1));
    EXPECT_TRUE(conn.supports(1, 2));
    EXPECT_TRUE(conn.supports(1, 3));
    EXPECT_FALSE(conn.supports(1, 99));  // Non-existent method
    EXPECT_FALSE(conn.supports(99, 1));  // Non-existent service

    proc.terminate();
}

TEST(ProcessTest, ConnectionAddMethod) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    // Call add method (service=1, method=2)
    Buffer args;
    encode_i32(args, 40);
    encode_i32(args, 2);

    Buffer result = conn.call(1, 2, args);
    i32 sum = decode_i32(result);
    EXPECT_EQ(sum, 42);

    proc.terminate();
}

TEST(ProcessTest, ConnectionMultipleCalls) {
    std::string echo_path = get_test_service_path("echo_service");
    if (!std::filesystem::exists(echo_path)) {
        GTEST_SKIP() << "echo_service not found";
    }

    ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
    ServiceConnection conn(&proc);

    for (int ndx = 0; ndx < 10; ++ndx) {
        Buffer args;
        encode_i32(args, ndx);
        encode_i32(args, ndx);

        Buffer result = conn.call(1, 2, args);  // add method
        i32 sum = decode_i32(result);
        EXPECT_EQ(sum, ndx + ndx);
    }

    proc.terminate();
}

// =============================================================================
// ServiceProcess: alive()/terminate() after a natural (unreaped) death
// =============================================================================

// alive() is const but performs the waitpid() reap when it observes the child
// has exited, and clears pid_ (mutable) at that instant so a later terminate()
// cannot signal a stale/reused pid. Repeated alive() calls must stay false (no
// ECHILD-driven flip back to true / double-reap misbehavior), and terminate() on
// the already-reaped handle must be a prompt no-op.
TEST(ProcessTest, AliveIsStableAfterNaturalDeath) {
  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
  const pid_t original_pid = proc.pid();
  ASSERT_GT(original_pid, 0);

  // Kill the child out from under proc. The first alive() that observes the
  // exit performs the reap.
  ::kill(original_pid, SIGKILL);
  for (int ndx = 0; ndx < 200 && proc.alive(); ++ndx) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  EXPECT_FALSE(proc.alive());
  EXPECT_FALSE(proc.alive());  // double-reap must not misbehave
  EXPECT_EQ(proc.pid(), -1);   // alive() cleared pid_ the instant it reaped

  // terminate() is now a no-op: it must not signal the stale pid nor busy-wait
  // the full graceful-shutdown second on an already-gone child.
  const auto start = std::chrono::steady_clock::now();
  proc.terminate();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
  EXPECT_EQ(proc.pid(), -1);
  EXPECT_FALSE(proc.alive());
}

// =============================================================================
// ServiceProcess: dead-service send/receive contracts
// =============================================================================

// After terminate(), pid_ is -1: send() must throw (not silently no-op) and
// receive() must return false immediately without blocking on the (default -1)
// timeout.
TEST(ProcessTest, SendAndReceiveAfterTerminate) {
  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
  proc.terminate();
  ASSERT_EQ(proc.pid(), -1);

  Buffer args;
  encode_string(args, "hello");
  Buffer msg = wire::create_call_message(1, 1, 1, args);

  EXPECT_THROW(proc.send(msg), ServiceError);

  Buffer resp;
  EXPECT_FALSE(proc.receive(resp));  // pid_ <= 0 -> false, no block
}

// =============================================================================
// ServiceConnection: move ctor / move assignment (local, proc-backed)
// =============================================================================

// Move-construct a local connection: the process pointer transfers to the
// destination, the source is nulled out and reports is_remote()==false, and the
// destination remains fully usable.
TEST(ProcessTest, MoveConstructLocalConnection) {
  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
  ServiceConnection src(&proc);

  // Advance internal state with a real call before moving.
  Buffer warm;
  encode_string(warm, "warm");
  Buffer warm_result = src.call(1, 1, warm);
  EXPECT_EQ(decode_string(warm_result), "warm");

  ServiceConnection dst(std::move(src));

  EXPECT_EQ(dst.process(), &proc);
  EXPECT_FALSE(dst.is_remote());
  EXPECT_EQ(src.process(), nullptr);
  EXPECT_FALSE(src.is_remote());

  // Destination must still be able to talk to the service.
  Buffer args;
  encode_string(args, "moved");
  Buffer result = dst.call(1, 1, args);
  EXPECT_EQ(decode_string(result), "moved");

  proc.terminate();
}

// Move-assign one local connection over another: destination adopts the source
// process pointer, source is nulled, and the destination talks to the moved-in
// process.
TEST(ProcessTest, MoveAssignLocalConnection) {
  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc1 = ServiceProcess::spawn(echo_path.c_str());
  ServiceProcess proc2 = ServiceProcess::spawn(echo_path.c_str());

  ServiceConnection src(&proc1);
  ServiceConnection dst(&proc2);

  dst = std::move(src);

  EXPECT_EQ(dst.process(), &proc1);
  EXPECT_EQ(src.process(), nullptr);

  Buffer args;
  encode_string(args, "reassigned");
  Buffer result = dst.call(1, 1, args);  // routed to proc1
  EXPECT_EQ(decode_string(result), "reassigned");

  proc1.terminate();
  proc2.terminate();
}

// Self-move-assignment is guarded by `if (this != &other)` and must leave the
// connection fully usable. The pointer indirection keeps the syntactic form
// clear of -Wself-move while still exercising the self-assignment guard.
TEST(ProcessTest, SelfMoveAssignLocalConnection) {
  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
  ServiceConnection conn(&proc);

  ServiceConnection* self = &conn;
  conn = std::move(*self);

  EXPECT_EQ(conn.process(), &proc);
  EXPECT_FALSE(conn.is_remote());

  Buffer args;
  encode_string(args, "still-here");
  Buffer result = conn.call(1, 1, args);
  EXPECT_EQ(decode_string(result), "still-here");

  proc.terminate();
}

// =============================================================================
// ServiceConnection: empty / dead connection contracts
// =============================================================================

// A default-constructed connection has neither a process nor a transport: it is
// not remote, exposes no process, and every RPC entry point throws ServiceError
// via the "no transport or process" guard.
TEST(ProcessTest, DefaultConnectionRejectsCalls) {
  ServiceConnection conn;

  EXPECT_FALSE(conn.is_remote());
  EXPECT_EQ(conn.process(), nullptr);

  Buffer args;
  encode_i32(args, 1);

  EXPECT_THROW(conn.call(1, 1, args), ServiceError);
  EXPECT_THROW(conn.call_oneway(1, 1, args), ServiceError);
  EXPECT_THROW(conn.call_streaming(1, 1, args), ServiceError);
}

// A connection whose backing process has been terminated must reject calls via
// the proc_->alive() guard ("Service not running"), rather than blocking or
// crashing.
TEST(ProcessTest, DeadProcessConnectionRejectsCall) {
  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
  ServiceConnection conn(&proc);
  proc.terminate();

  Buffer args;
  encode_i32(args, 1);
  EXPECT_THROW(conn.call(1, 1, args), ServiceError);
}

// release_object is fire-and-forget: a null (object_id == 0) reference is a
// no-op, a live reference with no transport/process quietly does nothing, and a
// dead-process reference is best-effort (the guard skips the send). None of
// these may throw.
TEST(ProcessTest, ReleaseObjectIsBestEffort) {
  // Null reference on an empty connection: early-return no-op.
  ServiceConnection empty;
  EXPECT_NO_THROW(empty.release_object(1, 0));
  // Non-null reference with no transport/process: builds the message, sends
  // nothing, does not throw.
  EXPECT_NO_THROW(empty.release_object(1, 5));

  std::string echo_path = get_test_service_path("echo_service");
  if (!std::filesystem::exists(echo_path)) {
    GTEST_SKIP() << "echo_service not found";
  }

  ServiceProcess proc = ServiceProcess::spawn(echo_path.c_str());
  ServiceConnection conn(&proc);
  proc.terminate();

  // Dead process: alive() guard skips the send, best-effort try/catch swallows.
  EXPECT_NO_THROW(conn.release_object(1, 5));
}
