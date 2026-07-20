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

// H21: a user-typed class property with prefix array/optional markers must parse
// like a struct field. Previously the class-property path handled only the
// postfix C-style form, so "Point[] pts;" threw "Expected property name (got [)".
TEST(ParserTest, ClassUserTypeArrayProperty) {
    Parser parser(R"(
        namespace test;
        class C { Point[] pts; }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 1u);
    EXPECT_EQ(c.properties[0].name, "pts");
    EXPECT_EQ(get_user_type(c.properties[0].type), "Point");
    EXPECT_TRUE(c.properties[0].type.is_array);
    EXPECT_EQ(c.properties[0].type.array_dimensions, 1);
}

TEST(ParserTest, ClassUserTypeOptionalProperty) {
    Parser parser(R"(
        namespace test;
        class C { Point? maybe; }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 1u);
    EXPECT_EQ(c.properties[0].name, "maybe");
    EXPECT_EQ(get_user_type(c.properties[0].type), "Point");
    EXPECT_TRUE(c.properties[0].type.is_optional);
}

// Contrast: a primitive-typed array and a user-typed array property must parse
// identically now (previously the primitive parsed and the user type threw).
TEST(ParserTest, ClassPrimitiveAndUserTypeArraysParseIdentically) {
    Parser parser(R"(
        namespace test;
        class C {
            i32[] a;
            Point[] b;
        }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 2u);
    EXPECT_TRUE(c.properties[0].type.is_array);
    EXPECT_EQ(c.properties[0].type.array_dimensions, 1);
    EXPECT_TRUE(c.properties[1].type.is_array);
    EXPECT_EQ(c.properties[1].type.array_dimensions, 1);
    EXPECT_EQ(get_user_type(c.properties[1].type), "Point");
}

TEST(ParserTest, ClassUserTypeMultiDimArrayProperty) {
    Parser parser(R"(
        namespace test;
        class C { Point[][] grid; }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 1u);
    EXPECT_EQ(c.properties[0].type.array_dimensions, 2);
}

// H23: a property whose type equals the enclosing class name must parse as a
// property, not be mis-dispatched as a constructor.
TEST(ParserTest, ClassSelfReferentialProperty) {
    Parser parser(R"(
        namespace test;
        class Node { Node next; }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 1u);
    EXPECT_EQ(c.properties[0].name, "next");
    EXPECT_EQ(get_user_type(c.properties[0].type), "Node");
    EXPECT_EQ(c.constructors.size(), 0u);
}

// The self-typed property must parse whether or not 'readonly' is present; the
// readonly path already worked, the plain path did not.
TEST(ParserTest, ClassSelfReferentialReadonlyAndPlainAgree) {
    Parser plain(R"(namespace test; class Node { Node parent; })");
    auto ast1 = plain.parse();
    EXPECT_EQ(ast1.namespaces[0].classes[0].properties.size(), 1u);

    Parser ro(R"(namespace test; class Node { readonly Node parent; })");
    auto ast2 = ro.parse();
    ASSERT_EQ(ast2.namespaces[0].classes[0].properties.size(), 1u);
    EXPECT_TRUE(ast2.namespaces[0].classes[0].properties[0].readonly);
}

TEST(ParserTest, ClassSelfReferentialArrayProperty) {
    Parser parser(R"(
        namespace test;
        class Tree { Tree[] children; }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.properties.size(), 1u);
    EXPECT_EQ(c.properties[0].name, "children");
    EXPECT_EQ(get_user_type(c.properties[0].type), "Tree");
    EXPECT_TRUE(c.properties[0].type.is_array);
}

// Regression guard: a genuine constructor still parses correctly after unifying
// the identifier-led dispatch.
TEST(ParserTest, ClassConstructorStillParses) {
    Parser parser(R"(
        namespace test;
        class Node { Node(i32 v); }
    )");
    auto ast = parser.parse();
    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.constructors.size(), 1u);
    EXPECT_EQ(c.constructors[0].params.size(), 1u);
    EXPECT_EQ(c.properties.size(), 0u);
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


// =============================================================================
// Class-body Method Modifiers (stream / optional)
// =============================================================================

