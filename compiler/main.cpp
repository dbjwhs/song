// MIT License
// Copyright (c) 2026 dbjwhs

#include "parser.hpp"
#include "resolver.hpp"
#include "codegen.hpp"
#include "python_codegen.hpp"
#include "scaffold.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace song;
using namespace song::compiler;

// Read entire file into string
static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Write string to file
static void write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot write file: " + path);
    }
    file << content;
}

static void print_usage(const char* program) {
    std::cerr << "songc - Song IDL Compiler\n";
    std::cerr << "Usage: " << program << " [options] <input.song>\n";
    std::cerr << "\nOptions:\n";
    std::cerr << "  -o, --output <dir>  Output directory (default: current)\n";
    std::cerr << "  --lang <lang>       Output language: cpp (default), python\n";
    std::cerr << "  --single            Generate single header file (default, C++ only)\n";
    std::cerr << "  --split             Generate separate files (C++ only)\n";
    std::cerr << "  --scaffold          Generate implementation skeleton (C++ only)\n";
    std::cerr << "                      Creates new file or appends sync report to existing\n";
    std::cerr << "  -h, --help          Show this help\n";
    std::cerr << "\nC++ Output:\n";
    std::cerr << "  Single mode: <name>.hpp (all-in-one header)\n";
    std::cerr << "  Split mode:  <name>_types.hpp, <name>_wire.cpp,\n";
    std::cerr << "               <name>_client.hpp, <name>_server.hpp\n";
    std::cerr << "  Scaffold:    <name>_impl.cpp (implementation skeleton)\n";
    std::cerr << "\nPython Output:\n";
    std::cerr << "  <name>.py (client proxies and type definitions)\n";
}

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_dir = ".";
    std::string lang = "cpp";
    bool split_mode = false;
    bool scaffold_mode = false;

    // Parse arguments
    for (int ndx = 1; ndx < argc; ++ndx) {
        std::string arg = argv[ndx];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }

        if (arg == "-o" || arg == "--output") {
            if (ndx + 1 >= argc) {
                std::cerr << "Error: -o requires an argument\n";
                return 1;
            }
            output_dir = argv[++ndx];
            continue;
        }

        if (arg == "--lang") {
            if (ndx + 1 >= argc) {
                std::cerr << "Error: --lang requires an argument (cpp or python)\n";
                return 1;
            }
            lang = argv[++ndx];
            if (lang != "cpp" && lang != "python") {
                std::cerr << "Error: Unknown language: " << lang << " (use cpp or python)\n";
                return 1;
            }
            continue;
        }

        if (arg == "--single") {
            split_mode = false;
            continue;
        }

        if (arg == "--split") {
            split_mode = true;
            continue;
        }

        if (arg == "--scaffold") {
            scaffold_mode = true;
            continue;
        }

        if (arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }

        // Positional argument = input file
        if (input_path.empty()) {
            input_path = arg;
        } else {
            std::cerr << "Error: Multiple input files not supported\n";
            return 1;
        }
    }

    if (input_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Read source file
    std::string source;
    try {
        source = read_file(input_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    // Parse
    Parser parser(source);
    AST ast;
    try {
        ast = parser.parse();
    } catch (const ParserError& e) {
        std::cerr << input_path << ":" << e.line() << ":" << e.column()
                  << ": error: " << e.what() << "\n";
        return 1;
    }

    // Resolve (semantic analysis)
    Resolver resolver(ast);
    if (!resolver.resolve()) {
        for (const auto& err : resolver.errors()) {
            std::cerr << input_path << ":" << err.line() << ":" << err.column()
                      << ": error: " << err.what() << "\n";
        }
        return 1;
    }

    // Generate code
    const Namespace& ns = ast.namespaces.front();

    // Create output directory if needed
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }

    std::string base_name = ns.name;

    try {
        if (lang == "python") {
            // Python code generation
            PythonCodeGenerator pygen;
            std::string output_path = output_dir + "/" + base_name + ".py";
            write_file(output_path, pygen.generate_module(ns));
            std::cout << "Generated: " << output_path << "\n";
        } else if (scaffold_mode) {
            // C++ scaffold generation
            ScaffoldGenerator scaffold;

            // Check if header file exists (for struct/enum comparison)
            std::string header_path = output_dir + "/" + base_name + ".hpp";
            std::optional<std::string> existing_header;
            if (fs::exists(header_path)) {
                existing_header = read_file(header_path);
            }

            for (const auto& service : ns.services) {
                std::string impl_path = output_dir + "/" + base_name + "_" +
                                        service.name + "_impl.cpp";

                // Check if impl file exists
                std::optional<std::string> existing_impl;
                if (fs::exists(impl_path)) {
                    existing_impl = read_file(impl_path);
                }

                // Generate skeleton or sync report
                std::string content = scaffold.generate(ns, service, existing_impl, existing_header);
                write_file(impl_path, content);

                if (existing_impl.has_value()) {
                    std::cout << "Updated: " << impl_path << " (sync report appended)\n";
                } else {
                    std::cout << "Generated: " << impl_path << " (new skeleton)\n";
                }
            }

            if (ns.services.empty()) {
                std::cerr << "Warning: No services found in " << input_path << "\n";
            }
        } else {
            // C++ code generation
            CodeGenerator gen;

            if (split_mode) {
                // Split mode: separate files
                write_file(output_dir + "/" + base_name + "_types.hpp",
                           gen.generate_types_header(ns));
                write_file(output_dir + "/" + base_name + "_wire.cpp",
                           gen.generate_wire_impl(ns));
                write_file(output_dir + "/" + base_name + "_client.hpp",
                           gen.generate_client_header(ns));
                write_file(output_dir + "/" + base_name + "_server.hpp",
                           gen.generate_server_header(ns));

                std::cout << "Generated:\n";
                std::cout << "  " << output_dir << "/" << base_name << "_types.hpp\n";
                std::cout << "  " << output_dir << "/" << base_name << "_wire.cpp\n";
                std::cout << "  " << output_dir << "/" << base_name << "_client.hpp\n";
                std::cout << "  " << output_dir << "/" << base_name << "_server.hpp\n";
            } else {
                // Single mode: all-in-one header
                std::string output_path = output_dir + "/" + base_name + ".hpp";
                write_file(output_path, gen.generate_header(ns));
                std::cout << "Generated: " << output_path << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error writing output: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
