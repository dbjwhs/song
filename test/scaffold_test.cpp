// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include "parser.hpp"
#include "resolver.hpp"
#include "scaffold.hpp"

using namespace song;
using namespace song::compiler;

// Helper to parse and resolve IDL
static AST parse_idl(const std::string& source) {
    Parser parser(source);
    auto ast = parser.parse();

    Resolver resolver(ast);
    EXPECT_TRUE(resolver.resolve());

    return ast;
}

// =============================================================================
// Skeleton Generation (new file)
// =============================================================================

TEST(ScaffoldTest, GenerateSkeletonBasic) {
    auto ast = parse_idl(R"(
        namespace calculator;
        service Calculator {
            add(i32 a, i32 b) -> i32;
        }
    )");

    ScaffoldGenerator scaffold;
    std::string code = scaffold.generate_skeleton(ast.namespaces.front(),
                                                   ast.namespaces.front().services.front());

    // Should have implementation class
    EXPECT_NE(code.find("class CalculatorImpl : public ICalculator"), std::string::npos);

    // Should have method with override
    EXPECT_NE(code.find("i32 add(i32 a, i32 b) override"), std::string::npos);

    // Should have TODO comment
    EXPECT_NE(code.find("// TODO: implement"), std::string::npos);

    // Should have runtime error stub
    EXPECT_NE(code.find("throw std::runtime_error"), std::string::npos);

    // Should have correct namespace
    EXPECT_NE(code.find("namespace song::calculator"), std::string::npos);

    // Should include the header
    EXPECT_NE(code.find("#include \"calculator.hpp\""), std::string::npos);
}

TEST(ScaffoldTest, GenerateSkeletonMultipleMethods) {
    auto ast = parse_idl(R"(
        namespace math;
        service Math {
            add(i32 a, i32 b) -> i32;
            subtract(i32 a, i32 b) -> i32;
            multiply(i32 a, i32 b) -> i32;
        }
    )");

    ScaffoldGenerator scaffold;
    std::string code = scaffold.generate_skeleton(ast.namespaces.front(),
                                                   ast.namespaces.front().services.front());

    EXPECT_NE(code.find("i32 add("), std::string::npos);
    EXPECT_NE(code.find("i32 subtract("), std::string::npos);
    EXPECT_NE(code.find("i32 multiply("), std::string::npos);
}

TEST(ScaffoldTest, GenerateSkeletonWithDocComments) {
    auto ast = parse_idl(R"(
        namespace test;
        service Test {
            /// Does something important
            do_something() -> void;
        }
    )");

    ScaffoldGenerator scaffold;
    std::string code = scaffold.generate_skeleton(ast.namespaces.front(),
                                                   ast.namespaces.front().services.front());

    // Doc comment should be preserved
    EXPECT_NE(code.find("/// Does something important"), std::string::npos);
}

TEST(ScaffoldTest, GenerateSkeletonWithComplexTypes) {
    auto ast = parse_idl(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
        }

        service Canvas {
            set_point(Point p) -> void;
            get_point() -> Point;
        }
    )");

    ScaffoldGenerator scaffold;
    std::string code = scaffold.generate_skeleton(ast.namespaces.front(),
                                                   ast.namespaces.front().services.front());

    // Should have const ref for struct param
    EXPECT_NE(code.find("const Point& p"), std::string::npos);

    // Should have Point return type
    EXPECT_NE(code.find("Point get_point("), std::string::npos);
}

// =============================================================================
// Parse Existing Implementation
// =============================================================================

TEST(ScaffoldTest, ParseExistingSimple) {
    std::string content = R"(
class FooImpl : public IFoo {
public:
    i32 bar(i32 x) override {
        return x * 2;
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "IFoo");

    ASSERT_EQ(methods.size(), 1u);
    EXPECT_EQ(methods[0].name, "bar");
    EXPECT_EQ(methods[0].return_type, "i32");
    EXPECT_EQ(methods[0].params, "i32 x");
}

TEST(ScaffoldTest, ParseExistingMultipleMethods) {
    std::string content = R"(
class CalcImpl : public ICalc {
public:
    i32 add(i32 a, i32 b) override {
        return a + b;
    }

    i32 subtract(i32 a, i32 b) override {
        return a - b;
    }

    void reset() override {
        // nothing
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "ICalc");

    ASSERT_EQ(methods.size(), 3u);
    EXPECT_EQ(methods[0].name, "add");
    EXPECT_EQ(methods[1].name, "subtract");
    EXPECT_EQ(methods[2].name, "reset");
}

TEST(ScaffoldTest, ParseExistingIgnoresDocComments) {
    std::string content = R"(
class FooImpl : public IFoo {
public:
    /// This is a doc comment
    i32 method1(i32 x) override {
        return x;
    }

    // Regular comment
    void method2() override {
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "IFoo");

    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0].name, "method1");
    EXPECT_EQ(methods[0].return_type, "i32");
    EXPECT_EQ(methods[1].name, "method2");
    EXPECT_EQ(methods[1].return_type, "void");
}

TEST(ScaffoldTest, ParseExistingWithComplexTypes) {
    std::string content = R"(
class FooImpl : public IFoo {
public:
    std::vector<i32> get_values() override {
        return {};
    }

    void set_point(const Point& p) override {
    }

    std::optional<i32> find(const std::string& key) override {
        return std::nullopt;
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "IFoo");

    ASSERT_EQ(methods.size(), 3u);
    EXPECT_EQ(methods[0].name, "get_values");
    EXPECT_NE(methods[0].return_type.find("vector"), std::string::npos);
    EXPECT_EQ(methods[1].name, "set_point");
    EXPECT_EQ(methods[2].name, "find");
}

TEST(ScaffoldTest, ParseExistingTracksLineNumbers) {
    std::string content = R"(line1
line2
line3
    i32 method_on_line4(i32 x) override {
line5
line6
    void method_on_line7() override {
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "IFoo");

    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0].line_number, 4);
    EXPECT_EQ(methods[1].line_number, 7);
}

// =============================================================================
// Compare IDL with Existing
// =============================================================================

TEST(ScaffoldTest, CompareDetectsNewMethods) {
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            existing_method() -> void;
            new_method() -> i32;
        }
    )");

    std::string existing = R"(
