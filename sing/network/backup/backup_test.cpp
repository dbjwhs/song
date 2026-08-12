// MIT License
// Copyright (c) 2026 dbjwhs

// Backup Agent Integration Tests - Phase 1: Core RPC

#include <gtest/gtest.h>
#include <song/song.hpp>
#include <song/security.hpp>
#include <song/transport.hpp>
#include "backup.hpp"
#include "backup_constants.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/wait.h>

using namespace song;
using namespace song::backup;

namespace fs = std::filesystem;


// Helper to find the service binary
static std::string get_service_path() {
    fs::path base = fs::current_path();
    std::vector<fs::path> candidates = {
        base / "sing" / "network" / "backup" / "sing_network_backup_service",
        base / "sing_network_backup_service",
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            return p.string();
        }
    }
    return (base / "sing" / "network" / "backup" / "sing_network_backup_service").string();
}

// Helper to write a test file
static void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

class BackupAgentTest : public ::testing::Test {
protected:
    static constexpr u16 kTestPort = 19876;

    void SetUp() override {
        // Create unique sandbox directory
        sandbox_ = fs::temp_directory_path() / ("song_backup_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(sandbox_);

        std::string path = get_service_path();
        if (!fs::exists(path)) {
            GTEST_SKIP() << "Backup service not found at " << path;
        }

        // Fork and exec the service
        std::string port_str = std::to_string(kTestPort);
        std::string root_str = sandbox_.string();

        server_pid_ = fork();
        if (server_pid_ == 0) {
            execl(path.c_str(), path.c_str(),
                  port_str.c_str(), root_str.c_str(), nullptr);
            _exit(1);
        }

        // Connect with retry
        mgr_.register_remote_service("backup", "127.0.0.1", kTestPort, 1);
        for (int attempt = 0; attempt < 10; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
            try {
                conn_ = std::make_unique<ServiceConnection>(mgr_.connect("backup"));
                return;
            } catch (const std::exception&) {
                // Server not ready yet
            }
        }
        conn_ = std::make_unique<ServiceConnection>(mgr_.connect("backup"));
    }

    void TearDown() override {
        conn_.reset();
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
            int status;
            waitpid(server_pid_, &status, 0);
        }
        // Clean up sandbox
        if (fs::exists(sandbox_)) {
            fs::remove_all(sandbox_);
        }
    }

    // Create test files in the sandbox (called by tests that need pre-populated data)
    void populate_sandbox() {
        write_file(sandbox_ / "hello.txt", "Hello, Song!");
        write_file(sandbox_ / "data" / "numbers.bin", std::string(256, '\x42'));
        write_file(sandbox_ / "readme.md", "# Backup Test\nThis is a test file.\n");
    }

    pid_t server_pid_ = -1;
    ServiceManager mgr_;
    std::unique_ptr<ServiceConnection> conn_;
    fs::path sandbox_;
};

// =============================================================================
// List Files Tests
// =============================================================================

TEST_F(BackupAgentTest, ListFilesEmpty) {
    BackupAgentProxy proxy(*conn_);
    auto files = proxy.list_files();
    EXPECT_TRUE(files.empty());
}

TEST_F(BackupAgentTest, ListFilesPopulated) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    auto files = proxy.list_files();

    ASSERT_EQ(files.size(), 3u);
    // Sorted alphabetically
    EXPECT_EQ(files[0].path, "data/numbers.bin");
    EXPECT_EQ(files[0].size, 256);
    EXPECT_EQ(files[1].path, "hello.txt");
    EXPECT_EQ(files[1].size, 12);
    EXPECT_EQ(files[2].path, "readme.md");
    EXPECT_GT(files[2].modified_at, 0);
}

// =============================================================================
// Get File Info Tests
// =============================================================================

TEST_F(BackupAgentTest, GetFileInfoExisting) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    auto info = proxy.get_file_info("hello.txt");

    EXPECT_EQ(info.path, "hello.txt");
    EXPECT_EQ(info.size, 12);
    EXPECT_GT(info.modified_at, 0);
}

