// MIT License
// Copyright (c) 2026 dbjwhs

#include "codegen.hpp"
#include <iostream>
#include <fstream>

using namespace song::compiler;

// Test: Generate code for a simple Point struct
void test_point_struct() {
    // Manually construct AST for:
    // namespace example;
    // struct Point { i32 x; i32 y; }

    Namespace ns;
    ns.name = "example";

    StructDef point;
    point.name = "Point";
    point.doc = "A 2D point";

    Field x;
    x.name = "x";
    x.type.base = PrimitiveType::i32;
    x.doc = "X coordinate";

    Field y;
    y.name = "y";
    y.type.base = PrimitiveType::i32;
    y.doc = "Y coordinate";

    point.fields.push_back(x);
    point.fields.push_back(y);
    ns.structs.push_back(point);

    // Also test a more complex struct: Rect with nested Point
    StructDef rect;
    rect.name = "Rect";
    rect.doc = "A rectangle defined by origin and size";

    Field origin;
    origin.name = "origin";
    origin.type.base = std::string("Point");
    origin.doc = "Top-left corner";

    Field size;
    size.name = "size";
    size.type.base = std::string("Point");
    size.doc = "Width and height";

    rect.fields.push_back(origin);
    rect.fields.push_back(size);
    ns.structs.push_back(rect);

    // Test array field
    StructDef polygon;
    polygon.name = "Polygon";
    polygon.doc = "A polygon defined by vertices";

    Field vertices;
    vertices.name = "vertices";
    vertices.type.base = std::string("Point");
    vertices.type.is_array = true;
    vertices.doc = "List of vertices";

    polygon.fields.push_back(vertices);
    ns.structs.push_back(polygon);

    // Generate code
    CodeGenerator gen;

    std::cout << "=== Types Header ===\n";
    std::cout << gen.generate_types_header(ns);

    std::cout << "\n=== Wire Implementation ===\n";
    std::cout << gen.generate_wire_impl(ns);
}

// Test: Generate enum
void test_enum() {
    Namespace ns;
    ns.name = "example";

    EnumDef status;
    status.name = "Status";
    status.doc = "Operation status";
    status.is_flags = false;

    EnumItem idle;
    idle.name = "idle";
    idle.value = 0;
    status.items.push_back(idle);

    EnumItem running;
    running.name = "running";
    running.value = 1;
    status.items.push_back(running);

    EnumItem error;
    error.name = "error";
    error.value = 2;
    status.items.push_back(error);

    ns.enums.push_back(status);

    // Flags example
    EnumDef options;
    options.name = "Options";
    options.doc = "Configuration flags";
    options.is_flags = true;

    EnumItem verbose;
    verbose.name = "verbose";
    verbose.value = 0x01;
    options.items.push_back(verbose);

    EnumItem debug;
    debug.name = "debug";
    debug.value = 0x02;
    options.items.push_back(debug);

    EnumItem async;
    async.name = "async";
    async.value = 0x04;
    options.items.push_back(async);

    ns.enums.push_back(options);

    CodeGenerator gen;
    std::cout << "=== Enum Test ===\n";
    std::cout << gen.generate_types_header(ns);
}

void print_usage() {
    std::cout << "songc - Song IDL Compiler\n";
    std::cout << "Usage: songc [options] <input.song>\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --test-struct   Run struct generation test\n";
    std::cout << "  --test-enum     Run enum generation test\n";
    std::cout << "  --help          Show this help\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string arg = argv[1];

    if (arg == "--help" || arg == "-h") {
        print_usage();
        return 0;
    }

    if (arg == "--test-struct") {
        test_point_struct();
        return 0;
    }

    if (arg == "--test-enum") {
        test_enum();
        return 0;
    }

    // TODO: Parse actual .song file
    std::cout << "songc: parsing " << arg << " not yet implemented\n";
    std::cout << "Use --test-struct or --test-enum to test code generation\n";

    return 0;
}
