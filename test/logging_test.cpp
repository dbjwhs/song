// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/logging.hpp>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <cstdio>
#include <unistd.h>

using namespace song;

// =============================================================================
// Test Fixtures
// =============================================================================

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear all handlers and reset state for each test
        Log::clear_handlers();
        Log::set_level(LogLevel::debug);
        captured_.clear();
    }

    void TearDown() override {
        // Restore default state
        Log::clear_handlers();
        init_logging();  // Re-register console handler
    }

    // Capture handler for testing
    void install_capture_handler() {
        Log::add_handler("capture", [this](const LogEntry& entry) {
            captured_.push_back({
                entry.level,
                std::string(entry.message),
                std::string(entry.file()),
                entry.line()
            });
        });
    }

    struct CapturedEntry {
        LogLevel level;
        std::string message;
        std::string file;
        uint32_t line;
    };

    std::vector<CapturedEntry> captured_;
};

// =============================================================================
// Basic Logging Tests
// =============================================================================

TEST_F(LoggingTest, LogDebug) {
    install_capture_handler();

    Log::debug("debug message");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::debug);
    EXPECT_EQ(captured_[0].message, "debug message");
}

TEST_F(LoggingTest, LogInfo) {
    install_capture_handler();

    Log::info("info message");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::info);
    EXPECT_EQ(captured_[0].message, "info message");
}

TEST_F(LoggingTest, LogWarn) {
    install_capture_handler();

    Log::warn("warning message");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::warn);
    EXPECT_EQ(captured_[0].message, "warning message");
}

TEST_F(LoggingTest, LogError) {
    install_capture_handler();

    Log::error("error message");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::error);
    EXPECT_EQ(captured_[0].message, "error message");
}

TEST_F(LoggingTest, LogFatal) {
    install_capture_handler();

    Log::fatal("fatal message");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::fatal);
    EXPECT_EQ(captured_[0].message, "fatal message");
}

TEST_F(LoggingTest, LogWithExplicitLevel) {
    install_capture_handler();

    Log::log(LogLevel::warn, "explicit level");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::warn);
}

// =============================================================================
// Level Filtering Tests
// =============================================================================

TEST_F(LoggingTest, LevelFilteringBlocksLowerLevels) {
    install_capture_handler();
    Log::set_level(LogLevel::warn);

    Log::debug("should not appear");
    Log::info("should not appear");
    Log::warn("should appear");
    Log::error("should appear");

    ASSERT_EQ(captured_.size(), 2);
    EXPECT_EQ(captured_[0].level, LogLevel::warn);
    EXPECT_EQ(captured_[1].level, LogLevel::error);
}

TEST_F(LoggingTest, LevelFilteringDebugAllowsAll) {
    install_capture_handler();
    Log::set_level(LogLevel::debug);

    Log::debug("d");
    Log::info("i");
    Log::warn("w");
    Log::error("e");
    Log::fatal("f");

    EXPECT_EQ(captured_.size(), 5);
}

TEST_F(LoggingTest, LevelFilteringFatalBlocksMost) {
    install_capture_handler();
    Log::set_level(LogLevel::fatal);

    Log::debug("d");
    Log::info("i");
    Log::warn("w");
    Log::error("e");
    Log::fatal("f");

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_EQ(captured_[0].level, LogLevel::fatal);
}

TEST_F(LoggingTest, IsEnabledReflectsLevel) {
    Log::set_level(LogLevel::warn);

    EXPECT_FALSE(Log::is_enabled(LogLevel::debug));
    EXPECT_FALSE(Log::is_enabled(LogLevel::info));
    EXPECT_TRUE(Log::is_enabled(LogLevel::warn));
    EXPECT_TRUE(Log::is_enabled(LogLevel::error));
    EXPECT_TRUE(Log::is_enabled(LogLevel::fatal));
}

TEST_F(LoggingTest, GetLevelReturnsCurrentLevel) {
    Log::set_level(LogLevel::error);
    EXPECT_EQ(Log::get_level(), LogLevel::error);

    Log::set_level(LogLevel::debug);
    EXPECT_EQ(Log::get_level(), LogLevel::debug);
}

// =============================================================================
// Handler Management Tests
// =============================================================================