// Class-body dispatch (parser.cpp KwStream/KwOptional branch) routes
// modifier-prefixed members to parse_method just like the service path; only the
// service side was previously tested (ServiceWithStreamMethod/OptionalMethod).
TEST(ParserTest, ClassStreamMethod) {
    Parser parser(R"(
        namespace test;
        class Feed {
            stream tick() -> i32;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.methods.size(), 1u);
    EXPECT_EQ(c.methods[0].name, "tick");
    EXPECT_TRUE(c.methods[0].is_stream);
    EXPECT_FALSE(c.methods[0].is_optional);
}

TEST(ParserTest, ClassOptionalMethod) {
    Parser parser(R"(
        namespace test;
        class Repo {
            optional find(string id) -> Record;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.methods.size(), 1u);
    EXPECT_EQ(c.methods[0].name, "find");
    EXPECT_TRUE(c.methods[0].is_optional);
    EXPECT_FALSE(c.methods[0].is_stream);
}

// A class-body modifier method and a throws clause must coexist.
TEST(ParserTest, ClassStreamMethodWithThrows) {
    Parser parser(R"(
        namespace test;
        class Repo {
            stream items() -> Record throws IOError;
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.methods.size(), 1u);
    EXPECT_TRUE(c.methods[0].is_stream);
    ASSERT_EQ(c.methods[0].throws.size(), 1u);
    EXPECT_EQ(c.methods[0].throws[0], "IOError");
}

// =============================================================================
// Type Modifier Ordering (rejected grammars)
// =============================================================================

// Only T[]? is legal (that accepted form is covered by OptionalArrayType). The
// reversed optional-before-array ordering must surface as a clean ParserError
// rather than crashing: after '?' is consumed the '[' is left where a field name
// is expected.
TEST(ParserTest, StructOptionalBeforeArrayRejected) {
    Parser parser("namespace test; struct S { string?[] a; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

// A double optional marker ('??') is rejected: the second '?' remains where a
// field name is expected.
TEST(ParserTest, StructDoubleOptionalRejected) {
    Parser parser("namespace test; struct S { string?? a; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

// Struct fields do NOT accept C-style trailing name-side brackets (contrast with
// the class user-type property path which does accept them).
TEST(ParserTest, StructCStyleArrayFieldRejected) {
    Parser parser("namespace test; struct S { i32 a[]; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

// =============================================================================
// Empty and Degenerate Bodies
// =============================================================================

// Every definition kind accepts an empty body; each parse loop guards on RBrace
// before reading any member.
TEST(ParserTest, EmptyDefinitionBodies) {
    Parser parser(R"(
        namespace test;
        struct S {}
        enum E {}
        class C {}
        service Sv {}
        error Er {}
    )");
    auto ast = parser.parse();

    const auto& ns = ast.namespaces[0];
    ASSERT_EQ(ns.structs.size(), 1u);
    EXPECT_TRUE(ns.structs[0].fields.empty());
    ASSERT_EQ(ns.enums.size(), 1u);
    EXPECT_TRUE(ns.enums[0].items.empty());
    ASSERT_EQ(ns.classes.size(), 1u);
    EXPECT_TRUE(ns.classes[0].properties.empty());
    EXPECT_TRUE(ns.classes[0].constructors.empty());
    EXPECT_TRUE(ns.classes[0].methods.empty());
    ASSERT_EQ(ns.services.size(), 1u);
    EXPECT_TRUE(ns.services[0].methods.empty());
    ASSERT_EQ(ns.errors.size(), 1u);
    EXPECT_TRUE(ns.errors[0].fields.empty());
}

// A constructor with an empty parameter list parses to zero params.
TEST(ParserTest, ClassEmptyConstructor) {
    Parser parser(R"(
        namespace test;
        class C {
            C();
        }
    )");
    auto ast = parser.parse();

    const auto& c = ast.namespaces[0].classes[0];
    ASSERT_EQ(c.constructors.size(), 1u);
    EXPECT_TRUE(c.constructors[0].params.empty());
}

// A 'throws' keyword with no following error type is a ParserError.
TEST(ParserTest, ServiceThrowsWithNoErrorTypeRejected) {
    Parser parser("namespace test; service S { save(Record r) -> void throws; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

// A trailing comma in a throws list is a ParserError.
TEST(ParserTest, ServiceThrowsTrailingCommaRejected) {
    Parser parser("namespace test; service S { save(Record r) -> void throws A, ; }");
    EXPECT_THROW(parser.parse(), ParserError);
}

// =============================================================================
// Top-level Error and EOF Paths
// =============================================================================

// Empty input hits the EOF branch of parse_namespace (distinct from the
// leading-non-namespace-token case already covered by MissingNamespace).
TEST(ParserTest, EmptyInputThrows) {
    Parser parser("");
    EXPECT_THROW(parser.parse(), ParserError);
}

// A file containing only a (non-doc) comment lexes to EOF with no namespace.
TEST(ParserTest, CommentOnlyInputThrows) {
    Parser parser("// just a comment\n");
    EXPECT_THROW(parser.parse(), ParserError);
}

// Only a single leading namespace is supported; a second 'namespace' token in
// the definition stream is rejected as an unexpected definition (documents the
// single-namespace-per-file limitation despite AST::namespaces being a vector).
TEST(ParserTest, SecondNamespaceRejected) {
    Parser parser("namespace a; struct S { i32 x; } namespace b;");
    EXPECT_THROW(parser.parse(), ParserError);
}

// Negative enum discriminants are unsupported: the '-' is unlexable and surfaces
// as a LexerError (not a ParserError).
TEST(ParserTest, NegativeEnumValueThrowsLexerError) {
    Parser parser("namespace test; enum E { neg = -1 }");
    EXPECT_THROW(parser.parse(), LexerError);
}

// A dangling doc comment at EOF is silently dropped: the file parses to a single
// empty namespace rather than crashing.
TEST(ParserTest, DanglingDocAtEofParses) {
    Parser parser("namespace test; /// dangling");
    auto ast = parser.parse();

    ASSERT_EQ(ast.namespaces.size(), 1u);
    EXPECT_TRUE(ast.namespaces[0].structs.empty());
}
