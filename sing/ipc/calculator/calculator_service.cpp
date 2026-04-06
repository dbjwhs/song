// MIT License
// Copyright (c) 2026 dbjwhs

#include "calculator.hpp"
#include <song/logging.hpp>
#include <string>

using namespace song;
using namespace song::calculator;

// Calculator implementation
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

    i64 sum(const std::vector<i32>& values) override {
        i64 result = 0;
        for (auto v : values) {
            result += v;
        }
        return result;
    }

    f64 sum_matrix(const std::vector<std::vector<f64>>& matrix) override {
        f64 result = 0.0;
        for (const auto& row : matrix) {
            for (auto val : row) {
                result += val;
            }
        }
        return result;
    }

    std::vector<std::vector<i32>> transpose(const std::vector<std::vector<i32>>& matrix) override {
        if (matrix.empty()) {
            return {};
        }

        // Handle jagged arrays - find max row length
        size_t max_cols = 0;
        for (const auto& row : matrix) {
            max_cols = std::max(max_cols, row.size());
        }

        if (max_cols == 0) {
            return {};
        }

        std::vector<std::vector<i32>> result(max_cols);
        for (size_t i = 0; i < matrix.size(); ++i) {
            for (size_t j = 0; j < matrix[i].size(); ++j) {
                if (result[j].size() <= i) {
                    result[j].resize(i + 1, 0);
                }
                result[j][i] = matrix[i][j];
            }
        }
        return result;
    }

    std::vector<i32> flatten_3d(const std::vector<std::vector<std::vector<i32>>>& cube) override {
        std::vector<i32> result;
        for (const auto& plane : cube) {
            for (const auto& row : plane) {
                for (auto val : row) {
                    result.push_back(val);
                }
            }
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

int main() {
    Log::info("IPC Calculator service starting");

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
    runtime.register_method(kService_Calculator, kMethod_Calculator_sum_matrix);
    runtime.register_method(kService_Calculator, kMethod_Calculator_transpose);
    runtime.register_method(kService_Calculator, kMethod_Calculator_flatten_3d);

    Log::debug("Service ready: " + std::to_string(runtime.service_count()) + " service(s), " +
               std::to_string(runtime.method_count()) + " method(s)");

    // Run the service
    runtime.run();

    return 0;
}
