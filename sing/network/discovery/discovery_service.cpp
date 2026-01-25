// MIT License
// Copyright (c) 2026 dbjwhs

#include "calculator.hpp"
#include <song/logging.hpp>
#include <cstdlib>
#include <string>

using namespace song;
using namespace song::calculator;

// Calculator implementation (same as IPC version)
class CalculatorImpl : public ICalculator {
public:
    i32 add(i32 a, i32 b) override {
        return a + b;
    }

    i32 subtract(i32 a, i32 b) override {
        return a - b;
    }

    i32 multiply(i32 a, i32 b) override {
        return a * b;
    }

    DivResult divide(i32 a, i32 b) override {
        if (b == 0) {
            throw std::runtime_error("Division by zero");
        }
        return DivResult{a / b, a % b};
    }

    i64 factorial(i32 n) override {
        if (n < 0) {
            throw std::runtime_error("Factorial of negative number");
        }
        i64 result = 1;
        for (i32 i = 2; i <= n; ++i) {
            result *= i;
        }
        return result;
    }

    i64 sum(std::vector<i32> values) override {
        i64 result = 0;
        for (auto v : values) {
            result += v;
        }
        return result;
    }
};

// Global instance for dispatcher
static CalculatorImpl g_calculator;

// Dispatcher wrapper for ServiceRuntime
void calculator_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_Calculator(g_calculator, method_id, request, response);
}

int main(int argc, char* argv[]) {
    // Service instance name (for mDNS uniqueness)
    std::string instance_name = "TestCalculator";
    if (argc > 1) {
        instance_name = argv[1];
    }

    Log::info("Discovery service starting with mDNS registration: " + instance_name);

    ServiceRuntime runtime;

    // Register calculator service
    runtime.register_dispatcher(kService_Calculator, calculator_dispatcher);

    // Register all methods for capability exchange
    runtime.register_method(kService_Calculator, kMethod_Calculator_add);
    runtime.register_method(kService_Calculator, kMethod_Calculator_subtract);
    runtime.register_method(kService_Calculator, kMethod_Calculator_multiply);
    runtime.register_method(kService_Calculator, kMethod_Calculator_divide);
    runtime.register_method(kService_Calculator, kMethod_Calculator_factorial);
    runtime.register_method(kService_Calculator, kMethod_Calculator_sum);

    Log::debug("Service ready: " + std::to_string(runtime.service_count()) + " service(s), " +
               std::to_string(runtime.method_count()) + " method(s)");

    // Run the service with mDNS discovery registration
    // Port 0 = let OS assign an ephemeral port
    // Service type: "testcalc" -> registered as "_testcalc._song._tcp"
    runtime.run_tcp_discoverable(0, instance_name, "testcalc");

    return 0;
}
