// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/runtime.hpp"
#include "song/transport.hpp"
#include "song/discovery.hpp"
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <cerrno>

namespace song {

namespace {

// Write all bytes to fd, handling partial writes and EINTR
bool write_all(int fd, const void* data, size_t len) {
    const auto* ptr = static_cast<const std::byte*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t written = ::write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;  // Interrupted, retry
            return false;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

// Read exactly len bytes from fd, handling partial reads and EINTR
bool read_all(int fd, void* data, size_t len) {
    auto* ptr = static_cast<std::byte*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::read(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;  // Interrupted, retry
            return false;
        }
        if (n == 0) return false;  // EOF
        ptr += static_cast<size_t>(n);
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

} // anonymous namespace

void ServiceRuntime::register_dispatcher(u16 service_id,
                                        std::function<void(u16, Buffer&, Buffer&)> dispatcher) {
    dispatchers_[service_id] = std::move(dispatcher);
}

void ServiceRuntime::register_method(u16 service_id, u16 method_id, wire::MethodFlags flags) {
    methods_.push_back(wire::MethodDescriptor{service_id, method_id, flags, 0});
}

void ServiceRuntime::register_factory(u32 type_id, ObjectFactory factory) {
    object_registry_.register_factory(type_id, std::move(factory));
}

// =============================================================================
// Introspection
// =============================================================================

size_t ServiceRuntime::service_count() const {
    return dispatchers_.size();
}

size_t ServiceRuntime::method_count() const {
    return methods_.size();
}

std::vector<u16> ServiceRuntime::get_service_ids() const {
    std::vector<u16> ids;
    ids.reserve(dispatchers_.size());
    for (const auto& [id, _] : dispatchers_) {
        ids.push_back(id);
    }
    return ids;
}

const std::vector<wire::MethodDescriptor>& ServiceRuntime::get_methods() const {
    return methods_;
}

bool ServiceRuntime::has_service(u16 service_id) const {
    return dispatchers_.find(service_id) != dispatchers_.end();
}

bool ServiceRuntime::has_method(u16 service_id, u16 method_id) const {
    for (const auto& m : methods_) {
        if (m.service_id == service_id && m.method_id == method_id) {
            return true;
        }
    }
    return false;
}

void ServiceRuntime::send_init_confirmation_fd(int fd) {
    // Send init message with method list
    Buffer init_msg = wire::create_init_message(
        wire::kFirstVersion,
        wire::kCurrentVersion,
        0,  // capabilities
        methods_
    );

    // Write to fd
    if (!write_all(fd, init_msg.data(), init_msg.size())) {
        std::exit(1);
    }
}

void ServiceRuntime::send_init_confirmation_transport(Transport& transport) {
    // Send init message with method list
    Buffer init_msg = wire::create_init_message(
        wire::kFirstVersion,
        wire::kCurrentVersion,
        0,  // capabilities
        methods_
    );

    try {
        transport.send(init_msg);
    } catch (...) {
        // Transport error - will be handled in client_loop
    }
}

void ServiceRuntime::handle_message_fd(const wire::Header& hdr, Buffer& payload, int write_fd) {
    if (hdr.type == wire::MsgType::call) {
        // Decode method call header
        auto [service_id, method_id] = wire::decode_method_call_header(payload);

        // Find dispatcher
        auto it = dispatchers_.find(service_id);
        if (it == dispatchers_.end()) {
            // Unknown service - send error
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::unknown_service,
                "Unknown service ID"
            );
            write_all(write_fd, error_msg.data(), error_msg.size());
            return;
        }

        // Dispatch to service
        Buffer response;
        try {
            it->second(method_id, payload, response);

            // Send result
            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            write_all(write_fd, result_msg.data(), result_msg.size());
        } catch (const std::exception& e) {
            // Send error
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::unknown_method,
                e.what()
            );
            write_all(write_fd, error_msg.data(), error_msg.size());
        }
    } else if (hdr.type == wire::MsgType::create) {
        // Object creation
        auto create_hdr = wire::decode_object_create_header(payload);

        Buffer response;
        try {
            // Create object using factory
            i32 object_id = object_registry_.create_object(
                create_hdr.type_id, create_hdr.constructor_id, payload);

            // Send object reference as result
            wire::ObjectRef ref{create_hdr.type_id, object_id};
            wire::encode_object_ref(response, ref);

            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            write_all(write_fd, result_msg.data(), result_msg.size());
        } catch (const std::exception& e) {
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::object_creation_failed,
                e.what()
            );
            write_all(write_fd, error_msg.data(), error_msg.size());
        }
    } else if (hdr.type == wire::MsgType::release) {
        // Object release (fire-and-forget)
        auto release_hdr = wire::decode_object_release_header(payload);
        object_registry_.release(release_hdr.object_id);
        // No response for release
    } else if (hdr.type == wire::MsgType::prop_get) {
        // Property get
        auto prop_hdr = wire::decode_property_header(payload);

        Buffer response;
        try {
            Object* obj = object_registry_.get(prop_hdr.object_id);
            if (!obj) {
                Buffer error_msg = wire::create_error_message(
                    hdr.sequence_id,
                    ErrorCode::object_not_found,
                    "Object not found"
                );
                write_all(write_fd, error_msg.data(), error_msg.size());
                return;
            }

            obj->prop_get(prop_hdr.property_id, response);

            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            write_all(write_fd, result_msg.data(), result_msg.size());
        } catch (const std::exception& e) {
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::property_error,
                e.what()
            );
            write_all(write_fd, error_msg.data(), error_msg.size());
        }
    } else if (hdr.type == wire::MsgType::prop_set) {
        // Property set
        auto prop_hdr = wire::decode_property_header(payload);

        Buffer response;
        try {
            Object* obj = object_registry_.get(prop_hdr.object_id);
            if (!obj) {
                Buffer error_msg = wire::create_error_message(
                    hdr.sequence_id,
                    ErrorCode::object_not_found,
                    "Object not found"
                );
                write_all(write_fd, error_msg.data(), error_msg.size());
                return;
            }

            obj->prop_set(prop_hdr.property_id, payload, response);

            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            write_all(write_fd, result_msg.data(), result_msg.size());
        } catch (const std::exception& e) {
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::property_error,
                e.what()
            );
            write_all(write_fd, error_msg.data(), error_msg.size());
        }
    }
}

