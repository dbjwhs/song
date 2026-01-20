// MIT License
// Copyright (c) 2026 dbjwhs

// Data Copy Service - Integration Tests
// Tests file transfer with binary data and chunked operations

#include <gtest/gtest.h>
#include <song/song.hpp>
#include "datacopy.hpp"
#include <unistd.h>
#include <algorithm>

using namespace song;
using namespace song::datacopy;

class DataCopyTest : public ::testing::Test {
protected:
    ServiceProcess proc_;
    std::unique_ptr<ServiceConnection> conn_;
    std::unique_ptr<DataCopyProxy> dc_;

    void SetUp() override {
        // Find the service executable
        std::string path = "./sing_datacopy_service";
        if (access(path.c_str(), X_OK) != 0) {
            path = "./sing/datacopy/sing_datacopy_service";
        }

        proc_ = ServiceProcess::spawn(path.c_str());
        conn_ = std::make_unique<ServiceConnection>(&proc_);
        dc_ = std::make_unique<DataCopyProxy>(*conn_);

        // Clear any state from previous tests
        dc_->clear_all();
    }

    void TearDown() override {
        dc_.reset();
        conn_.reset();
    }

    // Helper to create bytes from a string
    std::vector<std::byte> make_bytes(const std::string& s) {
        std::vector<std::byte> result;
        result.reserve(s.size());
        for (char c : s) {
            result.push_back(static_cast<std::byte>(c));
        }
        return result;
    }

    // Helper to convert bytes back to string
    std::string bytes_to_string(const std::vector<std::byte>& bytes) {
        std::string result;
        result.reserve(bytes.size());
        for (std::byte b : bytes) {
            result.push_back(static_cast<char>(b));
        }
        return result;
    }

    // Helper to create a FileChunk
    FileChunk make_chunk(const std::string& filename, i64 offset,
                         const std::string& data, bool is_final = false) {
        FileChunk chunk;
        chunk.filename = filename;
        chunk.offset = offset;
        chunk.data = make_bytes(data);
        chunk.is_final = is_final;
        return chunk;
    }
};

// ============================================================================
// Basic Write/Read Tests
// ============================================================================

TEST_F(DataCopyTest, WriteSingleChunk) {
    auto chunk = make_chunk("test.txt", 0, "Hello, World!", true);
    auto result = dc_->write_chunk(chunk);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.bytes_written, 13);
    EXPECT_EQ(result.total_received, 13);
}

TEST_F(DataCopyTest, WriteAndReadFile) {
    std::string content = "This is test content for the file.";
    auto chunk = make_chunk("readme.txt", 0, content, true);
    dc_->write_chunk(chunk);

    auto result = dc_->read_file("readme.txt");

    EXPECT_TRUE(result.found);
    EXPECT_EQ(result.size, static_cast<i64>(content.size()));
    EXPECT_EQ(bytes_to_string(result.data), content);
}

TEST_F(DataCopyTest, ReadNonexistentFile) {
    auto result = dc_->read_file("does_not_exist.txt");

    EXPECT_FALSE(result.found);
    EXPECT_EQ(result.size, 0);
    EXPECT_TRUE(result.data.empty());
}

TEST_F(DataCopyTest, WriteEmptyFile) {
    auto chunk = make_chunk("empty.txt", 0, "", true);
    auto result = dc_->write_chunk(chunk);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.bytes_written, 0);

    auto read_result = dc_->read_file("empty.txt");
    EXPECT_TRUE(read_result.found);
    EXPECT_EQ(read_result.size, 0);
}

// ============================================================================
// Chunked Transfer Tests
// ============================================================================

TEST_F(DataCopyTest, WriteMultipleChunks) {
    // Write file in 3 chunks
    dc_->write_chunk(make_chunk("chunked.txt", 0, "Part1", false));
    dc_->write_chunk(make_chunk("chunked.txt", 5, "Part2", false));
    auto result = dc_->write_chunk(make_chunk("chunked.txt", 10, "Part3", true));

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.total_received, 15);

    // Verify combined content
    auto read_result = dc_->read_file("chunked.txt");
    EXPECT_TRUE(read_result.found);
    EXPECT_EQ(bytes_to_string(read_result.data), "Part1Part2Part3");
}