TEST_F(LoggingTest, AddHandler) {
    int count = 0;
    Log::add_handler("counter", [&count](const LogEntry&) {
        ++count;
    });

    Log::info("test");

    EXPECT_EQ(count, 1);
}

TEST_F(LoggingTest, RemoveHandler) {
    int count = 0;
    Log::add_handler("counter", [&count](const LogEntry&) {
        ++count;
    });

    Log::info("first");
    EXPECT_EQ(count, 1);

    bool removed = Log::remove_handler("counter");
    EXPECT_TRUE(removed);

    Log::info("second");
    EXPECT_EQ(count, 1);  // Still 1, handler was removed
}

TEST_F(LoggingTest, RemoveNonexistentHandler) {
    bool removed = Log::remove_handler("does_not_exist");
    EXPECT_FALSE(removed);
}

TEST_F(LoggingTest, HasHandler) {
    EXPECT_FALSE(Log::has_handler("test"));

    Log::add_handler("test", [](const LogEntry&) {});

    EXPECT_TRUE(Log::has_handler("test"));
}

TEST_F(LoggingTest, ClearHandlers) {
    Log::add_handler("a", [](const LogEntry&) {});
    Log::add_handler("b", [](const LogEntry&) {});
    Log::add_handler("c", [](const LogEntry&) {});

    Log::clear_handlers();

    EXPECT_FALSE(Log::has_handler("a"));
    EXPECT_FALSE(Log::has_handler("b"));
    EXPECT_FALSE(Log::has_handler("c"));
}

TEST_F(LoggingTest, MultipleHandlers) {
    int count_a = 0;
    int count_b = 0;

    Log::add_handler("a", [&count_a](const LogEntry&) { ++count_a; });
    Log::add_handler("b", [&count_b](const LogEntry&) { ++count_b; });

    Log::info("test");

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
}

TEST_F(LoggingTest, ReplaceHandler) {
    int first_count = 0;
    int second_count = 0;

    Log::add_handler("test", [&first_count](const LogEntry&) { ++first_count; });
    Log::info("a");

    Log::add_handler("test", [&second_count](const LogEntry&) { ++second_count; });
    Log::info("b");

    EXPECT_EQ(first_count, 1);   // Only called before replacement
    EXPECT_EQ(second_count, 1);  // Called after replacement
}

// =============================================================================
// Source Location Tests
// =============================================================================

TEST_F(LoggingTest, SourceLocationCaptured) {
    install_capture_handler();

    Log::info("test");  // This line number should be captured

    ASSERT_EQ(captured_.size(), 1);
    EXPECT_GT(captured_[0].line, 0);
    // File should contain "logging_test.cpp"
    EXPECT_NE(captured_[0].file.find("logging_test"), std::string::npos);
}

// =============================================================================
// Level Name Tests
// =============================================================================

TEST_F(LoggingTest, LogLevelNames) {
    EXPECT_STREQ(log_level_name(LogLevel::debug), "DEBUG");
    EXPECT_STREQ(log_level_name(LogLevel::info), "INFO");
    EXPECT_STREQ(log_level_name(LogLevel::warn), "WARN");
    EXPECT_STREQ(log_level_name(LogLevel::error), "ERROR");
    EXPECT_STREQ(log_level_name(LogLevel::fatal), "FATAL");
}

// =============================================================================
// Built-in Handler Tests
// =============================================================================

TEST_F(LoggingTest, NullHandlerDiscards) {
    Log::add_handler("null", handlers::make_null_handler());

    // Should not crash
    Log::info("discarded");
    Log::error("also discarded");
}

TEST_F(LoggingTest, CallbackHandler) {
    std::vector<std::pair<LogLevel, std::string>> captured;

    Log::add_handler("callback", handlers::make_callback_handler(
        [&captured](LogLevel level, const char* msg) {
            captured.push_back({level, std::string(msg)});
        }
    ));

    Log::warn("callback test");

    ASSERT_EQ(captured.size(), 1);
    EXPECT_EQ(captured[0].first, LogLevel::warn);
    EXPECT_EQ(captured[0].second, "callback test");
}

// =============================================================================
// Initialization Tests
// =============================================================================

TEST_F(LoggingTest, InitLoggingIsIdempotent) {
    // init_logging() should be safe to call multiple times
    // but only registers console handler on first call
    init_logging();
    init_logging();
    init_logging();
    // No crash = success
}