void ServiceRuntime::handle_message(const wire::Header& hdr, Buffer& payload,
                                   Transport& transport) {
    if (hdr.type == wire::MsgType::call) {
        // Decode method call header
        auto [service_id, method_id] = wire::decode_method_call_header(payload);

        // Find dispatcher
        auto it = dispatchers_.find(service_id);
        if (it == dispatchers_.end()) {
            // Unknown service - send error
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::unknown_service,
                "Unknown service ID"
            );
            transport.send(error_msg);
            return;
        }

        // Dispatch to service
        Buffer response;
        try {
            it->second(method_id, payload, response);

            // Send result
            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            transport.send(result_msg);
        } catch (const std::exception& e) {
            // Send error
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::unknown_method,
                e.what()
            );
            transport.send(error_msg);
        }
    } else if (hdr.type == wire::MsgType::create) {
        // Object creation
        auto create_hdr = wire::decode_object_create_header(payload);

        Buffer response;
        try {
            // Create object using factory
            i32 object_id = object_registry_.create_object(
                create_hdr.type_id, create_hdr.constructor_id, payload);

            // Send object reference as result
            wire::ObjectRef ref{create_hdr.type_id, object_id};
            wire::encode_object_ref(response, ref);

            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            transport.send(result_msg);
        } catch (const std::exception& e) {
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::object_creation_failed,
                e.what()
            );
            transport.send(error_msg);
        }
    } else if (hdr.type == wire::MsgType::release) {
        // Object release (fire-and-forget)
        auto release_hdr = wire::decode_object_release_header(payload);
        object_registry_.release(release_hdr.object_id);
        // No response for release
    } else if (hdr.type == wire::MsgType::prop_get) {
        // Property get
        auto prop_hdr = wire::decode_property_header(payload);

        Buffer response;
        try {
            Object* obj = object_registry_.get(prop_hdr.object_id);
            if (!obj) {
                Buffer error_msg = wire::create_error_message(
                    hdr.sequence_id,
                    ErrorCode::object_not_found,
                    "Object not found"
                );
                transport.send(error_msg);
                return;
            }

            obj->prop_get(prop_hdr.property_id, response);

            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            transport.send(result_msg);
        } catch (const std::exception& e) {
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::property_error,
                e.what()
            );
            transport.send(error_msg);
        }
    } else if (hdr.type == wire::MsgType::prop_set) {
        // Property set
        auto prop_hdr = wire::decode_property_header(payload);

        Buffer response;
        try {
            Object* obj = object_registry_.get(prop_hdr.object_id);
            if (!obj) {
                Buffer error_msg = wire::create_error_message(
                    hdr.sequence_id,
                    ErrorCode::object_not_found,
                    "Object not found"
                );
                transport.send(error_msg);
                return;
            }

            obj->prop_set(prop_hdr.property_id, payload, response);

            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            transport.send(result_msg);
        } catch (const std::exception& e) {
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::property_error,
                e.what()
            );
            transport.send(error_msg);
        }
    }
}

