// MIT License
// Copyright (c) 2026 dbjwhs

// A service that intentionally crashes after a few messages
// Used to test ServiceManager auto-restart functionality

#include <song/song.hpp>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

using namespace song;

// Service/method IDs
constexpr u16 kService_Crash = 1;
constexpr u16 kMethod_test = 1;

// Message counter
static int message_count = 0;

void crash_dispatcher([[maybe_unused]] u16 method_id, [[maybe_unused]] Buffer& request, Buffer& response) {
    message_count++;
    std::cerr << "[crash_service] Got message #" << message_count << "\n";

    if (message_count >= 2) {
        std::cerr << "[crash_service] Crashing now!\n";
        std::abort();  // Simulate crash
    }

    encode_string(response, "ok");
}

int main() {
    std::cerr << "[crash_service] Starting (pid=" << getpid() << ")...\n";

    ServiceRuntime runtime;

    // Register dispatcher
    runtime.register_dispatcher(kService_Crash, crash_dispatcher);

    // Register method for capability exchange
    runtime.register_method(kService_Crash, kMethod_test);

    // Run (will crash after 2 messages)
    runtime.run();

    return 0;
}