class FooImpl : public IFoo {
public:
    void existing_method() override {}
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(existing, "IFoo");
    auto report = scaffold.compare(ast.namespaces.front().services.front(), methods);

    EXPECT_EQ(report.new_methods.size(), 1u);
    EXPECT_EQ(report.new_methods[0]->name, "new_method");
    EXPECT_TRUE(report.removed_methods.empty());
    EXPECT_TRUE(report.modified_methods.empty());
}

TEST(ScaffoldTest, CompareDetectsRemovedMethods) {
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            method_a() -> void;
        }
    )");

    std::string existing = R"(
class FooImpl : public IFoo {
public:
    void method_a() override {}
    void old_method() override {}
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(existing, "IFoo");
    auto report = scaffold.compare(ast.namespaces.front().services.front(), methods);

    EXPECT_TRUE(report.new_methods.empty());
    EXPECT_EQ(report.removed_methods.size(), 1u);
    EXPECT_EQ(report.removed_methods[0].name, "old_method");
    EXPECT_TRUE(report.modified_methods.empty());
}

TEST(ScaffoldTest, CompareDetectsModifiedSignature) {
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            compute(i32 a, i32 b) -> i64;
        }
    )");

    std::string existing = R"(
class FooImpl : public IFoo {
public:
    i32 compute(i32 a) override { return a; }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(existing, "IFoo");
    auto report = scaffold.compare(ast.namespaces.front().services.front(), methods);

    EXPECT_TRUE(report.new_methods.empty());
    EXPECT_TRUE(report.removed_methods.empty());
    EXPECT_EQ(report.modified_methods.size(), 1u);
    EXPECT_EQ(report.modified_methods[0].first.name, "compute");
}

TEST(ScaffoldTest, CompareNoChanges) {
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            add(i32 a, i32 b) -> i32;
        }
    )");

    std::string existing = R"(
class FooImpl : public IFoo {
public:
    i32 add(i32 a, i32 b) override { return a + b; }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(existing, "IFoo");
    auto report = scaffold.compare(ast.namespaces.front().services.front(), methods);

    EXPECT_FALSE(report.has_changes());
}

// =============================================================================
// Sync Report Generation
// =============================================================================

