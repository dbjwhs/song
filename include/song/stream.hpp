// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "wire.hpp"
#include <functional>
#include <vector>

namespace song {

// Forward declarations
class Transport;
class ServiceProcess;

/// Service-side stream writer
/// Used by streaming dispatchers to send incremental results back to the client.
/// Sends MSG_STREAM for each chunk, then MSG_STREAM_END when done.
///
/// Usage in a dispatcher:
///   void my_dispatcher(u16 method_id, Buffer& request, StreamWriter& writer) {
///       for (auto& chunk : data) {
///           Buffer buf;
///           encode_string(buf, chunk);
///           writer.write(buf);
///       }
///   }  // stream_end sent automatically on destruction (or call end() explicitly)
class StreamWriter {
    Transport* transport_ = nullptr;
    int fd_ = -1;  // For pipe-based (fd mode)
    u32 sequence_id_ = 0;
    bool ended_ = false;

public:
    /// Create writer for Transport-based connections (TCP, TLS)
    StreamWriter(Transport& transport, u32 sequence_id);

    /// Create writer for fd-based connections (pipe/stdout)
    StreamWriter(int write_fd, u32 sequence_id);

    /// Destructor sends stream_end if not already sent
    ~StreamWriter();

    // Non-copyable, movable
    StreamWriter(const StreamWriter&) = delete;
    StreamWriter& operator=(const StreamWriter&) = delete;
    StreamWriter(StreamWriter&& other) noexcept;
    StreamWriter& operator=(StreamWriter&& other) noexcept;

    /// Write a chunk to the stream
    /// @param chunk Buffer containing the chunk payload
    void write(const Buffer& chunk);

    /// Signal end of stream (called automatically by destructor)
    void end();

    /// Check if the stream has been ended
    bool ended() const { return ended_; }

    /// Get the sequence ID for this stream
    u32 sequence_id() const { return sequence_id_; }
};

/// Streaming dispatcher function signature
/// Takes (method_id, request, writer) instead of (method_id, request, response)
using StreamDispatcher = std::function<void(u16, Buffer&, StreamWriter&)>;

/// Client-side stream reader
/// Collects MSG_STREAM messages until MSG_STREAM_END, matching by sequence_id.
///
/// Usage:
///   auto reader = conn.call_streaming(service_id, method_id, args);
///   while (reader.next()) {
///       Buffer& chunk = reader.chunk();
///       // process chunk
///   }
class StreamReader {
    std::vector<Buffer> chunks_;
    size_t index_ = 0;
    u32 sequence_id_ = 0;
    bool complete_ = false;

public:
    StreamReader() = default;
    explicit StreamReader(u32 sequence_id) : sequence_id_(sequence_id) {}

    /// Add a chunk (used internally by ServiceConnection and custom readers)
    void add_chunk(Buffer chunk);

    /// Mark the stream as complete (stream_end received)
    void set_complete() { complete_ = true; }

    /// Advance to the next chunk
    /// @return true if a chunk is available, false if stream ended
    bool next();

    /// Get the current chunk (valid after next() returns true)
    Buffer& chunk();
    const Buffer& chunk() const;

    /// Get all chunks collected so far
    const std::vector<Buffer>& chunks() const { return chunks_; }

    /// Get total number of chunks received
    size_t chunk_count() const { return chunks_.size(); }

    /// Check if the stream has completed (stream_end received)
    bool complete() const { return complete_; }

    /// Get the sequence ID
    u32 sequence_id() const { return sequence_id_; }
};

} // namespace song