TEST_F(BackupAgentTest, GetFileInfoNonexistent) {
    BackupAgentProxy proxy(*conn_);
    auto info = proxy.get_file_info("no_such_file.txt");

    EXPECT_EQ(info.path, "no_such_file.txt");
    EXPECT_EQ(info.size, 0);
    EXPECT_EQ(info.modified_at, 0);
}

// =============================================================================
// File Exists Tests
// =============================================================================

TEST_F(BackupAgentTest, FileExistsTrue) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    EXPECT_TRUE(proxy.file_exists("hello.txt"));
    EXPECT_TRUE(proxy.file_exists("data/numbers.bin"));
}

TEST_F(BackupAgentTest, FileExistsFalse) {
    BackupAgentProxy proxy(*conn_);
    EXPECT_FALSE(proxy.file_exists("nonexistent.txt"));
}

// =============================================================================
// Changes Since Tests
// =============================================================================

TEST_F(BackupAgentTest, ChangesSinceAll) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    // Timestamp 0 should return all files
    auto changed = proxy.changes_since(0);
    EXPECT_EQ(changed.size(), 3u);
}

TEST_F(BackupAgentTest, ChangesSinceNone) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    // Far future timestamp should return nothing
    auto changed = proxy.changes_since(std::numeric_limits<i64>::max());
    EXPECT_TRUE(changed.empty());
}

// =============================================================================
// Receive Chunk Tests (Upload)
// =============================================================================

TEST_F(BackupAgentTest, ReceiveChunkSingle) {
    BackupAgentProxy proxy(*conn_);

    std::string content = "uploaded file content";
    FileChunk chunk;
    chunk.path = "uploaded.txt";
    chunk.offset = 0;
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.bytes_written, static_cast<i64>(content.size()));
    EXPECT_EQ(result.total_received, static_cast<i64>(content.size()));

    // Verify the file is now accessible
    EXPECT_TRUE(proxy.file_exists("uploaded.txt"));
    auto info = proxy.get_file_info("uploaded.txt");
    EXPECT_EQ(info.size, static_cast<i64>(content.size()));
}

TEST_F(BackupAgentTest, ReceiveChunkMultiple) {
    BackupAgentProxy proxy(*conn_);

    std::string part1 = "first part ";
    std::string part2 = "second part";

    // Chunk 1
    FileChunk c1;
    c1.path = "multi.txt";
    c1.offset = 0;
    c1.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(part1.data()),
        reinterpret_cast<const std::byte*>(part1.data()) + part1.size());
    c1.is_final = false;

    auto r1 = proxy.receive_chunk(c1);
    EXPECT_TRUE(r1.success);

    // Chunk 2
    FileChunk c2;
    c2.path = "multi.txt";
    c2.offset = static_cast<i64>(part1.size());
    c2.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(part2.data()),
        reinterpret_cast<const std::byte*>(part2.data()) + part2.size());
    c2.is_final = true;

    auto r2 = proxy.receive_chunk(c2);
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(r2.total_received, static_cast<i64>(part1.size() + part2.size()));
}

TEST_F(BackupAgentTest, ReceiveChunkSubdirectory) {
    BackupAgentProxy proxy(*conn_);

    std::string content = "nested content";
    FileChunk chunk;
    chunk.path = "deep/nested/dir/file.txt";
    chunk.offset = 0;
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(proxy.file_exists("deep/nested/dir/file.txt"));
}

// =============================================================================
// Delete File Tests
// =============================================================================

TEST_F(BackupAgentTest, DeleteFileExisting) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    EXPECT_TRUE(proxy.file_exists("hello.txt"));
    EXPECT_TRUE(proxy.delete_file("hello.txt"));
    EXPECT_FALSE(proxy.file_exists("hello.txt"));
}

TEST_F(BackupAgentTest, DeleteFileNonexistent) {
    BackupAgentProxy proxy(*conn_);
    EXPECT_FALSE(proxy.delete_file("nothing.txt"));
}

// =============================================================================
// Storage Used Tests
// =============================================================================

TEST_F(BackupAgentTest, GetStorageUsedEmpty) {
    BackupAgentProxy proxy(*conn_);
    EXPECT_EQ(proxy.get_storage_used(), 0);
}