TEST(ScaffoldTest, SyncReportNewMethods) {
    auto ast = parse_idl(R"(
        namespace calc;
        service Calculator {
            add(i32 a, i32 b) -> i32;
            multiply(i32 a, i32 b) -> i32;
        }
    )");

    SyncReport report;
    report.new_methods.push_back(&ast.namespaces.front().services.front().methods[1]);

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("NEW METHODS"), std::string::npos);
    EXPECT_NE(output.find("multiply"), std::string::npos);
    EXPECT_NE(output.find("i32 a, i32 b"), std::string::npos);
    EXPECT_NE(output.find("override"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportRemovedMethods) {
    auto ast = parse_idl(R"(
        namespace test;
        service Test {
            method_a() -> void;
        }
    )");

    SyncReport report;
    ExistingMethod removed;
    removed.name = "old_method";
    removed.return_type = "i32";
    removed.params = "i32 x";
    removed.line_number = 42;
    report.removed_methods.push_back(removed);

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("REMOVED FROM IDL"), std::string::npos);
    EXPECT_NE(output.find("old_method"), std::string::npos);
    EXPECT_NE(output.find("Line 42"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportModifiedMethods) {
    auto ast = parse_idl(R"(
        namespace test;
        service Test {
            compute(i32 a, i32 b) -> i64;
        }
    )");

    SyncReport report;
    ExistingMethod existing;
    existing.name = "compute";
    existing.return_type = "i32";
    existing.params = "i32 a";
    existing.line_number = 15;
    report.modified_methods.push_back({existing, &ast.namespaces.front().services.front().methods[0]});

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("SIGNATURE CHANGED"), std::string::npos);
    EXPECT_NE(output.find("WAS:"), std::string::npos);
    EXPECT_NE(output.find("NOW:"), std::string::npos);
    EXPECT_NE(output.find("Line 15"), std::string::npos);
}

// =============================================================================
// Main Entry Point (generate)
// =============================================================================

TEST(ScaffoldTest, GenerateNewFile) {
    auto ast = parse_idl(R"(
        namespace test;
        service Test {
            foo() -> void;
        }
    )");

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate(ast.namespaces.front(),
                                           ast.namespaces.front().services.front(),
                                           std::nullopt);

    // Should be a full skeleton
    EXPECT_NE(output.find("class TestImpl"), std::string::npos);
    EXPECT_NE(output.find("void foo(") , std::string::npos);
}

TEST(ScaffoldTest, GenerateExistingUpToDate) {
    auto ast = parse_idl(R"(
        namespace test;
        service Test {
            foo() -> void;
        }
    )");

    std::string existing = R"(
class TestImpl : public ITest {
public:
    void foo() override {}
};
)";

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate(ast.namespaces.front(),
                                           ast.namespaces.front().services.front(),
                                           existing);

    // Should preserve original and add "up to date" note
    EXPECT_NE(output.find("class TestImpl"), std::string::npos);
    EXPECT_NE(output.find("all methods up to date"), std::string::npos);
}

TEST(ScaffoldTest, GenerateExistingWithChanges) {
    auto ast = parse_idl(R"(
        namespace test;
        service Test {
            foo() -> void;
            bar() -> i32;
        }
    )");

    std::string existing = R"(
class TestImpl : public ITest {
public:
    void foo() override {}
};
)";

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate(ast.namespaces.front(),
                                           ast.namespaces.front().services.front(),
                                           existing);

    // Should preserve original
    EXPECT_NE(output.find("class TestImpl"), std::string::npos);
    EXPECT_NE(output.find("void foo()"), std::string::npos);

    // Should have sync report with new method
    EXPECT_NE(output.find("SCAFFOLD SYNC REPORT"), std::string::npos);
    EXPECT_NE(output.find("NEW METHODS"), std::string::npos);
    EXPECT_NE(output.find("bar"), std::string::npos);
}

// =============================================================================
// Struct Parsing
// =============================================================================

TEST(ScaffoldTest, ParseExistingStructsSimple) {
    std::string header = R"(
struct Point {
    i32 x;
    i32 y;
};
)";

    ScaffoldGenerator scaffold;
    auto structs = scaffold.parse_existing_structs(header);

    ASSERT_EQ(structs.size(), 1u);
    EXPECT_EQ(structs[0].name, "Point");
    ASSERT_EQ(structs[0].fields.size(), 2u);
    EXPECT_EQ(structs[0].fields[0].name, "x");
    EXPECT_EQ(structs[0].fields[1].name, "y");
}

TEST(ScaffoldTest, ParseExistingStructsMultiple) {
    std::string header = R"(
struct Point {
    i32 x;
    i32 y;
};

struct Color {
    u8 r;
    u8 g;
    u8 b;
};
)";

    ScaffoldGenerator scaffold;
    auto structs = scaffold.parse_existing_structs(header);

    ASSERT_EQ(structs.size(), 2u);
    EXPECT_EQ(structs[0].name, "Point");
    EXPECT_EQ(structs[1].name, "Color");
    EXPECT_EQ(structs[1].fields.size(), 3u);
}

TEST(ScaffoldTest, ParseExistingStructsComplexTypes) {
    std::string header = R"(
struct Container {
    std::vector<i32> values;
    std::optional<std::string> name;
};
)";

    ScaffoldGenerator scaffold;
    auto structs = scaffold.parse_existing_structs(header);

    ASSERT_EQ(structs.size(), 1u);
    EXPECT_EQ(structs[0].name, "Container");
    ASSERT_EQ(structs[0].fields.size(), 2u);
    EXPECT_EQ(structs[0].fields[0].name, "values");
    EXPECT_NE(structs[0].fields[0].type.find("vector"), std::string::npos);
}

// =============================================================================
// Enum Parsing
// =============================================================================

