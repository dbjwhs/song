// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include "parser.hpp"
#include "resolver.hpp"
#include "codegen.hpp"

using namespace song;
using namespace song::compiler;

// Helper to parse, resolve, and generate code
static std::string parse_and_generate(const std::string& source) {
    Parser parser(source);
    auto ast = parser.parse();

    Resolver resolver(ast);
    EXPECT_TRUE(resolver.resolve());

    CodeGenerator gen;
    return gen.generate_header(ast.namespaces.front());
}

// =============================================================================
// Struct Generation
// =============================================================================

TEST(CodegenTest, SimpleStruct) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
    )");

    // Should contain struct definition
    EXPECT_NE(code.find("struct Point {"), std::string::npos);
    EXPECT_NE(code.find("i32 x;"), std::string::npos);
    EXPECT_NE(code.find("i32 y;"), std::string::npos);

    // Should contain encode/decode functions
    EXPECT_NE(code.find("encode_Point"), std::string::npos);
    EXPECT_NE(code.find("decode_Point"), std::string::npos);
}

TEST(CodegenTest, StructWithUserType) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
        struct Rect {
            Point origin;
            Point size;
        }
    )");

    EXPECT_NE(code.find("struct Rect {"), std::string::npos);
    EXPECT_NE(code.find("Point origin;"), std::string::npos);
    EXPECT_NE(code.find("encode_Rect"), std::string::npos);
    EXPECT_NE(code.find("decode_Rect"), std::string::npos);
}

TEST(CodegenTest, StructWithArrayField) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Container {
            i32[] values;
        }
    )");

    EXPECT_NE(code.find("std::vector<i32> values;"), std::string::npos);
}

TEST(CodegenTest, StructWithOptionalField) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Person {
            string name;
            string? nickname;
        }
    )");

    EXPECT_NE(code.find("std::string name;"), std::string::npos);
    EXPECT_NE(code.find("std::optional<std::string> nickname;"), std::string::npos);
}

// =============================================================================
// Enum Generation
// =============================================================================

TEST(CodegenTest, SimpleEnum) {
    std::string code = parse_and_generate(R"(
        namespace test;
        enum Status {
            idle,
            running,
            stopped
        }
    )");

    EXPECT_NE(code.find("enum class Status : i32 {"), std::string::npos);
    EXPECT_NE(code.find("idle"), std::string::npos);
    EXPECT_NE(code.find("running"), std::string::npos);
    EXPECT_NE(code.find("stopped"), std::string::npos);
}

TEST(CodegenTest, FlagsEnum) {
    std::string code = parse_and_generate(R"(
        namespace test;
        flags Permissions {
            read = 0x01,
            write = 0x02,
            execute = 0x04
        }
    )");

    EXPECT_NE(code.find("enum class Permissions : u32 {"), std::string::npos);
    EXPECT_NE(code.find("read = 1"), std::string::npos);
    EXPECT_NE(code.find("write = 2"), std::string::npos);
    EXPECT_NE(code.find("execute = 4"), std::string::npos);
}

// =============================================================================
// Service Generation
// =============================================================================

TEST(CodegenTest, ServiceIds) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
            multiply(i32 a, i32 b) -> i32;
        }
    )");

    // Should have service ID
    EXPECT_NE(code.find("kService_Calculator"), std::string::npos);

    // Should have method IDs
    EXPECT_NE(code.find("kMethod_Calculator_add"), std::string::npos);
    EXPECT_NE(code.find("kMethod_Calculator_multiply"), std::string::npos);
}

TEST(CodegenTest, ServiceProxy) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
        }
    )");

    // Should have proxy class
    EXPECT_NE(code.find("class CalculatorProxy {"), std::string::npos);

    // Proxy should have connection member
    EXPECT_NE(code.find("ServiceConnection& m_conn"), std::string::npos);

    // Proxy should have method
    EXPECT_NE(code.find("i32 add("), std::string::npos);
}

TEST(CodegenTest, ServiceInterface) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
            multiply(i32 a, i32 b) -> i32;
        }
    )");

    // Should have interface class
    EXPECT_NE(code.find("class ICalculator {"), std::string::npos);

    // Interface should have virtual destructor
    EXPECT_NE(code.find("virtual ~ICalculator()"), std::string::npos);

    // Interface should have pure virtual methods
    EXPECT_NE(code.find("virtual i32 add("), std::string::npos);
    EXPECT_NE(code.find("= 0;"), std::string::npos);
}

TEST(CodegenTest, ServiceDispatcher) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
        }
    )");

    // Should have dispatcher function
    EXPECT_NE(code.find("dispatch_Calculator"), std::string::npos);

    // Should have switch on method_id
    EXPECT_NE(code.find("switch (method_id)"), std::string::npos);

    // Should decode parameters
    EXPECT_NE(code.find("decode_i32"), std::string::npos);
}

TEST(CodegenTest, VoidReturnMethod) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Logger {
            log(string message) -> void;
        }
    )");

    // Should have void method in interface
    EXPECT_NE(code.find("virtual void log("), std::string::npos);

    // Proxy should handle void return
    EXPECT_NE(code.find("void log(const std::string& message)"), std::string::npos);
}

TEST(CodegenTest, ComplexService) {
    std::string code = parse_and_generate(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
        }

        struct Color {
            u8 r;
            u8 g;
            u8 b;
        }

        service Canvas {
            set_pixel(Point p, Color c) -> void;
            get_pixel(Point p) -> Color;
            clear(Color c) -> void;
        }
    )");

    // Should have types
    EXPECT_NE(code.find("struct Point {"), std::string::npos);
    EXPECT_NE(code.find("struct Color {"), std::string::npos);

    // Should have service
    EXPECT_NE(code.find("class CanvasProxy {"), std::string::npos);
    EXPECT_NE(code.find("class ICanvas {"), std::string::npos);

    // Methods should use struct types
    EXPECT_NE(code.find("set_pixel(const Point& p, const Color& c)"), std::string::npos);
    EXPECT_NE(code.find("Color get_pixel(const Point& p)"), std::string::npos);
}

// =============================================================================
// Header Structure
// =============================================================================

TEST(CodegenTest, HeaderHasPragmaOnce) {
    std::string code = parse_and_generate("namespace test;");
    EXPECT_NE(code.find("#pragma once"), std::string::npos);
}

TEST(CodegenTest, HeaderHasIncludes) {
    std::string code = parse_and_generate("namespace test;");
    EXPECT_NE(code.find("#include <song/song.hpp>"), std::string::npos);
    EXPECT_NE(code.find("#include <string>"), std::string::npos);
    EXPECT_NE(code.find("#include <vector>"), std::string::npos);
    EXPECT_NE(code.find("#include <optional>"), std::string::npos);
}

TEST(CodegenTest, HeaderHasNamespace) {
    std::string code = parse_and_generate("namespace myapp;");
    EXPECT_NE(code.find("namespace song::myapp {"), std::string::npos);
    EXPECT_NE(code.find("} // namespace song::myapp"), std::string::npos);
}

TEST(CodegenTest, HeaderHasGeneratedComment) {
    std::string code = parse_and_generate("namespace test;");
    EXPECT_NE(code.find("Generated by songc"), std::string::npos);
    EXPECT_NE(code.find("DO NOT EDIT"), std::string::npos);
}
