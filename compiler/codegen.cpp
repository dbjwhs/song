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
// Class ID Generation
// =============================================================================

std::string CodeGenerator::generate_class_ids(const Namespace& ns) {
    std::ostringstream out;

    out << "// Class type IDs, property IDs, and method IDs\n";

    u16 type_id = 1;
    for (const auto& c : ns.classes) {
        out << "constexpr u32 kType_" << c.name << " = " << type_id++ << ";\n";

        // Property IDs
        u16 prop_id = 1;
        for (const auto& p : c.properties) {
            out << "constexpr u16 kProp_" << c.name << "_" << p.name
                << " = " << prop_id++ << ";\n";
        }

        // Constructor IDs (0 = default, 1+ = overloads)
        u16 ctor_id = 0;
        for (size_t i = 0; i < c.constructors.size(); ++i) {
            out << "constexpr u16 kCtor_" << c.name << "_" << ctor_id++
                << " = " << i << ";\n";
        }

        // Method IDs
        u16 method_id = 1;
        for (const auto& m : c.methods) {
            out << "constexpr u16 kMethod_" << c.name << "_" << m.name
                << " = " << method_id++ << ";\n";
        }
        out << "\n";
    }

    return out.str();
}

// =============================================================================
// Class Proxy Generation (Client Side)
// =============================================================================

std::string CodeGenerator::generate_class_proxy(const ClassDef& c) {
    std::ostringstream out;

    if (!c.doc.empty()) {
        out << "/// " << c.doc << "\n";
    }
    out << "class " << c.name << "Proxy {\n";
    out << "    ServiceConnection& m_conn;\n";
    out << "    u32 m_type_id;\n";
    out << "    i32 m_object_id;\n";
    out << "\n";
    out << "public:\n";

    // Constructor from object reference
    out << "    " << c.name << "Proxy(ServiceConnection& conn, u32 type_id, i32 object_id)\n";
    out << "        : m_conn(conn), m_type_id(type_id), m_object_id(object_id) {}\n\n";

    // Move constructor (transfers ownership)
    out << "    " << c.name << "Proxy(" << c.name << "Proxy&& other) noexcept\n";
    out << "        : m_conn(other.m_conn), m_type_id(other.m_type_id), m_object_id(other.m_object_id) {\n";
    out << "        other.m_object_id = 0;  // Prevent double-release\n";
    out << "    }\n\n";

    // Move assignment
    out << "    " << c.name << "Proxy& operator=(" << c.name << "Proxy&& other) noexcept {\n";
    out << "        if (this != &other) {\n";
    out << "            release();\n";
    out << "            m_type_id = other.m_type_id;\n";
    out << "            m_object_id = other.m_object_id;\n";
    out << "            other.m_object_id = 0;\n";
    out << "        }\n";
    out << "        return *this;\n";
    out << "    }\n\n";

    // Delete copy (reference semantics)
    out << "    " << c.name << "Proxy(const " << c.name << "Proxy&) = delete;\n";
    out << "    " << c.name << "Proxy& operator=(const " << c.name << "Proxy&) = delete;\n\n";

    // Destructor releases object
    out << "    ~" << c.name << "Proxy() { release(); }\n\n";

    // Release method
    out << "    void release() {\n";
    out << "        if (m_object_id != 0) {\n";
    out << "            m_conn.release_object(m_type_id, m_object_id);\n";
    out << "            m_object_id = 0;\n";
    out << "        }\n";
    out << "    }\n\n";

    // Null check
    out << "    bool is_null() const { return m_object_id == 0; }\n";
    out << "    explicit operator bool() const { return !is_null(); }\n\n";

    // Accessors
    out << "    u32 type_id() const { return m_type_id; }\n";
    out << "    i32 object_id() const { return m_object_id; }\n\n";

    // Property getters and setters
    for (const auto& p : c.properties) {
        std::string cpp_type = type_to_cpp(p.type);

        if (!p.doc.empty()) {
            out << "    /// " << p.doc << "\n";
        }

        // Getter
        out << "    " << cpp_type << " " << p.name << "() const {\n";
        out << "        Buffer resp = m_conn.get_property(m_type_id, m_object_id, kProp_"
            << c.name << "_" << p.name << ");\n";
        std::string decode = decode_call(p.type);
        size_t pos = decode.find("buf");
        if (pos != std::string::npos) {
            decode.replace(pos, 3, "resp");
        }
        out << "        return " << decode << ";\n";
        out << "    }\n";

        // Setter (if not readonly)
        if (!p.readonly) {
            out << "    void set_" << p.name << "(" << type_to_param(p.type, "value") << ") {\n";
            out << "        Buffer val;\n";
            std::string encode = encode_call(p.type, "value");
            pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "val");
            }
            out << "        " << encode << ";\n";
            out << "        m_conn.set_property(m_type_id, m_object_id, kProp_"
                << c.name << "_" << p.name << ", val);\n";
            out << "    }\n";
        }
        out << "\n";
    }

    // Methods
    for (const auto& m : c.methods) {
        std::string return_type = type_to_cpp(m.return_type);
        bool is_void = is_primitive(m.return_type) &&
                       get_primitive(m.return_type) == PrimitiveType::void_;

        if (!m.doc.empty()) {
            out << "    /// " << m.doc << "\n";
        }

        out << "    " << return_type << " " << m.name << "(";
        out << generate_method_params(m.params);
        out << ") {\n";

        // Encode arguments
        out << "        Buffer args;\n";
        for (const auto& p : m.params) {
            std::string encode = encode_call(p.type, p.name);
            size_t pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "args");
            }
            out << "        " << encode << ";\n";
        }

        // Call object method
        out << "        Buffer resp = m_conn.call_object(m_type_id, m_object_id, kMethod_"
            << c.name << "_" << m.name << ", args);\n";

        // Decode response
        if (!is_void) {
            std::string decode = decode_call(m.return_type);
            size_t pos = decode.find("buf");
            if (pos != std::string::npos) {
                decode.replace(pos, 3, "resp");
            }
            out << "        return " << decode << ";\n";
        }

        out << "    }\n\n";
    }

    out << "};\n";

    // Factory function(s) for creating instances
    for (size_t i = 0; i < c.constructors.size(); ++i) {
        const auto& ctor = c.constructors[i];

        out << "\n/// Create a new " << c.name << " object\n";
        out << "inline " << c.name << "Proxy create_" << c.name << "(ServiceConnection& conn";
        for (const auto& p : ctor.params) {
            out << ", " << type_to_param(p.type, p.name);
        }
        out << ") {\n";

        out << "    Buffer args;\n";
        for (const auto& p : ctor.params) {
            std::string encode = encode_call(p.type, p.name);
            size_t pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "args");
            }
            out << "    " << encode << ";\n";
        }

        out << "    auto ref = conn.create_object(kType_" << c.name << ", " << i << ", args);\n";
        out << "    return " << c.name << "Proxy(conn, ref.type_id, ref.object_id);\n";
        out << "}\n";
    }

    return out.str();
}