TEST_F(BackupAgentTest, GetStorageUsedPopulated) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    i64 used = proxy.get_storage_used();
    // hello.txt(12) + numbers.bin(256) + readme.md(35) = 303
    EXPECT_EQ(used, 303);
}

// =============================================================================
// Streaming Tests (Phase 2)
// =============================================================================

TEST_F(BackupAgentTest, StreamFileSmall) {
    populate_sandbox();

    // Stream hello.txt (12 bytes) - should be one chunk
    Buffer req;
    encode_string(req, std::string("hello.txt"));
    auto reader = conn_->call_streaming(kService_BackupAgent_Stream,
                                        kMethod_BackupAgent_stream_file, req);

    std::vector<std::byte> reassembled;
    while (reader.next()) {
        auto data = decode_bytes(reader.chunk());
        reassembled.insert(reassembled.end(), data.begin(), data.end());
    }
    EXPECT_TRUE(reader.complete());

    std::string content(reinterpret_cast<const char*>(reassembled.data()),
                        reassembled.size());
    EXPECT_EQ(content, "Hello, Song!");
}

TEST_F(BackupAgentTest, StreamFileLarge) {
    // Create a ~100KB file
    std::string large_content(100 * 1024, 'X');
    write_file(sandbox_ / "large.bin", large_content);

    Buffer req;
    encode_string(req, std::string("large.bin"));
    auto reader = conn_->call_streaming(kService_BackupAgent_Stream,
                                        kMethod_BackupAgent_stream_file, req);

    std::vector<std::byte> reassembled;
    size_t chunk_count = 0;
    while (reader.next()) {
        auto data = decode_bytes(reader.chunk());
        reassembled.insert(reassembled.end(), data.begin(), data.end());
        chunk_count++;
    }
    EXPECT_TRUE(reader.complete());
    // 100KB / 64KB chunks = 2 chunks (64K + 36K)
    EXPECT_GE(chunk_count, 2u);

    std::string content(reinterpret_cast<const char*>(reassembled.data()),
                        reassembled.size());
    EXPECT_EQ(content, large_content);
}

TEST_F(BackupAgentTest, StreamFileNonexistent) {
    Buffer req;
    encode_string(req, std::string("no_such_file.dat"));
    auto reader = conn_->call_streaming(kService_BackupAgent_Stream,
                                        kMethod_BackupAgent_stream_file, req);

    // Should get zero data chunks, just stream_end
    EXPECT_FALSE(reader.next());
    EXPECT_TRUE(reader.complete());
    EXPECT_EQ(reader.chunk_count(), 0u);
}

// =============================================================================
// Checksum Tests (Phase 2)
// =============================================================================

TEST_F(BackupAgentTest, ComputeChecksum) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    auto checksum = proxy.compute_checksum("hello.txt");
    EXPECT_FALSE(checksum.empty());
    // Checksum should be deterministic (16 hex chars for 8-byte HMAC)
    EXPECT_EQ(checksum.size(), 16u);
}

TEST_F(BackupAgentTest, ChecksumDeterministic) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    auto c1 = proxy.compute_checksum("hello.txt");
    auto c2 = proxy.compute_checksum("hello.txt");
    EXPECT_EQ(c1, c2);
}

TEST_F(BackupAgentTest, ChecksumDifferentFiles) {
    populate_sandbox();

    BackupAgentProxy proxy(*conn_);
    auto c1 = proxy.compute_checksum("hello.txt");
    auto c2 = proxy.compute_checksum("data/numbers.bin");
    EXPECT_NE(c1, c2);
}

TEST_F(BackupAgentTest, ChecksumNonexistent) {
    BackupAgentProxy proxy(*conn_);
    auto checksum = proxy.compute_checksum("missing.txt");
    EXPECT_TRUE(checksum.empty());
}

// =============================================================================
// Progress Tests
// =============================================================================

TEST_F(BackupAgentTest, ProgressInitiallyIdle) {
    BackupAgentProxy proxy(*conn_);
    auto progress = proxy.get_progress();
    EXPECT_EQ(progress.status, static_cast<i32>(BackupStatus::idle));
    EXPECT_EQ(progress.files_done, 0);
    EXPECT_EQ(progress.bytes_done, 0);
}

