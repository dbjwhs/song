// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include "parser.hpp"
#include "resolver.hpp"
#include "codegen.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>

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

// Optional codegen is not implemented. songc must reject a struct with an
// optional field with a clear CodegenError rather than emitting code that
// references the nonexistent encode_optional/decode_optional runtime helpers.
TEST(CodegenTest, OptionalFieldRejected) {
    EXPECT_THROW(parse_and_generate(R"(
        namespace test;
        struct Person {
            string name;
            string? nickname;
        }
    )"), CodegenError);
}

// An enum used as a value type has no generated serializer (only the enum class
// definition is emitted), so it must be rejected rather than emit calls to a
// nonexistent encode_<Enum>/decode_<Enum>.
TEST(CodegenTest, EnumTypedFieldRejected) {
    EXPECT_THROW(parse_and_generate(R"(
        namespace test;
        enum Status { idle, running }
        struct Job {
            Status state;
        }
    )"), CodegenError);
}

TEST(CodegenTest, ArrayOfEnumRejected) {
    EXPECT_THROW(parse_and_generate(R"(
        namespace test;
        enum Status { idle, running }
        struct Log {
            Status[] history;
        }
    )"), CodegenError);
}

// A multi-dimensional array of a struct would route to
// encode_array<std::vector<Point>>, which recurses to encode_array<Point> and
// trips a runtime static_assert. Reject it at generate time.
TEST(CodegenTest, MultiDimStructArrayRejected) {
    EXPECT_THROW(parse_and_generate(R"(
        namespace test;
        struct Point { i32 x; i32 y; }
        struct Grid {
            Point[][] cells;
        }
    )"), CodegenError);
}

// Contrast: a primitive multi-dimensional array remains supported; it routes to
// the runtime template, which handles primitive element types.
TEST(CodegenTest, PrimitiveMultiDimArrayStillGenerates) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Matrix {
            f64[][] data;
        }
    )");
    EXPECT_NE(code.find("struct Matrix"), std::string::npos);
}

// Contrast: a one-dimensional array of a struct remains supported via the
// generated encode_array_<T> helper.
TEST(CodegenTest, StructArrayStillGenerates) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Point { i32 x; i32 y; }
        struct Path {
            Point[] points;
        }
    )");
    EXPECT_NE(code.find("encode_array_Point"), std::string::npos);
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

// =============================================================================
// Compiled Codegen — generate C++ from IDL and compile it
// =============================================================================

TEST(CodegenTest, GeneratedHeaderCompiles) {
    // Generate a non-trivial header from IDL and verify it compiles
    std::string source = R"(
        namespace calculator;

        enum Operation {
            Add,
            Subtract,
            Multiply,
            Divide
        }

        struct Point {
            i32 x;
            i32 y;
        }

        struct Line {
            Point start;
            Point end;
            f64 length;
            i32[] tags;
        }

        service Calculator {
            add(i32 a, i32 b) -> i32;
            distance(Point a, Point b) -> f64;
            batch_add(i32[] values) -> i32;
        }
    )";

    Parser parser(source);
    auto ast = parser.parse();
    Resolver resolver(ast);
    ASSERT_TRUE(resolver.resolve());

    CodeGenerator gen;
    std::string header = gen.generate_header(ast.namespaces.front());

    // Write to a temp file
    auto tmp_dir = std::filesystem::temp_directory_path();
    auto header_path = tmp_dir / "song_codegen_test.hpp";
    auto source_path = tmp_dir / "song_codegen_test.cpp";

    {
        std::ofstream hf(header_path);
        ASSERT_TRUE(hf.is_open());
        hf << header;
    }

    // Write a minimal .cpp that #includes the generated header
    {
        std::ofstream sf(source_path);
        ASSERT_TRUE(sf.is_open());
        sf << "#include \"song_codegen_test.hpp\"\n"
           << "int main() { return 0; }\n";
    }

    // Find song include directory (relative to test binary location)
    std::filesystem::path song_include;
    for (auto candidate : {
        std::filesystem::current_path() / ".." / "include",
        std::filesystem::current_path() / "include",
        std::filesystem::current_path().parent_path() / "include",
    }) {
        if (std::filesystem::exists(candidate / "song" / "song.hpp")) {
            song_include = candidate;
            break;
        }
    }

    if (song_include.empty()) {
        std::filesystem::remove(header_path);
        std::filesystem::remove(source_path);
        GTEST_SKIP() << "Could not locate song include directory";
    }

    // Compile with syntax check only (no linking needed)
    std::string cmd = "clang++ -std=c++20 -fsyntax-only"
                      " -I" + song_include.string() +
                      " -I" + tmp_dir.string() +
                      " " + source_path.string() +
                      " 2>&1";

    int ret = std::system(cmd.c_str());

    // Clean up temp files
    std::filesystem::remove(header_path);
    std::filesystem::remove(source_path);

    EXPECT_EQ(ret, 0) << "Generated C++ header failed to compile.\n"
                       << "Command: " << cmd << "\n"
                       << "Generated header:\n" << header;
}

