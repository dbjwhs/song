// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include "parser.hpp"

using namespace song;
using namespace song::compiler;

// =============================================================================
// Basic Parsing
// =============================================================================

TEST(ParserTest, EmptyNamespace) {
    Parser parser("namespace test;");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces.size(), 1);
    EXPECT_EQ(ast.namespaces[0].name, "test");
    EXPECT_TRUE(ast.namespaces[0].structs.empty());
    EXPECT_TRUE(ast.namespaces[0].enums.empty());
    EXPECT_TRUE(ast.namespaces[0].classes.empty());
    EXPECT_TRUE(ast.namespaces[0].services.empty());
}

TEST(ParserTest, NamespaceWithDot) {
    Parser parser("namespace com;");  // Simple namespace for now
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces.size(), 1);
    EXPECT_EQ(ast.namespaces[0].name, "com");
}

// =============================================================================
// Struct Parsing
// =============================================================================

TEST(ParserTest, SimpleStruct) {
    Parser parser(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
    )");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces[0].structs.size(), 1);
    const auto& s = ast.namespaces[0].structs[0];
    EXPECT_EQ(s.name, "Point");
    EXPECT_FALSE(s.base.has_value());
    ASSERT_EQ(s.fields.size(), 2);
    EXPECT_EQ(s.fields[0].name, "x");
    EXPECT_EQ(s.fields[1].name, "y");
}

