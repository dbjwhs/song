// MIT License
// Copyright (c) 2026 dbjwhs

// Interop service, C++ side: the service of interop.song served by the C++
// runtime. The Rust implementation (rust/tests/common/interop_impl.rs) does
// the same thing value for value, so the cross-language tests can state
// what either peer must answer.
//
//   sing_network_rust_interop_service <port> [key]
//
// With a key, every client is served through SecureTransport.

#include <song/song.hpp>
#include "interop.hpp"
#include "interop_samples.hpp"
#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace song;
using namespace song::interop;

namespace {

class InteropImpl : public IInterop {
public:
    i32 add(i32 a, i32 b) override {
        return static_cast<i32>(static_cast<u32>(a) + static_cast<u32>(b));
    }

    Sample echo_sample(const Sample& s) override {
        return s;
    }

    Batch echo_batch(const Batch& b) override {
        return b;
    }

    std::string describe(const Status& status, const Access& access,
                         const std::string& prefix) override {
        return prefix + Status_name(status) + ":" +
               std::to_string(static_cast<u32>(access));
    }

    void fail(const std::string& message) override {
        throw std::runtime_error(message);
    }

    void ping() override {}

    void count_to(i32 n, StreamWriter& writer) override {
        for (i32 value = 1; value <= n; ++value) {
            Buffer chunk;
            encode_i32(chunk, value);
            writer.write(chunk);
        }
    }

    void samples(i32 n, const std::string& label, StreamWriter& writer) override {
        for (i32 ndx = 1; ndx <= n; ++ndx) {
            Buffer chunk;
            encode_Sample(chunk, make_sample(ndx, label));
            writer.write(chunk);
        }
    }

    void fail_after(i32 n, const std::string& message, StreamWriter& writer) override {
        count_to(n, writer);
        throw std::runtime_error(message);
    }

    std::optional<i64> bump(const std::optional<i64>& id,
                            const std::optional<std::string>& note) override {
        if (!id) {
            return std::nullopt;
        }
        return *id + static_cast<i64>(note ? note->size() : 0);
    }
};

// Answers with the language of the peer, so a test can tell which
// implementation it reached.
class SecondImpl : public ISecond {
public:
    std::string whoami(const std::string& caller) override {
        return "cpp serving " + caller;
    }
};

InteropImpl g_interop;
SecondImpl g_second;

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        Log::error("usage: sing_network_rust_interop_service <port> [key]");
        return 2;
    }
    u16 port = static_cast<u16>(std::atoi(argv[1]));

    ServiceRuntime runtime;
    runtime.register_dispatcher(kService_Interop, [](u16 method_id, Buffer& request, Buffer& response) {
        dispatch_Interop(g_interop, method_id, request, response);
    });
    runtime.register_stream_dispatcher(kService_Interop_Stream,
                                       [](u16 method_id, Buffer& request, StreamWriter& writer) {
        dispatch_Interop_stream(g_interop, method_id, request, writer);
    });
    runtime.register_dispatcher(kService_Second, [](u16 method_id, Buffer& request, Buffer& response) {
        dispatch_Second(g_second, method_id, request, response);
    });

    runtime.register_method(kService_Interop, kMethod_Interop_add);
    runtime.register_method(kService_Interop, kMethod_Interop_echo_sample);
    runtime.register_method(kService_Interop, kMethod_Interop_echo_batch);
    runtime.register_method(kService_Interop, kMethod_Interop_describe);
    runtime.register_method(kService_Interop, kMethod_Interop_fail);
    runtime.register_method(kService_Interop, kMethod_Interop_ping);
    runtime.register_method(kService_Interop, kMethod_Interop_bump);
    runtime.register_method(kService_Interop_Stream, kMethod_Interop_count_to, wire::MethodFlags::streaming);
    runtime.register_method(kService_Interop_Stream, kMethod_Interop_samples, wire::MethodFlags::streaming);
    runtime.register_method(kService_Interop_Stream, kMethod_Interop_fail_after, wire::MethodFlags::streaming);
    runtime.register_method(kService_Second, kMethod_Second_whoami);

    if (argc > 2) {
        std::string key = argv[2];
        runtime.set_transport_wrapper(
            [key](std::unique_ptr<Transport> t) -> std::unique_ptr<Transport> {
                return std::make_unique<SecureTransport>(std::move(t), SecurityConfig(key));
            });
    }

    runtime.run_tcp_multi(port);
    return 0;
}
