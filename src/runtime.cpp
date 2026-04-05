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

void ServiceRuntime::register_stream_dispatcher(u16 service_id, StreamDispatcher dispatcher) {
    stream_dispatchers_[service_id] = std::move(dispatcher);
}

void ServiceRuntime::set_capability(wire::Capability cap) {
    local_capabilities_ |= static_cast<u32>(cap);
}

void ServiceRuntime::clear_capability(wire::Capability cap) {
    local_capabilities_ &= ~static_cast<u32>(cap);
}

u32 ServiceRuntime::capabilities() const {
    // Start with manually set capabilities
    u32 caps = local_capabilities_;

    // Auto-detect from registered features
    if (!stream_dispatchers_.empty()) {
        caps |= static_cast<u32>(wire::Capability::streaming);
    }
    if (object_registry_.has_factories()) {
        caps |= static_cast<u32>(wire::Capability::objects);
    }

    return caps;
}

wire::Capability ServiceRuntime::register_extension(const std::string& name) {
    if (next_extension_slot_ >= 8) {
        throw std::runtime_error("All 8 extension capability slots are in use");
    }

    // Check for duplicate
    for (u8 i = 0; i < next_extension_slot_; ++i) {
        if (extension_names_[i] == name) {
            // Already registered -- return existing bit
            auto bit = static_cast<wire::Capability>(
                static_cast<u32>(wire::Capability::ext_0) << i);
            return bit;
        }
    }

    u8 slot = next_extension_slot_++;
    extension_names_[slot] = name;
    auto bit = static_cast<wire::Capability>(
        static_cast<u32>(wire::Capability::ext_0) << slot);
    set_capability(bit);
    return bit;
}

bool ServiceRuntime::has_extension(const std::string& name) const {
    for (u8 i = 0; i < next_extension_slot_; ++i) {
        if (extension_names_[i] == name) return true;
    }
    return false;
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
    // Send init message with method list and capabilities
    Buffer init_msg = wire::create_init_message(
        wire::kFirstVersion,
        wire::kCurrentVersion,
        capabilities(),
        methods_
    );

    // Write to fd
    if (!write_all(fd, init_msg.data(), init_msg.size())) {
        std::exit(1);
    }
}

void ServiceRuntime::send_init_confirmation_transport(Transport& transport) {
    // Send init message with method list and capabilities
    Buffer init_msg = wire::create_init_message(
        wire::kFirstVersion,
        wire::kCurrentVersion,
        capabilities(),
        methods_
    );

    try {
        transport.send(init_msg);
    } catch (...) {
        // Transport error - will be handled in client_loop
    }
}

void ServiceRuntime::release_connection_objects(std::unordered_set<i32>& tracked) {
    for (i32 id : tracked) {
        object_registry_.release(id);
    }
    tracked.clear();
}

