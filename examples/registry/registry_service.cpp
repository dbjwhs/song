// MIT License
// Copyright (c) 2026 dbjwhs

#include <song/song.hpp>
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace song;

static std::atomic<bool> g_running{true};
static MemoryRegistry g_registry;

void signal_handler(int) {
    g_running = false;
}

void registry_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_Registry(g_registry, method_id, request, response);
}

void cleanup_thread_func() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (g_running) {
            g_registry.cleanup_stale();
        }
    }
}

int main(int argc, char* argv[]) {
    u16 port = 9999;  // Default registry port

    // Parse command line
    for (int ndx = 1; ndx < argc; ++ndx) {
        std::string arg = argv[ndx];
        if (arg == "-p" || arg == "--port") {
            if (ndx + 1 < argc) {
                port = static_cast<u16>(std::stoi(argv[++ndx]));
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: registry_service [options]\n"
                      << "Options:\n"
                      << "  -p, --port PORT    Listen port (default: 9999)\n"
                      << "  -h, --help         Show this help\n";
            return 0;
        }
    }

    // Install signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start cleanup thread
    std::thread cleanup_thread(cleanup_thread_func);

    std::cout << "Registry service starting on port " << port << "\n";

    // Create TCP listener
    TcpListener listener;
    try {
        listener.listen(port);
    } catch (const std::exception& e) {
        std::cerr << "Failed to listen on port " << port << ": " << e.what() << "\n";
        g_running = false;
        cleanup_thread.join();
        return 1;
    }

    std::cout << "Registry service listening on port " << listener.bound_port() << "\n";

    // Accept connections and handle requests
    while (g_running) {
        try {
            auto transport = listener.accept(1000);  // 1 second timeout
            if (!transport) {
                continue;  // Timeout, check g_running and try again
            }

            std::cout << "Client connected\n";

            // Handle requests from this client
            // Note: In production, would spawn a thread per client
            while (g_running && transport->is_connected()) {
                Buffer msg;
                try {
                    if (!transport->receive(msg, 1000)) {
                        continue;  // Timeout
                    }
                } catch (const ServiceError&) {
                    // Timeout or error
                    continue;
                }

                // Decode header
                auto hdr = wire::decode_header(msg);

                if (hdr.magic != wire::kMagic) {
                    std::cerr << "Invalid magic number\n";
                    break;
                }

                if (hdr.type == wire::MsgType::shutdown) {
                    std::cout << "Client disconnected\n";
                    break;
                }

                if (hdr.type != wire::MsgType::call) {
                    std::cerr << "Unexpected message type\n";
                    continue;
                }

                // Decode service and method IDs
                auto [service_id, method_id] = wire::decode_method_call_header(msg);

                if (service_id != kService_Registry) {
                    std::cerr << "Unknown service: " << service_id << "\n";
                    // Send error response
                    Buffer error = wire::create_error_message(hdr.sequence_id,
                        ErrorCode::unknown_service, "Unknown service");
                    transport->send(error);
                    continue;
                }

                // Dispatch the call
                Buffer response_payload;
                try {
                    registry_dispatcher(method_id, msg, response_payload);

                    // Send success response
                    Buffer response = wire::create_result_message(hdr.sequence_id, response_payload);
                    transport->send(response);
                } catch (const std::exception& e) {
                    // Send error response
                    Buffer error = wire::create_error_message(hdr.sequence_id,
                        ErrorCode::service_crashed, e.what());
                    transport->send(error);
                }
            }

            transport->close();

        } catch (const std::exception& e) {
            if (g_running) {
                std::cerr << "Error: " << e.what() << "\n";
            }
        }
    }

    std::cout << "Registry service shutting down\n";
    std::cout << "Registered services at shutdown: " << g_registry.size() << "\n";

    listener.close();
    cleanup_thread.join();

    return 0;
}
