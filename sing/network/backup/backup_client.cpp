// MIT License
// Copyright (c) 2026 dbjwhs

// Backup Agent Client - Demonstrates backup agent RPC, streaming, and security
// Usage: backup_client [host] [port] [--key KEY]

#include <song/song.hpp>
#include <song/security.hpp>
#include "backup.hpp"
#include "backup_constants.hpp"
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>

using namespace song;
using namespace song::backup;

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    u16 port = 19000;
    std::string key;
    int positional = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            key = argv[++i];
        } else if (argv[i][0] != '-') {
            if (positional == 0) {
                host = argv[i];
            } else if (positional == 1) {
                port = static_cast<u16>(std::atoi(argv[i]));
            }
            positional++;
        }
    }

    std::cout << "Connecting to " << host << ":" << port;
    if (!key.empty()) {
        std::cout << " [HMAC auth]";
    }
    std::cout << "...\n";

    try {
        std::unique_ptr<ServiceConnection> conn;
        ServiceManager mgr;  // Must outlive conn for the TCP case

        if (key.empty()) {
            // Plain TCP connection
            mgr.register_remote_service("backup", host, port, kService_BackupAgent);
            conn = std::make_unique<ServiceConnection>(mgr.connect("backup"));
        } else {
            // Secure connection
            auto tcp = std::make_unique<TcpTransport>();
            tcp->connect(host, port, 5000);
            auto secure = std::make_unique<SecureTransport>(
                std::move(tcp), SecurityConfig(key));
            conn = std::make_unique<ServiceConnection>(std::move(secure));
            conn->init_handshake();
        }

        BackupAgentProxy proxy(*conn);

        std::cout << "\n=== Backup Agent Demo ===\n\n";

        // List files
        std::cout << "--- File Listing ---\n";
        auto files = proxy.list_files();
        if (files.empty()) {
            std::cout << "  (no files)\n";
        } else {
            for (const auto& f : files) {
                std::cout << "  " << std::setw(40) << std::left << f.path
                          << std::setw(10) << std::right << f.size << " bytes\n";
            }
        }

        std::cout << "\nStorage used: " << proxy.get_storage_used() << " bytes\n";

        // Upload a test file
        std::cout << "\n--- Upload Test ---\n";
        std::string content = "Hello from backup client!";
        FileChunk chunk;
        chunk.path = "client_upload.txt";
        chunk.offset = 0;
        chunk.data = std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(content.data()),
            reinterpret_cast<const std::byte*>(content.data()) + content.size());
        chunk.is_final = true;

        auto result = proxy.receive_chunk(chunk);
        std::cout << "  Upload " << (result.success ? "succeeded" : "failed")
                  << ": " << result.bytes_written << " bytes written\n";

        // Compute checksum
        auto checksum = proxy.compute_checksum("client_upload.txt");
        std::cout << "  Checksum: " << checksum << "\n";

        // Stream a file back
        if (!files.empty()) {
            std::cout << "\n--- Stream Test ---\n";
            std::cout << "  Streaming: " << files[0].path << "\n";

            Buffer req;
            encode_string(req, files[0].path);
            auto reader = conn->call_streaming(kService_BackupAgent_Stream,
                                               kMethod_BackupAgent_stream_file, req);

            size_t total_bytes = 0;
            size_t chunk_count = 0;
            while (reader.next()) {
                auto data = decode_bytes(reader.chunk());
                total_bytes += data.size();
                chunk_count++;
            }

            std::cout << "  Received: " << total_bytes << " bytes in "
                      << chunk_count << " chunk(s)\n";
        }

        // Check progress
        auto progress = proxy.get_progress();
        std::cout << "\n--- Progress ---\n";
        std::cout << "  Files done: " << progress.files_done << "\n";
        std::cout << "  Bytes done: " << progress.bytes_done << "\n";
        std::cout << "  Current:    " << progress.current_file << "\n";

        // Clean up
        proxy.delete_file("client_upload.txt");
        std::cout << "\nSuccess!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