TEST(ScaffoldTest, ParseExistingEnumsSimple) {
    std::string header = R"(
enum class Status : i32 {
    idle,
    running,
    stopped
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    ASSERT_EQ(enums.size(), 1u);
    EXPECT_EQ(enums[0].name, "Status");
    ASSERT_EQ(enums[0].values.size(), 3u);
    EXPECT_EQ(enums[0].values[0].name, "idle");
    EXPECT_EQ(enums[0].values[1].name, "running");
    EXPECT_EQ(enums[0].values[2].name, "stopped");
}

TEST(ScaffoldTest, ParseExistingEnumsWithValues) {
    std::string header = R"(
enum class Permissions : u32 {
    read = 1,
    write = 2,
    execute = 4
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    ASSERT_EQ(enums.size(), 1u);
    EXPECT_EQ(enums[0].name, "Permissions");
    ASSERT_EQ(enums[0].values.size(), 3u);
    EXPECT_EQ(enums[0].values[0].name, "read");
    EXPECT_TRUE(enums[0].values[0].value.has_value());
    EXPECT_EQ(enums[0].values[0].value.value(), 1);
    EXPECT_TRUE(enums[0].is_flags);  // Powers of 2 detected
}

// Regression: a hand-edited header whose enum value exceeds int64_t must be
// tolerated (the value is left unparsed) rather than crashing the scaffold
// generator with an uncaught std::out_of_range from std::stoll.
TEST(ScaffoldTest, ParseExistingEnumsToleratesOutOfRangeValue) {
    std::string header = R"(
enum class Big : u64 {
    small = 1,
    huge = 99999999999999999999
};
)";

    ScaffoldGenerator scaffold;
    ASSERT_NO_THROW({ scaffold.parse_existing_enums(header); });

    auto enums = scaffold.parse_existing_enums(header);
    ASSERT_EQ(enums.size(), 1u);
    ASSERT_EQ(enums[0].values.size(), 2u);

    EXPECT_EQ(enums[0].values[0].name, "small");
    EXPECT_TRUE(enums[0].values[0].value.has_value());
    EXPECT_EQ(enums[0].values[0].value.value(), 1);

    // The out-of-range value is retained by name but its value is left absent.
    EXPECT_EQ(enums[0].values[1].name, "huge");
    EXPECT_FALSE(enums[0].values[1].value.has_value());
}

// =============================================================================
// Struct Comparison
// =============================================================================

TEST(ScaffoldTest, CompareStructsDetectsNew) {
    auto ast = parse_idl(R"(
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

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
struct Point {
    i32 x;
    i32 y;
};
)";

    ScaffoldGenerator scaffold;
    auto existing_structs = scaffold.parse_existing_structs(existing_header);
    SyncReport report;
    scaffold.compare_structs(ast.namespaces.front(), existing_structs, report);

    EXPECT_EQ(report.new_structs.size(), 1u);
    EXPECT_EQ(report.new_structs[0]->name, "Color");
    EXPECT_TRUE(report.removed_structs.empty());
    EXPECT_TRUE(report.modified_structs.empty());
}

TEST(ScaffoldTest, CompareStructsDetectsRemoved) {
    auto ast = parse_idl(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
struct Point {
    i32 x;
    i32 y;
};

struct OldStruct {
    i32 value;
};
)";

    ScaffoldGenerator scaffold;
    auto existing_structs = scaffold.parse_existing_structs(existing_header);
    SyncReport report;
    scaffold.compare_structs(ast.namespaces.front(), existing_structs, report);

    EXPECT_TRUE(report.new_structs.empty());
    EXPECT_EQ(report.removed_structs.size(), 1u);
    EXPECT_EQ(report.removed_structs[0].name, "OldStruct");
    EXPECT_TRUE(report.modified_structs.empty());
}

TEST(ScaffoldTest, CompareStructsDetectsModified) {
    auto ast = parse_idl(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
            i32 z;
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
struct Point {
    i32 x;
    i32 y;
};
)";

    ScaffoldGenerator scaffold;
    auto existing_structs = scaffold.parse_existing_structs(existing_header);
    SyncReport report;
    scaffold.compare_structs(ast.namespaces.front(), existing_structs, report);

    EXPECT_TRUE(report.new_structs.empty());
    EXPECT_TRUE(report.removed_structs.empty());
    EXPECT_EQ(report.modified_structs.size(), 1u);
    EXPECT_EQ(report.modified_structs[0].first.name, "Point");
}

// =============================================================================
// Enum Comparison
// =============================================================================

TEST(ScaffoldTest, CompareEnumsDetectsNew) {
    auto ast = parse_idl(R"(
        namespace test;

        enum Status {
            idle,
            running
        }

        enum Priority {
            low,
            high
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
enum class Status : i32 {
    idle,
    running
};
)";

    ScaffoldGenerator scaffold;
    auto existing_enums = scaffold.parse_existing_enums(existing_header);
    SyncReport report;
    scaffold.compare_enums(ast.namespaces.front(), existing_enums, report);

    EXPECT_EQ(report.new_enums.size(), 1u);
    EXPECT_EQ(report.new_enums[0]->name, "Priority");
    EXPECT_TRUE(report.removed_enums.empty());
    EXPECT_TRUE(report.modified_enums.empty());
}

TEST(ScaffoldTest, CompareEnumsDetectsRemoved) {
    auto ast = parse_idl(R"(
        namespace test;

        enum Status {
            idle,
            running
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
enum class Status : i32 {
    idle,
    running
};

enum class OldEnum : i32 {
    value1,
    value2
};
)";

    ScaffoldGenerator scaffold;
    auto existing_enums = scaffold.parse_existing_enums(existing_header);
    SyncReport report;
    scaffold.compare_enums(ast.namespaces.front(), existing_enums, report);

    EXPECT_TRUE(report.new_enums.empty());
    EXPECT_EQ(report.removed_enums.size(), 1u);
    EXPECT_EQ(report.removed_enums[0].name, "OldEnum");
    EXPECT_TRUE(report.modified_enums.empty());
}

TEST(ScaffoldTest, CompareEnumsDetectsModified) {
    auto ast = parse_idl(R"(
        namespace test;

        enum Status {
            idle,
            running,
            stopped
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
enum class Status : i32 {
    idle,
    running
};
)";

    ScaffoldGenerator scaffold;
    auto existing_enums = scaffold.parse_existing_enums(existing_header);
    SyncReport report;
    scaffold.compare_enums(ast.namespaces.front(), existing_enums, report);

    EXPECT_TRUE(report.new_enums.empty());
    EXPECT_TRUE(report.removed_enums.empty());
    EXPECT_EQ(report.modified_enums.size(), 1u);
    EXPECT_EQ(report.modified_enums[0].first.name, "Status");
}

// =============================================================================
// Sync Report for Structs/Enums
// =============================================================================

TEST(ScaffoldTest, SyncReportNewStructs) {
    auto ast = parse_idl(R"(
        namespace test;

        struct NewStruct {
            i32 x;
            i32 y;
        }

        service Foo {
            foo() -> void;
        }
    )");

    SyncReport report;
    report.new_structs.push_back(&ast.namespaces.front().structs[0]);

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("NEW STRUCTS"), std::string::npos);
    EXPECT_NE(output.find("NewStruct"), std::string::npos);
    EXPECT_NE(output.find("i32 x"), std::string::npos);
    EXPECT_NE(output.find("i32 y"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportRemovedStructs) {
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            foo() -> void;
        }
    )");

    SyncReport report;
    ExistingStruct removed;
    removed.name = "OldStruct";
    removed.fields = {{"value", "i32"}};
    report.removed_structs.push_back(removed);

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("STRUCTS REMOVED FROM IDL"), std::string::npos);
    EXPECT_NE(output.find("OldStruct"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportModifiedStructs) {
    auto ast = parse_idl(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
            i32 z;
        }

        service Foo {
            foo() -> void;
        }
    )");

    SyncReport report;
    ExistingStruct existing;
    existing.name = "Point";
    existing.fields = {{"x", "i32"}, {"y", "i32"}};
    report.modified_structs.push_back({existing, &ast.namespaces.front().structs[0]});

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("STRUCT FIELDS CHANGED"), std::string::npos);
    EXPECT_NE(output.find("Point"), std::string::npos);
    EXPECT_NE(output.find("WAS:"), std::string::npos);
    EXPECT_NE(output.find("NOW:"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportNewEnums) {
    auto ast = parse_idl(R"(
        namespace test;

        enum Status {
            idle,
            running
        }

        service Foo {
            foo() -> void;
        }
    )");

    SyncReport report;
    report.new_enums.push_back(&ast.namespaces.front().enums[0]);

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("NEW ENUMS"), std::string::npos);
    EXPECT_NE(output.find("Status"), std::string::npos);
    EXPECT_NE(output.find("idle"), std::string::npos);
    EXPECT_NE(output.find("running"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportRemovedEnums) {
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            foo() -> void;
        }
    )");

    SyncReport report;
    ExistingEnum removed;
    removed.name = "OldEnum";
    removed.is_flags = false;
    removed.values = {{"value1", std::nullopt}, {"value2", std::nullopt}};
    report.removed_enums.push_back(removed);

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("ENUMS REMOVED FROM IDL"), std::string::npos);
    EXPECT_NE(output.find("OldEnum"), std::string::npos);
}

TEST(ScaffoldTest, SyncReportModifiedEnums) {
    auto ast = parse_idl(R"(
        namespace test;

        enum Status {
            idle,
            running,
            stopped
        }

        service Foo {
            foo() -> void;
        }
    )");

    SyncReport report;
    ExistingEnum existing;
    existing.name = "Status";
    existing.is_flags = false;
    existing.values = {{"idle", std::nullopt}, {"running", std::nullopt}};
    report.modified_enums.push_back({existing, &ast.namespaces.front().enums[0]});

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate_sync_report(ast.namespaces.front(),
                                                        ast.namespaces.front().services.front(),
                                                        report);

    EXPECT_NE(output.find("ENUM VALUES CHANGED"), std::string::npos);
    EXPECT_NE(output.find("Status"), std::string::npos);
    EXPECT_NE(output.find("WAS:"), std::string::npos);
    EXPECT_NE(output.find("NOW:"), std::string::npos);
}

// =============================================================================
// Full Generate with Structs/Enums
// =============================================================================

TEST(ScaffoldTest, GenerateWithStructChanges) {
    auto ast = parse_idl(R"(
        namespace test;

        struct Point {
            i32 x;
            i32 y;
            i32 z;
        }

        struct NewStruct {
            string name;
        }

        service Test {
            foo() -> void;
        }
    )");

    std::string existing_impl = R"(
class TestImpl : public ITest {
public:
    void foo() override {}
};
)";

    std::string existing_header = R"(
struct Point {
    i32 x;
    i32 y;
};

struct OldStruct {
    i32 value;
};
)";

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate(ast.namespaces.front(),
                                           ast.namespaces.front().services.front(),
                                           existing_impl,
                                           existing_header);

    // Should have struct changes in sync report
    EXPECT_NE(output.find("SCAFFOLD SYNC REPORT"), std::string::npos);
    EXPECT_NE(output.find("NEW STRUCTS"), std::string::npos);
    EXPECT_NE(output.find("NewStruct"), std::string::npos);
    EXPECT_NE(output.find("STRUCTS REMOVED FROM IDL"), std::string::npos);
    EXPECT_NE(output.find("OldStruct"), std::string::npos);
    EXPECT_NE(output.find("STRUCT FIELDS CHANGED"), std::string::npos);
    EXPECT_NE(output.find("Point"), std::string::npos);
}

