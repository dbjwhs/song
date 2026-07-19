// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include "parser.hpp"
#include "resolver.hpp"

using namespace song;
using namespace song::compiler;

// Helper to parse and resolve, returning success status
static bool parse_and_resolve(const std::string& source, Resolver** out_resolver = nullptr) {
    static std::unique_ptr<AST> ast;
    static std::unique_ptr<Resolver> resolver;

    Parser parser(source);
    ast = std::make_unique<AST>(parser.parse());
    resolver = std::make_unique<Resolver>(*ast);

    if (out_resolver) {
        *out_resolver = resolver.get();
    }

    return resolver->resolve();
}

// True if any reported resolver error message contains the given substring.
static bool any_error_contains(Resolver* resolver, const std::string& needle) {
    for (const auto& err : resolver->errors()) {
        if (std::string(err.what()).find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// Valid Cases
// =============================================================================

TEST(ResolverTest, EmptyNamespace) {
    EXPECT_TRUE(parse_and_resolve("namespace test;"));
}

TEST(ResolverTest, StructWithPrimitiveFields) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
    )"));
}

TEST(ResolverTest, StructReferencingStruct) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
        struct Rect {
            Point origin;
            Point size;
        }
    )"));
}

TEST(ResolverTest, StructInheritance) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
        struct Point3D : Point {
            i32 z;
        }
    )"));
}

TEST(ResolverTest, EnumWithItems) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        enum Color {
            red,
            green,
            blue
        }
    )"));
}

TEST(ResolverTest, FlagsWithHexValues) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        flags Permissions {
            read = 0x01,
            write = 0x02,
            execute = 0x04
        }
    )"));
}

TEST(ResolverTest, ClassWithProperties) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        class Canvas {
            i32 width;
            i32 height;
        }
    )"));
}

TEST(ResolverTest, ClassWithMethods) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Color {
            u8 r;
            u8 g;
            u8 b;
        }
        class Canvas {
            set_pixel(i32 x, i32 y, Color c) -> void;
            get_pixel(i32 x, i32 y) -> Color;
        }
    )"));
}

TEST(ResolverTest, ClassInheritance) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        class Base {
            i32 value;
        }
        class Derived : Base {
            i32 extra;
        }
    )"));
}

TEST(ResolverTest, ServiceWithMethods) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
            multiply(i32 a, i32 b) -> i32;
        }
    )"));
}

TEST(ResolverTest, ErrorDefinition) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        error ValidationError {
            string message;
            i32 code;
        }
    )"));
}

TEST(ResolverTest, ErrorInheritance) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        error BaseError {
            string message;
        }
        error SpecificError : BaseError {
            i32 code;
        }
    )"));
}

TEST(ResolverTest, MethodWithThrowsClause) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        error IOError {
            string message;
        }
        service FileService {
            read(string path) -> bytes throws IOError;
        }
    )"));
}

TEST(ResolverTest, ArrayTypes) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Container {
            i32[] values;
            string[] names;
        }
    )"));
}

TEST(ResolverTest, OptionalTypes) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Person {
            string name;
            string? nickname;
            i32? age;
        }
    )"));
}

TEST(ResolverTest, ComplexMultipleDefinitions) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
        }

        struct Color {
            u8 r;
            u8 g;
            u8 b;
            u8 a;
        }

        enum BlendMode {
            normal,
            multiply,
            screen
        }

        error DrawError {
            string message;
        }

        class Canvas {
            readonly i32 width;
            readonly i32 height;
            Canvas(i32 w, i32 h);
            set_pixel(Point p, Color c) -> void;
            get_pixel(Point p) -> Color;
            fill(Color c, BlendMode mode) -> void throws DrawError;
        }

        service CanvasFactory {
            create(i32 width, i32 height) -> Canvas;
        }
    )"));
}

TEST(ResolverTest, UserTypeArrays) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 y;
        }
        struct Polygon {
            Point[] vertices;
        }
    )"));
}

TEST(ResolverTest, MultipleThrows) {
    EXPECT_TRUE(parse_and_resolve(R"(
        namespace test;
        error IOError {
            string message;
        }
        error ValidationError {
            string field;
        }
        service DataService {
            save(string data) -> void throws IOError, ValidationError;
        }
    )"));
}

// =============================================================================
// Error Cases - Duplicates
// =============================================================================

TEST(ResolverTest, DuplicateStructName) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
        }
        struct Point {
            i32 y;
        }
    )", &resolver));
    EXPECT_EQ(resolver->errors().size(), 1);
}

TEST(ResolverTest, DuplicateEnumName) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        enum Status {
            ok
        }
        enum Status {
            failed
        }
    )", &resolver));
    EXPECT_EQ(resolver->errors().size(), 1);
}

