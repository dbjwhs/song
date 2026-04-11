// MIT License
// Copyright (c) 2026 dbjwhs

// Backup Agent Client - Upload files and verify via checksum
// Usage: backup_client <host> <port> [file...] [--key KEY]
//
// With no files: lists remote files and storage used.
// With files: uploads each file, verifies via checksum, then streams
// it back and compares byte-for-byte.

#include <song/song.hpp>
#include <song/security.hpp>
#include "backup.hpp"
#include "backup_constants.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace song;
using namespace song::backup;

namespace fs = std::filesystem;

// Compute local checksum using the same HMAC key as the service
static std::string local_checksum(const std::vector<char>& data) {
    auto tag = compute_hmac(kChecksumKey, kChecksumKeySize,
                            data.data(), data.size());
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : tag) {
        oss << std::setw(2) << static_cast<unsigned>(b);
    }
    return oss.str();
}

// Upload a file in chunks, return true on success
static bool upload_file(BackupAgentProxy& proxy, const std::string& local_path,
                        const std::string& remote_name) {
    std::ifstream file(local_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "  [FAIL] Cannot open: " << local_path << "\n";
        return false;
    }

    auto file_size = fs::file_size(local_path);
    i64 offset = 0;
    std::vector<char> buf(kUploadChunkSize);

    while (file.read(buf.data(), static_cast<std::streamsize>(buf.size())) || file.gcount() > 0) {
        auto bytes_read = file.gcount();
        bool at_end = (static_cast<size_t>(offset + bytes_read) >= file_size);

        FileChunk chunk;
        chunk.path = remote_name;
        chunk.offset = offset;
        chunk.data = std::vector<std::byte>(
            reinterpret_cast<std::byte*>(buf.data()),
            reinterpret_cast<std::byte*>(buf.data()) + bytes_read);
        chunk.is_final = at_end;

        auto result = proxy.receive_chunk(chunk);
        if (!result.success) {
            std::cerr << "  [FAIL] Upload failed at offset " << offset << "\n";
            return false;
        }
        offset += bytes_read;
    }

    std::cout << "  Uploaded " << offset << " bytes\n";
    return true;
}

// Stream a file back from the server
static std::vector<char> stream_back(ServiceConnection& conn,
                                     const std::string& remote_name) {
    Buffer req;
    encode_string(req, remote_name);
    auto reader = conn.call_streaming(kService_BackupAgent_Stream,
                                      kMethod_BackupAgent_stream_file, req);

    std::vector<char> result;
    while (reader.next()) {
        auto data = decode_bytes(reader.chunk());
        result.insert(result.end(),
                      reinterpret_cast<const char*>(data.data()),
                      reinterpret_cast<const char*>(data.data()) + data.size());
    }
    return result;
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    u16 port = 19000;
    std::string key;
    bool use_tls = false;
    std::vector<std::string> files;

    for (int ndx = 1; ndx < argc; ++ndx) {
        if (std::strcmp(argv[ndx], "--key") == 0 && ndx + 1 < argc) {
            key = argv[++ndx];
        } else if (std::strcmp(argv[ndx], "--tls") == 0) {
            use_tls = true;
        } else if (argv[ndx][0] != '-') {
            // First two positionals are host and port, rest are files
            if (files.empty() && host == std::string("127.0.0.1")) {
                host = argv[ndx];
            } else if (files.empty() && port == 19000) {
                port = static_cast<u16>(std::atoi(argv[ndx]));
            } else {
                files.push_back(argv[ndx]);
            }
        }
    }

    std::cout << "Connecting to " << host << ":" << port;
    if (use_tls) {
        std::cout << " [TLS encrypted]";
    } else if (!key.empty()) {
        std::cout << " [HMAC auth]";
    }
    std::cout << "...\n";

    try {
        std::unique_ptr<ServiceConnection> conn;
        ServiceManager mgr;

#if defined(SONG_HAS_TLS)
        if (use_tls && !key.empty()) {
            // TLS-PSK encrypted connection
            TlsConfig tls_config(key, "song-backup", TlsConfig::Mode::psk);
            // Connect TCP first, then wrap in TLS
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                throw ServiceError("socket() failed");
            }
            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, host, &addr.sin_addr);
            if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr),
                          sizeof(addr)) < 0) {
                ::close(sock);
                throw ServiceError("connect() failed");
            }
            auto tls = std::make_unique<TlsTransport>(sock, std::move(tls_config),
                                                       host, port);
            tls->handshake();
            conn = std::make_unique<ServiceConnection>(std::move(tls));
            conn->init_handshake();
        } else
