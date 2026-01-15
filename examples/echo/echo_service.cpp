// MIT License
// Copyright (c) 2026 dbjwhs

#include <song/song.hpp>
#include <string>

using namespace song;

// Service IDs
constexpr u16 kService_Echo = 1;

// Method IDs
constexpr u16 kMethod_echo = 1;
constexpr u16 kMethod_add = 2;

// Echo service dispatcher
void echo_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    switch (method_id) {
        case kMethod_echo: {
            // Echo method: string -> string
            std::string msg = decode_string(request);
            encode_string(response, msg);
            break;
        }
        case kMethod_add: {
            // Add method: (i32, i32) -> i32
            i32 a = decode_i32(request);
            i32 b = decode_i32(request);
            i32 result = a + b;
            encode_i32(response, result);
            break;
        }
        default:
            throw std::runtime_error("Unknown method ID");
    }
}

int main() {
    ServiceRuntime runtime;

    // Register echo service
    runtime.register_dispatcher(kService_Echo, echo_dispatcher);

    // Run forever (reads stdin, writes stdout)
    runtime.run();
}