// =============================================================================
// End-to-End Workflow
// =============================================================================

TEST_F(BackupAgentTest, UploadAndVerify) {
    BackupAgentProxy proxy(*conn_);

    // Upload a file
    std::string content = "workflow test data for backup agent";
    FileChunk chunk;
    chunk.path = "workflow.txt";
    chunk.offset = 0;
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_TRUE(result.success);

    // Verify via list_files
    auto files = proxy.list_files();
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].path, "workflow.txt");
    EXPECT_EQ(files[0].size, static_cast<i64>(content.size()));

    // Verify via streaming
    Buffer req;
    encode_string(req, std::string("workflow.txt"));
    auto reader = conn_->call_streaming(kService_BackupAgent_Stream,
                                        kMethod_BackupAgent_stream_file, req);

    std::vector<std::byte> reassembled;
    while (reader.next()) {
        auto data = decode_bytes(reader.chunk());
        reassembled.insert(reassembled.end(), data.begin(), data.end());
    }

    std::string streamed(reinterpret_cast<const char*>(reassembled.data()),
                         reassembled.size());
    EXPECT_EQ(streamed, content);

    // Verify checksum is consistent
    auto c1 = proxy.compute_checksum("workflow.txt");
    EXPECT_FALSE(c1.empty());
    auto c2 = proxy.compute_checksum("workflow.txt");
    EXPECT_EQ(c1, c2);

    // Delete and verify gone
    EXPECT_TRUE(proxy.delete_file("workflow.txt"));
    EXPECT_FALSE(proxy.file_exists("workflow.txt"));
    EXPECT_EQ(proxy.get_storage_used(), 0);
}

// =============================================================================
// Path Traversal Protection
// =============================================================================

TEST_F(BackupAgentTest, RejectsPathTraversal) {
    BackupAgentProxy proxy(*conn_);
    EXPECT_FALSE(proxy.file_exists("../../etc/passwd"));
    auto info = proxy.get_file_info("../../../etc/passwd");
    EXPECT_EQ(info.size, 0);
}

TEST_F(BackupAgentTest, RejectsPathTraversalUpload) {
    BackupAgentProxy proxy(*conn_);

    FileChunk chunk;
    chunk.path = "../../etc/evil.txt";
    chunk.offset = 0;
    std::string content = "should not be written";
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_FALSE(result.success);
}

TEST_F(BackupAgentTest, RejectsPathTraversalDelete) {
    populate_sandbox();
    BackupAgentProxy proxy(*conn_);

    // Should fail, not delete anything outside root
    EXPECT_FALSE(proxy.delete_file("../../etc/passwd"));
    // Original files still intact
    EXPECT_TRUE(proxy.file_exists("hello.txt"));
}

// =============================================================================
// Edge Case Tests (from validation review)
// =============================================================================

TEST_F(BackupAgentTest, OverwriteShorterPayload) {
    BackupAgentProxy proxy(*conn_);

    // Upload a longer file first
    auto upload = [&](const std::string& content) {
        FileChunk chunk;
        chunk.path = "overwrite.txt";
        chunk.offset = 0;
        chunk.data = std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(content.data()),
            reinterpret_cast<const std::byte*>(content.data()) + content.size());
        chunk.is_final = true;
        return proxy.receive_chunk(chunk);
    };

    upload("hello world long content");
    EXPECT_EQ(proxy.get_file_info("overwrite.txt").size, 24);

    // Overwrite with shorter content - must not leave stale tail bytes
    upload("short");
    auto info = proxy.get_file_info("overwrite.txt");
    EXPECT_EQ(info.size, 5);
}

TEST_F(BackupAgentTest, UploadEmptyFile) {
    BackupAgentProxy proxy(*conn_);

    FileChunk chunk;
    chunk.path = "empty.txt";
    chunk.offset = 0;
    chunk.data = {};
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(proxy.file_exists("empty.txt"));
    EXPECT_EQ(proxy.get_file_info("empty.txt").size, 0);
}