TEST(ResolverTest, DuplicateFieldName) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
            i32 x;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, DuplicateMethodName) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        service Calculator {
            add(i32 a, i32 b) -> i32;
            add(i32 x, i32 y) -> i32;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, DuplicateEnumItem) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        enum Color {
            red,
            green,
            red
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

// =============================================================================
// Error Cases - Undefined Types
// =============================================================================

TEST(ResolverTest, UndefinedFieldType) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Container {
            Unknown item;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, UndefinedMethodReturnType) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        service DataService {
            fetch() -> UnknownType;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, UndefinedMethodParamType) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        service DataService {
            process(UnknownType data) -> void;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, UndefinedBaseStruct) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Derived : NonExistent {
            i32 x;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, UndefinedErrorInThrows) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        service DataService {
            save(string data) -> void throws UnknownError;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

// =============================================================================
// Error Cases - Wrong Inheritance Kind
// =============================================================================

TEST(ResolverTest, StructInheritsFromEnum) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        enum Color {
            red
        }
        struct Point : Color {
            i32 x;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, StructInheritsFromClass) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        class Base {
            i32 value;
        }
        struct Point : Base {
            i32 x;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, ClassInheritsFromStruct) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
        }
        class Canvas : Point {
            i32 width;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, ErrorInheritsFromStruct) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
        }
        error MyError : Point {
            string message;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

// =============================================================================
// Error Cases - Inheritance Cycles
// =============================================================================

TEST(ResolverTest, DirectInheritanceCycle) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct A : A {
            i32 x;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, IndirectInheritanceCycle) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct A : B {
            i32 x;
        }
        struct B : A {
            i32 y;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

TEST(ResolverTest, ThreeWayInheritanceCycle) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct A : C {
            i32 a;
        }
        struct B : A {
            i32 b;
        }
        struct C : B {
            i32 c;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

// =============================================================================
// Error Cases - Non-Error in Throws
// =============================================================================

TEST(ResolverTest, StructInThrowsClause) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Point {
            i32 x;
        }
        service DataService {
            save() -> void throws Point;
        }
    )", &resolver));
    EXPECT_GE(resolver->errors().size(), 1);
}

// =============================================================================
// Symbol Table Tests
// =============================================================================

TEST(ResolverTest, TypeLookup) {
    Parser parser(R"(
        namespace test;
        struct Point {
            i32 x;
        }
        enum Color {
            red
        }
        class Canvas {
            i32 width;
        }
    )");
    auto ast = parser.parse();
    Resolver resolver(ast);
    resolver.resolve();

    EXPECT_TRUE(resolver.type_exists("Point"));
    EXPECT_TRUE(resolver.type_exists("Color"));
    EXPECT_TRUE(resolver.type_exists("Canvas"));
    EXPECT_FALSE(resolver.type_exists("Unknown"));

    auto point_info = resolver.lookup("Point");
    ASSERT_NE(point_info, nullptr);
    EXPECT_EQ(point_info->kind, TypeKind::Struct);

    auto color_info = resolver.lookup("Color");
    ASSERT_NE(color_info, nullptr);
    EXPECT_EQ(color_info->kind, TypeKind::Enum);

    auto canvas_info = resolver.lookup("Canvas");
    ASSERT_NE(canvas_info, nullptr);
    EXPECT_EQ(canvas_info->kind, TypeKind::Class);
}

TEST(ResolverTest, ErrorHasLocation) {
    Resolver* resolver;
    parse_and_resolve(R"(
        namespace test;
        struct Point {
            Unknown x;
        }
    )", &resolver);

    ASSERT_FALSE(resolver->errors().empty());
    EXPECT_GT(resolver->errors()[0].line(), 0);
    EXPECT_GT(resolver->errors()[0].column(), 0);
}

// =============================================================================
// Class member duplicate detection
// =============================================================================

// Regression: duplicate property names in a class were not detected, unlike
// struct fields. validate_class now checks them.
TEST(ResolverTest, DuplicateClassProperty) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        class C {
            i32 x;
            i32 x;
        }
    )", &resolver));
    EXPECT_TRUE(any_error_contains(resolver, "Duplicate property"));
}

// Coverage: the class path of check_duplicate_methods (previously only exercised
// for services).
TEST(ResolverTest, DuplicateClassMethod) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        class C {
            foo() -> void;
            foo() -> void;
        }
    )", &resolver));
    EXPECT_TRUE(any_error_contains(resolver, "Duplicate method"));
}

// =============================================================================
// Enum value collisions and inherited-field shadowing
// (These validation paths existed but had no test coverage.)
// =============================================================================

// Two enum items with the same explicit value collide.
TEST(ResolverTest, DuplicateEnumValueExplicit) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        enum E { a = 1, b = 1 }
    )", &resolver));
    EXPECT_TRUE(any_error_contains(resolver, "collides"));
}

// Auto-increment interacts with explicit values: 'a' auto-assigns 0, then 'b'
// explicitly takes 0 -> collision.
TEST(ResolverTest, DuplicateEnumValueAutoIncrement) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        enum E { a, b = 0 }
    )", &resolver));
    EXPECT_TRUE(any_error_contains(resolver, "collides"));
}

// A derived struct field that shadows an inherited base field is rejected.
TEST(ResolverTest, InheritedFieldShadowingStruct) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        struct Base { i32 x; }
        struct Derived : Base { i32 x; }
    )", &resolver));
    EXPECT_TRUE(any_error_contains(resolver, "shadows inherited field"));
}

// The same detection applies to error types (the error branch of the ancestor
// walk in check_duplicate_fields).
TEST(ResolverTest, InheritedFieldShadowingError) {
    Resolver* resolver;
    EXPECT_FALSE(parse_and_resolve(R"(
        namespace test;
        error B { string m; }
        error S : B { string m; }
    )", &resolver));
    EXPECT_TRUE(any_error_contains(resolver, "shadows inherited field"));
}

