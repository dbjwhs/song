// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/process.hpp"
#include "song/transport.hpp"
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
    // Use 5 second timeout to handle cold start scenarios (first exec is slower)
    Buffer init_msg;
    if (!receive(init_msg, 5000)) {
        throw ServiceError("Service failed to send init message");
    }

    // Decode and validate init message
    auto hdr = wire::decode_header_validated(init_msg);
    if (hdr.type != wire::MsgType::init) {
        throw ProtocolError("Expected init message from service");
    }

    // Decode init payload (validates magic internally)
    auto init = wire::decode_init(init_msg);

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

// Destructor must be in cpp file because unique_ptr<Transport> requires complete type
ServiceConnection::~ServiceConnection() = default;

ServiceConnection::ServiceConnection(ServiceProcess* proc)
    : proc_(proc)
    , transport_(nullptr) {
    // For local connections, methods come from the ServiceProcess
}

ServiceConnection::ServiceConnection(Transport* transport)
    : proc_(nullptr)
    , transport_(transport) {}

ServiceConnection::ServiceConnection(std::unique_ptr<Transport> transport)
    : proc_(nullptr)
    , transport_(transport.get())
    , owned_transport_(std::move(transport)) {}

ServiceConnection::ServiceConnection(ServiceConnection&& other) noexcept
    : proc_(other.proc_)
    , transport_(other.transport_)
    , owned_transport_(std::move(other.owned_transport_))
    , next_seq_(other.next_seq_)
    , methods_(std::move(other.methods_)) {
    other.proc_ = nullptr;
    other.transport_ = nullptr;
    other.next_seq_ = 1;
}

ServiceConnection& ServiceConnection::operator=(ServiceConnection&& other) noexcept {
    if (this != &other) {
        proc_ = other.proc_;
        transport_ = other.transport_;
        owned_transport_ = std::move(other.owned_transport_);
        next_seq_ = other.next_seq_;
        methods_ = std::move(other.methods_);
        other.proc_ = nullptr;
        other.transport_ = nullptr;
        other.next_seq_ = 1;
    }
    return *this;
}

void ServiceConnection::init_handshake() {
    if (!transport_ || !transport_->is_connected()) {
        throw ServiceError("ServiceConnection: not connected for init handshake");
    }

    // Wait for init message from service (with timeout)
    Buffer init_msg;
    if (!transport_->receive(init_msg, 5000)) {
        throw ServiceError("Service failed to send init message");
    }

    // Decode and validate init message
    auto hdr = wire::decode_header_validated(init_msg);
    if (hdr.type != wire::MsgType::init) {
        throw ProtocolError("Expected init message from service");
    }

    // Decode init payload (validates magic internally)
    auto init = wire::decode_init(init_msg);

    // Version negotiation
    if (init.current_version < wire::kFirstVersion) {
        throw VersionMismatchError("Service version too old: service=" +
            std::to_string(init.current_version >> 8) + "." +
            std::to_string(init.current_version & 0xFF) +
            ", minimum required=" +
            std::to_string(wire::kFirstVersion >> 8) + "." +
            std::to_string(wire::kFirstVersion & 0xFF));
    }

    if (init.first_version > wire::kCurrentVersion) {
        throw VersionMismatchError("Client version too old: client=" +
            std::to_string(wire::kCurrentVersion >> 8) + "." +
            std::to_string(wire::kCurrentVersion & 0xFF) +
            ", service requires=" +
            std::to_string(init.first_version >> 8) + "." +
            std::to_string(init.first_version & 0xFF));
    }

    // Decode method list
    methods_.clear();
    methods_.reserve(init.method_count);
    for (u32 i = 0; i < init.method_count; ++i) {
        methods_.push_back(wire::decode_method_descriptor(init_msg));
    }
}

const std::vector<wire::MethodDescriptor>& ServiceConnection::methods() const {
    if (proc_) {
        return proc_->methods();
    }
    return methods_;
}

