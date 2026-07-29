// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/stream.hpp"
#include "song/transport.hpp"
#include "song/error.hpp"
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace song {

namespace {

bool write_all(int fd, const void* data, size_t len) {
    const auto* ptr = static_cast<const std::byte*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t written = ::write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

} // anonymous namespace

// =============================================================================
// StreamWriter Implementation
// =============================================================================

StreamWriter::StreamWriter(Transport& transport, u32 sequence_id)
    : transport_(&transport)
    , sequence_id_(sequence_id) {}

StreamWriter::StreamWriter(int write_fd, u32 sequence_id)
    : fd_(write_fd)
    , sequence_id_(sequence_id) {}

StreamWriter::~StreamWriter() {
    if (!ended_) {
        try { end(); } catch (...) {}
    }
}

StreamWriter::StreamWriter(StreamWriter&& other) noexcept
    : transport_(other.transport_)
    , fd_(other.fd_)
    , sequence_id_(other.sequence_id_)
    , ended_(other.ended_) {
    other.ended_ = true;  // Prevent double-end
}

StreamWriter& StreamWriter::operator=(StreamWriter&& other) noexcept {
    if (this != &other) {
        if (!ended_) {
            try { end(); } catch (...) {}
        }
        transport_ = other.transport_;
        fd_ = other.fd_;
        sequence_id_ = other.sequence_id_;
        ended_ = other.ended_;
        other.ended_ = true;
    }
    return *this;
}

void StreamWriter::write(const Buffer& chunk) {
    if (ended_) {
        throw ServiceError("StreamWriter: cannot write after stream ended");
    }

    Buffer msg = wire::create_stream_message(sequence_id_, chunk);

    if (transport_) {
        transport_->send(msg);
    } else if (fd_ >= 0) {
        if (!write_all(fd_, msg.data(), msg.size())) {
            throw ServiceError("StreamWriter: write failed");
        }
    }
}

void StreamWriter::abort() {
    ended_ = true;
}

void StreamWriter::end() {
    if (ended_) {
        return;
    }
    ended_ = true;

    Buffer msg = wire::create_stream_end_message(sequence_id_);

    if (transport_) {
        transport_->send(msg);
    } else if (fd_ >= 0) {
        write_all(fd_, msg.data(), msg.size());
    }
}

// =============================================================================
// StreamReader Implementation
// =============================================================================

void StreamReader::add_chunk(Buffer chunk) {
    chunks_.push_back(std::move(chunk));
}

bool StreamReader::next() {
    if (index_ < chunks_.size()) {
        return true;
    }
    return false;
}

Buffer& StreamReader::chunk() {
    if (index_ >= chunks_.size()) {
        throw ServiceError("StreamReader: no chunk available");
    }
    Buffer& c = chunks_[index_];
    ++index_;
    return c;
}

const Buffer& StreamReader::chunk() const {
    if (index_ >= chunks_.size()) {
        throw ServiceError("StreamReader: no chunk available");
    }
    return chunks_[index_];
}

} // namespace song