// =============================================================================
// Class Skeleton Generation (Server Side)
// =============================================================================

std::string CodeGenerator::generate_class_skeleton(const ClassDef& c) {
    std::ostringstream out;
    std::string upper_name;
    for (char ch : c.name) {
        upper_name += static_cast<char>(toupper(ch));
    }

    if (!c.doc.empty()) {
        out << "/// " << c.doc << " (server implementation base)\n";
    }

    // Determine base class
    std::string base_class = "song::Object";
    if (c.base.has_value()) {
        base_class = c.base.value() + "Base";
    }

    out << "class " << c.name << "Base : public " << base_class << " {\n";
    out << "protected:\n";

    // Property storage
    for (const auto& p : c.properties) {
        out << "    " << type_to_cpp(p.type) << " " << p.name << "_{};\n";
    }

    // Macro injection point for private members
    out << "\n";
    out << "    // User extension point: define " << upper_name << "_SERVER_PRIVATE before including\n";
    out << "    // to add private members/methods to this class\n";
    out << "#ifdef " << upper_name << "_SERVER_PRIVATE\n";
    out << "    " << upper_name << "_SERVER_PRIVATE\n";
    out << "#endif\n";

    out << "\npublic:\n";
    out << "    virtual ~" << c.name << "Base() = default;\n\n";

    // Property accessors (virtual for override)
    for (const auto& p : c.properties) {
        std::string cpp_type = type_to_cpp(p.type);

        if (!p.doc.empty()) {
            out << "    /// " << p.doc << "\n";
        }

        // Getter
        out << "    virtual " << cpp_type << " get_" << p.name << "() const { return "
            << p.name << "_; }\n";

        // Setter (if not readonly)
        if (!p.readonly) {
            out << "    virtual void set_" << p.name << "(" << type_to_param(p.type, "value")
                << ") { " << p.name << "_ = value; }\n";
        }
        out << "\n";
    }

    // Method declarations (pure virtual)
    for (const auto& m : c.methods) {
        std::string return_type = type_to_cpp(m.return_type);

        if (!m.doc.empty()) {
            out << "    /// " << m.doc << "\n";
        }

        out << "    virtual " << return_type << " " << m.name << "(";
        out << generate_method_params(m.params);
        out << ") = 0;\n";
    }

    // Object interface implementations
    out << "\n    // Object interface implementation (generated)\n";
    out << "    void prop_get(u16 prop_id, Buffer& resp) override;\n";
    out << "    void prop_set(u16 prop_id, Buffer& req, Buffer& resp) override;\n";
    out << "    void dispatch(u16 method_id, Buffer& req, Buffer& resp) override;\n";

    out << "};\n";

    return out.str();
}

