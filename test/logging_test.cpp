// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/logging.hpp>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

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