// =============================================================================
// Split-mode generators (generate_types_header / generate_wire_impl /
// generate_client_header / generate_server_header) -- these public entry points
// are used by songc --split but were never exercised by a test.
// =============================================================================

namespace {

// Parse + resolve `source`, then hand the code generator and the resolved
// namespace to `emit`. Mirrors parse_and_generate but lets a test target the
// split-mode generators instead of generate_header.
template <typename Emit>
std::string generate_split(const std::string& source, Emit emit) {
    Parser parser(source);
    auto ast = parser.parse();
    Resolver resolver(ast);
    EXPECT_TRUE(resolver.resolve());
    CodeGenerator gen;
    return emit(gen, ast.namespaces.front());
}

}  // namespace

TEST(CodegenTest, TypesHeaderEmitsNonInlineSerializerDecls) {
    std::string code = generate_split(R"(
        namespace wiremod;
        struct P { i32 x; }
        struct L { P[] items; }
    )", [](CodeGenerator& gen, const Namespace& ns) {
        return gen.generate_types_header(ns);
    });

    // Forward declaration and struct definition present
    EXPECT_NE(code.find("struct P;"), std::string::npos);
    EXPECT_NE(code.find("struct P {"), std::string::npos);

    // The types header emits non-inline serialization *declarations*; the
    // definitions live in the wire impl TU.
    EXPECT_NE(code.find("void encode_P(Buffer& buf, const P& val);"), std::string::npos);
    EXPECT_NE(code.find("P decode_P(Buffer& buf);"), std::string::npos);
    EXPECT_EQ(code.find("inline void encode_P("), std::string::npos);
}

TEST(CodegenTest, WireImplEmitsInlineStructSerializers) {
    std::string code = generate_split(R"(
        namespace wiremod;
        struct P { i32 x; }
        struct L { P[] items; }
    )", [](CodeGenerator& gen, const Namespace& ns) {
        return gen.generate_wire_impl(ns);
    });

    // Wire impl includes its matching types header and defines inline serializers
    EXPECT_NE(code.find("#include \"wiremod_types.hpp\""), std::string::npos);
    EXPECT_NE(code.find("inline void encode_P(Buffer& buf, const P& val) {"), std::string::npos);
    EXPECT_NE(code.find("inline P decode_P(Buffer& buf) {"), std::string::npos);
}

// generate_wire_impl only emits per-struct encode/decode, not the
// encode_array_<S>/decode_array_<S> helpers that generate_header emits. A
// split-mode struct with an array-of-struct field therefore *calls*
// encode_array_P without any definition being produced by this generator. This
// pins that split-mode gap (the call is present, the definition is not).
TEST(CodegenTest, WireImplOmitsStructArrayHelper) {
    std::string code = generate_split(R"(
        namespace wiremod;
        struct P { i32 x; }
        struct L { P[] items; }
    )", [](CodeGenerator& gen, const Namespace& ns) {
        return gen.generate_wire_impl(ns);
    });

    // The array-of-struct field routes to encode_array_P (a generated helper)...
    EXPECT_NE(code.find("encode_array_P(buf, val.items)"), std::string::npos);
    // ...but generate_wire_impl never emits that helper's definition.
    EXPECT_EQ(code.find("inline void encode_array_P("), std::string::npos);
    EXPECT_EQ(code.find("inline std::vector<P> decode_array_P("), std::string::npos);
}