void ServiceRuntime::handle_message_fd(const wire::Header& hdr, Buffer& payload,
                                       int write_fd,
                                       std::unordered_set<i32>& tracked_objects) {
    if (hdr.type == wire::MsgType::call) {
        // Decode method call header
        auto [service_id, method_id] = wire::decode_method_call_header(payload);

        // Check for streaming dispatcher first
        auto stream_it = stream_dispatchers_.find(service_id);
        if (stream_it != stream_dispatchers_.end()) {
            try {
                StreamWriter writer(write_fd, hdr.sequence_id);
                stream_it->second(method_id, payload, writer);
                // writer destructor sends stream_end
            } catch (const std::exception& e) {
                Buffer error_msg = wire::create_error_message(
                    hdr.sequence_id,
                    ErrorCode::unknown_method,
                    e.what()
                );
                write_all(write_fd, error_msg.data(), error_msg.size());
            }
            return;
        }

        // Find regular dispatcher
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

            // Track this object for cleanup on disconnect
            tracked_objects.insert(object_id);

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
        tracked_objects.erase(release_hdr.object_id);
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
                                   Transport& transport,
                                   std::unordered_set<i32>& tracked_objects) {
    SubscriptionSet unused_subs;
    handle_message(hdr, payload, transport, tracked_objects, unused_subs);
}

void ServiceRuntime::handle_message(const wire::Header& hdr, Buffer& payload,
                                   Transport& transport,
                                   std::unordered_set<i32>& tracked_objects,
                                   SubscriptionSet& subscriptions) {
    // Property subscribe/unsubscribe (fire-and-forget, no response)
    if (hdr.type == wire::MsgType::prop_subscribe) {
        auto prop_hdr = wire::decode_property_header(payload);
        subscriptions.insert({prop_hdr.object_id, prop_hdr.property_id});

        // Set up notification callback on the object so prop changes push to this client
        Object* obj = object_registry_.get(prop_hdr.object_id);
        if (obj) {
            Transport* tp = &transport;
            obj->set_notify_callback([tp](u32 type_id, i32 obj_id, u16 prop_id, const Buffer& value) {
                try {
                    Buffer notify_msg = wire::create_property_notify_message(type_id, obj_id, prop_id, value);
                    tp->send(notify_msg);
                } catch (...) {
                    // Client may have disconnected
                }
            });
        }
        return;
    }

    if (hdr.type == wire::MsgType::prop_unsubscribe) {
        auto prop_hdr = wire::decode_property_header(payload);
        subscriptions.erase({prop_hdr.object_id, prop_hdr.property_id});

        // Clear notification callback if no subscriptions remain for this object
        Object* obj = object_registry_.get(prop_hdr.object_id);
        if (obj) {
            bool any_subs = false;
            for (const auto& [oid, pid] : subscriptions) {
                if (oid == prop_hdr.object_id) { any_subs = true; break; }
            }
            if (!any_subs) {
                obj->set_notify_callback(nullptr);
            }
        }
        return;
    }

    if (hdr.type == wire::MsgType::call) {
        // Decode method call header
        auto [service_id, method_id] = wire::decode_method_call_header(payload);

        // Check for streaming dispatcher first
        auto stream_it = stream_dispatchers_.find(service_id);
        if (stream_it != stream_dispatchers_.end()) {
            try {
                StreamWriter writer(transport, hdr.sequence_id);
                stream_it->second(method_id, payload, writer);
                // writer destructor sends stream_end
            } catch (const std::exception& e) {
                Buffer error_msg = wire::create_error_message(
                    hdr.sequence_id,
                    ErrorCode::unknown_method,
                    e.what()
                );
                transport.send(error_msg);
            }
            return;
        }

        // Find regular dispatcher
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

            // Track this object for cleanup on disconnect
            tracked_objects.insert(object_id);

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
        tracked_objects.erase(release_hdr.object_id);
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

    // Track objects created by this connection for cleanup on disconnect
    std::unordered_set<i32> tracked_objects;

    // Handle messages until client disconnects
    for (;;) {
        Buffer msg;
        if (!transport.receive(msg, -1)) {  // Blocking receive
            // Client disconnected - release all tracked objects
            release_connection_objects(tracked_objects);
            return;
        }

        // Decode header
        wire::Header hdr = wire::decode_header(msg);

        // Validate
        if (hdr.magic != wire::kMagic) {
            release_connection_objects(tracked_objects);
            return;  // Invalid client, disconnect
        }

        if (hdr.type == wire::MsgType::shutdown) {
            // Client requested shutdown of this connection
            release_connection_objects(tracked_objects);
            return;
        }

        // Handle init_ack from v1.1+ clients (silently consume)
        if (hdr.type == wire::MsgType::init_ack) {
            // Client is telling us its version and capabilities.
            // Decode for logging/debugging but don't require it.
            continue;
        }

        // Handle message
        handle_message(hdr, msg, transport, tracked_objects);
    }
}

[[noreturn]] void ServiceRuntime::run() {
    // Send init confirmation via stdout
    send_init_confirmation_fd(STDOUT_FILENO);

    // Track objects created by this connection for cleanup on disconnect
    std::unordered_set<i32> tracked_objects;

    // Main loop
    for (;;) {
        // Read header (16 bytes)
        std::byte header_buf[16];
        if (!read_all(STDIN_FILENO, header_buf, 16)) {
            // EOF or error - parent died or pipe broken
            release_connection_objects(tracked_objects);
            std::exit(0);
        }

        // Parse header
        Buffer header_buffer;
        header_buffer.write(header_buf, 16);
        header_buffer.reset_read();
        wire::Header hdr = wire::decode_header(header_buffer);

        // Validate
        if (hdr.magic != wire::kMagic) {
            release_connection_objects(tracked_objects);
            std::exit(1);
        }

        if (hdr.type == wire::MsgType::shutdown) {
            // Graceful shutdown
            release_connection_objects(tracked_objects);
            std::exit(0);
        }

        // Handle init_ack from v1.1+ clients (silently consume)
        if (hdr.type == wire::MsgType::init_ack) {
            // Read and discard init_ack payload
            if (hdr.payload_size > 0) {
                std::vector<std::byte> discard_buf(hdr.payload_size);
                read_all(STDIN_FILENO, discard_buf.data(), hdr.payload_size);
            }
            continue;
        }

        // Read payload
        Buffer payload;
        if (hdr.payload_size > 0) {
            std::vector<std::byte> payload_buf(hdr.payload_size);
            if (!read_all(STDIN_FILENO, payload_buf.data(), hdr.payload_size)) {
                release_connection_objects(tracked_objects);
                std::exit(1);
            }
            payload.write(payload_buf.data(), hdr.payload_size);
            payload.reset_read();
        }

        // Handle message
        handle_message_fd(hdr, payload, STDOUT_FILENO, tracked_objects);
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