TEST(ScaffoldTest, GenerateWithEnumChanges) {
    auto ast = parse_idl(R"(
        namespace test;

        enum Status {
            idle,
            running,
            stopped
        }

        enum NewEnum {
            low,
            high
        }

        service Test {
            foo() -> void;
        }
    )");

    std::string existing_impl = R"(
class TestImpl : public ITest {
public:
    void foo() override {}
};
)";

    std::string existing_header = R"(
enum class Status : i32 {
    idle,
    running
};

enum class OldEnum : i32 {
    value1,
    value2
};
)";

    ScaffoldGenerator scaffold;
    std::string output = scaffold.generate(ast.namespaces.front(),
                                           ast.namespaces.front().services.front(),
                                           existing_impl,
                                           existing_header);

    // Should have enum changes in sync report
    EXPECT_NE(output.find("SCAFFOLD SYNC REPORT"), std::string::npos);
    EXPECT_NE(output.find("NEW ENUMS"), std::string::npos);
    EXPECT_NE(output.find("NewEnum"), std::string::npos);
    EXPECT_NE(output.find("ENUMS REMOVED FROM IDL"), std::string::npos);
    EXPECT_NE(output.find("OldEnum"), std::string::npos);
    EXPECT_NE(output.find("ENUM VALUES CHANGED"), std::string::npos);
    EXPECT_NE(output.find("Status"), std::string::npos);
}