TEST(CodegenTest, ClientHeaderHasProxyNotDispatcherOrInterface) {
    std::string code = generate_split(R"(
        namespace calc;
        service Calculator {
            add(i32 a, i32 b) -> i32;
        }
    )", [](CodeGenerator& gen, const Namespace& ns) {
        return gen.generate_client_header(ns);
    });

    // Client header carries IDs and the proxy...
    EXPECT_NE(code.find("#include \"calc_types.hpp\""), std::string::npos);
    EXPECT_NE(code.find("kService_Calculator"), std::string::npos);
    EXPECT_NE(code.find("class CalculatorProxy {"), std::string::npos);

    // ...but not the server-side interface or dispatcher.
    EXPECT_EQ(code.find("dispatch_Calculator"), std::string::npos);
    EXPECT_EQ(code.find("class ICalculator {"), std::string::npos);
}

TEST(CodegenTest, ServerHeaderHasInterfaceAndDispatcherNotProxy) {
    std::string code = generate_split(R"(
        namespace calc;
        service Calculator {
            add(i32 a, i32 b) -> i32;
        }
    )", [](CodeGenerator& gen, const Namespace& ns) {
        return gen.generate_server_header(ns);
    });

    // Server header carries the interface and dispatcher...
    EXPECT_NE(code.find("#include \"calc_types.hpp\""), std::string::npos);
    EXPECT_NE(code.find("class ICalculator {"), std::string::npos);
    EXPECT_NE(code.find("dispatch_Calculator"), std::string::npos);

    // ...but not the client-side proxy.
    EXPECT_EQ(code.find("class CalculatorProxy {"), std::string::npos);
}

// =============================================================================
// readonly property suppression
// =============================================================================

// A readonly property must get a getter but no setter in the proxy or skeleton,
// and no prop_set case, while a writable sibling gets both.
TEST(CodegenTest, ReadonlyPropertyHasNoSetter) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Sensor {
            readonly i32 id;
            f64 value;
            Sensor();
        }
    )");

    // Readonly 'id': getter present, setter fully suppressed everywhere.
    EXPECT_NE(code.find("i32 id() const"), std::string::npos);          // proxy getter
    EXPECT_NE(code.find("virtual i32 get_id() const"), std::string::npos);  // skeleton getter
    EXPECT_EQ(code.find("set_id"), std::string::npos);                  // no setter at all

    // Writable 'value': setter present in proxy and skeleton.
    EXPECT_NE(code.find("void set_value("), std::string::npos);

    // prop_set default arm documents the readonly/unknown case.
    EXPECT_NE(code.find("Unknown or readonly property ID"), std::string::npos);
}

// =============================================================================
// class inheritance: base delegation in skeleton and dispatcher
// =============================================================================

TEST(CodegenTest, DerivedClassDelegatesToBase) {
    std::string code = parse_and_generate(R"(
        namespace test;
        class Base { i32 a; Base(); foo() -> void; }
        class Derived : Base { i32 b; Derived(); bar() -> void; }
    )");

    // Skeleton derives from the base skeleton.
    EXPECT_NE(code.find("class DerivedBase : public BaseBase {"), std::string::npos);

    // Dispatcher default arms delegate to the base rather than throwing.
    EXPECT_NE(code.find("BaseBase::prop_get(prop_id, resp);"), std::string::npos);
    EXPECT_NE(code.find("BaseBase::prop_set(prop_id, req, resp);"), std::string::npos);
    EXPECT_NE(code.find("BaseBase::dispatch(method_id, req, resp);"), std::string::npos);
}

// =============================================================================
// Boundary / degenerate definitions
// =============================================================================

