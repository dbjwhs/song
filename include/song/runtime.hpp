// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "wire.hpp"
#include <unordered_map>
#include <functional>

namespace song {

/// Service runtime
/// Used by service implementations to handle incoming requests
class ServiceRuntime {
    std::unordered_map<u16, std::function<void(u16, Buffer&, Buffer&)>> dispatchers_;

public:
    /// Register a service dispatcher
    /// service_id: unique identifier for this service
    /// dispatcher: function that takes (method_id, request, response)
    void register_dispatcher(u16 service_id,
                           std::function<void(u16, Buffer&, Buffer&)> dispatcher);

    /// Main service loop - reads from stdin, writes to stdout
    /// Never returns (until shutdown message received)
    [[noreturn]] void run();

private:
    void send_init_confirmation();
    void handle_message(const wire::Header& hdr, Buffer& payload);
};

} // namespace song