// =============================================================================
// Class Dispatcher Generation (Server Side)
// =============================================================================

std::string CodeGenerator::generate_class_dispatcher(const ClassDef& c) {
    std::ostringstream out;

    // prop_get implementation
    out << "inline void " << c.name << "Base::prop_get(u16 prop_id, Buffer& resp) {\n";
    out << "    switch (prop_id) {\n";

    for (const auto& p : c.properties) {
        out << "        case kProp_" << c.name << "_" << p.name << ": {\n";
        std::string encode = encode_call(p.type, "get_" + p.name + "()");
        size_t pos = encode.find("buf");
        if (pos != std::string::npos) {
            encode.replace(pos, 3, "resp");
        }
        out << "            " << encode << ";\n";
        out << "            break;\n";
        out << "        }\n";
    }

    out << "        default:\n";
    // If has base class, delegate to base
    if (c.base.has_value()) {
        out << "            " << c.base.value() << "Base::prop_get(prop_id, resp);\n";
        out << "            break;\n";
    } else {
        out << "            throw std::runtime_error(\"Unknown property ID: \" + std::to_string(prop_id));\n";
    }
    out << "    }\n";
    out << "}\n\n";

    // prop_set implementation
    out << "inline void " << c.name << "Base::prop_set(u16 prop_id, Buffer& req, Buffer& resp) {\n";
    out << "    switch (prop_id) {\n";

    for (const auto& p : c.properties) {
        if (!p.readonly) {
            out << "        case kProp_" << c.name << "_" << p.name << ": {\n";

            // Decode the new value
            std::string decode = decode_call(p.type);
            size_t pos = decode.find("buf");
            if (pos != std::string::npos) {
                decode.replace(pos, 3, "req");
            }
            out << "            auto value = " << decode << ";\n";
            out << "            set_" << p.name << "(value);\n";

            // Encode the stored value back
            std::string encode = encode_call(p.type, "get_" + p.name + "()");
            pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "resp");
            }
            out << "            " << encode << ";\n";
            out << "            break;\n";
            out << "        }\n";
        }
    }

    out << "        default:\n";
    if (c.base.has_value()) {
        out << "            " << c.base.value() << "Base::prop_set(prop_id, req, resp);\n";
        out << "            break;\n";
    } else {
        out << "            throw std::runtime_error(\"Unknown or readonly property ID: \" + std::to_string(prop_id));\n";
    }
    out << "    }\n";
    out << "}\n\n";

    // dispatch implementation
    out << "inline void " << c.name << "Base::dispatch(u16 method_id, Buffer& req, Buffer& resp) {\n";
    out << "    switch (method_id) {\n";

    for (const auto& m : c.methods) {
        out << "        case kMethod_" << c.name << "_" << m.name << ": {\n";

        // Decode parameters
        for (const auto& p : m.params) {
            std::string decode = decode_call(p.type);
            size_t pos = decode.find("buf");
            if (pos != std::string::npos) {
                decode.replace(pos, 3, "req");
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
        out << m.name << "(";
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (i > 0) out << ", ";
            out << m.params[i].name;
        }
        out << ");\n";

        // Encode response
        if (!is_void) {
            std::string encode = encode_call(m.return_type, "result");
            size_t pos = encode.find("buf");
            if (pos != std::string::npos) {
                encode.replace(pos, 3, "resp");
            }
            out << "            " << encode << ";\n";
        }

        out << "            break;\n";
        out << "        }\n";
    }

    out << "        default:\n";
    if (c.base.has_value()) {
        out << "            " << c.base.value() << "Base::dispatch(method_id, req, resp);\n";
        out << "            break;\n";
    } else {
        out << "            throw std::runtime_error(\"Unknown method ID: \" + std::to_string(method_id));\n";
    }
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

    // Class/property/method IDs
    if (!ns.classes.empty()) {
        out << generate_class_ids(ns);
    }

    // Forward declarations for structs
    for (const auto& s : ns.structs) {
        out << "struct " << s.name << ";\n";
    }
    if (!ns.structs.empty()) {
        out << "\n";
    }

    // Forward declarations for classes
    for (const auto& c : ns.classes) {
        out << "class " << c.name << "Proxy;\n";
        out << "class " << c.name << "Base;\n";
    }
    if (!ns.classes.empty()) {
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

    // Class proxies (client side)
    for (const auto& c : ns.classes) {
        out << generate_class_proxy(c) << "\n";
    }

    // Class skeletons (server side)
    for (const auto& c : ns.classes) {
        out << generate_class_skeleton(c) << "\n";
    }

    // Class dispatchers (server side)
    for (const auto& c : ns.classes) {
        out << generate_class_dispatcher(c) << "\n";
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