// Struct inheritance is accepted by the parser/resolver but silently ignored by
// codegen: the derived struct is emitted with only its own fields and no base,
// so inherited fields are dropped from both the type and the wire format. This
// pins that aspirational-drift behavior (per CLAUDE.md, flag doc/impl drift).
TEST(CodegenTest, StructInheritanceIgnoredByCodegen) {
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Base { i32 a; }
        struct Derived : Base { i32 b; }
    )");

    // No ': public Base' is emitted, and Derived carries only its own field 'b'
    // (inherited 'a' is absent from the struct body).
    EXPECT_EQ(code.find("struct Derived : public Base"), std::string::npos);
    EXPECT_NE(code.find("struct Derived {\n    i32 b;\n};"), std::string::npos);
}

TEST(CodegenTest, EmptyAndSingleItemBoundaries) {
    // Empty struct and empty service must generate without error.
    std::string code = parse_and_generate(R"(
        namespace test;
        struct Empty {}
        service Noop {}
    )");
    EXPECT_NE(code.find("struct Empty {"), std::string::npos);
    EXPECT_NE(code.find("dispatch_Noop"), std::string::npos);

    // Single-item enum must emit no trailing comma after the sole item.
    std::string enum_code = parse_and_generate(R"(
        namespace test;
        enum One { only }
    )");
    EXPECT_NE(enum_code.find("enum class One : i32 {"), std::string::npos);
    EXPECT_NE(enum_code.find("only"), std::string::npos);
    EXPECT_EQ(enum_code.find("only,"), std::string::npos);
}

// =============================================================================
// Generated class output compiles against the runtime (not just string-matched)
// =============================================================================

TEST(CodegenTest, GeneratedClassHeaderCompiles) {
    // Class codegen (proxy + skeleton + dispatcher) is otherwise only
    // string-matched; compile it against include/song to verify it type-checks
    // against Object/ServiceConnection. Exercises a struct property, a readonly
    // property, multiple constructors, and void/value/struct-returning methods.
    std::string source = R"(
        namespace counterdemo;

        struct Pt { i32 x; i32 y; }

        class Counter {
            i32 value;
            readonly i32 id;
            Counter();
            Counter(i32 start);
            increment() -> void;
            add(i32 d) -> i32;
            move_by(Pt d) -> Pt;
        }
    )";

    Parser parser(source);
    auto ast = parser.parse();
    Resolver resolver(ast);
    ASSERT_TRUE(resolver.resolve());

    CodeGenerator gen;
    std::string header = gen.generate_header(ast.namespaces.front());

    auto tmp_dir = std::filesystem::temp_directory_path();
    auto header_path = tmp_dir / "song_codegen_class_test.hpp";
    auto source_path = tmp_dir / "song_codegen_class_test.cpp";

    {
        std::ofstream hf(header_path);
        ASSERT_TRUE(hf.is_open());
        hf << header;
    }
    {
        std::ofstream sf(source_path);
        ASSERT_TRUE(sf.is_open());
        sf << "#include \"song_codegen_class_test.hpp\"\n"
           << "int main() { return 0; }\n";
    }

    std::filesystem::path song_include;
    for (auto candidate : {
        std::filesystem::current_path() / ".." / "include",
        std::filesystem::current_path() / "include",
        std::filesystem::current_path().parent_path() / "include",
    }) {
        if (std::filesystem::exists(candidate / "song" / "song.hpp")) {
            song_include = candidate;
            break;
        }
    }

    if (song_include.empty()) {
        std::filesystem::remove(header_path);
        std::filesystem::remove(source_path);
        GTEST_SKIP() << "Could not locate song include directory";
    }

    std::string cmd = "clang++ -std=c++20 -fsyntax-only"
                      " -I" + song_include.string() +
                      " -I" + tmp_dir.string() +
                      " " + source_path.string() +
                      " 2>&1";

    int ret = std::system(cmd.c_str());

    std::filesystem::remove(header_path);
    std::filesystem::remove(source_path);

    EXPECT_EQ(ret, 0) << "Generated C++ class header failed to compile.\n"
                      << "Command: " << cmd << "\n"
                      << "Generated header:\n" << header;
}