TEST_F(LoggingTest, ManualConsoleHandlerRegistration) {
    // After clearing, user can manually re-add console handler
    Log::clear_handlers();
    EXPECT_FALSE(Log::has_handler("console"));

    Log::add_handler("console", handlers::make_console_handler());

    EXPECT_TRUE(Log::has_handler("console"));
}

TEST_F(LoggingTest, ConsoleHandlerPlugsInLikeUserHandler) {
    // This demonstrates that Song's console handler uses the same
    // add_handler() API as any user would
    Log::clear_handlers();

    // Song's built-in console handler
    Log::add_handler("console", handlers::make_console_handler());

    // Is equivalent to a user doing:
    Log::add_handler("my_console", [](const LogEntry& entry) {
        // Custom console output...
        (void)entry;
    });

    EXPECT_TRUE(Log::has_handler("console"));
    EXPECT_TRUE(Log::has_handler("my_console"));
}

// =============================================================================
// Handler Exception Safety Tests
// =============================================================================

TEST_F(LoggingTest, HandlerExceptionDoesNotCrash) {
    Log::add_handler("thrower", [](const LogEntry&) {
        throw std::runtime_error("handler exception");
    });

    install_capture_handler();

    // Should not crash, and capture handler should still be called
    Log::info("test");

    EXPECT_EQ(captured_.size(), 1);
}

// =============================================================================
// Integration: User Handler Plugs In Like Built-in
// =============================================================================

TEST_F(LoggingTest, UserHandlerSameAsBuiltIn) {
    // This test demonstrates that user handlers use exactly the same
    // interface as Song's built-in console handler.

    std::vector<std::string> messages;

    // User defines their handler just like Song defined console handler
    LogHandler my_handler = [&messages](const LogEntry& entry) {
        messages.push_back(std::string(entry.message));
    };

    // User registers it the same way
    Log::add_handler("my_handler", my_handler);

    Log::info("user message");

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0], "user message");
}

// =============================================================================
// Reentrancy: a handler may call Log APIs without deadlocking
// =============================================================================

// A handler that logs used to re-lock the registry mutex (handlers ran while it
// was held) and self-deadlock. The nested message is now dropped and the handler
// runs exactly once. A watchdog makes a regression fail rather than hang CI.
TEST_F(LoggingTest, ReentrantHandlerDoesNotDeadlock) {
    std::atomic<int> handler_calls{0};
    Log::add_handler("reenter", [&](const LogEntry&) {
        ++handler_calls;
        Log::info("nested message from within a handler");
    });

    std::atomic<bool> done{false};
    std::thread worker([&] {
        Log::warn("outer");
        done = true;
    });

    for (int i = 0; i < 300 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool completed = done.load();
    if (completed) {
        worker.join();
    } else {
        worker.detach();  // deadlocked; leak the thread so the assert can report
    }

    ASSERT_TRUE(completed) << "logging deadlocked on a reentrant handler";
    // The nested Log::info was dropped, so the handler ran once, not recursively.
    EXPECT_EQ(handler_calls.load(), 1);
}

// A handler that adds/removes handlers must not deadlock either: add_handler
// locks the same mutex that used to be held during dispatch.
TEST_F(LoggingTest, HandlerCanModifyHandlerSetWithoutDeadlock) {
    Log::add_handler("mutator", [&](const LogEntry&) {
        Log::add_handler("added_from_handler", [](const LogEntry&) {});
    });

    std::atomic<bool> done{false};
    std::thread worker([&] {
        Log::warn("trigger");
        done = true;
    });

    for (int i = 0; i < 300 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool completed = done.load();
    if (completed) {
        worker.join();
    } else {
        worker.detach();
    }

    EXPECT_TRUE(completed) << "logging deadlocked when a handler modified the handler set";
}


// =============================================================================
// Console Handler Output Format Tests
// =============================================================================

namespace {

// Capture whatever the callable writes to stderr and return it as a string.
// POSIX-only (this suite already relies on the POSIX toolchain elsewhere).
template <typename Fn>
std::string capture_stderr(Fn&& fn) {
  std::fflush(stderr);
  int saved_fd = ::dup(STDERR_FILENO);
  FILE* tmp = std::tmpfile();
  int tmp_fd = ::fileno(tmp);
  ::dup2(tmp_fd, STDERR_FILENO);

  fn();

  std::fflush(stderr);
  ::dup2(saved_fd, STDERR_FILENO);
  ::close(saved_fd);

  std::rewind(tmp);
  std::string out;
  char buf[512];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), tmp)) > 0) {
    out.append(buf, n);
  }
  std::fclose(tmp);
  return out;
}

}  // namespace

