// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/process.hpp"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdexcept>
#include <cstring>

namespace song {

ServiceProcess::ServiceProcess(pid_t pid, Pipe to_service, Pipe from_service)
    : pid_(pid), to_service_(std::move(to_service)), from_service_(std::move(from_service)) {}

ServiceProcess::~ServiceProcess() {
    if (pid_ > 0) {
        terminate();
    }
}

ServiceProcess::ServiceProcess(ServiceProcess&& other) noexcept
    : pid_(other.pid_)
    , to_service_(std::move(other.to_service_))
    , from_service_(std::move(other.from_service_))
    , reusable_(other.reusable_)
    , negotiated_version_(other.negotiated_version_)
    , methods_(std::move(other.methods_)) {
    other.pid_ = -1;
    other.negotiated_version_ = 0;
}

ServiceProcess& ServiceProcess::operator=(ServiceProcess&& other) noexcept {
    if (this != &other) {
        if (pid_ > 0) {
            terminate();
        }
        pid_ = other.pid_;
        to_service_ = std::move(other.to_service_);
        from_service_ = std::move(other.from_service_);
        reusable_ = other.reusable_;
        negotiated_version_ = other.negotiated_version_;
        methods_ = std::move(other.methods_);
        other.pid_ = -1;
        other.negotiated_version_ = 0;
    }
    return *this;
}

ServiceProcess ServiceProcess::spawn(const char* executable) {
    // Create pipe pairs for bidirectional communication
    auto [parent_read, child_write] = Pipe::create_pair();
    auto [child_read, parent_write] = Pipe::create_pair();

    pid_t pid = fork();
    if (pid < 0) {
        throw ServiceError("Failed to fork service process");
    }

    if (pid == 0) {
        // Child process
        // Redirect stdin to child_read, stdout to child_write
        dup2(child_read.read_fd(), STDIN_FILENO);
        dup2(child_write.write_fd(), STDOUT_FILENO);

        // Close all pipes (duplicated to stdin/stdout)
        parent_read.close();
        parent_write.close();
        child_read.close();
        child_write.close();

        // Execute the service
        execl(executable, executable, nullptr);

        // If we get here, exec failed
        _exit(1);
    }

    // Parent process
    // Close child ends of pipes
    child_read.close();
    child_write.close();

    ServiceProcess proc(pid, std::move(parent_write), std::move(parent_read));

    // Wait for init message from service
    proc.init_handshake();

    return proc;
}

void ServiceProcess::init_handshake() {
    // Wait for init message from service (with timeout)
    Buffer init_msg;
    if (!receive(init_msg, 1000)) {  // 1 second timeout
        throw ServiceError("Service failed to send init message");
    }

    // Decode and validate init message
    auto hdr = wire::decode_header_validated(init_msg);
    if (hdr.type != wire::MsgType::init) {
        throw ProtocolError("Expected init message from service");
    }

    // Decode init payload
    auto init = wire::decode_init(init_msg);
    if (init.magic != wire::kMagic) {
        throw ProtocolError("Invalid magic in init message");
    }

    // Version negotiation
    // Rule 1: If peer's current version < our first supported version, peer is too old
    if (init.current_version < wire::kFirstVersion) {
        throw VersionMismatchError("Service version too old: service=" +
            std::to_string(init.current_version >> 8) + "." +
            std::to_string(init.current_version & 0xFF) +
            ", minimum required=" +
            std::to_string(wire::kFirstVersion >> 8) + "." +
            std::to_string(wire::kFirstVersion & 0xFF));
    }

    // Rule 2: If peer's first supported version > our current version, we are too old
    if (init.first_version > wire::kCurrentVersion) {
        throw VersionMismatchError("Host version too old: host=" +
            std::to_string(wire::kCurrentVersion >> 8) + "." +
            std::to_string(wire::kCurrentVersion & 0xFF) +
            ", service requires=" +
            std::to_string(init.first_version >> 8) + "." +
            std::to_string(init.first_version & 0xFF));
    }

    // Rule 3: Effective version = min(our current, peer current)
    negotiated_version_ = std::min(wire::kCurrentVersion, init.current_version);

    // Decode method list
    methods_.clear();
    methods_.reserve(init.method_count);
    for (u32 i = 0; i < init.method_count; ++i) {
        methods_.push_back(wire::decode_method_descriptor(init_msg));
    }
}

bool ServiceProcess::alive() const {
    if (pid_ <= 0) return false;

    int status;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) {
        return true;  // Still running
    } else if (result == pid_) {
        return false;  // Process exited
    }
    return false;
}

