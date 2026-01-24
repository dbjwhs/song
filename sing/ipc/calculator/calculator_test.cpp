// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include "calculator.hpp"
#include <filesystem>

using namespace song;
using namespace song::calculator;

// Helper to get service path
static std::string get_service_path() {
    std::filesystem::path base = std::filesystem::current_path();

    // Try a few common locations relative to build directory
    std::vector<std::filesystem::path> candidates = {
        base / "sing" / "ipc" / "calculator" / "sing_ipc_calculator_service",
        base / "sing_ipc_calculator_service",
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    // Fall back to assuming we're in build directory
    return (base / "sing" / "ipc" / "calculator" / "sing_ipc_calculator_service").string();
}

class CalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string path = get_service_path();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "Calculator service not found at " << path;
        }

        // Spawn calculator service
        proc_ = ServiceProcess::spawn(path.c_str());
        conn_ = std::make_unique<ServiceConnection>(&proc_);
    }

    void TearDown() override {
        conn_.reset();
        if (proc_.alive()) {
            proc_.terminate();
        }
    }

    ServiceProcess proc_;
    std::unique_ptr<ServiceConnection> conn_;
};

// =============================================================================
// Basic Arithmetic Tests
// =============================================================================

TEST_F(CalculatorTest, Add) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.add(2, 3), 5);
    EXPECT_EQ(calc.add(-1, 1), 0);
    EXPECT_EQ(calc.add(0, 0), 0);
    EXPECT_EQ(calc.add(100, 200), 300);
    EXPECT_EQ(calc.add(-50, -30), -80);
}

TEST_F(CalculatorTest, Subtract) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.subtract(5, 3), 2);
    EXPECT_EQ(calc.subtract(3, 5), -2);
    EXPECT_EQ(calc.subtract(0, 0), 0);
    EXPECT_EQ(calc.subtract(100, 100), 0);
    EXPECT_EQ(calc.subtract(-10, -20), 10);
}

TEST_F(CalculatorTest, Multiply) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.multiply(2, 3), 6);
    EXPECT_EQ(calc.multiply(-2, 3), -6);
    EXPECT_EQ(calc.multiply(-2, -3), 6);
    EXPECT_EQ(calc.multiply(0, 100), 0);
    EXPECT_EQ(calc.multiply(100, 0), 0);
}

TEST_F(CalculatorTest, Divide) {
    CalculatorProxy calc(*conn_);

    auto result = calc.divide(10, 3);
    EXPECT_EQ(result.quotient, 3);
    EXPECT_EQ(result.remainder, 1);

    result = calc.divide(20, 4);
    EXPECT_EQ(result.quotient, 5);
    EXPECT_EQ(result.remainder, 0);

    result = calc.divide(7, 2);
    EXPECT_EQ(result.quotient, 3);
    EXPECT_EQ(result.remainder, 1);

    result = calc.divide(-10, 3);
    EXPECT_EQ(result.quotient, -3);
    EXPECT_EQ(result.remainder, -1);
}

// =============================================================================
// Factorial Tests
// =============================================================================

TEST_F(CalculatorTest, Factorial) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.factorial(0), 1);
    EXPECT_EQ(calc.factorial(1), 1);
    EXPECT_EQ(calc.factorial(5), 120);
    EXPECT_EQ(calc.factorial(10), 3628800);
    EXPECT_EQ(calc.factorial(12), 479001600);
}

// =============================================================================
// Sum Tests
// =============================================================================

TEST_F(CalculatorTest, SumEmpty) {
    CalculatorProxy calc(*conn_);

    std::vector<i32> empty;
    EXPECT_EQ(calc.sum(empty), 0);
}

TEST_F(CalculatorTest, SumSingle) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.sum({42}), 42);
    EXPECT_EQ(calc.sum({-10}), -10);
}

TEST_F(CalculatorTest, SumMultiple) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.sum({1, 2, 3, 4, 5}), 15);
    EXPECT_EQ(calc.sum({-1, -2, -3}), -6);
    EXPECT_EQ(calc.sum({100, -50, 25, -75}), 0);
}

TEST_F(CalculatorTest, SumLarge) {
    CalculatorProxy calc(*conn_);

    // Sum of 1 to 100 = 5050
    std::vector<i32> values;
    for (i32 i = 1; i <= 100; ++i) {
        values.push_back(i);
    }
    EXPECT_EQ(calc.sum(values), 5050);
}

// =============================================================================
// Multiple Operations
// =============================================================================

TEST_F(CalculatorTest, MultipleOperations) {
    CalculatorProxy calc(*conn_);

    // Perform a series of operations
    i32 a = calc.add(10, 20);          // 30
    i32 b = calc.multiply(a, 2);        // 60
    i32 c = calc.subtract(b, 10);       // 50
    auto d = calc.divide(c, 7);         // 7 remainder 1

    EXPECT_EQ(a, 30);
    EXPECT_EQ(b, 60);
    EXPECT_EQ(c, 50);
    EXPECT_EQ(d.quotient, 7);
    EXPECT_EQ(d.remainder, 1);
}

TEST_F(CalculatorTest, RapidFireCalls) {
    CalculatorProxy calc(*conn_);

    // Make many rapid calls to test connection stability
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(calc.add(i, i), i * 2);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(CalculatorTest, LargeNumbers) {
    CalculatorProxy calc(*conn_);

    // Test with large i32 values
    i32 large = 1000000;
    EXPECT_EQ(calc.add(large, large), 2000000);
    EXPECT_EQ(calc.multiply(1000, 1000), 1000000);
}

TEST_F(CalculatorTest, NegativeNumbers) {
    CalculatorProxy calc(*conn_);

    EXPECT_EQ(calc.add(-100, -200), -300);
    EXPECT_EQ(calc.subtract(-100, -200), 100);
    EXPECT_EQ(calc.multiply(-100, -200), 20000);
}