TEST_F(BackupAgentTest, ReceiveChunkMultipleVerifyContent) {
    BackupAgentProxy proxy(*conn_);

    std::string part1 = "first-part-";
    std::string part2 = "second-part";

    FileChunk c1;
    c1.path = "verify.txt";
    c1.offset = 0;
    c1.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(part1.data()),
        reinterpret_cast<const std::byte*>(part1.data()) + part1.size());
    c1.is_final = false;
    proxy.receive_chunk(c1);

    FileChunk c2;
    c2.path = "verify.txt";
    c2.offset = static_cast<i64>(part1.size());
    c2.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(part2.data()),
        reinterpret_cast<const std::byte*>(part2.data()) + part2.size());
    c2.is_final = true;
    proxy.receive_chunk(c2);

    // Stream the file back and verify the assembled content
    Buffer req;
    encode_string(req, std::string("verify.txt"));
    auto reader = conn_->call_streaming(kService_BackupAgent_Stream,
                                        kMethod_BackupAgent_stream_file, req);

    std::vector<std::byte> data;
    while (reader.next()) {
        auto chunk_data = decode_bytes(reader.chunk());
        data.insert(data.end(), chunk_data.begin(), chunk_data.end());
    }
    std::string content(reinterpret_cast<const char*>(data.data()), data.size());
    EXPECT_EQ(content, "first-part-second-part");
}

TEST_F(BackupAgentTest, RejectsNegativeOffset) {
    BackupAgentProxy proxy(*conn_);

    FileChunk chunk;
    chunk.path = "negative.txt";
    chunk.offset = -1;
    std::string content = "bad offset";
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(proxy.file_exists("negative.txt"));
}

TEST_F(BackupAgentTest, ValidFilenameWithDoubleDots) {
    BackupAgentProxy proxy(*conn_);

    // "resume..final.txt" contains ".." as a substring but NOT as a path component
    std::string content = "valid filename";
    FileChunk chunk;
    chunk.path = "resume..final.txt";
    chunk.offset = 0;
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    auto result = proxy.receive_chunk(chunk);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(proxy.file_exists("resume..final.txt"));
}

// =============================================================================
// Multi-Client Tests (Phase 3)
// Uses the same fixture -- service runs in multi-client mode by default
// =============================================================================

TEST_F(BackupAgentTest, MultiClientListFiles) {
    populate_sandbox();

    // Create a second connection
    ServiceManager mgr2;
    mgr2.register_remote_service("backup2", "127.0.0.1", kTestPort, 1);
    auto conn2 = std::make_unique<ServiceConnection>(mgr2.connect("backup2"));

    BackupAgentProxy proxy1(*conn_);
    BackupAgentProxy proxy2(*conn2);

    // Both clients should see the same files
    auto files1 = proxy1.list_files();
    auto files2 = proxy2.list_files();

    EXPECT_EQ(files1.size(), files2.size());
    EXPECT_EQ(files1.size(), 3u);
}

TEST_F(BackupAgentTest, MultiClientConcurrentUpload) {
    BackupAgentProxy proxy1(*conn_);

    // Create a second connection
    ServiceManager mgr2;
    mgr2.register_remote_service("backup2", "127.0.0.1", kTestPort, 1);
    auto conn2 = std::make_unique<ServiceConnection>(mgr2.connect("backup2"));
    BackupAgentProxy proxy2(*conn2);

    // Upload different files from each client concurrently
    auto upload = [](BackupAgentProxy& proxy, const std::string& name,
                     const std::string& content) {
        FileChunk chunk;
        chunk.path = name;
        chunk.offset = 0;
        chunk.data = std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(content.data()),
            reinterpret_cast<const std::byte*>(content.data()) + content.size());
        chunk.is_final = true;
        return proxy.receive_chunk(chunk);
    };

    auto r1 = upload(proxy1, "client1.txt", "data from client 1");
    auto r2 = upload(proxy2, "client2.txt", "data from client 2");

    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);

    // Both files should exist
    EXPECT_TRUE(proxy1.file_exists("client1.txt"));
    EXPECT_TRUE(proxy1.file_exists("client2.txt"));
}