bool ServiceProcess::available() const {
    if (!reusable_ || pid_ <= 0) return false;
    return alive();
}

void ServiceProcess::terminate() {
    if (pid_ <= 0) return;

    // Send SIGTERM
    kill(pid_, SIGTERM);

    // Wait up to 1 second for graceful shutdown
    for (int i = 0; i < 10; ++i) {
        int status;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            pid_ = -1;
            return;
        }
        usleep(100000);  // 100ms
    }

    // Still alive, send SIGKILL
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
    pid_ = -1;
}

void ServiceProcess::send(const Buffer& msg) {
    if (pid_ <= 0) {
        throw ServiceError("Cannot send to dead service");
    }

    // Send the entire message
    size_t offset = 0;
    while (offset < msg.size()) {
        ssize_t written = to_service_.write(
            reinterpret_cast<const char*>(msg.data()) + offset,
            msg.size() - offset
        );
        if (written < 0) {
            throw ServiceError("Failed to write to service pipe");
        }
        offset += written;
    }
}

bool ServiceProcess::receive(Buffer& msg, int timeout_ms) {
    if (pid_ <= 0) {
        return false;
    }

    msg.reset();

    // First, read the header (16 bytes)
    std::byte header_buf[16];
    ssize_t n = timeout_ms >= 0
        ? from_service_.read_timeout(header_buf, 16, timeout_ms)
        : from_service_.read(header_buf, 16);

    if (n == 0) {
        return false;  // EOF - service died
    }
    if (n < 0) {
        throw ServiceError("Failed to read header from service");
    }
    if (n != 16) {
        throw ProtocolError("Incomplete header received");
    }

    msg.write(header_buf, 16);
    msg.reset_read();

    // Decode header to get payload size
    auto hdr = wire::decode_header(msg);

    // Read payload if present
    if (hdr.payload_size > 0) {
        if (hdr.payload_size > wire::kMaxPayloadSize) {
            throw ProtocolError("Payload size exceeds maximum");
        }

        std::vector<std::byte> payload_buf(hdr.payload_size);
        size_t offset = 0;
        while (offset < hdr.payload_size) {
            n = from_service_.read(
                payload_buf.data() + offset,
                hdr.payload_size - offset
            );
            if (n <= 0) {
                throw ServiceError("Failed to read payload from service");
            }
            offset += n;
        }

        msg.write(payload_buf.data(), hdr.payload_size);
    }

    msg.reset_read();
    return true;
}

// ServiceConnection implementation

ServiceConnection::ServiceConnection(ServiceProcess* proc)
    : proc_(proc) {}

Buffer ServiceConnection::call(u16 service_id, u16 method_id, const Buffer& args) {
    if (!proc_ || !proc_->alive()) {
        throw ServiceError("Service not running");
    }

    u32 seq = next_seq_++;

    // Create call message
    Buffer call_msg = wire::create_call_message(seq, service_id, method_id, args);

    // Send to service
    proc_->send(call_msg);

    // Wait for response
    Buffer response;
    if (!proc_->receive(response, 5000)) {  // 5 second timeout
        throw ServiceError("Service died or timed out");
    }

    // Decode response header
    auto hdr = wire::decode_header_validated(response);

    if (hdr.sequence_id != seq) {
        throw ProtocolError("Sequence ID mismatch");
    }

    if (hdr.type == wire::MsgType::error) {
        u16 code = decode_u16(response);
        std::string msg = decode_string(response);
        throw ServiceError("Service error: " + msg);
    }

    if (hdr.type != wire::MsgType::result) {
        throw ProtocolError("Unexpected message type in response");
    }

    // Return the payload (skip header)
    Buffer result;
    result.write(response.data() + 16, response.size() - 16);
    result.reset_read();
    return result;
}

void ServiceConnection::call_oneway(u16 service_id, u16 method_id, const Buffer& args) {
    if (!proc_ || !proc_->alive()) {
        throw ServiceError("Service not running");
    }

    u32 seq = next_seq_++;
    Buffer call_msg = wire::create_call_message(seq, service_id, method_id, args);
    proc_->send(call_msg);
}

bool ServiceConnection::supports(u16 service_id, u16 method_id) const {
    if (!proc_) return false;

    const auto& methods = proc_->methods();
    for (const auto& m : methods) {
        if (m.service_id == service_id && m.method_id == method_id) {
            return true;
        }
    }
    return false;
}

} // namespace song
