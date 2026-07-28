// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <source_location>

namespace song {

// =============================================================================
// Log Levels
// =============================================================================

enum class LogLevel {
    debug = 0,
    info  = 1,
    warn  = 2,
    error = 3,
    fatal = 4
};

// Convert level to string (e.g., "DEBUG", "INFO")
const char* log_level_name(LogLevel level);

// =============================================================================
// Log Entry - passed to handlers
// =============================================================================

struct LogEntry {
    LogLevel level;
    std::string_view message;
    std::source_location location;

    // Convenience accessors
    const char* file() const { return location.file_name(); }
    const char* function() const { return location.function_name(); }
    uint32_t line() const { return location.line(); }
};

// =============================================================================
// Handler Interface
// =============================================================================

// Handlers receive log entries and do whatever they want with them.
// Examples: write to console, file, network, in-memory buffer, etc.
//
// The built-in console handler uses this same interface - no special cases.
using LogHandler = std::function<void(const LogEntry& entry)>;

// =============================================================================
// Log - The Main Interface
// =============================================================================
//
// Usage:
//   // Register handlers (Song's console handler is pre-registered as "console")
//   Log::add_handler("myfile", my_file_handler);
//   Log::add_handler("metrics", my_metrics_handler);
//
//   // Set minimum level (default: info in release, debug in debug builds)
//   Log::set_level(LogLevel::warn);
//
//   // Log messages
//   Log::debug("Starting connection to {}", host);
//   Log::info("Connected");
//   Log::error("Failed to connect: {}", error_msg);
//
// To disable all logging:
//   Log::remove_handler("console");  // Remove built-in
//   // Or: Log::set_level(LogLevel::fatal + 1) to filter everything
//

class Log {
public:
    // -------------------------------------------------------------------------
    // Handler Management
    // -------------------------------------------------------------------------

    // Register a handler with a name. If name exists, replaces it.
    static void add_handler(const std::string& name, LogHandler handler);

    // Remove a handler by name. Returns true if it existed.
    static bool remove_handler(const std::string& name);

    // Check if a handler exists
    static bool has_handler(const std::string& name);

    // Remove all handlers (for testing or shutdown)
    static void clear_handlers();

    // -------------------------------------------------------------------------
    // Level Control
    // -------------------------------------------------------------------------

    // Set minimum log level. Messages below this are discarded.
    static void set_level(LogLevel min_level);

    // Get current minimum level
    static LogLevel get_level();

    // Check if a level would be logged (for expensive message construction)
    static bool is_enabled(LogLevel level);

    // -------------------------------------------------------------------------
    // Logging Methods
    // -------------------------------------------------------------------------

    static void debug(std::string_view msg,
                      std::source_location loc = std::source_location::current());

    static void info(std::string_view msg,
                     std::source_location loc = std::source_location::current());

    static void warn(std::string_view msg,
                     std::source_location loc = std::source_location::current());

    static void error(std::string_view msg,
                      std::source_location loc = std::source_location::current());

    static void fatal(std::string_view msg,
                      std::source_location loc = std::source_location::current());

    // Generic log with explicit level
    static void log(LogLevel level, std::string_view msg,
                    std::source_location loc = std::source_location::current());

private:
    // Dispatch to all registered handlers
    static void emit(LogLevel level, std::string_view msg,
                     const std::source_location& loc);
};

// =============================================================================
// Built-in Handlers
// =============================================================================
//
// These are provided as examples and for convenience.
// They plug in through add_handler() like any user handler.
//

namespace handlers {

// Console handler - writes to stderr with colors (if terminal supports it)
// Format: [LEVEL] message (file:line)
LogHandler make_console_handler(bool use_colors = true);

// Null handler - discards all messages (useful for benchmarks/tests)
LogHandler make_null_handler();

// Callback handler - wraps a simple callback (for quick integration)
using SimpleCallback = std::function<void(LogLevel, const char* message)>;
LogHandler make_callback_handler(SimpleCallback callback);

} // namespace handlers

// =============================================================================
// Initialization
// =============================================================================

// Initialize logging with default console handler.
// Called automatically on first log, but can be called explicitly.
// Safe to call multiple times.
void init_logging();

} // namespace song
