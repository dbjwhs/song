// MIT License
// Copyright (c) 2026 dbjwhs

#include "codegen.hpp"
#include <sstream>

namespace song::compiler {

// Convert Type to C++ type string
std::string CodeGenerator::type_to_cpp(const Type& t) {
    std::string base_type;

    if (is_primitive(t)) {
        base_type = primitive_to_cpp(get_primitive(t));
    } else {
        base_type = get_user_type(t);
    }

    // Handle arrays
    if (t.is_array) {
        // Treat array_dimensions == 0 as 1D array for backwards compatibility
        int dims = t.array_dimensions > 0 ? t.array_dimensions : 1;
        std::string result = "";
        for (int i = 0; i < dims; ++i) {
            result += "std::vector<";
        }
        result += base_type;
        for (int i = 0; i < dims; ++i) {
            result += ">";
        }
        return result;
    }

    // Handle optional
    if (t.is_optional) {
        return "std::optional<" + base_type + ">";
    }

    return base_type;
}

// Convert Type to function parameter (const ref for complex types)
std::string CodeGenerator::type_to_param(const Type& t, const std::string& name) {
    std::string cpp_type = type_to_cpp(t);

    // Pass by const ref for complex types
    bool by_ref = t.is_array || t.is_optional;
    if (is_primitive(t)) {
        auto p = get_primitive(t);
        by_ref = (p == PrimitiveType::string || p == PrimitiveType::bytes);
    } else {
        by_ref = true;  // User-defined types always by ref
    }

    if (by_ref) {
        return "const " + cpp_type + "& " + name;
    }
    return cpp_type + " " + name;
}

// Generate encode call for a type
std::string CodeGenerator::encode_call(const Type& t, const std::string& expr) {
    if (t.is_array) {
        // For arrays, need to determine element type
        Type elem = t;
        elem.is_array = false;
        elem.array_dimensions = 0;

        if (is_primitive(elem)) {
            return "encode_array<" + std::string(primitive_to_cpp(get_primitive(elem))) +
                   ">(buf, " + expr + ")";
        } else {
            return "encode_array_" + get_user_type(elem) + "(buf, " + expr + ")";
        }
    }

    if (t.is_optional) {
        Type inner = t;
        inner.is_optional = false;
        // Optional encoding: presence byte + value
        return "encode_optional(buf, " + expr + ", [&](const auto& v) { " +
               encode_call(inner, "v") + "; })";
    }

    if (is_primitive(t)) {
        auto p = get_primitive(t);
        switch (p) {
            case PrimitiveType::bool_: return "encode_bool(buf, " + expr + ")";
            case PrimitiveType::i8: return "encode_i8(buf, " + expr + ")";
            case PrimitiveType::i16: return "encode_i16(buf, " + expr + ")";
            case PrimitiveType::i32: return "encode_i32(buf, " + expr + ")";
            case PrimitiveType::i64: return "encode_i64(buf, " + expr + ")";
            case PrimitiveType::u8: return "encode_u8(buf, " + expr + ")";
            case PrimitiveType::u16: return "encode_u16(buf, " + expr + ")";
            case PrimitiveType::u32: return "encode_u32(buf, " + expr + ")";
            case PrimitiveType::u64: return "encode_u64(buf, " + expr + ")";
            case PrimitiveType::f32: return "encode_f32(buf, " + expr + ")";
            case PrimitiveType::f64: return "encode_f64(buf, " + expr + ")";
            case PrimitiveType::string: return "encode_string(buf, " + expr + ")";
            case PrimitiveType::bytes: return "encode_bytes(buf, " + expr + ")";
            case PrimitiveType::void_: return "";
        }
    }

    // User-defined type
    return "encode_" + get_user_type(t) + "(buf, " + expr + ")";
}

// Generate decode call for a type
std::string CodeGenerator::decode_call(const Type& t) {
    if (t.is_array) {
        Type elem = t;
        elem.is_array = false;
        elem.array_dimensions = 0;

        if (is_primitive(elem)) {
            return "decode_array<" + std::string(primitive_to_cpp(get_primitive(elem))) + ">(buf)";
        } else {
            return "decode_array_" + get_user_type(elem) + "(buf)";
        }
    }

    if (t.is_optional) {
        Type inner = t;
        inner.is_optional = false;
        return "decode_optional<" + type_to_cpp(inner) + ">(buf, [&]() { return " +
               decode_call(inner) + "; })";
    }

    if (is_primitive(t)) {
        auto p = get_primitive(t);
        switch (p) {
            case PrimitiveType::bool_: return "decode_bool(buf)";
            case PrimitiveType::i8: return "decode_i8(buf)";
            case PrimitiveType::i16: return "decode_i16(buf)";
            case PrimitiveType::i32: return "decode_i32(buf)";
            case PrimitiveType::i64: return "decode_i64(buf)";
            case PrimitiveType::u8: return "decode_u8(buf)";
            case PrimitiveType::u16: return "decode_u16(buf)";
            case PrimitiveType::u32: return "decode_u32(buf)";
            case PrimitiveType::u64: return "decode_u64(buf)";
            case PrimitiveType::f32: return "decode_f32(buf)";
            case PrimitiveType::f64: return "decode_f64(buf)";
            case PrimitiveType::string: return "decode_string(buf)";
            case PrimitiveType::bytes: return "decode_bytes(buf)";
            case PrimitiveType::void_: return "";
        }
    }

    // User-defined type
    return "decode_" + get_user_type(t) + "(buf)";
}

// Generate struct definition
std::string CodeGenerator::generate_struct_def(const StructDef& s) {
    std::ostringstream out;

    // Doc comment
    if (!s.doc.empty()) {
        out << "/// " << s.doc << "\n";
    }

    out << "struct " << s.name << " {\n";

    // Fields
    for (const auto& f : s.fields) {
        if (!f.doc.empty()) {
            out << "    /// " << f.doc << "\n";
        }
        out << "    " << type_to_cpp(f.type) << " " << f.name << ";\n";
    }

    out << "};\n";
    return out.str();
}

// Generate encode function for struct
std::string CodeGenerator::generate_struct_encode(const StructDef& s) {
    std::ostringstream out;

    out << "inline void encode_" << s.name << "(Buffer& buf, const " << s.name << "& val) {\n";

    for (const auto& f : s.fields) {
        out << "    " << encode_call(f.type, "val." + f.name) << ";\n";
    }

    out << "}\n";
    return out.str();
}

// Generate decode function for struct
std::string CodeGenerator::generate_struct_decode(const StructDef& s) {
    std::ostringstream out;

    out << "inline " << s.name << " decode_" << s.name << "(Buffer& buf) {\n";
    out << "    " << s.name << " val;\n";

    for (const auto& f : s.fields) {
        out << "    val." << f.name << " = " << decode_call(f.type) << ";\n";
    }

    out << "    return val;\n";
    out << "}\n";
    return out.str();
}

// Generate enum definition
std::string CodeGenerator::generate_enum_def(const EnumDef& e) {
    std::ostringstream out;

    if (!e.doc.empty()) {
        out << "/// " << e.doc << "\n";
    }

    // Use enum class for type safety
    out << "enum class " << e.name << " : ";

    // Determine underlying type based on values
    out << (e.is_flags ? "u32" : "i32");
    out << " {\n";

    for (size_t i = 0; i < e.items.size(); ++i) {
        const auto& item = e.items[i];
        if (!item.doc.empty()) {
            out << "    /// " << item.doc << "\n";
        }
        out << "    " << item.name;
        if (item.value.has_value()) {
            out << " = " << item.value.value();
        }
        if (i < e.items.size() - 1) {
            out << ",";
        }
        out << "\n";
    }

    out << "};\n";
    return out.str();
}

// Generate types header for a namespace
std::string CodeGenerator::generate_types_header(const Namespace& ns) {
    std::ostringstream out;

    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Source: " << ns.name << "\n\n";
    out << "#pragma once\n\n";
    out << "#include <song/song.hpp>\n";
    out << "#include <string>\n";
    out << "#include <vector>\n";
    out << "#include <optional>\n\n";

    out << "namespace song::" << ns.name << " {\n\n";

    // Forward declarations
    for (const auto& s : ns.structs) {
        out << "struct " << s.name << ";\n";
    }
    if (!ns.structs.empty()) {
        out << "\n";
    }

    // Enums
    for (const auto& e : ns.enums) {
        out << generate_enum_def(e) << "\n";
    }

    // Structs
    for (const auto& s : ns.structs) {
        out << generate_struct_def(s) << "\n";
    }

    // Serialization declarations
    out << "// Serialization functions\n";
    for (const auto& s : ns.structs) {
        out << "void encode_" << s.name << "(Buffer& buf, const " << s.name << "& val);\n";
        out << s.name << " decode_" << s.name << "(Buffer& buf);\n";
    }
    out << "\n";

    out << "} // namespace song::" << ns.name << "\n";

    return out.str();
}

// Generate wire implementation for a namespace
std::string CodeGenerator::generate_wire_impl(const Namespace& ns) {
    std::ostringstream out;

    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Source: " << ns.name << "\n\n";
    out << "#include \"" << ns.name << "_types.hpp\"\n\n";

    out << "namespace song::" << ns.name << " {\n\n";

    out << "using namespace song;\n\n";

    // Struct encode/decode implementations
    for (const auto& s : ns.structs) {
        out << generate_struct_encode(s) << "\n";
        out << generate_struct_decode(s) << "\n";
    }

    out << "} // namespace song::" << ns.name << "\n";

    return out.str();
}

// Placeholder implementations for client/server generation
std::string CodeGenerator::generate_client_header(const Namespace& ns) {
    std::ostringstream out;
    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Client proxies for " << ns.name << "\n";
    out << "#pragma once\n\n";
    out << "#include \"" << ns.name << "_types.hpp\"\n\n";
    out << "namespace song::" << ns.name << " {\n\n";
    out << "// TODO: Generate client proxy classes\n\n";
    out << "} // namespace song::" << ns.name << "\n";
    return out.str();
}

std::string CodeGenerator::generate_server_header(const Namespace& ns) {
    std::ostringstream out;
    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Server skeletons for " << ns.name << "\n";
    out << "#pragma once\n\n";
    out << "#include \"" << ns.name << "_types.hpp\"\n\n";
    out << "namespace song::" << ns.name << " {\n\n";
    out << "// TODO: Generate server skeleton classes\n\n";
    out << "} // namespace song::" << ns.name << "\n";
    return out.str();
}

} // namespace song::compiler