TEST_F(DataCopyTest, WriteChunksOutOfOrder) {
    // Write chunks out of order
    dc_->write_chunk(make_chunk("outoforder.txt", 10, "Third", false));
    dc_->write_chunk(make_chunk("outoforder.txt", 0, "First", false));
    dc_->write_chunk(make_chunk("outoforder.txt", 5, "Secon", true));

    auto read_result = dc_->read_file("outoforder.txt");
    EXPECT_TRUE(read_result.found);
    EXPECT_EQ(bytes_to_string(read_result.data), "FirstSeconThird");
}

TEST_F(DataCopyTest, OverwriteChunk) {
    // Write initial data (10 A's)
    dc_->write_chunk(make_chunk("overwrite.txt", 0, "AAAAAAAAAA", true));

    // Overwrite middle portion (positions 3,4,5)
    dc_->write_chunk(make_chunk("overwrite.txt", 3, "BBB", false));

    // Result: AAA + BBB + AAAA = AAABBBAAAA (still 10 chars)
    auto read_result = dc_->read_file("overwrite.txt");
    EXPECT_EQ(bytes_to_string(read_result.data), "AAABBBAAAA");
}

// ============================================================================
// Binary Data Tests
// ============================================================================

TEST_F(DataCopyTest, WriteBinaryData) {
    // Create binary data with all byte values
    std::vector<std::byte> binary_data;
    for (int i = 0; i < 256; ++i) {
        binary_data.push_back(static_cast<std::byte>(i));
    }

    FileChunk chunk;
    chunk.filename = "binary.dat";
    chunk.offset = 0;
    chunk.data = binary_data;
    chunk.is_final = true;

    dc_->write_chunk(chunk);

    auto result = dc_->read_file("binary.dat");
    EXPECT_TRUE(result.found);
    EXPECT_EQ(result.size, 256);
    EXPECT_EQ(result.data, binary_data);
}

TEST_F(DataCopyTest, WriteLargeFile) {
    // Write 100KB file in chunks
    const int chunk_size = 10000;
    const int num_chunks = 10;
    std::vector<std::byte> expected_data;

    for (int i = 0; i < num_chunks; ++i) {
        std::vector<std::byte> chunk_data;
        for (int j = 0; j < chunk_size; ++j) {
            std::byte b = static_cast<std::byte>((i * chunk_size + j) % 256);
            chunk_data.push_back(b);
            expected_data.push_back(b);
        }

        FileChunk chunk;
        chunk.filename = "large.bin";
        chunk.offset = i * chunk_size;
        chunk.data = chunk_data;
        chunk.is_final = (i == num_chunks - 1);

        auto result = dc_->write_chunk(chunk);
        EXPECT_TRUE(result.success);
    }

    auto read_result = dc_->read_file("large.bin");
    EXPECT_TRUE(read_result.found);
    EXPECT_EQ(read_result.size, chunk_size * num_chunks);
    EXPECT_EQ(read_result.data, expected_data);
}

// ============================================================================
// File Info Tests
// ============================================================================

TEST_F(DataCopyTest, GetFileInfoExisting) {
    dc_->write_chunk(make_chunk("info_test.txt", 0, "Test content", true));

    auto info = dc_->get_file_info("info_test.txt");

    EXPECT_EQ(info.filename, "info_test.txt");
    EXPECT_EQ(info.size, 12);
    EXPECT_TRUE(info.complete);
}

TEST_F(DataCopyTest, GetFileInfoIncomplete) {
    dc_->write_chunk(make_chunk("incomplete.txt", 0, "Part one", false));

    auto info = dc_->get_file_info("incomplete.txt");

    EXPECT_EQ(info.filename, "incomplete.txt");
    EXPECT_EQ(info.size, 8);
    EXPECT_FALSE(info.complete);
}

TEST_F(DataCopyTest, GetFileInfoNonexistent) {
    auto info = dc_->get_file_info("nonexistent.txt");

    EXPECT_EQ(info.filename, "nonexistent.txt");
    EXPECT_EQ(info.size, 0);
    EXPECT_FALSE(info.complete);
}

// ============================================================================
// List Files Tests
// ============================================================================

TEST_F(DataCopyTest, ListFilesEmpty) {
    auto files = dc_->list_files();
    EXPECT_TRUE(files.empty());
}

TEST_F(DataCopyTest, ListFilesMultiple) {
    dc_->write_chunk(make_chunk("file1.txt", 0, "Content1", true));
    dc_->write_chunk(make_chunk("file2.txt", 0, "Content2", true));
    dc_->write_chunk(make_chunk("file3.txt", 0, "Content3", true));

    auto files = dc_->list_files();

    EXPECT_EQ(files.size(), 3);

    // Check all files are present (order may vary)
    std::vector<std::string> names;
    for (const auto& f : files) {
        names.push_back(f.filename);
    }
    std::sort(names.begin(), names.end());

    EXPECT_EQ(names[0], "file1.txt");
    EXPECT_EQ(names[1], "file2.txt");
    EXPECT_EQ(names[2], "file3.txt");
}

