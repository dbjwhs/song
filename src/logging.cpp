// MIT License
// Copyright (c) 2026 dbjwhs

#include <song/logging.hpp>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdio>

// Platform detection for terminal colors
#ifdef _WIN32
#include <io.h>
#define SONG_ISATTY _isatty
#define SONG_FILENO _fileno
#else
#include <unistd.h>
#define SONG_ISATTY isatty
#define SONG_FILENO fileno
#endif

namespace song {

// =============================================================================
// Level Names
// =============================================================================

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info:  return "INFO";
        case LogLevel::warn:  return "WARN";
        case LogLevel::error: return "ERROR";
        case LogLevel::fatal: return "FATAL";
        default:              return "UNKNOWN";
    }
}

// =============================================================================
// Global State
// =============================================================================

namespace {

struct LogState {
    std::mutex mutex;
    std::unordered_map<std::string, LogHandler> handlers;
    std::atomic<LogLevel> min_level{LogLevel::info};
    std::atomic<bool> initialized{false};
};

LogState& state() {
    static LogState s;
    return s;
}

// Ensure logging is initialized before first use
void ensure_init() {
    auto& s = state();
    if (!s.initialized.load(std::memory_order_acquire)) {
        init_logging();
    }
}

} // anonymous namespace

// =============================================================================
// Handler Management
// =============================================================================

void Log::add_handler(const std::string& name, LogHandler handler) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.handlers[name] = std::move(handler);
}

bool Log::remove_handler(const std::string& name) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.handlers.erase(name) > 0;
}

bool Log::has_handler(const std::string& name) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.handlers.find(name) != s.handlers.end();
}

void Log::clear_handlers() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.handlers.clear();
}

// =============================================================================
// Level Control
// =============================================================================

void Log::set_level(LogLevel min_level) {
    state().min_level.store(min_level, std::memory_order_release);
}

LogLevel Log::get_level() {
    return state().min_level.load(std::memory_order_acquire);
}

bool Log::is_enabled(LogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(get_level());
}

// =============================================================================
// Logging Methods
// =============================================================================

void Log::debug(std::string_view msg, std::source_location loc) {
    emit(LogLevel::debug, msg, loc);
}

void Log::info(std::string_view msg, std::source_location loc) {
    emit(LogLevel::info, msg, loc);
}

void Log::warn(std::string_view msg, std::source_location loc) {
    emit(LogLevel::warn, msg, loc);
}

void Log::error(std::string_view msg, std::source_location loc) {
    emit(LogLevel::error, msg, loc);
}

void Log::fatal(std::string_view msg, std::source_location loc) {
    emit(LogLevel::fatal, msg, loc);
}

void Log::log(LogLevel level, std::string_view msg, std::source_location loc) {
    emit(level, msg, loc);
}

void Log::emit(LogLevel level, std::string_view msg, const std::source_location& loc) {
    // Fast path: check level before acquiring lock
    if (!is_enabled(level)) {
        return;
    }

    // A handler that itself calls a Log method would re-enter emit(). Handlers
    // previously ran while the registry mutex was held, so such a handler would
    // deadlock re-locking the non-recursive mutex (and could recurse without
    // bound). Drop log messages emitted from within a handler on this thread.
    static thread_local bool in_dispatch = false;
    if (in_dispatch) {
        return;
    }

    // Ensure initialized (registers default console handler)
    ensure_init();

    // Build entry
    LogEntry entry{level, msg, loc};

    // Snapshot the handlers under the lock, then invoke them WITHOUT holding it,
    // so a handler may safely call other Log APIs -- including add_handler() and
    // remove_handler() -- without deadlocking on the same non-recursive mutex.
    std::vector<LogHandler> snapshot;
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        snapshot.reserve(s.handlers.size());
        for (const auto& [name, handler] : s.handlers) {
            if (handler) {
                snapshot.push_back(handler);
            }
        }
    }

    in_dispatch = true;
    struct ResetGuard {
        bool& flag;
        ~ResetGuard() { flag = false; }
    } reset_guard{in_dispatch};

    for (const auto& handler : snapshot) {
        try {
            handler(entry);
        } catch (...) {
            // Don't let handler exceptions crash the app.
        }
    }
}

// =============================================================================
// Built-in Handlers
// =============================================================================

namespace handlers {

LogHandler make_console_handler(bool use_colors) {
    // Check if stderr is a terminal (for color support)
    bool colors_enabled = use_colors && SONG_ISATTY(SONG_FILENO(stderr));

    return [colors_enabled](const LogEntry& entry) {
        // ANSI color codes
        const char* color_start = "";
        const char* color_end = "";

        if (colors_enabled) {
            switch (entry.level) {
                case LogLevel::debug: color_start = "\033[36m"; break;  // Cyan
                case LogLevel::info:  color_start = "\033[32m"; break;  // Green
                case LogLevel::warn:  color_start = "\033[33m"; break;  // Yellow
                case LogLevel::error: color_start = "\033[31m"; break;  // Red
                case LogLevel::fatal: color_start = "\033[35m"; break;  // Magenta
                default: break;
            }
            color_end = "\033[0m";
        }

        // Extract just filename from path
        std::string_view file = entry.file();
        if (auto pos = file.rfind('/'); pos != std::string_view::npos) {
            file = file.substr(pos + 1);
        }
#ifdef _WIN32
        if (auto pos = file.rfind('\\'); pos != std::string_view::npos) {
            file = file.substr(pos + 1);
        }
#endif

        // Format: [LEVEL] message (file:line)
        std::fprintf(stderr, "%s[%5s]%s %.*s (%.*s:%u)\n",
                     color_start,
                     log_level_name(entry.level),
                     color_end,
                     static_cast<int>(entry.message.size()), entry.message.data(),
                     static_cast<int>(file.size()), file.data(),
                     entry.line());
    };
}

LogHandler make_null_handler() {
    return [](const LogEntry&) {
        // Intentionally empty - discards all messages
    };
}

LogHandler make_callback_handler(SimpleCallback callback) {
    return [cb = std::move(callback)](const LogEntry& entry) {
        // Convert message to null-terminated string for C-style callback
        std::string msg(entry.message);
        cb(entry.level, msg.c_str());
    };
}

} // namespace handlers

// =============================================================================
// Initialization
// =============================================================================

void init_logging() {
    auto& s = state();

    // Double-checked locking
    if (s.initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.initialized.load(std::memory_order_relaxed)) {
        return;
    }

    // Register the built-in console handler through the public API
    // This proves the handler interface works - we eat our own dog food
    s.handlers["console"] = handlers::make_console_handler();

    // Set default level based on build type
#ifdef NDEBUG
    s.min_level.store(LogLevel::info, std::memory_order_relaxed);
#else
    s.min_level.store(LogLevel::debug, std::memory_order_relaxed);
#endif

    s.initialized.store(true, std::memory_order_release);
}

} // namespace song