// =============================================================================
// Struct Field Default Initializers (characterization)
// =============================================================================
//
// The struct body/field regexes in parse_existing_structs do not understand C++
// default member initializers. These tests pin the CURRENT (buggy) behavior so a
// future fix is a deliberate, visible change rather than an accidental one.
// Gap: scaffold-struct-field-default-initializer.

TEST(ScaffoldTest, ParseExistingStructsDefaultInitializerMisparsesFieldName) {
    // "i32 timeout = 30;" - the field regex binds the field name to the trailing
    // token, so the name becomes the literal "30" and the type absorbs "= ".
    std::string header = R"(
struct Config {
    i32 timeout = 30;
    i32 retries;
};
)";

    ScaffoldGenerator scaffold;
    auto structs = scaffold.parse_existing_structs(header);

    ASSERT_EQ(structs.size(), 1u);
    EXPECT_EQ(structs[0].name, "Config");
    ASSERT_EQ(structs[0].fields.size(), 2u);

    // The default-initialized field is misparsed: name is the literal, not the
    // identifier, and the type keeps the trailing "=".
    EXPECT_EQ(structs[0].fields[0].name, "30");
    EXPECT_EQ(structs[0].fields[0].type, "i32 timeout =");

    // A plain field with no initializer still parses correctly.
    EXPECT_EQ(structs[0].fields[1].name, "retries");
    EXPECT_EQ(structs[0].fields[1].type, "i32");
}

TEST(ScaffoldTest, ParseExistingStructsBraceInitializerTruncatesBody) {
    // A "{}" default initializer contains a '}' that terminates the struct-body
    // regex early, so every field (including the trailing one) is lost.
    std::string header = R"(
struct Foo {
    std::vector<i32> v = {};
    i32 x;
};
)";

    ScaffoldGenerator scaffold;
    auto structs = scaffold.parse_existing_structs(header);

    ASSERT_EQ(structs.size(), 1u);
    EXPECT_EQ(structs[0].name, "Foo");
    // Both fields are dropped because the '{}' truncates the parsed body.
    EXPECT_TRUE(structs[0].fields.empty());
}

