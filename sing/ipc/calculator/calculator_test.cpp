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
    for (int ndx = 0; ndx < 100; ++ndx) {
        EXPECT_EQ(calc.add(ndx, ndx), ndx * 2);
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

// =============================================================================
// 2D Array Tests (Matrix Operations)
// =============================================================================

TEST_F(CalculatorTest, SumMatrixEmpty) {
    CalculatorProxy calc(*conn_);

    std::vector<std::vector<f64>> empty;
    EXPECT_DOUBLE_EQ(calc.sum_matrix(empty), 0.0);
}

TEST_F(CalculatorTest, SumMatrixSingleElement) {
    CalculatorProxy calc(*conn_);

    std::vector<std::vector<f64>> matrix = {{42.5}};
    EXPECT_DOUBLE_EQ(calc.sum_matrix(matrix), 42.5);
}

TEST_F(CalculatorTest, SumMatrixRegular) {
    CalculatorProxy calc(*conn_);

    // 2x3 matrix
    std::vector<std::vector<f64>> matrix = {
        {1.0, 2.0, 3.0},
        {4.0, 5.0, 6.0}
    };
    // Sum = 1+2+3+4+5+6 = 21
    EXPECT_DOUBLE_EQ(calc.sum_matrix(matrix), 21.0);
}

TEST_F(CalculatorTest, SumMatrixJagged) {
    CalculatorProxy calc(*conn_);

    // Jagged array (different row lengths)
    std::vector<std::vector<f64>> matrix = {
        {1.0, 2.0},
        {3.0},
        {4.0, 5.0, 6.0}
    };
    // Sum = 1+2+3+4+5+6 = 21
    EXPECT_DOUBLE_EQ(calc.sum_matrix(matrix), 21.0);
}

TEST_F(CalculatorTest, SumMatrixWithEmptyRows) {
    CalculatorProxy calc(*conn_);

    // Sparse array with empty rows
    std::vector<std::vector<f64>> matrix = {
        {1.0, 2.0},
        {},
        {3.0}
    };
    // Sum = 1+2+3 = 6
    EXPECT_DOUBLE_EQ(calc.sum_matrix(matrix), 6.0);
}

TEST_F(CalculatorTest, TransposeEmpty) {
    CalculatorProxy calc(*conn_);

    std::vector<std::vector<i32>> empty;
    auto result = calc.transpose(empty);
    EXPECT_TRUE(result.empty());
}

TEST_F(CalculatorTest, TransposeRegular) {
    CalculatorProxy calc(*conn_);

    // 2x3 matrix
    std::vector<std::vector<i32>> matrix = {
        {1, 2, 3},
        {4, 5, 6}
    };

    auto result = calc.transpose(matrix);

    // Should be 3x2 matrix
    ASSERT_EQ(result.size(), 3);
    ASSERT_EQ(result[0].size(), 2);
    ASSERT_EQ(result[1].size(), 2);
    ASSERT_EQ(result[2].size(), 2);

    EXPECT_EQ(result[0][0], 1);
    EXPECT_EQ(result[0][1], 4);
    EXPECT_EQ(result[1][0], 2);
    EXPECT_EQ(result[1][1], 5);
    EXPECT_EQ(result[2][0], 3);
    EXPECT_EQ(result[2][1], 6);
}

TEST_F(CalculatorTest, TransposeSingleRow) {
    CalculatorProxy calc(*conn_);

    std::vector<std::vector<i32>> matrix = {{1, 2, 3, 4}};

    auto result = calc.transpose(matrix);

    // Should be 4x1 matrix
    ASSERT_EQ(result.size(), 4);
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_GE(result[i].size(), 1);
        EXPECT_EQ(result[i][0], static_cast<i32>(i + 1));
    }
}

// =============================================================================
// 3D Array Tests
// =============================================================================

TEST_F(CalculatorTest, Flatten3DEmpty) {
    CalculatorProxy calc(*conn_);

    std::vector<std::vector<std::vector<i32>>> empty;
    auto result = calc.flatten_3d(empty);
    EXPECT_TRUE(result.empty());
}

TEST_F(CalculatorTest, Flatten3DSingleElement) {
    CalculatorProxy calc(*conn_);

    std::vector<std::vector<std::vector<i32>>> cube = {{{42}}};
    auto result = calc.flatten_3d(cube);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 42);
}

TEST_F(CalculatorTest, Flatten3DRegular) {
    CalculatorProxy calc(*conn_);

    // 2x2x2 cube
    std::vector<std::vector<std::vector<i32>>> cube = {
        {{1, 2}, {3, 4}},
        {{5, 6}, {7, 8}}
    };

    auto result = calc.flatten_3d(cube);

    // Should be [1,2,3,4,5,6,7,8]
    std::vector<i32> expected = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(result, expected);
}

TEST_F(CalculatorTest, Flatten3DJagged) {
    CalculatorProxy calc(*conn_);

    // Jagged 3D array
    std::vector<std::vector<std::vector<i32>>> cube = {
        {{1, 2}, {3}},       // First plane: 2 rows with 2 and 1 elements
        {{4, 5, 6}},         // Second plane: 1 row with 3 elements
        {{7}, {8, 9}, {10}}  // Third plane: 3 rows with 1, 2, and 1 elements
    };

    auto result = calc.flatten_3d(cube);

    // Should be [1,2,3,4,5,6,7,8,9,10]
    std::vector<i32> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    EXPECT_EQ(result, expected);
}

TEST_F(CalculatorTest, Flatten3DWithEmptyLayers) {
    CalculatorProxy calc(*conn_);

    // Sparse 3D array with empty planes and rows
    std::vector<std::vector<std::vector<i32>>> cube = {
        {{1, 2}},
        {},           // Empty plane
        {{}, {3, 4}}  // Plane with empty row followed by populated row
    };

    auto result = calc.flatten_3d(cube);

    // Should be [1,2,3,4]
    std::vector<i32> expected = {1, 2, 3, 4};
    EXPECT_EQ(result, expected);
}