void ServiceRuntime::client_loop(Transport& transport) {
    // Send init confirmation to client
    send_init_confirmation_transport(transport);

    // Handle messages until client disconnects
    for (;;) {
        Buffer msg;
        if (!transport.receive(msg, -1)) {  // Blocking receive
            // Client disconnected
            return;
        }

        // Decode header
        wire::Header hdr = wire::decode_header(msg);

        // Validate
        if (hdr.magic != wire::kMagic) {
            return;  // Invalid client, disconnect
        }

        if (hdr.type == wire::MsgType::shutdown) {
            // Client requested shutdown of this connection
            return;
        }

        // Handle message
        handle_message(hdr, msg, transport);
    }
}

[[noreturn]] void ServiceRuntime::run() {
    // Send init confirmation via stdout
    send_init_confirmation_fd(STDOUT_FILENO);

    // Main loop
    for (;;) {
        // Read header (16 bytes)
        std::byte header_buf[16];
        if (!read_all(STDIN_FILENO, header_buf, 16)) {
            // EOF or error - parent died or pipe broken
            std::exit(0);
        }

        // Parse header
        Buffer header_buffer;
        header_buffer.write(header_buf, 16);
        header_buffer.reset_read();
        wire::Header hdr = wire::decode_header(header_buffer);

        // Validate
        if (hdr.magic != wire::kMagic) {
            std::exit(1);
        }

        if (hdr.type == wire::MsgType::shutdown) {
            // Graceful shutdown
            std::exit(0);
        }

        // Read payload
        Buffer payload;
        if (hdr.payload_size > 0) {
            std::vector<std::byte> payload_buf(hdr.payload_size);
            if (!read_all(STDIN_FILENO, payload_buf.data(), hdr.payload_size)) {
                std::exit(1);
            }
            payload.write(payload_buf.data(), hdr.payload_size);
            payload.reset_read();
        }

        // Handle message
        handle_message_fd(hdr, payload, STDOUT_FILENO);
    }
}

[[noreturn]] void ServiceRuntime::run_tcp(u16 port) {
    TcpListener listener;
    listener.listen(port);
    run_tcp(listener);
}

[[noreturn]] void ServiceRuntime::run_tcp(TcpListener& listener) {
    // Accept clients in a loop
    for (;;) {
        // Wait for client connection (blocking)
        auto client = listener.accept(-1);
        if (!client) {
            continue;  // Shouldn't happen with blocking accept
        }

        // Handle this client until they disconnect
        try {
            client_loop(*client);
        } catch (...) {
            // Client error, continue accepting new clients
        }
    }
}

[[noreturn]] void ServiceRuntime::run_tcp_discoverable(u16 port,
                                                       const std::string& name,
                                                       const std::string& type) {
    TcpListener listener;
    listener.listen(port);
    run_tcp_discoverable(listener, name, type);
}

[[noreturn]] void ServiceRuntime::run_tcp_discoverable(TcpListener& listener,
                                                       const std::string& name,
                                                       const std::string& type) {
    // Create discovery and register the service
    auto discovery = create_discovery();
    bool registered = false;

    if (discovery && discovery->is_available()) {
        registered = discovery->register_service(name, type, listener.bound_port());
    }

    // Log registration status (useful for debugging)
    if (registered) {
        std::cerr << "[" << name << "] Registered on mDNS as "
                  << Discovery::make_service_type(type) << " port "
                  << listener.bound_port() << std::endl;
    }

    // Accept clients in a loop (same as run_tcp)
    for (;;) {
        auto client = listener.accept(-1);
        if (!client) {
            continue;
        }

        try {
            client_loop(*client);
        } catch (...) {
            // Client error, continue accepting new clients
        }
    }
}

} // namespace song
