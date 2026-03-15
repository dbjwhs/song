// MIT License
// Copyright (c) 2026 dbjwhs

#include "python_codegen.hpp"
#include <sstream>
#include <cstdint>

namespace song::compiler {

using u16 = std::uint16_t;

// Convert Type to Python type string for runtime
std::string PythonCodeGenerator::type_to_python(const Type& t) {
    std::string base_type;

    if (is_primitive(t)) {
        auto p = get_primitive(t);
        switch (p) {
            case PrimitiveType::bool_: base_type = "bool"; break;
            case PrimitiveType::i8:
            case PrimitiveType::i16:
            case PrimitiveType::i32:
            case PrimitiveType::i64:
            case PrimitiveType::u8:
            case PrimitiveType::u16:
            case PrimitiveType::u32:
            case PrimitiveType::u64: base_type = "int"; break;
            case PrimitiveType::f32:
            case PrimitiveType::f64: base_type = "float"; break;
            case PrimitiveType::string: base_type = "str"; break;
            case PrimitiveType::bytes: base_type = "bytes"; break;
            case PrimitiveType::void_: base_type = "None"; break;
        }
    } else {
        base_type = get_user_type(t);
    }

    // Handle arrays
    if (t.is_array) {
        int dims = t.array_dimensions > 0 ? t.array_dimensions : 1;
        std::string result;
        for (int i = 0; i < dims; ++i) {
            result += "list[";
        }
        result += base_type;
        for (int i = 0; i < dims; ++i) {
            result += "]";
        }
        return result;
    }

    // Handle optional
    if (t.is_optional) {
        return "Optional[" + base_type + "]";
    }

    return base_type;
}

// Convert Type to Python type hint
std::string PythonCodeGenerator::type_to_type_hint(const Type& t) {
    return type_to_python(t);
}

// Generate encode call for a type
std::string PythonCodeGenerator::encode_call(const Type& t, const std::string& expr) {
    if (t.is_array) {
        // Peel off one array dimension to get element type
        Type elem = t;
        int dims = elem.array_dimensions > 0 ? elem.array_dimensions : 1;
        elem.array_dimensions = dims - 1;
        elem.is_array = (elem.array_dimensions > 0);

        if (is_primitive(elem) && !elem.is_array) {
            auto p = get_primitive(elem);
            switch (p) {
                case PrimitiveType::i32: return "req.encode_array_i32(" + expr + ")";
                case PrimitiveType::i64: return "req.encode_array_i64(" + expr + ")";
                case PrimitiveType::string: return "req.encode_array_string(" + expr + ")";
                default: break;
            }
        }
        // For compound array types (multi-dim or user-type arrays), generate encode loop
        if (!is_primitive(elem) && !elem.is_array) {
            // User-defined type array: encode each element via its encode function
            return "req.encode_u32(len(" + expr + "))\n"
                   "        for _item in " + expr + ":\n"
                   "            encode_" + get_user_type(elem) + "(req, _item)";
        }
        // Multi-dimensional or other unsupported array: emit runtime error
        return "raise NotImplementedError('encode for " + type_to_python(t) + " not yet supported')";
    }

    if (t.is_optional) {
        Type inner = t;
        inner.is_optional = false;
        return "req.encode_optional(" + expr + ", lambda b, v: " + encode_call(inner, "v") + ")";
    }

    if (is_primitive(t)) {
        auto p = get_primitive(t);
        switch (p) {
            case PrimitiveType::bool_: return "req.encode_bool(" + expr + ")";
            case PrimitiveType::i8: return "req.encode_i8(" + expr + ")";
            case PrimitiveType::i16: return "req.encode_i16(" + expr + ")";
            case PrimitiveType::i32: return "req.encode_i32(" + expr + ")";
            case PrimitiveType::i64: return "req.encode_i64(" + expr + ")";
            case PrimitiveType::u8: return "req.encode_u8(" + expr + ")";
            case PrimitiveType::u16: return "req.encode_u16(" + expr + ")";
            case PrimitiveType::u32: return "req.encode_u32(" + expr + ")";
            case PrimitiveType::u64: return "req.encode_u64(" + expr + ")";
            case PrimitiveType::f32: return "req.encode_f32(" + expr + ")";
            case PrimitiveType::f64: return "req.encode_f64(" + expr + ")";
            case PrimitiveType::string: return "req.encode_string(" + expr + ")";
            case PrimitiveType::bytes: return "req.encode_bytes(" + expr + ")";
            case PrimitiveType::void_: return "";
        }
    }

    // User-defined type (struct)
    return "encode_" + get_user_type(t) + "(req, " + expr + ")";
}

// Generate decode call for a type
std::string PythonCodeGenerator::decode_call(const Type& t) {
    if (t.is_array) {
        // Peel off one array dimension to get element type
        Type elem = t;
        int dims = elem.array_dimensions > 0 ? elem.array_dimensions : 1;
        elem.array_dimensions = dims - 1;
        elem.is_array = (elem.array_dimensions > 0);

        if (is_primitive(elem) && !elem.is_array) {
            auto p = get_primitive(elem);
            switch (p) {
                case PrimitiveType::i32: return "resp.decode_array_i32()";
                case PrimitiveType::i64: return "resp.decode_array_i64()";
                case PrimitiveType::string: return "resp.decode_array_string()";
                default: break;
            }
        }
        // For compound array types (multi-dim or user-type arrays), generate decode loop
        if (!is_primitive(elem) && !elem.is_array) {
            // User-defined type array: decode each element via its decode function
            return "[decode_" + get_user_type(elem) + "(resp) for _ in range(resp.decode_u32())]";
        }
        // Multi-dimensional or other unsupported array: emit runtime error
        return "raise NotImplementedError('decode for " + type_to_python(t) + " not yet supported')";
    }

    if (t.is_optional) {
        Type inner = t;
        inner.is_optional = false;
        return "resp.decode_optional(lambda b: " + decode_call(inner) + ")";
    }

    if (is_primitive(t)) {
        auto p = get_primitive(t);
        switch (p) {
            case PrimitiveType::bool_: return "resp.decode_bool()";
            case PrimitiveType::i8: return "resp.decode_i8()";
            case PrimitiveType::i16: return "resp.decode_i16()";
            case PrimitiveType::i32: return "resp.decode_i32()";
            case PrimitiveType::i64: return "resp.decode_i64()";
            case PrimitiveType::u8: return "resp.decode_u8()";
            case PrimitiveType::u16: return "resp.decode_u16()";
            case PrimitiveType::u32: return "resp.decode_u32()";
            case PrimitiveType::u64: return "resp.decode_u64()";
            case PrimitiveType::f32: return "resp.decode_f32()";
            case PrimitiveType::f64: return "resp.decode_f64()";
            case PrimitiveType::string: return "resp.decode_string()";
            case PrimitiveType::bytes: return "resp.decode_bytes()";
            case PrimitiveType::void_: return "";
        }
    }

    // User-defined type (struct)
    return "decode_" + get_user_type(t) + "(resp)";
}

// =============================================================================
// Struct Generation
// =============================================================================

std::string PythonCodeGenerator::generate_struct_def(const StructDef& s) {
    std::ostringstream out;

    out << "@dataclass\n";
    out << "class " << s.name << ":\n";

    if (!s.doc.empty()) {
        out << "    \"\"\"" << s.doc << "\"\"\"\n";
    }

    if (s.fields.empty()) {
        out << "    pass\n";
    } else {
        for (const auto& f : s.fields) {
            out << "    " << f.name << ": " << type_to_type_hint(f.type);
            // Add default for optional types
            if (f.type.is_optional) {
                out << " = None";
            }
            out << "\n";
        }
    }

    out << "\n";

    // Generate encode function
    out << "def encode_" << s.name << "(buf: Buffer, val: " << s.name << ") -> None:\n";
    out << "    \"\"\"Encode " << s.name << " to buffer.\"\"\"\n";
    for (const auto& f : s.fields) {
        // Build encode call with buf.encode_X
        std::string encode = encode_call(f.type, "val." + f.name);
        // Replace all occurrences of "req" with "buf" (method calls and function args)
        size_t pos = 0;
        while ((pos = encode.find("req", pos)) != std::string::npos) {
            encode.replace(pos, 3, "buf");
            pos += 3;
        }
        out << "    " << encode << "\n";
    }
    out << "\n";

    // Generate decode function
    out << "def decode_" << s.name << "(buf: Buffer) -> " << s.name << ":\n";
    out << "    \"\"\"Decode " << s.name << " from buffer.\"\"\"\n";
    out << "    return " << s.name << "(\n";
    for (size_t i = 0; i < s.fields.size(); ++i) {
        const auto& f = s.fields[i];
        std::string decode = decode_call(f.type);
        // Replace all occurrences of "resp" with "buf" (method calls and function args)
        size_t pos = 0;
        while ((pos = decode.find("resp", pos)) != std::string::npos) {
            decode.replace(pos, 4, "buf");
            pos += 3;
        }
        out << "        " << f.name << "=" << decode;
        if (i < s.fields.size() - 1) {
            out << ",";
        }
        out << "\n";
    }
    out << "    )\n";

    return out.str();
}

// =============================================================================
// Enum Generation
// =============================================================================

std::string PythonCodeGenerator::generate_enum_def(const EnumDef& e) {
    std::ostringstream out;

    out << "class " << e.name << "(IntEnum):\n";

    if (!e.doc.empty()) {
        out << "    \"\"\"" << e.doc << "\"\"\"\n";
    }

    int64_t next_value = 0;
    for (const auto& item : e.items) {
        out << "    " << item.name << " = ";
        if (item.value.has_value()) {
            out << item.value.value();
            next_value = item.value.value() + 1;
        } else {
            out << next_value++;
        }
        out << "\n";
    }

    return out.str();
}

// =============================================================================
// Service ID Generation
// =============================================================================

std::string PythonCodeGenerator::generate_service_ids(const Namespace& ns) {
    std::ostringstream out;

    out << "# Service and method IDs\n";

    u16 service_id = 1;
    for (const auto& s : ns.services) {
        out << "SERVICE_" << s.name << " = " << service_id++ << "\n";

        u16 method_id = 1;
        for (const auto& m : s.methods) {
            out << "METHOD_" << s.name << "_" << m.name << " = " << method_id++ << "\n";
        }
        out << "\n";
    }

    return out.str();
}

// =============================================================================
// Service Proxy Generation
// =============================================================================

std::string PythonCodeGenerator::generate_method_params(const std::vector<Param>& params) {
    std::ostringstream out;
    for (size_t i = 0; i < params.size(); ++i) {
        out << ", " << params[i].name << ": " << type_to_type_hint(params[i].type);
    }
    return out.str();
}

std::string PythonCodeGenerator::generate_service_proxy(const ServiceDef& s) {
    std::ostringstream out;

    out << "class " << s.name << "Proxy:\n";

    if (!s.doc.empty()) {
        out << "    \"\"\"" << s.doc << "\"\"\"\n";
    } else {
        out << "    \"\"\"Client proxy for " << s.name << " service.\"\"\"\n";
    }

    out << "\n";
    out << "    def __init__(self, conn: ServiceConnection):\n";
    out << "        self._conn = conn\n";
    out << "\n";

    for (const auto& m : s.methods) {
        std::string return_type = type_to_type_hint(m.return_type);
        bool is_void = is_primitive(m.return_type) &&
                       get_primitive(m.return_type) == PrimitiveType::void_;

        // Method signature
        out << "    def " << m.name << "(self" << generate_method_params(m.params)
            << ") -> " << return_type << ":\n";

        if (!m.doc.empty()) {
            out << "        \"\"\"" << m.doc << "\"\"\"\n";
        }

        // Encode request
        out << "        req = Buffer()\n";
        for (const auto& p : m.params) {
            out << "        " << encode_call(p.type, p.name) << "\n";
        }

        // Call
        out << "        resp = self._conn.call(SERVICE_" << s.name
            << ", METHOD_" << s.name << "_" << m.name << ", req)\n";

        // Decode response
        if (!is_void) {
            out << "        return " << decode_call(m.return_type) << "\n";

        }
        out << "\n";
    }

    return out.str();
}

// =============================================================================
// Module Generation
// =============================================================================

std::string PythonCodeGenerator::generate_module(const Namespace& ns) {
    std::ostringstream out;

    out << "# Generated by songc - DO NOT EDIT\n";
    out << "# Source: " << ns.name << ".song\n\n";

    out << "from dataclasses import dataclass\n";
    out << "from enum import IntEnum\n";
    out << "from typing import Optional, List\n";
    out << "\n";
    out << "from song.buffer import Buffer\n";
    out << "from song.connection import ServiceConnection\n";
    out << "\n\n";

    // Service/method IDs
    if (!ns.services.empty()) {
        out << generate_service_ids(ns);
        out << "\n";
    }

    // Enums
    for (const auto& e : ns.enums) {
        out << generate_enum_def(e) << "\n\n";
    }

    // Structs
    for (const auto& s : ns.structs) {
        out << generate_struct_def(s) << "\n\n";
    }

    // Service proxies
    for (const auto& s : ns.services) {
        out << generate_service_proxy(s) << "\n";
    }

    return out.str();
}

std::string PythonCodeGenerator::generate_client(const Namespace& ns) {
    // For now, same as generate_module
    return generate_module(ns);
}

} // namespace song::compiler
