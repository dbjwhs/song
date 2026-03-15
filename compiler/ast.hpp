// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace song::compiler {

// Source location for error reporting
struct SourceLoc {
    int line = 0;
    int column = 0;
    std::string file;
};

// Primitive types
enum class PrimitiveType {
    bool_,
    i8, i16, i32, i64,
    u8, u16, u32, u64,
    f32, f64,
    string,
    bytes,
    void_
};

// Type representation
struct Type {
    std::variant<PrimitiveType, std::string> base;  // Primitive or user-defined name
    bool is_array = false;      // T[]
    bool is_optional = false;   // T?
    int array_dimensions = 0;   // 0 = not array, 1 = T[], 2 = T[][], etc.
};

// Field in a struct
struct Field {
    std::string name;
    Type type;
    std::string doc;  // Doc comment
    SourceLoc loc;
};

// Struct definition
struct StructDef {
    std::string name;
    std::optional<std::string> base;  // Inheritance (optional)
    std::vector<Field> fields;
    std::string doc;
    SourceLoc loc;
};

// Enum item
struct EnumItem {
    std::string name;
    std::optional<int64_t> value;
    std::string doc;
    SourceLoc loc;
};

// Enum definition
struct EnumDef {
    std::string name;
    std::vector<EnumItem> items;
    bool is_flags = false;  // true for 'flags', false for 'enum'
    std::string doc;
    SourceLoc loc;
};

// Method parameter
struct Param {
    std::string name;
    Type type;
    SourceLoc loc;
};

// Property in a class
struct Property {
    std::string name;
    Type type;
    bool readonly = false;
    std::string doc;
    SourceLoc loc;
};

// Constructor in a class
struct Constructor {
    std::vector<Param> params;
    std::string doc;
    SourceLoc loc;
};

// Method in a class or service
struct Method {
    std::string name;
    std::vector<Param> params;
    Type return_type;
    bool is_optional = false;
    bool is_stream = false;
    std::vector<std::string> throws;  // Error types
    std::string doc;
    SourceLoc loc;
};

// Class definition (reference type)
struct ClassDef {
    std::string name;
    std::optional<std::string> base;  // Inheritance
    std::vector<Property> properties;
    std::vector<Constructor> constructors;
    std::vector<Method> methods;
    std::string doc;
    SourceLoc loc;
};

// Service definition (stateless RPC interface)
struct ServiceDef {
    std::string name;
    std::vector<Method> methods;
    std::string doc;
    SourceLoc loc;
};

// Error definition
struct ErrorDef {
    std::string name;
    std::optional<std::string> base;
    std::vector<Field> fields;
    std::string doc;
    SourceLoc loc;
};

// Namespace containing definitions
struct Namespace {
    std::string name;
    std::vector<EnumDef> enums;
    std::vector<StructDef> structs;
    std::vector<ClassDef> classes;
    std::vector<ServiceDef> services;
    std::vector<ErrorDef> errors;
    SourceLoc loc;
};

// Top-level AST
struct AST {
    std::vector<Namespace> namespaces;
};

// Helper functions for Type
inline bool is_primitive(const Type& t) {
    return std::holds_alternative<PrimitiveType>(t.base);
}

inline PrimitiveType get_primitive(const Type& t) {
    return std::get<PrimitiveType>(t.base);
}

inline const std::string& get_user_type(const Type& t) {
    return std::get<std::string>(t.base);
}

// Convert primitive type to string
inline const char* primitive_to_string(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::bool_: return "bool";
        case PrimitiveType::i8: return "i8";
        case PrimitiveType::i16: return "i16";
        case PrimitiveType::i32: return "i32";
        case PrimitiveType::i64: return "i64";
        case PrimitiveType::u8: return "u8";
        case PrimitiveType::u16: return "u16";
        case PrimitiveType::u32: return "u32";
        case PrimitiveType::u64: return "u64";
        case PrimitiveType::f32: return "f32";
        case PrimitiveType::f64: return "f64";
        case PrimitiveType::string: return "string";
        case PrimitiveType::bytes: return "bytes";
        case PrimitiveType::void_: return "void";
    }
    return "unknown";
}

// Convert primitive type to C++ type
inline const char* primitive_to_cpp(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::bool_: return "bool";
        case PrimitiveType::i8: return "i8";
        case PrimitiveType::i16: return "i16";
        case PrimitiveType::i32: return "i32";
        case PrimitiveType::i64: return "i64";
        case PrimitiveType::u8: return "u8";
        case PrimitiveType::u16: return "u16";
        case PrimitiveType::u32: return "u32";
        case PrimitiveType::u64: return "u64";
        case PrimitiveType::f32: return "f32";
        case PrimitiveType::f64: return "f64";
        case PrimitiveType::string: return "std::string";
        case PrimitiveType::bytes: return "std::vector<std::byte>";
        case PrimitiveType::void_: return "void";
    }
    return "unknown";
}

// Convert Type to C++ type string (used by codegen and scaffold)
inline std::string type_to_cpp(const Type& t) {
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
inline std::string type_to_param(const Type& t, const std::string& name) {
    std::string cpp_type = type_to_cpp(t);

    // Pass by const ref for complex types (arrays, optionals, strings, bytes, user types)
    bool by_ref = t.is_array || t.is_optional;
    if (!by_ref) {
        if (is_primitive(t)) {
            auto p = get_primitive(t);
            by_ref = (p == PrimitiveType::string || p == PrimitiveType::bytes);
        } else {
            by_ref = true;  // User-defined types always by ref
        }
    }

    if (by_ref) {
        return "const " + cpp_type + "& " + name;
    }
    return cpp_type + " " + name;
}

} // namespace song::compiler