TEST_F(LoggingTest, ConsoleHandlerFormatNoColor) {
  LogHandler handler = handlers::make_console_handler(false);

  std::string output = capture_stderr([&handler] {
    LogEntry entry{LogLevel::info, "hello", std::source_location::current()};
    handler(entry);
  });

  // Level padded to 5 chars (" INFO"), message verbatim, open paren for location.
  EXPECT_NE(output.find("[ INFO] hello ("), std::string::npos);
  // Path is stripped to a basename, so no directory separators remain.
  EXPECT_EQ(output.find('/'), std::string::npos);
  EXPECT_NE(output.find("logging_test.cpp:"), std::string::npos);
  // use_colors=false must emit no ANSI escape sequences.
  EXPECT_EQ(output.find('\033'), std::string::npos);
  ASSERT_FALSE(output.empty());
  EXPECT_EQ(output.back(), '\n');
}

TEST_F(LoggingTest, ConsoleHandlerColorGuardWhenNotTty) {
  // stderr is redirected to a regular file inside capture_stderr, so
  // isatty() is false and colors must stay disabled even with use_colors=true.
  std::string output = capture_stderr([] {
    LogHandler handler = handlers::make_console_handler(true);
    LogEntry entry{LogLevel::error, "boom", std::source_location::current()};
    handler(entry);
  });

  EXPECT_NE(output.find("[ERROR] boom"), std::string::npos);
  EXPECT_EQ(output.find('\033'), std::string::npos);
}

TEST_F(LoggingTest, ConsoleHandlerPercentIsLiteral) {
  LogHandler handler = handlers::make_console_handler(false);

  std::string output = capture_stderr([&handler] {
    LogEntry entry{LogLevel::warn, "100% done %d", std::source_location::current()};
    handler(entry);
  });

  // Message flows through %.*s, so '%' must appear literally, not as a format spec.
  EXPECT_NE(output.find("[ WARN] 100% done %d"), std::string::npos);
}

TEST_F(LoggingTest, ConsoleHandlerOutOfRangeLevelDoesNotCrash) {
  LogHandler handler = handlers::make_console_handler(false);

  std::string output = capture_stderr([&handler] {
    LogEntry entry{static_cast<LogLevel>(9), "weird", std::source_location::current()};
    handler(entry);
  });

  // log_level_name() falls through to its default branch for an unknown level.
  EXPECT_NE(output.find("[UNKNOWN] weird"), std::string::npos);
}

// =============================================================================
// Concurrency Tests
// =============================================================================

