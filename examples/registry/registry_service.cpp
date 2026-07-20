// MIT License
// Copyright (c) 2026 dbjwhs

// Registry service example.
//
// SECURITY: This registry has no ownership or authorization model. Any peer that
// can reach it may register, unregister, re-point, or heartbeat ANY service name
// -- there is no check that the caller owns the name it mutates. Clients that
// resolve a name via discover() trust the returned host:port without
// verification, so a peer that re-points a name can redirect a victim's RPCs to
// an endpoint it controls. The wire protocol is plaintext with no peer
// authentication.
//
// Consequently this example binds loopback (127.0.0.1) by default and must only
// be run on a trusted, isolated network. A production registry needs a per-name
// ownership token (issued at register, required for unregister/heartbeat/
// re-register) and a transport that authenticates peers; both are intentionally
// out of scope for this demo.

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

    // Bind loopback by default. This registry has NO ownership/authorization
    // model: any peer that can reach it may unregister or re-point any service
    // name (see the security note at the top of this file), and clients trust
    // discover() output blindly. Keeping it on 127.0.0.1 by default prevents an
    // anonymous remote peer from hijacking names; --all-interfaces is an explicit
    // opt-in for trusted, isolated networks only.
    std::string bind_address = "127.0.0.1";

    // Parse command line
    for (int ndx = 1; ndx < argc; ++ndx) {
        std::string arg = argv[ndx];
        if (arg == "-p" || arg == "--port") {
            if (ndx + 1 < argc) {
                port = static_cast<u16>(std::stoi(argv[++ndx]));
            }
        } else if (arg == "--all-interfaces") {
            bind_address = "";  // INADDR_ANY -- exposes the registry to the network
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: registry_service [options]\n"
                      << "Options:\n"
                      << "  -p, --port PORT    Listen port (default: 9999)\n"
                      << "  --all-interfaces   Bind all interfaces instead of loopback\n"
                      << "                     (no authorization -- trusted networks only)\n"
                      << "  -h, --help         Show this help\n";
            return 0;
        }
    }

    // Install signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start cleanup thread
    std::thread cleanup_thread(cleanup_thread_func);

    std::cout << "Registry service starting on port " << port
              << (bind_address.empty() ? " (all interfaces)" : " (loopback)") << "\n";
    if (bind_address.empty()) {
        std::cout << "WARNING: --all-interfaces exposes an unauthenticated registry to "
                     "the network; any peer can re-point service names. Use only on a "
                     "trusted, isolated network.\n";
    }

    // Create TCP listener
    TcpListener listener;
    try {
        listener.listen(port, 128, bind_address);
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
