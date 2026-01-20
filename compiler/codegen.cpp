// MIT License
// Copyright (c) 2026 dbjwhs

#include "codegen.hpp"
#include <sstream>
#include <cstdint>

namespace song::compiler {

using u16 = std::uint16_t;

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
        int dims = t.array_dimensions > 0 ? t.array_dimensions : 1;
        std::string result;
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

// =============================================================================
// Struct Generation
// =============================================================================

std::string CodeGenerator::generate_struct_def(const StructDef& s) {
    std::ostringstream out;

    if (!s.doc.empty()) {
        out << "/// " << s.doc << "\n";
    }

    out << "struct " << s.name << " {\n";

    for (const auto& f : s.fields) {
        if (!f.doc.empty()) {
            out << "    /// " << f.doc << "\n";
        }
        out << "    " << type_to_cpp(f.type) << " " << f.name << ";\n";
    }

    out << "};\n";
    return out.str();
}

std::string CodeGenerator::generate_struct_encode(const StructDef& s) {
    std::ostringstream out;

    out << "inline void encode_" << s.name << "(Buffer& buf, const " << s.name << "& val) {\n";

    for (const auto& f : s.fields) {
        out << "    " << encode_call(f.type, "val." + f.name) << ";\n";
    }

    out << "}\n";
    return out.str();
}

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

// Generate array encode helper for struct
std::string generate_struct_array_encode(const StructDef& s) {
    std::ostringstream out;
    out << "inline void encode_array_" << s.name << "(Buffer& buf, const std::vector<" << s.name << ">& arr) {\n";
    out << "    encode_u32(buf, static_cast<u32>(arr.size()));\n";
    out << "    for (const auto& val : arr) {\n";
    out << "        encode_" << s.name << "(buf, val);\n";
    out << "    }\n";
    out << "}\n";
    return out.str();
}

// Generate array decode helper for struct
std::string generate_struct_array_decode(const StructDef& s) {
    std::ostringstream out;
    out << "inline std::vector<" << s.name << "> decode_array_" << s.name << "(Buffer& buf) {\n";
    out << "    u32 count = decode_u32(buf);\n";
    out << "    std::vector<" << s.name << "> arr;\n";
    out << "    arr.reserve(count);\n";
    out << "    for (u32 i = 0; i < count; ++i) {\n";
    out << "        arr.push_back(decode_" << s.name << "(buf));\n";
    out << "    }\n";
    out << "    return arr;\n";
    out << "}\n";
    return out.str();
}

// =============================================================================
// Enum Generation
// =============================================================================

std::string CodeGenerator::generate_enum_def(const EnumDef& e) {
    std::ostringstream out;

    if (!e.doc.empty()) {
        out << "/// " << e.doc << "\n";
    }

    out << "enum class " << e.name << " : ";
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

// =============================================================================
// Service ID Generation
// =============================================================================

std::string CodeGenerator::generate_service_ids(const Namespace& ns) {
    std::ostringstream out;

    out << "// Service and method IDs\n";

    u16 service_id = 1;
    for (const auto& s : ns.services) {
        out << "constexpr u16 kService_" << s.name << " = " << service_id++ << ";\n";

        u16 method_id = 1;
        for (const auto& m : s.methods) {
            out << "constexpr u16 kMethod_" << s.name << "_" << m.name
                << " = " << method_id++ << ";\n";
        }
        out << "\n";
    }

    return out.str();
}

// =============================================================================
// Service Proxy Generation (Client Side)
// =============================================================================

std::string CodeGenerator::generate_method_params(const std::vector<Param>& params) {
    std::ostringstream out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) out << ", ";
        out << type_to_param(params[i].type, params[i].name);
    }
    return out.str();
}

std::string CodeGenerator::generate_encode_params(const std::vector<Param>& params) {
    std::ostringstream out;
    for (const auto& p : params) {
        out << "        " << encode_call(p.type, p.name) << ";\n";
    }
    return out.str();
}

std::string CodeGenerator::generate_decode_params(const std::vector<Param>& params) {
    std::ostringstream out;
    for (const auto& p : params) {
        out << "        " << type_to_cpp(p.type) << " " << p.name
            << " = " << decode_call(p.type) << ";\n";
    }
    return out.str();
}