TEST_F(LoggingTest, ConcurrentEmitAndHandlerMutation) {
  std::atomic<int> counter{0};
  Log::add_handler("counter", [&counter](const LogEntry&) {
    counter.fetch_add(1, std::memory_order_relaxed);
  });

  constexpr int kEmitThreads = 4;
  constexpr int kMutatorThreads = 4;
  constexpr int kIterations = 500;

  std::vector<std::thread> threads;

  for (int t = 0; t < kEmitThreads; ++t) {
    threads.emplace_back([] {
      for (int ndx = 0; ndx < kIterations; ++ndx) {
        Log::info("concurrent");
      }
    });
  }
  for (int t = 0; t < kMutatorThreads; ++t) {
    threads.emplace_back([t] {
      std::string name = "h" + std::to_string(t);
      for (int ndx = 0; ndx < kIterations; ++ndx) {
        Log::add_handler(name, [](const LogEntry&) {});
        Log::remove_handler(name);
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  // The "counter" handler is never removed, so every emit must reach it exactly
  // once -- a lost or double increment would indicate a torn dispatch snapshot.
  EXPECT_EQ(counter.load(), kEmitThreads * kIterations);
}

// =============================================================================
// Callback Handler Fidelity Tests
// =============================================================================

TEST_F(LoggingTest, CallbackHandlerTruncatesAtEmbeddedNull) {
  install_capture_handler();

  std::string cb_message = "unset";
  Log::add_handler("callback", handlers::make_callback_handler(
      [&cb_message](LogLevel, const char* msg) {
        cb_message = msg;  // C-string assignment stops at the first NUL
      }));

  std::string with_null("ab\0cd", 5);
  Log::info(std::string_view(with_null.data(), with_null.size()));

  ASSERT_EQ(captured_.size(), 1);
  // The raw LogEntry view preserves all five bytes...
  EXPECT_EQ(captured_[0].message.size(), 5);
  // ...but the c-string callback bridge truncates at the embedded NUL.
  EXPECT_EQ(cb_message, "ab");
}

TEST_F(LoggingTest, CallbackHandlerEmptyMessage) {
  bool called = false;
  std::string cb_message = "unset";
  Log::add_handler("callback", handlers::make_callback_handler(
      [&called, &cb_message](LogLevel, const char* msg) {
        called = true;
        cb_message = msg;  // valid pointer to a lone '\0'
      }));

  Log::info("");

  EXPECT_TRUE(called);
  EXPECT_EQ(cb_message, "");
}

// =============================================================================
// Level Name / Disable-All Tests
// =============================================================================

TEST_F(LoggingTest, LogLevelNameUnknownForOutOfRange) {
  EXPECT_STREQ(log_level_name(static_cast<LogLevel>(99)), "UNKNOWN");
  EXPECT_STREQ(log_level_name(static_cast<LogLevel>(-1)), "UNKNOWN");
  EXPECT_STREQ(log_level_name(static_cast<LogLevel>(5)), "UNKNOWN");
}

TEST_F(LoggingTest, AboveFatalSentinelSuppressesAllLevels) {
  install_capture_handler();

  // The header documents disabling everything via an above-fatal sentinel.
  Log::set_level(static_cast<LogLevel>(static_cast<int>(LogLevel::fatal) + 1));

  EXPECT_FALSE(Log::is_enabled(LogLevel::fatal));

  Log::debug("d");
  Log::info("i");
  Log::warn("w");
  Log::error("e");
  Log::fatal("f");

  EXPECT_EQ(captured_.size(), 0);
}

// =============================================================================
// Null / Empty Handler Tests
// =============================================================================

TEST_F(LoggingTest, EmptyFunctionHandlerIsSkipped) {
  Log::add_handler("empty", LogHandler{});
  install_capture_handler();

  Log::info("x");

  EXPECT_TRUE(Log::has_handler("empty"));
  // The if(handler) guard skips the empty function; the real handler still fires.
  ASSERT_EQ(captured_.size(), 1);
}

TEST_F(LoggingTest, NullCallbackHandlerDoesNotCrash) {
  Log::add_handler("cb", handlers::make_callback_handler(nullptr));
  install_capture_handler();

  // The callback wrapper is a valid function that invokes a null SimpleCallback,
  // throwing std::bad_function_call, which emit()'s catch(...) must swallow.
  Log::info("x");

  ASSERT_EQ(captured_.size(), 1);
}

// =============================================================================
// Exception Continuation Tests
// =============================================================================

TEST_F(LoggingTest, DispatchContinuesPastThrowingHandlerRegardlessOfOrder) {
  int cap_a = 0;
  int cap_b = 0;
  Log::add_handler("cap_a", [&cap_a](const LogEntry&) { ++cap_a; });
  Log::add_handler("thrower", [](const LogEntry&) {
    throw std::runtime_error("boom");
  });
  Log::add_handler("cap_b", [&cap_b](const LogEntry&) { ++cap_b; });

  Log::info("x");

  // Whatever the unordered traversal order, at least one capture handler is
  // dispatched after the thrower, and catch(...) lets the loop continue to both.
  EXPECT_EQ(cap_a, 1);
  EXPECT_EQ(cap_b, 1);
}

TEST_F(LoggingTest, DispatchSurvivesMultipleThrowingHandlers) {
  int captures = 0;
  Log::add_handler("thrower1", [](const LogEntry&) {
    throw std::runtime_error("boom1");
  });
  Log::add_handler("thrower2", [](const LogEntry&) {
    throw std::runtime_error("boom2");
  });
  Log::add_handler("capture", [&captures](const LogEntry&) { ++captures; });

  EXPECT_NO_THROW(Log::info("x"));
  EXPECT_EQ(captures, 1);
}
