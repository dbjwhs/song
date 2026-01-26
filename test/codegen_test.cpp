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

TEST(CodegenTest, StructWith2DArray) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Matrix {
            f64[][] data;
        }
    )");

    // Type should be nested vector
    EXPECT_NE(code.find("std::vector<std::vector<f64>> data;"), std::string::npos);
    // Encode should use nested vector template
    EXPECT_NE(code.find("encode_array<std::vector<f64>>"), std::string::npos);
    // Decode should use nested vector template
    EXPECT_NE(code.find("decode_array<std::vector<f64>>"), std::string::npos);
}

TEST(CodegenTest, StructWith3DArray) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Tensor {
            i32[][][] values;
        }
    )");

    // Type should be triple-nested vector
    EXPECT_NE(code.find("std::vector<std::vector<std::vector<i32>>> values;"), std::string::npos);
    // Encode should use double-nested vector template
    EXPECT_NE(code.find("encode_array<std::vector<std::vector<i32>>>"), std::string::npos);
    // Decode should use double-nested vector template
    EXPECT_NE(code.find("decode_array<std::vector<std::vector<i32>>>"), std::string::npos);
}

TEST(CodegenTest, ServiceWith2DArrayParam) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Calculator {
            sum_matrix(f64[][] matrix) -> f64;
        }
    )");

    // Parameter type
    EXPECT_NE(code.find("const std::vector<std::vector<f64>>& matrix"), std::string::npos);
    // Encode call in proxy
    EXPECT_NE(code.find("encode_array<std::vector<f64>>"), std::string::npos);
}

TEST(CodegenTest, ServiceWith2DArrayReturn) {
    std::string code = parse_and_generate(R"(
        namespace test;
        service Grid {
            get_data() -> i32[][];
        }
    )");

    // Return type
    EXPECT_NE(code.find("std::vector<std::vector<i32>> get_data()"), std::string::npos);
    // Decode call in proxy
    EXPECT_NE(code.find("decode_array<std::vector<i32>>"), std::string::npos);
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

// =============================================================================
// Class Generation
// =============================================================================

TEST(CodegenTest, ClassIds) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Canvas {
            i32 width;
            i32 height;
            Canvas(i32 w, i32 h);
            clear() -> void;
        }
    )");

    // Should have type ID
    EXPECT_NE(code.find("kType_Canvas"), std::string::npos);

    // Should have property IDs
    EXPECT_NE(code.find("kProp_Canvas_width"), std::string::npos);
    EXPECT_NE(code.find("kProp_Canvas_height"), std::string::npos);

    // Should have constructor ID (numeric)
    EXPECT_NE(code.find("kCtor_Canvas_0"), std::string::npos);

    // Should have method ID
    EXPECT_NE(code.find("kMethod_Canvas_clear"), std::string::npos);
}

TEST(CodegenTest, ClassProxy) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Counter {
            i32 value;
            Counter();
            increment() -> void;
            add(i32 delta) -> i32;
        }
    )");

    // Should have proxy class
    EXPECT_NE(code.find("class CounterProxy {"), std::string::npos);

    // Should have connection member (reference)
    EXPECT_NE(code.find("ServiceConnection& m_conn"), std::string::npos);

    // Should have object ID member
    EXPECT_NE(code.find("i32 m_object_id"), std::string::npos);

    // Should have property getter
    EXPECT_NE(code.find("i32 value()"), std::string::npos);

    // Should have property setter
    EXPECT_NE(code.find("void set_value("), std::string::npos);

    // Should have method
    EXPECT_NE(code.find("void increment()"), std::string::npos);
    EXPECT_NE(code.find("i32 add("), std::string::npos);

    // Should have destructor that releases object
    EXPECT_NE(code.find("~CounterProxy()"), std::string::npos);
    EXPECT_NE(code.find("release_object"), std::string::npos);
}

TEST(CodegenTest, ClassProxyMoveSemantics) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Widget {
            Widget();
        }
    )");

    // Should have deleted copy constructor
    EXPECT_NE(code.find("WidgetProxy(const WidgetProxy&) = delete"), std::string::npos);

    // Should have deleted copy assignment
    EXPECT_NE(code.find("WidgetProxy& operator=(const WidgetProxy&) = delete"), std::string::npos);

    // Should have move constructor
    EXPECT_NE(code.find("WidgetProxy(WidgetProxy&& other) noexcept"), std::string::npos);

    // Should have move assignment
    EXPECT_NE(code.find("WidgetProxy& operator=(WidgetProxy&& other) noexcept"), std::string::npos);
}

TEST(CodegenTest, ClassSkeleton) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Counter {
            i32 value;
            Counter();
            increment() -> void;
        }
    )");

    // Should have skeleton base class
    EXPECT_NE(code.find("class CounterBase : public song::Object {"), std::string::npos);

    // Should have protected property storage
    EXPECT_NE(code.find("i32 value_"), std::string::npos);

    // Should have macro extension point
    EXPECT_NE(code.find("COUNTER_SERVER_PRIVATE"), std::string::npos);

    // Should have virtual method
    EXPECT_NE(code.find("virtual void increment()"), std::string::npos);
}

TEST(CodegenTest, ClassSkeletonMacroPattern) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class MyClass {
            MyClass();
        }
    )");

    // Should have ifdef for private extension
    EXPECT_NE(code.find("#ifdef MYCLASS_SERVER_PRIVATE"), std::string::npos);
    EXPECT_NE(code.find("MYCLASS_SERVER_PRIVATE"), std::string::npos);
    EXPECT_NE(code.find("#endif"), std::string::npos);
}

TEST(CodegenTest, ClassDispatcher) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Counter {
            i32 value;
            Counter();
            add(i32 delta) -> i32;
        }
    )");

    // Should have dispatch method implementation
    EXPECT_NE(code.find("prop_get(u16 prop_id, Buffer& resp)"), std::string::npos);
    EXPECT_NE(code.find("prop_set(u16 prop_id, Buffer& req, Buffer& resp)"), std::string::npos);
    EXPECT_NE(code.find("dispatch(u16 method_id, Buffer& req, Buffer& resp)"), std::string::npos);

    // Should switch on property/method IDs
    EXPECT_NE(code.find("switch (prop_id)"), std::string::npos);
    EXPECT_NE(code.find("switch (method_id)"), std::string::npos);
}

TEST(CodegenTest, ClassWithStructProperty) {
    std::string code = parse_and_generate(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
        }

        class Shape {
            Point position;
            Shape();
        }
    )");

    // Should have struct property
    EXPECT_NE(code.find("Point position_"), std::string::npos);

    // Should use struct encode/decode
    EXPECT_NE(code.find("encode_Point"), std::string::npos);
    EXPECT_NE(code.find("decode_Point"), std::string::npos);
}

TEST(CodegenTest, ClassWithMultipleConstructors) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Buffer {
            Buffer();
            Buffer(i32 size);
            Buffer(string name, i32 size);
        }
    )");

    // Should have constructor IDs for each (numeric)
    EXPECT_NE(code.find("kCtor_Buffer_0"), std::string::npos);
    EXPECT_NE(code.find("kCtor_Buffer_1"), std::string::npos);
    EXPECT_NE(code.find("kCtor_Buffer_2"), std::string::npos);
}

TEST(CodegenTest, ClassForwardDeclarations) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Widget {
            Widget();
        }
    )");

    // Should have forward declarations for proxy and base
    EXPECT_NE(code.find("class WidgetProxy;"), std::string::npos);
    EXPECT_NE(code.find("class WidgetBase;"), std::string::npos);
}