// ============================================================================
// Delete File Tests
// ============================================================================

TEST_F(DataCopyTest, DeleteExistingFile) {
    dc_->write_chunk(make_chunk("to_delete.txt", 0, "Delete me", true));
    EXPECT_TRUE(dc_->file_exists("to_delete.txt"));

    bool deleted = dc_->delete_file("to_delete.txt");

    EXPECT_TRUE(deleted);
    EXPECT_FALSE(dc_->file_exists("to_delete.txt"));
}

TEST_F(DataCopyTest, DeleteNonexistentFile) {
    bool deleted = dc_->delete_file("does_not_exist.txt");
    EXPECT_FALSE(deleted);
}

// ============================================================================
// Clear All Tests
// ============================================================================

TEST_F(DataCopyTest, ClearAllFiles) {
    dc_->write_chunk(make_chunk("file1.txt", 0, "A", true));
    dc_->write_chunk(make_chunk("file2.txt", 0, "B", true));
    dc_->write_chunk(make_chunk("file3.txt", 0, "C", true));

    i32 cleared = dc_->clear_all();

    EXPECT_EQ(cleared, 3);
    EXPECT_TRUE(dc_->list_files().empty());
}

TEST_F(DataCopyTest, ClearAllEmpty) {
    i32 cleared = dc_->clear_all();
    EXPECT_EQ(cleared, 0);
}

// ============================================================================
// File Exists Tests
// ============================================================================

TEST_F(DataCopyTest, FileExistsTrue) {
    dc_->write_chunk(make_chunk("exists.txt", 0, "Yes", true));
    EXPECT_TRUE(dc_->file_exists("exists.txt"));
}

TEST_F(DataCopyTest, FileExistsFalse) {
    EXPECT_FALSE(dc_->file_exists("nope.txt"));
}

// ============================================================================
// Storage Used Tests
// ============================================================================

TEST_F(DataCopyTest, StorageUsedEmpty) {
    EXPECT_EQ(dc_->get_storage_used(), 0);
}

TEST_F(DataCopyTest, StorageUsedAccumulates) {
    dc_->write_chunk(make_chunk("a.txt", 0, "12345", true));      // 5 bytes
    EXPECT_EQ(dc_->get_storage_used(), 5);

    dc_->write_chunk(make_chunk("b.txt", 0, "1234567890", true)); // 10 bytes
    EXPECT_EQ(dc_->get_storage_used(), 15);

    dc_->delete_file("a.txt");
    EXPECT_EQ(dc_->get_storage_used(), 10);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(DataCopyTest, FilenameWithSpaces) {
    dc_->write_chunk(make_chunk("file with spaces.txt", 0, "Content", true));

    auto result = dc_->read_file("file with spaces.txt");
    EXPECT_TRUE(result.found);
    EXPECT_EQ(bytes_to_string(result.data), "Content");
}

TEST_F(DataCopyTest, FilenameWithUnicode) {
    dc_->write_chunk(make_chunk("файл_日本語.txt", 0, "Unicode content", true));

    auto result = dc_->read_file("файл_日本語.txt");
    EXPECT_TRUE(result.found);
    EXPECT_EQ(bytes_to_string(result.data), "Unicode content");
}

TEST_F(DataCopyTest, MultipleFilesSimultaneous) {
    // Start writing two files in interleaved chunks
    dc_->write_chunk(make_chunk("fileA.txt", 0, "A1", false));
    dc_->write_chunk(make_chunk("fileB.txt", 0, "B1", false));
    dc_->write_chunk(make_chunk("fileA.txt", 2, "A2", false));
    dc_->write_chunk(make_chunk("fileB.txt", 2, "B2", false));
    dc_->write_chunk(make_chunk("fileA.txt", 4, "A3", true));
    dc_->write_chunk(make_chunk("fileB.txt", 4, "B3", true));

    auto resultA = dc_->read_file("fileA.txt");
    auto resultB = dc_->read_file("fileB.txt");

    EXPECT_EQ(bytes_to_string(resultA.data), "A1A2A3");
    EXPECT_EQ(bytes_to_string(resultB.data), "B1B2B3");
}