TEST_F(BackupAgentTest, MultiClientStreamSameFile) {
    populate_sandbox();

    // Create a second connection
    ServiceManager mgr2;
    mgr2.register_remote_service("backup2", "127.0.0.1", kTestPort, 1);
    auto conn2 = std::make_unique<ServiceConnection>(mgr2.connect("backup2"));

    // Both clients stream the same file
    auto stream_file = [](ServiceConnection& conn) -> std::string {
        Buffer req;
        encode_string(req, std::string("hello.txt"));
        auto reader = conn.call_streaming(kService_BackupAgent_Stream,
                                          kMethod_BackupAgent_stream_file, req);
        std::vector<std::byte> data;
        while (reader.next()) {
            auto chunk = decode_bytes(reader.chunk());
            data.insert(data.end(), chunk.begin(), chunk.end());
        }
        return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    };

    auto content1 = stream_file(*conn_);
    auto content2 = stream_file(*conn2);

    EXPECT_EQ(content1, "Hello, Song!");
    EXPECT_EQ(content2, "Hello, Song!");
}

// =============================================================================
// HMAC Security Tests (Phase 3)
// Separate fixture that launches service with --key
// =============================================================================

static const std::string kTestKey = "song-backup-test-key-32-bytes!!!";
static const std::string kWrongKey = "wrong-backup-key-will-not-work!!";

class SecureBackupAgentTest : public ::testing::Test {
protected:
    static constexpr u16 kTestPort = 19877;

    void SetUp() override {
        sandbox_ = fs::temp_directory_path() / ("song_backup_sec_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(sandbox_);

        // Create a test file
        write_file(sandbox_ / "secret.txt", "confidential data");

        std::string path = get_service_path();
        if (!fs::exists(path)) {
            GTEST_SKIP() << "Backup service not found at " << path;
        }

        std::string port_str = std::to_string(kTestPort);
        std::string root_str = sandbox_.string();

        server_pid_ = fork();
        if (server_pid_ == 0) {
            execl(path.c_str(), path.c_str(),
                  "--port", port_str.c_str(),
                  "--root", root_str.c_str(),
                  "--key", kTestKey.c_str(),
                  nullptr);
            _exit(1);
        }

        // Wait for server with retry
        for (int attempt = 0; attempt < 10; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
            try {
                auto tcp = std::make_unique<TcpTransport>();
                tcp->connect("127.0.0.1", kTestPort, 1000);
                return;  // Server is up
            } catch (const std::exception&) {
                // Not ready yet
            }
        }
    }

    void TearDown() override {
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
            int status;
            waitpid(server_pid_, &status, 0);
        }
        if (fs::exists(sandbox_)) {
            fs::remove_all(sandbox_);
        }
    }

    // Create a secure ServiceConnection with proper init handshake
    std::unique_ptr<ServiceConnection> connect_secure(const std::string& key) {
        auto tcp = std::make_unique<TcpTransport>();
        tcp->connect("127.0.0.1", kTestPort, 5000);
        auto secure = std::make_unique<SecureTransport>(
            std::move(tcp), SecurityConfig(key));
        auto conn = std::make_unique<ServiceConnection>(std::move(secure));
        conn->init_handshake();
        return conn;
    }

    pid_t server_pid_ = -1;
    fs::path sandbox_;
};

TEST_F(SecureBackupAgentTest, SecureConnectionWorks) {
    auto conn = connect_secure(kTestKey);
    BackupAgentProxy proxy(*conn);

    auto files = proxy.list_files();
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].path, "secret.txt");
}

TEST_F(SecureBackupAgentTest, SecureMultipleCalls) {
    auto conn = connect_secure(kTestKey);
    BackupAgentProxy proxy(*conn);

    // Multiple calls over the same secure connection
    EXPECT_TRUE(proxy.file_exists("secret.txt"));
    auto info = proxy.get_file_info("secret.txt");
    EXPECT_EQ(info.size, 17);
    auto used = proxy.get_storage_used();
    EXPECT_EQ(used, 17);
}

