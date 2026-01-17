// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/runtime.hpp"
#include <unistd.h>
#include <cstdlib>
#include <iostream>

namespace song {

void ServiceRuntime::register_dispatcher(u16 service_id,
                                        std::function<void(u16, Buffer&, Buffer&)> dispatcher) {
    dispatchers_[service_id] = std::move(dispatcher);
}

void ServiceRuntime::register_method(u16 service_id, u16 method_id, wire::MethodFlags flags) {
    methods_.push_back(wire::MethodDescriptor{service_id, method_id, flags, 0});
}

void ServiceRuntime::send_init_confirmation() {
    // Send init message with method list
    Buffer init_msg = wire::create_init_message(
        wire::kFirstVersion,
        wire::kCurrentVersion,
        0,  // capabilities
        methods_
    );

    // Write to stdout
    ssize_t written = ::write(STDOUT_FILENO, init_msg.data(), init_msg.size());
    if (written != static_cast<ssize_t>(init_msg.size())) {
        std::exit(1);
    }
}

void ServiceRuntime::handle_message(const wire::Header& hdr, Buffer& payload) {
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
            ::write(STDOUT_FILENO, error_msg.data(), error_msg.size());
            return;
        }

        // Dispatch to service
        Buffer response;
        try {
            it->second(method_id, payload, response);

            // Send result
            Buffer result_msg = wire::create_result_message(hdr.sequence_id, response);
            ::write(STDOUT_FILENO, result_msg.data(), result_msg.size());
        } catch (const std::exception& e) {
            // Send error
            Buffer error_msg = wire::create_error_message(
                hdr.sequence_id,
                ErrorCode::unknown_method,
                e.what()
            );
            ::write(STDOUT_FILENO, error_msg.data(), error_msg.size());
        }
    }
}

[[noreturn]] void ServiceRuntime::run() {
    // Send init confirmation
    send_init_confirmation();

    // Main loop
    for (;;) {
        // Read header (16 bytes)
        std::byte header_buf[16];
        ssize_t n = ::read(STDIN_FILENO, header_buf, 16);
        if (n == 0) {
            // EOF - parent died
            std::exit(0);
        }
        if (n < 0 || n != 16) {
            // Error reading
            std::exit(1);
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
            size_t offset = 0;
            while (offset < hdr.payload_size) {
                n = ::read(STDIN_FILENO, payload_buf.data() + offset, hdr.payload_size - offset);
                if (n <= 0) {
                    std::exit(1);
                }
                offset += n;
            }
            payload.write(payload_buf.data(), hdr.payload_size);
            payload.reset_read();
        }

        // Handle message
        handle_message(hdr, payload);
    }
}

} // namespace song
