// MIT License
// Copyright (c) 2026 dbjwhs

// Minimal pipe service exposing one remote object type with one property.
// Setting the property notifies subscribers; used to exercise property push
// over the pipe transport.

#include <song/song.hpp>

using namespace song;

namespace {

constexpr u32 kType_Counter = 100;
constexpr u16 kProp_value = 1;

class Counter : public Object {
    i32 value_ = 0;

public:
    void prop_get(u16 prop_id, Buffer& resp) override {
        if (prop_id != kProp_value) {
            throw ServiceError("unknown property");
        }
        encode_i32(resp, value_);
    }

    void prop_set(u16 prop_id, Buffer& req, Buffer& resp) override {
        if (prop_id != kProp_value) {
            throw ServiceError("unknown property");
        }
        value_ = decode_i32(req);
        encode_i32(resp, value_);

        Buffer changed;
        encode_i32(changed, value_);
        notify_property(kProp_value, changed);
    }

    void dispatch([[maybe_unused]] u16 method_id, [[maybe_unused]] Buffer& req,
                  [[maybe_unused]] Buffer& resp) override {
        throw ServiceError("no methods");
    }
};

} // anonymous namespace

int main() {
    ServiceRuntime runtime;
    runtime.register_factory(
        kType_Counter,
        []([[maybe_unused]] u16 constructor_id, [[maybe_unused]] Buffer& args) -> Object* {
            return new Counter;
        });
    runtime.run();
}