#endif
        if (!key.empty()) {
            // HMAC-authenticated connection
            auto tcp = std::make_unique<TcpTransport>();
            tcp->connect(host, port, 5000);
            auto secure = std::make_unique<SecureTransport>(
                std::move(tcp), SecurityConfig(key));
            conn = std::make_unique<ServiceConnection>(std::move(secure));
            conn->init_handshake();
        } else {
            // Plain TCP connection
            mgr.register_remote_service("backup", host, port, kService_BackupAgent);
            conn = std::make_unique<ServiceConnection>(mgr.connect("backup"));
        }

        (void)use_tls;

        BackupAgentProxy proxy(*conn);

        if (files.empty()) {
            // No files specified: list remote contents
            std::cout << "\n--- Remote Files ---\n";
            auto remote_files = proxy.list_files();
            if (remote_files.empty()) {
                std::cout << "  (empty)\n";
            } else {
                for (const auto& f : remote_files) {
                    std::cout << "  " << std::setw(50) << std::left << f.path
                              << std::setw(10) << std::right << f.size << " bytes\n";
                }
            }
            std::cout << "\nStorage used: " << proxy.get_storage_used() << " bytes\n";
        } else {
            // Upload and verify each file
            int passed = 0;
            int failed = 0;

            for (const auto& filepath : files) {
                if (!fs::exists(filepath)) {
                    std::cerr << "[FAIL] File not found: " << filepath << "\n";
                    failed++;
                    continue;
                }

                std::string remote_name = fs::path(filepath).filename().string();
                std::cout << "\n--- " << remote_name << " ---\n";

                // Read local file
                std::ifstream local_file(filepath, std::ios::binary);
                std::vector<char> local_data(
                    (std::istreambuf_iterator<char>(local_file)),
                     std::istreambuf_iterator<char>());
                local_file.close();

                std::cout << "  Local size: " << local_data.size() << " bytes\n";

                // Upload
                if (!upload_file(proxy, filepath, remote_name)) {
                    failed++;
                    continue;
                }

                // Verify size
                auto info = proxy.get_file_info(remote_name);
                if (info.size != static_cast<i64>(local_data.size())) {
                    std::cerr << "  [FAIL] Size mismatch: local="
                              << local_data.size() << " remote=" << info.size << "\n";
                    failed++;
                    continue;
                }
                std::cout << "  Remote size: " << info.size << " bytes [OK]\n";

                // Verify checksum
                auto local_sum = local_checksum(local_data);
                auto remote_sum = proxy.compute_checksum(remote_name);
                if (local_sum != remote_sum) {
                    std::cerr << "  [FAIL] Checksum mismatch:\n"
                              << "    local:  " << local_sum << "\n"
                              << "    remote: " << remote_sum << "\n";
                    failed++;
                    continue;
                }
                std::cout << "  Checksum: " << local_sum << " [OK]\n";

                // Verify content by streaming back
                auto remote_data = stream_back(*conn, remote_name);
                if (remote_data.size() != local_data.size() ||
                    std::memcmp(remote_data.data(), local_data.data(), local_data.size()) != 0) {
                    std::cerr << "  [FAIL] Content mismatch after stream-back\n";
                    failed++;
                    continue;
                }
                std::cout << "  Stream-back verify: " << remote_data.size()
                          << " bytes [OK]\n";

                passed++;
            }

            std::cout << "\n--- Results ---\n";
            std::cout << "  Passed: " << passed << "/" << (passed + failed) << "\n";
            if (failed > 0) {
                std::cout << "  Failed: " << failed << "\n";
                return 1;
            }
        }

        std::cout << "\nDone.\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