TEST(ScaffoldTest, CompareStructsDefaultInitializerReportsSpuriousModification) {
    // The IDL struct matches the header struct field-for-field, but the header's
    // default initializer corrupts the parsed fields, so compare_structs reports
    // a spurious modification.
    auto ast = parse_idl(R"(
        namespace test;

        struct Config {
            i32 timeout;
            i32 retries;
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
struct Config {
    i32 timeout = 30;
    i32 retries;
};
)";

    ScaffoldGenerator scaffold;
    auto existing_structs = scaffold.parse_existing_structs(existing_header);
    SyncReport report;
    scaffold.compare_structs(ast.namespaces.front(), existing_structs, report);

    EXPECT_TRUE(report.new_structs.empty());
    EXPECT_TRUE(report.removed_structs.empty());
    // Spurious: the semantically-identical struct is flagged as modified.
    ASSERT_EQ(report.modified_structs.size(), 1u);
    EXPECT_EQ(report.modified_structs[0].first.name, "Config");
}

// =============================================================================
// Comments Not Stripped from Struct/Enum Parsing (characterization)
// =============================================================================
//
// Unlike parse_existing_impl (which skips comment lines), parse_existing_structs
// and parse_existing_enums run their regexes over the raw text, so struct/enum
// keywords inside comments are parsed as real definitions.
// Gap: scaffold-comments-not-stripped.

TEST(ScaffoldTest, ParseExistingStructsDoesNotSkipCommentedStructs) {
    std::string header = R"(
// struct LegacyPoint {
//   i32 x;
// };
struct Point {
    i32 x;
};
)";

    ScaffoldGenerator scaffold;
    auto structs = scaffold.parse_existing_structs(header);

    // The commented-out struct is still matched (documents the gap).
    ASSERT_EQ(structs.size(), 2u);
    EXPECT_EQ(structs[0].name, "LegacyPoint");
    // Its body lines start with "//" so the field filter drops them.
    EXPECT_TRUE(structs[0].fields.empty());

    // The real struct parses normally.
    EXPECT_EQ(structs[1].name, "Point");
    ASSERT_EQ(structs[1].fields.size(), 1u);
    EXPECT_EQ(structs[1].fields[0].name, "x");
}

