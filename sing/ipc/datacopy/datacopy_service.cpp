// MIT License
// Copyright (c) 2026 dbjwhs

// Data Copy Service - Server Implementation
// In-memory file storage with chunked transfer support

#include <song/song.hpp>
#include <song/logging.hpp>
#include "datacopy.hpp"
#include <unordered_map>
#include <algorithm>
#include <string>

using namespace song;
using namespace song::datacopy;

// In-memory file storage
struct StoredFile {
    std::vector<std::byte> data;
    bool complete = false;
};

class DataCopyImpl : public IDataCopy {
    std::unordered_map<std::string, StoredFile> m_files;

public:
    WriteResult write_chunk(const FileChunk& chunk) override {
        WriteResult result;
        result.success = false;

        // chunk.offset and chunk.data come straight off the wire from an
        // untrusted client. Validate them before using offset to index or size
        // the buffer: a negative offset would std::copy before the allocation
        // (heap underflow / out-of-bounds write), and an oversized offset or
        // chunk would drive an unbounded resize (memory-exhaustion DoS). Reject
        // anything that would place the write outside a bounded per-file window.
        constexpr i64 kMaxFileSize = 64 * 1024 * 1024;  // 64 MB per file
        if (chunk.offset < 0 ||
            chunk.data.size() > static_cast<size_t>(kMaxFileSize) ||
            chunk.offset > kMaxFileSize - static_cast<i64>(chunk.data.size())) {
            return result;  // success stays false
        }

        auto& file = m_files[chunk.filename];

        // Ensure file has enough space (required_size is now bounded by kMaxFileSize)
        i64 required_size = chunk.offset + static_cast<i64>(chunk.data.size());
        if (static_cast<i64>(file.data.size()) < required_size) {
            file.data.resize(static_cast<size_t>(required_size));
        }

        // Copy data at offset
        std::copy(chunk.data.begin(), chunk.data.end(),
                  file.data.begin() + chunk.offset);

        // Mark complete if final chunk
        if (chunk.is_final) {
            file.complete = true;
        }

        result.success = true;
        result.bytes_written = static_cast<i64>(chunk.data.size());
        result.total_received = static_cast<i64>(file.data.size());

        return result;
    }

    ReadResult read_file(const std::string& filename) override {
        ReadResult result;

        auto it = m_files.find(filename);
        if (it == m_files.end()) {
            result.found = false;
            result.size = 0;
            return result;
        }

        result.found = true;
        result.data = it->second.data;
        result.size = static_cast<i64>(it->second.data.size());

        return result;
    }

    FileInfo get_file_info(const std::string& filename) override {
        FileInfo info;
        info.filename = filename;

        auto it = m_files.find(filename);
        if (it == m_files.end()) {
            info.size = 0;
            info.complete = false;
        } else {
            info.size = static_cast<i64>(it->second.data.size());
            info.complete = it->second.complete;
        }

        return info;
    }

    std::vector<FileInfo> list_files() override {
        std::vector<FileInfo> files;
        files.reserve(m_files.size());

        for (const auto& [name, file] : m_files) {
            FileInfo info;
            info.filename = name;
            info.size = static_cast<i64>(file.data.size());
            info.complete = file.complete;
            files.push_back(info);
        }

        return files;
    }

    bool delete_file(const std::string& filename) override {
        return m_files.erase(filename) > 0;
    }

    i32 clear_all() override {
        i32 count = static_cast<i32>(m_files.size());
        m_files.clear();
        return count;
    }

    bool file_exists(const std::string& filename) override {
        return m_files.find(filename) != m_files.end();
    }

    i64 get_storage_used() override {
        i64 total = 0;
        for (const auto& [_, file] : m_files) {
            total += static_cast<i64>(file.data.size());
        }
        return total;
    }
};

static DataCopyImpl g_datacopy;

void datacopy_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_DataCopy(g_datacopy, method_id, request, response);
}

int main() {
    Log::info("IPC DataCopy service starting");

    ServiceRuntime runtime;

    // Register data copy service dispatcher
    runtime.register_dispatcher(kService_DataCopy, datacopy_dispatcher);

    // Register all methods for capability exchange
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_write_chunk);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_read_file);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_get_file_info);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_list_files);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_delete_file);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_clear_all);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_file_exists);
    runtime.register_method(kService_DataCopy, kMethod_DataCopy_get_storage_used);

    Log::debug("Service ready: " + std::to_string(runtime.service_count()) + " service(s), " +
               std::to_string(runtime.method_count()) + " method(s)");

    // Run the service
    runtime.run();

    return 0;
}