TEST(ParserTest, StructWithInheritance) {
    Parser parser(R"(
        namespace test;
        struct Point3D : Point {
            i32 z;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].structs[0];
    EXPECT_EQ(s.name, "Point3D");
    ASSERT_TRUE(s.base.has_value());
    EXPECT_EQ(*s.base, "Point");
    ASSERT_EQ(s.fields.size(), 1);
    EXPECT_EQ(s.fields[0].name, "z");
}

TEST(ParserTest, StructWithAllPrimitiveTypes) {
    Parser parser(R"(
        namespace test;
        struct AllTypes {
            bool flag;
            i8 tiny;
            i16 small;
            i32 medium;
            i64 large;
            u8 utiny;
            u16 usmall;
            u32 umedium;
            u64 ularge;
            f32 single;
            f64 double_;
            string text;
            bytes data;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].structs[0];
    ASSERT_EQ(s.fields.size(), 13);

    // Verify types
    EXPECT_TRUE(is_primitive(s.fields[0].type));
    EXPECT_EQ(get_primitive(s.fields[0].type), PrimitiveType::bool_);
    EXPECT_EQ(get_primitive(s.fields[4].type), PrimitiveType::i64);
    EXPECT_EQ(get_primitive(s.fields[11].type), PrimitiveType::string);
}

TEST(ParserTest, StructWithUserTypes) {
    Parser parser(R"(
        namespace test;
        struct Container {
            Point origin;
            Size dimensions;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].structs[0];
    ASSERT_EQ(s.fields.size(), 2);
    EXPECT_FALSE(is_primitive(s.fields[0].type));
    EXPECT_EQ(get_user_type(s.fields[0].type), "Point");
    EXPECT_EQ(get_user_type(s.fields[1].type), "Size");
}

// =============================================================================
// Enum Parsing
// =============================================================================

TEST(ParserTest, SimpleEnum) {
    Parser parser(R"(
        namespace test;
        enum Status {
            idle,
            running,
            stopped
        }
    )");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces[0].enums.size(), 1);
    const auto& e = ast.namespaces[0].enums[0];
    EXPECT_EQ(e.name, "Status");
    EXPECT_FALSE(e.is_flags);
    ASSERT_EQ(e.items.size(), 3);
    EXPECT_EQ(e.items[0].name, "idle");
    EXPECT_EQ(e.items[1].name, "running");
    EXPECT_EQ(e.items[2].name, "stopped");
}

TEST(ParserTest, EnumWithValues) {
    Parser parser(R"(
        namespace test;
        enum Color {
            red = 1,
            green = 2,
            blue = 3
        }
    )");
    auto ast = parser.parse();

    const auto& e = ast.namespaces[0].enums[0];
    ASSERT_EQ(e.items.size(), 3);
    ASSERT_TRUE(e.items[0].value.has_value());
    EXPECT_EQ(*e.items[0].value, 1);
    EXPECT_EQ(*e.items[1].value, 2);
    EXPECT_EQ(*e.items[2].value, 3);
}

TEST(ParserTest, FlagsWithHexValues) {
    Parser parser(R"(
        namespace test;
        flags Permissions {
            read = 0x01,
            write = 0x02,
            execute = 0x04
        }
    )");
    auto ast = parser.parse();

    const auto& e = ast.namespaces[0].enums[0];
    EXPECT_EQ(e.name, "Permissions");
    EXPECT_TRUE(e.is_flags);
    ASSERT_EQ(e.items.size(), 3);
    EXPECT_EQ(*e.items[0].value, 0x01);
    EXPECT_EQ(*e.items[1].value, 0x02);
    EXPECT_EQ(*e.items[2].value, 0x04);
}

// An enum value that overflows int64_t must produce a clean compiler error
// (LexerError, raised while lexing the literal) rather than an uncaught
// std::out_of_range from std::stoll crashing songc.
TEST(ParserTest, EnumValueOverflowThrowsCompilerError) {
    Parser parser(R"(
        namespace test;
        enum Big {
            huge = 99999999999999999999
        }
    )");
    EXPECT_THROW(parser.parse(), LexerError);
}

TEST(ParserTest, EnumTrailingComma) {
    Parser parser(R"(
        namespace test;
        enum Status {
            idle,
            running,
        }
    )");
    auto ast = parser.parse();

    const auto& e = ast.namespaces[0].enums[0];
    ASSERT_EQ(e.items.size(), 2);
}

// =============================================================================
// Class Parsing
// =============================================================================

TEST(ParserTest, ClassWithProperties) {
    Parser parser(R"(
        namespace test;
        class Canvas {
            i32 width;
            i32 height;
        }
    )");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces[0].classes.size(), 1);
    const auto& c = ast.namespaces[0].classes[0];
    EXPECT_EQ(c.name, "Canvas");
    ASSERT_EQ(c.properties.size(), 2);
    EXPECT_EQ(c.properties[0].name, "width");
    EXPECT_FALSE(c.properties[0].readonly);
    EXPECT_EQ(c.properties[1].name, "height");
}

TEST(ParserTest, ClassWithReadonlyProperty) {
    Parser parser(R"(
        namespace test;
        class Canvas {
            readonly i32 width;
            readonly i32 height;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 2);
    EXPECT_TRUE(c.properties[0].readonly);
    EXPECT_TRUE(c.properties[1].readonly);
}

TEST(ParserTest, ClassWithConstructor) {
    Parser parser(R"(
        namespace test;
        class Canvas {
            Canvas(i32 w, i32 h);
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.constructors.size(), 1);
    const auto& ctor = c.constructors[0];
    ASSERT_EQ(ctor.params.size(), 2);
    EXPECT_EQ(ctor.params[0].name, "w");
    EXPECT_EQ(ctor.params[1].name, "h");
}

TEST(ParserTest, ClassWithMethods) {
    Parser parser(R"(
        namespace test;
        class Canvas {
            draw_pixel(i32 x, i32 y, u32 color) -> void;
            get_pixel(i32 x, i32 y) -> u32;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.methods.size(), 2);

    EXPECT_EQ(c.methods[0].name, "draw_pixel");
    ASSERT_EQ(c.methods[0].params.size(), 3);
    EXPECT_EQ(get_primitive(c.methods[0].return_type), PrimitiveType::void_);

    EXPECT_EQ(c.methods[1].name, "get_pixel");
    ASSERT_EQ(c.methods[1].params.size(), 2);
    EXPECT_EQ(get_primitive(c.methods[1].return_type), PrimitiveType::u32);
}

TEST(ParserTest, ClassWithInheritance) {
    Parser parser(R"(
        namespace test;
        class ColorCanvas : Canvas {
            set_color(u32 color) -> void;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    EXPECT_EQ(c.name, "ColorCanvas");
    ASSERT_TRUE(c.base.has_value());
    EXPECT_EQ(*c.base, "Canvas");
}

TEST(ParserTest, ClassComplete) {
    Parser parser(R"(
        namespace test;
        class Canvas {
            readonly i32 width;
            readonly i32 height;
            Canvas(i32 w, i32 h);
            draw_pixel(i32 x, i32 y, u32 color) -> void;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    EXPECT_EQ(c.properties.size(), 2);
    EXPECT_EQ(c.constructors.size(), 1);
    EXPECT_EQ(c.methods.size(), 1);
}

// =============================================================================
// Service Parsing
// =============================================================================

TEST(ParserTest, SimpleService) {
    Parser parser(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
            multiply(i32 a, i32 b) -> i32;
        }
    )");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces[0].services.size(), 1);
    const auto& s = ast.namespaces[0].services[0];
    EXPECT_EQ(s.name, "Calculator");
    ASSERT_EQ(s.methods.size(), 2);
    EXPECT_EQ(s.methods[0].name, "add");
    EXPECT_EQ(s.methods[1].name, "multiply");
}

TEST(ParserTest, ServiceWithStreamMethod) {
    Parser parser(R"(
        namespace test;
        service DataService {
            stream fetch_all() -> Record;
        }
    )");
    auto ast = parser.parse();

    const auto& m = ast.namespaces[0].services[0].methods[0];
    EXPECT_EQ(m.name, "fetch_all");
    EXPECT_TRUE(m.is_stream);
    EXPECT_FALSE(m.is_optional);
}

TEST(ParserTest, ServiceWithOptionalMethod) {
    Parser parser(R"(
        namespace test;
        service DataService {
            optional find(string id) -> Record;
        }
    )");
    auto ast = parser.parse();

    const auto& m = ast.namespaces[0].services[0].methods[0];
    EXPECT_EQ(m.name, "find");
    EXPECT_TRUE(m.is_optional);
    EXPECT_FALSE(m.is_stream);
}

TEST(ParserTest, ServiceWithThrows) {
    Parser parser(R"(
        namespace test;
        service DataService {
            save(Record r) -> void throws ValidationError;
        }
    )");
    auto ast = parser.parse();

    const auto& m = ast.namespaces[0].services[0].methods[0];
    ASSERT_EQ(m.throws.size(), 1);
    EXPECT_EQ(m.throws[0], "ValidationError");
}

TEST(ParserTest, ServiceWithMultipleThrows) {
    Parser parser(R"(
        namespace test;
        service DataService {
            save(Record r) -> void throws ValidationError, StorageError;
        }
    )");
    auto ast = parser.parse();

    const auto& m = ast.namespaces[0].services[0].methods[0];
    ASSERT_EQ(m.throws.size(), 2);
    EXPECT_EQ(m.throws[0], "ValidationError");
    EXPECT_EQ(m.throws[1], "StorageError");
}

// =============================================================================
// Error Definition Parsing
// =============================================================================

TEST(ParserTest, SimpleError) {
    Parser parser(R"(
        namespace test;
        error ValidationError {
            string message;
            i32 code;
        }
    )");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces[0].errors.size(), 1);
    const auto& e = ast.namespaces[0].errors[0];
    EXPECT_EQ(e.name, "ValidationError");
    ASSERT_EQ(e.fields.size(), 2);
}

TEST(ParserTest, ErrorWithInheritance) {
    Parser parser(R"(
        namespace test;
        error NotFoundError : ValidationError {
            string resource_id;
        }
    )");
    auto ast = parser.parse();

    const auto& e = ast.namespaces[0].errors[0];
    EXPECT_EQ(e.name, "NotFoundError");
    ASSERT_TRUE(e.base.has_value());
    EXPECT_EQ(*e.base, "ValidationError");
}

// =============================================================================
// Type Parsing
// =============================================================================

TEST(ParserTest, ArrayType) {
    Parser parser(R"(
        namespace test;
        struct Container {
            i32[] values;
        }
    )");
    auto ast = parser.parse();

    const auto& f = ast.namespaces[0].structs[0].fields[0];
    EXPECT_TRUE(f.type.is_array);
    EXPECT_EQ(f.type.array_dimensions, 1);
}

TEST(ParserTest, MultiDimensionalArray) {
    Parser parser(R"(
        namespace test;
        struct Matrix {
            f64[][] values;
        }
    )");
    auto ast = parser.parse();

    const auto& f = ast.namespaces[0].structs[0].fields[0];
    EXPECT_TRUE(f.type.is_array);
    EXPECT_EQ(f.type.array_dimensions, 2);
}

TEST(ParserTest, OptionalType) {
    Parser parser(R"(
        namespace test;
        struct Person {
            string? nickname;
        }
    )");
    auto ast = parser.parse();

    const auto& f = ast.namespaces[0].structs[0].fields[0];
    EXPECT_TRUE(f.type.is_optional);
    EXPECT_FALSE(f.type.is_array);
}

TEST(ParserTest, OptionalArrayType) {
    Parser parser(R"(
        namespace test;
        struct Container {
            string[]? items;
        }
    )");
    auto ast = parser.parse();

    const auto& f = ast.namespaces[0].structs[0].fields[0];
    EXPECT_TRUE(f.type.is_array);
    EXPECT_TRUE(f.type.is_optional);
}

// =============================================================================
// Doc Comments
// =============================================================================

TEST(ParserTest, StructDocComment) {
    Parser parser(R"(
        namespace test;
        /// A 2D point
        struct Point {
            i32 x;
            i32 y;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].structs[0];
    EXPECT_EQ(s.doc, "A 2D point");
}

TEST(ParserTest, MultiLineDocComment) {
    Parser parser(R"(
        namespace test;
        /// A 2D point
        /// with integer coordinates
        struct Point {
            i32 x;
            i32 y;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].structs[0];
    EXPECT_EQ(s.doc, "A 2D point\nwith integer coordinates");
}

TEST(ParserTest, FieldDocComment) {
    Parser parser(R"(
        namespace test;
        struct Point {
            /// X coordinate
            i32 x;
            /// Y coordinate
            i32 y;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].structs[0];
    EXPECT_EQ(s.fields[0].doc, "X coordinate");
    EXPECT_EQ(s.fields[1].doc, "Y coordinate");
}

TEST(ParserTest, EnumDocComments) {
    Parser parser(R"(
        namespace test;
        /// Status enum
        enum Status {
            /// Idle state
            idle,
            /// Running state
            running
        }
    )");
    auto ast = parser.parse();

    const auto& e = ast.namespaces[0].enums[0];
    EXPECT_EQ(e.doc, "Status enum");
    EXPECT_EQ(e.items[0].doc, "Idle state");
    EXPECT_EQ(e.items[1].doc, "Running state");
}

TEST(ParserTest, ServiceDocComments) {
    Parser parser(R"(
        namespace test;
        /// Calculator service
        service Calculator {
            /// Add two numbers
            add(i32 a, i32 b) -> i32;
        }
    )");
    auto ast = parser.parse();

    const auto& s = ast.namespaces[0].services[0];
    EXPECT_EQ(s.doc, "Calculator service");
    EXPECT_EQ(s.methods[0].doc, "Add two numbers");
}

// =============================================================================
// Multiple Definitions
// =============================================================================

TEST(ParserTest, MultipleDefinitions) {
    Parser parser(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
        }

        enum Color {
            red,
            green,
            blue
        }

        class Canvas {
            Canvas(i32 w, i32 h);
        }

        service CanvasFactory {
            create(i32 w, i32 h) -> Canvas;
        }

        error DrawError {
            string message;
        }
    )");
    auto ast = parser.parse();

    const auto& ns = ast.namespaces[0];
    EXPECT_EQ(ns.structs.size(), 1);
    EXPECT_EQ(ns.enums.size(), 1);
    EXPECT_EQ(ns.classes.size(), 1);
    EXPECT_EQ(ns.services.size(), 1);
    EXPECT_EQ(ns.errors.size(), 1);
}

// =============================================================================
// Error Cases
// =============================================================================

TEST(ParserTest, MissingNamespace) {
    Parser parser("struct Point { i32 x; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

TEST(ParserTest, MissingNamespaceSemicolon) {
    Parser parser("namespace test struct Point { }");
    EXPECT_THROW(parser.parse(), ParserError);
}

TEST(ParserTest, MissingStructBrace) {
    Parser parser("namespace test; struct Point i32 x; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

TEST(ParserTest, MissingFieldSemicolon) {
    Parser parser("namespace test; struct Point { i32 x }");
    EXPECT_THROW(parser.parse(), ParserError);
}

TEST(ParserTest, InvalidType) {
    Parser parser("namespace test; struct Point { @ x; }");
    // Lexer fails first on '@' character
    EXPECT_THROW(parser.parse(), LexerError);
}

TEST(ParserTest, ParserErrorHasLocation) {
    Parser parser("namespace test; struct Point { i32 x }");

    try {
        parser.parse();
        FAIL() << "Expected ParserError";
    } catch (const ParserError& e) {
        EXPECT_GT(e.line(), 0);
        EXPECT_GT(e.column(), 0);
    }
}

// =============================================================================
// Complex Examples
// =============================================================================

TEST(ParserTest, CanvasExample) {
    Parser parser(R"(
        namespace canvas;

        /// RGBA color value
        struct Color {
            u8 r;
            u8 g;
            u8 b;
            u8 a;
        }

        /// Pixel format flags
        flags PixelFormat {
            rgb = 0x01,
            rgba = 0x02,
            grayscale = 0x04
        }

        /// Drawing canvas
        class Canvas {
            readonly i32 width;
            readonly i32 height;

            Canvas(i32 w, i32 h);

            /// Set a pixel color
            set_pixel(i32 x, i32 y, Color c) -> void;

            /// Get a pixel color
            get_pixel(i32 x, i32 y) -> Color;

            /// Clear the canvas
            clear(Color c) -> void;
        }

        /// Canvas factory service
        service CanvasFactory {
            /// Create a new canvas
            create(i32 width, i32 height) -> Canvas;

            /// Load canvas from file
            optional load(string path) -> Canvas throws IOError;
        }

        /// IO error
        error IOError {
            string message;
            i32 code;
        }
    )");

    auto ast = parser.parse();

    EXPECT_EQ(ast.namespaces[0].name, "canvas");
    EXPECT_EQ(ast.namespaces[0].structs.size(), 1);
    EXPECT_EQ(ast.namespaces[0].enums.size(), 1);
    EXPECT_EQ(ast.namespaces[0].classes.size(), 1);
    EXPECT_EQ(ast.namespaces[0].services.size(), 1);
    EXPECT_EQ(ast.namespaces[0].errors.size(), 1);

    // Verify class details
    const auto& canvas = ast.namespaces[0].classes[0];
    EXPECT_EQ(canvas.name, "Canvas");
    EXPECT_EQ(canvas.doc, "Drawing canvas");
    EXPECT_EQ(canvas.properties.size(), 2);
    EXPECT_EQ(canvas.constructors.size(), 1);
    EXPECT_EQ(canvas.methods.size(), 3);

    // Verify service method with throws
    const auto& factory = ast.namespaces[0].services[0];
    EXPECT_TRUE(factory.methods[1].is_optional);
    EXPECT_EQ(factory.methods[1].throws.size(), 1);
}