TEST_F(SecureBackupAgentTest, SecureStreamingWorks) {
    auto conn = connect_secure(kTestKey);

    Buffer req;
    encode_string(req, std::string("secret.txt"));
    auto reader = conn->call_streaming(kService_BackupAgent_Stream,
                                       kMethod_BackupAgent_stream_file, req);

    std::vector<std::byte> data;
    while (reader.next()) {
        auto chunk = decode_bytes(reader.chunk());
        data.insert(data.end(), chunk.begin(), chunk.end());
    }
    EXPECT_TRUE(reader.complete());

    std::string content(reinterpret_cast<const char*>(data.data()), data.size());
    EXPECT_EQ(content, "confidential data");
}

TEST_F(SecureBackupAgentTest, WrongKeyRejected) {
    // Connect with wrong key -- should fail during handshake or first call
    EXPECT_THROW({
        auto conn = connect_secure(kWrongKey);
        BackupAgentProxy proxy(*conn);
        proxy.list_files();
    }, std::exception);
}

TEST_F(SecureBackupAgentTest, SecureMultiClient) {
    auto conn1 = connect_secure(kTestKey);
    auto conn2 = connect_secure(kTestKey);

    BackupAgentProxy proxy1(*conn1);
    BackupAgentProxy proxy2(*conn2);

    // Both secure clients should work
    auto files1 = proxy1.list_files();
    auto files2 = proxy2.list_files();

    EXPECT_EQ(files1.size(), 1u);
    EXPECT_EQ(files2.size(), 1u);
    EXPECT_EQ(files1[0].path, files2[0].path);
}

// =============================================================================
// Progress Notification Tests (Phase 4)
// =============================================================================

TEST_F(BackupAgentTest, ProgressUpdatedByUpload) {
    BackupAgentProxy proxy(*conn_);

    // Upload a file, then check progress was updated
    std::string content = "progress test data";
    FileChunk chunk;
    chunk.path = "progress_test.txt";
    chunk.offset = 0;
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    proxy.receive_chunk(chunk);

    auto progress = proxy.get_progress();
    EXPECT_EQ(progress.files_done, 1);
    EXPECT_EQ(progress.bytes_done, static_cast<i64>(content.size()));
    EXPECT_EQ(progress.current_file, "progress_test.txt");
}

TEST_F(BackupAgentTest, ProgressSubscribeNotification) {
    // Use a separate connection for notifications to avoid sequence ID clash.
    // When the dispatcher sends a notification to a subscribed transport *during*
    // RPC dispatch, it arrives before the RPC result, which breaks call().
    // Solution: subscribe on conn2, call on conn1.
    ServiceManager mgr2;
    mgr2.register_remote_service("backup_notify", "127.0.0.1", kTestPort, 1);
    auto conn2 = std::make_unique<ServiceConnection>(mgr2.connect("backup_notify"));

    bool notification_received = false;
    Progress received_progress{};

    conn2->subscribe_property(kType_BackupAgent, kObject_BackupAgent, kProp_progress,
        [&](const Buffer& value) {
            Buffer& mutable_val = const_cast<Buffer&>(value);
            mutable_val.reset_read();
            received_progress = decode_Progress(mutable_val);
            notification_received = true;
        });

    // Upload a file via the primary connection to trigger notification on conn2
    BackupAgentProxy proxy(*conn_);
    std::string content = "notification trigger data";
    FileChunk chunk;
    chunk.path = "notify_test.txt";
    chunk.offset = 0;
    chunk.data = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(content.data()),
        reinterpret_cast<const std::byte*>(content.data()) + content.size());
    chunk.is_final = true;

    proxy.receive_chunk(chunk);

    // Poll conn2 for notifications
    for (int ndx = 0; ndx < 10 && !notification_received; ++ndx) {
        conn2->poll_notifications(100);
    }

    EXPECT_TRUE(notification_received);
    EXPECT_EQ(received_progress.current_file, "notify_test.txt");
    EXPECT_GT(received_progress.bytes_done, 0);
}

// =============================================================================
// mDNS Discovery Test (Phase 4, macOS-only)
// =============================================================================

class DiscoverableBackupTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Skip ONLY when this host has no mDNS/DNS-SD stack. The old
        // "#ifndef __APPLE__ skip" predated Linux Avahi support and hid that
        // this test could run there; the test also connected to the wrong
        // instance name and swallowed the failure as a skip (see below).
        auto discovery = create_discovery();
        if (!discovery || !discovery->is_available()) {
            GTEST_SKIP() << "mDNS/DNS-SD not available on this host";
        }
        sandbox_ = fs::temp_directory_path() / ("song_backup_disc_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(sandbox_);
        write_file(sandbox_ / "found.txt", "discovered!");

        std::string path = get_service_path();
        if (!fs::exists(path)) {
            GTEST_SKIP() << "Backup service binary not built at " << path;
        }

        std::string root_str = sandbox_.string();

        server_pid_ = fork();
        if (server_pid_ == 0) {
            execl(path.c_str(), path.c_str(),
                  "--port", "0",
                  "--root", root_str.c_str(),
                  "--discover",
                  nullptr);
            _exit(1);
        }

        // Wait for mDNS registration
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    void TearDown() override {
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
            int status;
            waitpid(server_pid_, &status, 0);
        }
        if (fs::exists(sandbox_)) {
            fs::remove_all(sandbox_);
        }
    }

    pid_t server_pid_ = -1;
    fs::path sandbox_;
};

TEST_F(DiscoverableBackupTest, DiscoverAndConnect) {
    // The service advertises instance "BackupAgent" (backup_service.cpp:532),
    // so connect() must browse for that name -- connect("backup") could never
    // match, and the catch->skip hid it. mDNS is confirmed available in
    // SetUp, so a discovery/connect failure here is a real failure.
    ServiceManager mgr;
    mgr.register_discoverable_service("BackupAgent", "backup", 1);

    auto conn = std::make_unique<ServiceConnection>(mgr.connect("BackupAgent"));
    BackupAgentProxy proxy(*conn);

    auto files = proxy.list_files();
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].path, "found.txt");
}

// =============================================================================
// End-to-End Incremental Backup Workflow (Phase 4)
// =============================================================================

TEST_F(BackupAgentTest, IncrementalBackupWorkflow) {
    BackupAgentProxy proxy(*conn_);

    // Step 1: Upload initial files
    auto upload = [&](const std::string& name, const std::string& content) {
        FileChunk chunk;
        chunk.path = name;
        chunk.offset = 0;
        chunk.data = std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(content.data()),
            reinterpret_cast<const std::byte*>(content.data()) + content.size());
        chunk.is_final = true;
        return proxy.receive_chunk(chunk);
    };

    upload("file1.txt", "original content 1");
    upload("file2.txt", "original content 2");

    // Step 2: Record timestamp
    auto files_before = proxy.list_files();
    ASSERT_EQ(files_before.size(), 2u);

    // Use the max modification time as our "last backup" timestamp
    i64 last_backup_ts = 0;
    for (const auto& f : files_before) {
        if (f.modified_at > last_backup_ts) {
            last_backup_ts = f.modified_at;
        }
    }

    // Step 3: Wait a moment, then modify one file
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    upload("file1.txt", "modified content 1");

    // Step 4: Query changes since last backup
    auto changed = proxy.changes_since(last_backup_ts + 1);

    // Only file1.txt should be in the changes list
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0].path, "file1.txt");

    // Step 5: Verify checksums differ
    auto c1_old = proxy.compute_checksum("file1.txt");
    auto c2 = proxy.compute_checksum("file2.txt");
    EXPECT_NE(c1_old, c2);

    // Step 6: Stream the changed file back and verify content
    Buffer req;
    encode_string(req, std::string("file1.txt"));
    auto reader = conn_->call_streaming(kService_BackupAgent_Stream,
                                        kMethod_BackupAgent_stream_file, req);

    std::vector<std::byte> data;
    while (reader.next()) {
        auto chunk_data = decode_bytes(reader.chunk());
        data.insert(data.end(), chunk_data.begin(), chunk_data.end());
    }

    std::string content(reinterpret_cast<const char*>(data.data()), data.size());
    EXPECT_EQ(content, "modified content 1");
}