TEST(ScaffoldTest, ParseExistingEnumsDoesNotSkipBlockCommentedEnums) {
    std::string header = R"(
/* enum class Old : i32 { a, b }; */
enum class Real : i32 {
    x,
    y
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    // The enum inside the block comment is still matched (documents the gap).
    ASSERT_EQ(enums.size(), 2u);
    EXPECT_EQ(enums[0].name, "Old");
    EXPECT_EQ(enums[1].name, "Real");
}

TEST(ScaffoldTest, CompareStructsCommentedStructReportsSpuriousRemoval) {
    // The IDL only declares Point, and the header's real struct is Point, but a
    // commented-out struct is parsed as real and reported as removed-from-IDL.
    auto ast = parse_idl(R"(
        namespace test;

        struct Point {
            i32 x;
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
// struct LegacyPoint {
//   i32 x;
// };
struct Point {
    i32 x;
};
)";

    ScaffoldGenerator scaffold;
    auto existing_structs = scaffold.parse_existing_structs(existing_header);
    SyncReport report;
    scaffold.compare_structs(ast.namespaces.front(), existing_structs, report);

    EXPECT_TRUE(report.new_structs.empty());
    EXPECT_TRUE(report.modified_structs.empty());
    // Spurious: the commented-out struct is reported as removed from the IDL.
    ASSERT_EQ(report.removed_structs.size(), 1u);
    EXPECT_EQ(report.removed_structs[0].name, "LegacyPoint");
}

// =============================================================================
// Multi-Line Method Signatures (characterization)
// =============================================================================
//
// parse_existing_impl matches an anchored single-line regex per getline() line,
// so a method signature wrapped across lines matches nothing. These tests pin
// that behavior. Gap: scaffold-multiline-signature-not-parsed.

TEST(ScaffoldTest, ParseExistingImplDropsMultiLineSignature) {
    std::string content = R"(
class FooImpl : public IFoo {
public:
    i32 compute(
        i32 a,
        i32 b) override {
        return a + b;
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "IFoo");

    // The wrapped signature is not matched by the single-line regex.
    EXPECT_TRUE(methods.empty());
}

TEST(ScaffoldTest, ParseExistingImplMixedOnlyCapturesSingleLine) {
    // In a class with one single-line and one multi-line method, only the
    // single-line method is captured.
    std::string content = R"(
class FooImpl : public IFoo {
public:
    i32 one_line(i32 a) override { return a; }

    i32 wrapped(
        i32 a,
        i32 b) override {
        return a + b;
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(content, "IFoo");

    ASSERT_EQ(methods.size(), 1u);
    EXPECT_EQ(methods[0].name, "one_line");
    EXPECT_EQ(methods[0].return_type, "i32");
    EXPECT_EQ(methods[0].params, "i32 a");
}

TEST(ScaffoldTest, CompareMultiLineImplReportsSpuriousNewMethod) {
    // The method exists in the implementation but is wrapped across lines, so it
    // is invisible to the parser and compare() reports it as a new method.
    auto ast = parse_idl(R"(
        namespace test;
        service Foo {
            compute(i32 a, i32 b) -> i32;
        }
    )");

    std::string existing = R"(
class FooImpl : public IFoo {
public:
    i32 compute(
        i32 a,
        i32 b) override {
        return a + b;
    }
};
)";

    ScaffoldGenerator scaffold;
    auto methods = scaffold.parse_existing_impl(existing, "IFoo");
    auto report = scaffold.compare(ast.namespaces.front().services.front(), methods);

    EXPECT_TRUE(report.removed_methods.empty());
    EXPECT_TRUE(report.modified_methods.empty());
    // Spurious: an already-implemented method is flagged as new.
    ASSERT_EQ(report.new_methods.size(), 1u);
    EXPECT_EQ(report.new_methods[0]->name, "compute");
}

// =============================================================================
// Enum Underlying-Type Requirement (characterization)
// =============================================================================
//
// The enum regex hard-requires "enum class Name : <type>". Scoped enums without
// an explicit underlying type, and unscoped enums, are silently not parsed.
// Gap: scaffold-enum-underlying-type-required.

TEST(ScaffoldTest, ParseExistingEnumsRequiresUnderlyingType) {
    // No ": <type>" -> not matched.
    std::string header = R"(
enum class Color {
    red,
    green,
    blue
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    EXPECT_TRUE(enums.empty());
}

TEST(ScaffoldTest, ParseExistingEnumsIgnoresUnscopedEnum) {
    // "enum" without "class" -> not matched even with an underlying type.
    std::string header = R"(
enum Color : int {
    red,
    green
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    EXPECT_TRUE(enums.empty());
}

TEST(ScaffoldTest, CompareEnumsMissingUnderlyingTypeReportsSpuriousNew) {
    // The header defines Color, but without an underlying type it is not parsed,
    // so compare_enums reports the IDL enum as new.
    auto ast = parse_idl(R"(
        namespace test;

        enum Color {
            red,
            green
        }

        service Foo {
            foo() -> void;
        }
    )");

    std::string existing_header = R"(
enum class Color {
    red,
    green
};
)";

    ScaffoldGenerator scaffold;
    auto existing_enums = scaffold.parse_existing_enums(existing_header);
    SyncReport report;
    scaffold.compare_enums(ast.namespaces.front(), existing_enums, report);

    EXPECT_TRUE(report.removed_enums.empty());
    EXPECT_TRUE(report.modified_enums.empty());
    // Spurious: the already-present enum is flagged as new.
    ASSERT_EQ(report.new_enums.size(), 1u);
    EXPECT_EQ(report.new_enums[0]->name, "Color");
}

// =============================================================================
// Non-Decimal / Expression Enum Values (characterization)
// =============================================================================
//
// The enum value regex only accepts decimal digits (-?\d+), so hex literals and
// expression values are misparsed into spurious members. Gap:
// scaffold-comma-templates-and-nondecimal-enum-values.

TEST(ScaffoldTest, ParseExistingEnumsMisparsesHexValues) {
    std::string header = R"(
enum class Perm : u32 {
    read = 0x01,
    write = 0x02
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    ASSERT_EQ(enums.size(), 1u);
    EXPECT_EQ(enums[0].name, "Perm");
    // "0x01" -> the decimal regex captures only the leading "0", then "x01" is
    // matched as a separate (bogus) enum member. Same for write/0x02.
    ASSERT_EQ(enums[0].values.size(), 4u);
    EXPECT_EQ(enums[0].values[0].name, "read");
    ASSERT_TRUE(enums[0].values[0].value.has_value());
    EXPECT_EQ(enums[0].values[0].value.value(), 0);
    EXPECT_EQ(enums[0].values[1].name, "x01");
    EXPECT_FALSE(enums[0].values[1].value.has_value());
    EXPECT_EQ(enums[0].values[2].name, "write");
    EXPECT_EQ(enums[0].values[3].name, "x02");
}

TEST(ScaffoldTest, ParseExistingEnumsMisparsesExpressionValues) {
    std::string header = R"(
enum class Perm : u32 {
    read = 1,
    write = 2,
    all = read | write
};
)";

    ScaffoldGenerator scaffold;
    auto enums = scaffold.parse_existing_enums(header);

    ASSERT_EQ(enums.size(), 1u);
    ASSERT_EQ(enums[0].values.size(), 5u);
    EXPECT_EQ(enums[0].values[0].name, "read");
    EXPECT_EQ(enums[0].values[1].name, "write");
    // "all = read | write": "all" gets no numeric value, and the operands leak
    // in as spurious duplicate members.
    EXPECT_EQ(enums[0].values[2].name, "all");
    EXPECT_FALSE(enums[0].values[2].value.has_value());
    EXPECT_EQ(enums[0].values[3].name, "read");
    EXPECT_EQ(enums[0].values[4].name, "write");
}
