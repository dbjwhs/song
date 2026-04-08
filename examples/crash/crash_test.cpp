// MIT License
// Copyright (c) 2026 dbjwhs

// Test program for ServiceManager auto-restart functionality

#include <song/song.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace song;

// Must match crash_service.cpp
constexpr u16 kService_Crash = 1;
constexpr u16 kMethod_test = 1;

int main() {
    std::cout << "=== Auto-Restart Test ===\n\n";

    ServiceManager manager;

    // Register the crash service
    manager.register_service("crash", "./crash_service");

    // Enable auto-restart with max 3 restarts
    manager.set_auto_restart("crash", true);
    manager.set_max_restarts("crash", 3);

    // Track restart events
    std::atomic<int> restart_events{0};
    manager.set_restart_callback([&](const std::string& name, int count) {
        std::cout << "[CALLBACK] Service '" << name << "' restarted (count=" << count << ")\n";
        restart_events++;
    });

    // Start the monitor thread
    manager.start_monitor(500);  // Check every 500ms
    std::cout << "Monitor started (checking every 500ms)\n\n";

    // Start the service
    std::cout << "Starting crash service...\n";
    auto* proc = manager.start("crash");
    std::cout << "Service started with pid=" << proc->pid() << "\n\n";

    // Give service time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Send messages until we've seen a few restarts
    for (int round = 1; round <= 4; round++) {
        std::cout << "--- Round " << round << " ---\n";

        try {
            auto conn = manager.connect("crash");
            std::cout << "Connected to service\n";

            for (int ndx = 1; ndx <= 3; ndx++) {
                Buffer request;
                encode_string(request, "test message " + std::to_string(ndx));

                std::cout << "Sending message " << ndx << "...\n";
                auto response = conn.call(kService_Crash, kMethod_test, request);
                std::cout << "Got response: " << decode_string(response) << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }

        // Wait for monitor to detect crash and restart
        std::cout << "Waiting for monitor to detect crash and restart...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        std::cout << "Current restart count: " << manager.restart_count("crash") << "\n\n";

        if (manager.restart_count("crash") >= 3) {
            std::cout << "Reached max restarts, stopping test\n";
            break;
        }
    }

    // Summary
    std::cout << "\n=== Summary ===\n";
    std::cout << "Restart events received: " << restart_events << "\n";
    std::cout << "Final restart count: " << manager.restart_count("crash") << "\n";
    std::cout << "Service alive: " << (manager.is_alive("crash") ? "yes" : "no") << "\n";

    // Cleanup
    manager.stop_monitor();
    std::cout << "\nMonitor stopped. Test complete.\n";

    return 0;
}