Buffer ServiceConnection::call(u16 service_id, u16 method_id, const Buffer& args) {
    // For local connections, check process health
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;

    // Create call message
    Buffer call_msg = wire::create_call_message(seq, service_id, method_id, args);

    // Send via appropriate transport
    if (proc_) {
        proc_->send(call_msg);
    } else {
        transport_->send(call_msg);
    }

    // Wait for response
    Buffer response;
    bool received = false;
    if (proc_) {
        received = proc_->receive(response, 5000);
    } else {
        received = transport_->receive(response, 5000);
    }

    if (!received) {
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
        throw ServiceError("Service error (code " + std::to_string(code) + "): " + msg);
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

StreamReader ServiceConnection::call_streaming(u16 service_id, u16 method_id, const Buffer& args) {
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;

    // Send call message
    Buffer call_msg = wire::create_call_message(seq, service_id, method_id, args);
    if (proc_) {
        proc_->send(call_msg);
    } else {
        transport_->send(call_msg);
    }

    // Collect stream chunks until stream_end
    StreamReader reader(seq);

    for (;;) {
        Buffer response;
        bool received = false;
        if (proc_) {
            received = proc_->receive(response, 5000);
        } else {
            received = transport_->receive(response, 5000);
        }

        if (!received) {
            throw ServiceError("Service died or timed out during streaming");
        }

        auto hdr = wire::decode_header_validated(response);

        if (hdr.sequence_id != seq) {
            throw ProtocolError("Sequence ID mismatch in stream");
        }

        if (hdr.type == wire::MsgType::stream) {
            // Extract payload (skip 16-byte header)
            Buffer chunk;
            if (hdr.payload_size > 0) {
                chunk.write(response.data() + 16, hdr.payload_size);
                chunk.reset_read();
            }
            reader.add_chunk(std::move(chunk));
        } else if (hdr.type == wire::MsgType::stream_end) {
            reader.set_complete();
            break;
        } else if (hdr.type == wire::MsgType::error) {
            u16 code = decode_u16(response);
            std::string msg = decode_string(response);
            throw ServiceError("Service error (code " + std::to_string(code) + "): " + msg);
        } else {
            throw ProtocolError("Unexpected message type during streaming");
        }
    }

    return reader;
}

void ServiceConnection::call_oneway(u16 service_id, u16 method_id, const Buffer& args) {
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;
    Buffer call_msg = wire::create_call_message(seq, service_id, method_id, args);

    if (proc_) {
        proc_->send(call_msg);
    } else {
        transport_->send(call_msg);
    }
}

bool ServiceConnection::supports(u16 service_id, u16 method_id) const {
    const auto& method_list = methods();
    for (const auto& m : method_list) {
        if (m.service_id == service_id && m.method_id == method_id) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// Object Operations
// =============================================================================

wire::ObjectRef ServiceConnection::create_object(u32 type_id, u16 constructor_id, const Buffer& args) {
    // Validate connection
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;

    // Create object creation message
    Buffer msg = wire::create_object_create_message(seq, type_id, constructor_id, args);

    // Send
    if (proc_) {
        proc_->send(msg);
    } else {
        transport_->send(msg);
    }

    // Wait for response
    Buffer response;
    bool received = proc_ ? proc_->receive(response, 5000) : transport_->receive(response, 5000);

    if (!received) {
        throw ServiceError("Service died or timed out during object creation");
    }

    // Decode response header
    auto hdr = wire::decode_header_validated(response);

    if (hdr.sequence_id != seq) {
        throw ProtocolError("Sequence ID mismatch");
    }

    if (hdr.type == wire::MsgType::error) {
        u16 code = decode_u16(response);
        std::string error_msg = decode_string(response);
        throw ServiceError("Object creation error (code " + std::to_string(code) + "): " + error_msg);
    }

    if (hdr.type != wire::MsgType::result) {
        throw ProtocolError("Unexpected message type in object creation response");
    }

    // Decode object reference from result
    return wire::decode_object_ref(response);
}

void ServiceConnection::release_object(u32 type_id, i32 object_id) {
    // Don't release null references
    if (object_id == 0) {
        return;
    }

    // Create release message (fire-and-forget, no sequence ID)
    Buffer msg = wire::create_object_release_message(type_id, object_id);

    // Best-effort send - don't throw if connection is dead
    try {
        if (proc_) {
            if (proc_->alive()) {
                proc_->send(msg);
            }
        } else if (transport_) {
            if (transport_->is_connected()) {
                transport_->send(msg);
            }
        }
    } catch (...) {
        // Ignore errors during release - object may already be cleaned up
    }
}

Buffer ServiceConnection::get_property(u32 type_id, i32 object_id, u16 property_id) {
    // Validate connection
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;

    // Create property get message
    Buffer msg = wire::create_property_get_message(seq, type_id, object_id, property_id);

    // Send
    if (proc_) {
        proc_->send(msg);
    } else {
        transport_->send(msg);
    }

    // Wait for response
    Buffer response;
    bool received = proc_ ? proc_->receive(response, 5000) : transport_->receive(response, 5000);

    if (!received) {
        throw ServiceError("Service died or timed out during property get");
    }

    // Decode response header
    auto hdr = wire::decode_header_validated(response);

    if (hdr.sequence_id != seq) {
        throw ProtocolError("Sequence ID mismatch");
    }

    if (hdr.type == wire::MsgType::error) {
        u16 code = decode_u16(response);
        std::string error_msg = decode_string(response);
        throw ServiceError("Property get error (code " + std::to_string(code) + "): " + error_msg);
    }

    if (hdr.type != wire::MsgType::result) {
        throw ProtocolError("Unexpected message type in property get response");
    }

    // Return the payload (skip header)
    Buffer result;
    result.write(response.data() + 16, response.size() - 16);
    result.reset_read();
    return result;
}

Buffer ServiceConnection::set_property(u32 type_id, i32 object_id, u16 property_id, const Buffer& value) {
    // Validate connection
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;

    // Create property set message
    Buffer msg = wire::create_property_set_message(seq, type_id, object_id, property_id, value);

    // Send
    if (proc_) {
        proc_->send(msg);
    } else {
        transport_->send(msg);
    }

    // Wait for response
    Buffer response;
    bool received = proc_ ? proc_->receive(response, 5000) : transport_->receive(response, 5000);

    if (!received) {
        throw ServiceError("Service died or timed out during property set");
    }

    // Decode response header
    auto hdr = wire::decode_header_validated(response);

    if (hdr.sequence_id != seq) {
        throw ProtocolError("Sequence ID mismatch");
    }

    if (hdr.type == wire::MsgType::error) {
        u16 code = decode_u16(response);
        std::string error_msg = decode_string(response);
        throw ServiceError("Property set error (code " + std::to_string(code) + "): " + error_msg);
    }

    if (hdr.type != wire::MsgType::result) {
        throw ProtocolError("Unexpected message type in property set response");
    }

    // Return the payload (skip header)
    Buffer result;
    result.write(response.data() + 16, response.size() - 16);
    result.reset_read();
    return result;
}

Buffer ServiceConnection::call_object(u32 type_id, i32 object_id, u16 method_id, const Buffer& args) {
    // Validate connection
    if (proc_) {
        if (!proc_->alive()) {
            throw ServiceError("Service not running");
        }
    } else if (transport_) {
        if (!transport_->is_connected()) {
            throw ServiceError("Transport not connected");
        }
    } else {
        throw ServiceError("ServiceConnection: no transport or process");
    }

    u32 seq = next_seq_++;

    // Create object method call message
    Buffer msg = wire::create_object_method_message(seq, type_id, object_id, method_id, args);

    // Send
    if (proc_) {
        proc_->send(msg);
    } else {
        transport_->send(msg);
    }

    // Wait for response
    Buffer response;
    bool received = proc_ ? proc_->receive(response, 5000) : transport_->receive(response, 5000);

    if (!received) {
        throw ServiceError("Service died or timed out during object method call");
    }

    // Decode response header
    auto hdr = wire::decode_header_validated(response);

    if (hdr.sequence_id != seq) {
        throw ProtocolError("Sequence ID mismatch");
    }

    if (hdr.type == wire::MsgType::error) {
        u16 code = decode_u16(response);
        std::string error_msg = decode_string(response);
        throw ServiceError("Object method error (code " + std::to_string(code) + "): " + error_msg);
    }

    if (hdr.type != wire::MsgType::result) {
        throw ProtocolError("Unexpected message type in object method response");
    }

    // Return the payload (skip header)
    Buffer result;
    result.write(response.data() + 16, response.size() - 16);
    result.reset_read();
    return result;
}

} // namespace song