std::string CodeGenerator::generate_service_proxy(const ServiceDef& s) {
    std::ostringstream out;

    if (!s.doc.empty()) {
        out << "/// " << s.doc << "\n";
    }
    out << "class " << s.name << "Proxy {\n";
    out << "    ServiceConnection& m_conn;\n";
    out << "public:\n";
    out << "    explicit " << s.name << "Proxy(ServiceConnection& conn) : m_conn(conn) {}\n\n";

    for (const auto& m : s.methods) {
        // Method signature
        std::string return_type = type_to_cpp(m.return_type);
        bool is_void = is_primitive(m.return_type) &&
                       get_primitive(m.return_type) == PrimitiveType::void_;

        if (!m.doc.empty()) {
            out << "    /// " << m.doc << "\n";
        }

        out << "    " << return_type << " " << m.name << "(";
        out << generate_method_params(m.params);
        out << ") {\n";

        // Encode request
        out << "        Buffer req;\n";
        for (const auto& p : m.params) {
            std::string encode = encode_call(p.type, p.name);
            // Replace buf with req
            size_t pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "req");
            }
            out << "        " << encode << ";\n";
        }

        // Call
        out << "        Buffer resp = m_conn.call(kService_" << s.name
            << ", kMethod_" << s.name << "_" << m.name << ", req);\n";

        // Decode response
        if (!is_void) {
            // Generate decode call using resp buffer
            std::string decode = decode_call(m.return_type);
            // Replace buf with resp in the decode call
            size_t pos = decode.find("buf");
            if (pos != std::string::npos) {
                decode.replace(pos, 3, "resp");
            }
            out << "        return " << decode << ";\n";
        }

        out << "    }\n\n";
    }

    out << "};\n";
    return out.str();
}

// =============================================================================
// Service Interface Generation (Server Side)
// =============================================================================

std::string CodeGenerator::generate_service_interface(const ServiceDef& s) {
    std::ostringstream out;

    if (!s.doc.empty()) {
        out << "/// " << s.doc << " (interface)\n";
    }
    out << "class I" << s.name << " {\n";
    out << "public:\n";
    out << "    virtual ~I" << s.name << "() = default;\n\n";

    for (const auto& m : s.methods) {
        std::string return_type = type_to_cpp(m.return_type);

        if (!m.doc.empty()) {
            out << "    /// " << m.doc << "\n";
        }

        out << "    virtual " << return_type << " " << m.name << "(";
        out << generate_method_params(m.params);
        out << ") = 0;\n";
    }

    out << "};\n";
    return out.str();
}

// =============================================================================
// Service Dispatcher Generation (Server Side)
// =============================================================================

std::string CodeGenerator::generate_service_dispatcher(const ServiceDef& s) {
    std::ostringstream out;

    out << "inline void dispatch_" << s.name << "(I" << s.name << "& impl, "
        << "u16 method_id, Buffer& request, Buffer& response) {\n";
    out << "    switch (method_id) {\n";

    for (const auto& m : s.methods) {
        out << "        case kMethod_" << s.name << "_" << m.name << ": {\n";

        // Decode parameters from request buffer
        for (const auto& p : m.params) {
            std::string decode = decode_call(p.type);
            // Replace buf with request
            size_t pos = decode.find("buf");
            if (pos != std::string::npos) {
                decode.replace(pos, 3, "request");
            }
            out << "            " << type_to_cpp(p.type) << " " << p.name
                << " = " << decode << ";\n";
        }

        // Call implementation
        bool is_void = is_primitive(m.return_type) &&
                       get_primitive(m.return_type) == PrimitiveType::void_;

        out << "            ";
        if (!is_void) {
            out << "auto result = ";
        }
        out << "impl." << m.name << "(";
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (i > 0) out << ", ";
            out << m.params[i].name;
        }
        out << ");\n";

        // Encode response to response buffer
        if (!is_void) {
            std::string encode = encode_call(m.return_type, "result");
            // Replace buf with response
            size_t pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "response");
            }
            out << "            " << encode << ";\n";
        }

        out << "            break;\n";
        out << "        }\n";
    }

    out << "        default:\n";
    out << "            throw std::runtime_error(\"Unknown method ID: \" + std::to_string(method_id));\n";
    out << "    }\n";
    out << "}\n";

    return out.str();
}

