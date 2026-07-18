// MIT License
// Copyright (c) 2026 dbjwhs

// caps_service - a minimal service that advertises a non-zero capability set in
// its init handshake via ServiceRuntime::set_capability(). It demonstrates how
// a service announces optional protocol features to its peer, and it doubles as
// a test fixture for verifying that capabilities survive ServiceProcess moves
// (see test/process_test.cpp, ProcessTest.MovePreservesCapabilities).

#include <song/song.hpp>
#include <stdexcept>

using namespace song;

// Service and method IDs
constexpr u16 kService_Caps = 1;
constexpr u16 kMethod_ping = 1;

void caps_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    (void)request;
    switch (method_id) {
        case kMethod_ping:
            encode_i32(response, 1);
            break;
        default:
            throw std::runtime_error("Unknown method ID");
    }
}

int main() {
    ServiceRuntime runtime;

    runtime.register_dispatcher(kService_Caps, caps_dispatcher);
    runtime.register_method(kService_Caps, kMethod_ping);

    // Advertise a non-zero capability set. These bits are sent to the peer in
    // the init message and become the peer's peer_capabilities()/negotiated set.
    runtime.set_capability(wire::Capability::streaming);
    runtime.set_capability(wire::Capability::properties);

    runtime.run();  // Never returns (reads stdin, writes stdout)
}