// =============================================================================
// Header Generation
// =============================================================================

std::string CodeGenerator::generate_header(const Namespace& ns) {
    std::ostringstream out;

    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Source: " << ns.name << ".song\n\n";
    out << "#pragma once\n\n";
    out << "#include <song/song.hpp>\n";
    out << "#include <string>\n";
    out << "#include <vector>\n";
    out << "#include <optional>\n";
    out << "#include <stdexcept>\n\n";

    out << "namespace song::" << ns.name << " {\n\n";
    out << "using namespace song;\n\n";

    // Service/method IDs
    if (!ns.services.empty()) {
        out << generate_service_ids(ns);
    }

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

    // Struct serialization (inline)
    // First, generate array helpers for all structs (forward declarations)
    // This ensures they're available when struct encode/decode use them
    if (!ns.structs.empty()) {
        out << "// Forward declare array helpers\n";
        for (const auto& s : ns.structs) {
            out << "inline void encode_array_" << s.name << "(Buffer& buf, const std::vector<" << s.name << ">& arr);\n";
            out << "inline std::vector<" << s.name << "> decode_array_" << s.name << "(Buffer& buf);\n";
        }
        out << "\n";
        out << "// Serialization\n";
        for (const auto& s : ns.structs) {
            out << generate_struct_encode(s) << "\n";
            out << generate_struct_decode(s) << "\n";
        }
        // Array helper implementations
        for (const auto& s : ns.structs) {
            out << generate_struct_array_encode(s) << "\n";
            out << generate_struct_array_decode(s) << "\n";
        }
    }

    // Service proxies (client side)
    for (const auto& s : ns.services) {
        out << generate_service_proxy(s) << "\n";
    }

    // Service interfaces (server side)
    for (const auto& s : ns.services) {
        out << generate_service_interface(s) << "\n";
    }

    // Service dispatchers (server side)
    for (const auto& s : ns.services) {
        out << generate_service_dispatcher(s) << "\n";
    }

    out << "} // namespace song::" << ns.name << "\n";

    return out.str();
}

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

std::string CodeGenerator::generate_wire_impl(const Namespace& ns) {
    std::ostringstream out;

    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Source: " << ns.name << "\n\n";
    out << "#include \"" << ns.name << "_types.hpp\"\n\n";

    out << "namespace song::" << ns.name << " {\n\n";

    out << "using namespace song;\n\n";

    for (const auto& s : ns.structs) {
        out << generate_struct_encode(s) << "\n";
        out << generate_struct_decode(s) << "\n";
    }

    out << "} // namespace song::" << ns.name << "\n";

    return out.str();
}

std::string CodeGenerator::generate_client_header(const Namespace& ns) {
    std::ostringstream out;
    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Client proxies for " << ns.name << "\n";
    out << "#pragma once\n\n";
    out << "#include \"" << ns.name << "_types.hpp\"\n\n";

    out << "namespace song::" << ns.name << " {\n\n";
    out << "using namespace song;\n\n";

    out << generate_service_ids(ns);

    for (const auto& s : ns.services) {
        out << generate_service_proxy(s) << "\n";
    }

    out << "} // namespace song::" << ns.name << "\n";
    return out.str();
}

std::string CodeGenerator::generate_server_header(const Namespace& ns) {
    std::ostringstream out;
    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Server skeletons for " << ns.name << "\n";
    out << "#pragma once\n\n";
    out << "#include \"" << ns.name << "_types.hpp\"\n";
    out << "#include <stdexcept>\n\n";

    out << "namespace song::" << ns.name << " {\n\n";
    out << "using namespace song;\n\n";

    out << generate_service_ids(ns);

    for (const auto& s : ns.services) {
        out << generate_service_interface(s) << "\n";
        out << generate_service_dispatcher(s) << "\n";
    }

    out << "} // namespace song::" << ns.name << "\n";
    return out.str();
}

} // namespace song::compiler
