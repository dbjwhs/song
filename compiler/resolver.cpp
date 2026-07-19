// MIT License
// Copyright (c) 2026 dbjwhs

#include "resolver.hpp"

namespace song {

// ResolverError implementation
ResolverError::ResolverError(const std::string& message, int line, int column)
    : std::runtime_error("Resolve error at line " + std::to_string(line) +
                         ", column " + std::to_string(column) + ": " + message),
      m_line(line), m_column(column) {}

const char* type_kind_to_string(TypeKind kind) {
    switch (kind) {
        case TypeKind::Enum:    return "enum";
        case TypeKind::Struct:  return "struct";
        case TypeKind::Class:   return "class";
        case TypeKind::Service: return "service";
        case TypeKind::Error:   return "error";
    }
    return "unknown";
}

// Resolver implementation
Resolver::Resolver(compiler::AST& ast)
    : m_ast(ast) {}

bool Resolver::resolve() {
    m_errors.clear();
    m_types.clear();

    // Pass 1: Register all types
    register_types();

    // Pass 2: Validate all references
    validate_types();

    return m_errors.empty();
}

bool Resolver::type_exists(const std::string& name) const {
    return m_types.find(name) != m_types.end();
}

const TypeInfo* Resolver::lookup(const std::string& name) const {
    auto it = m_types.find(name);
    if (it != m_types.end()) {
        return &it->second;
    }
    return nullptr;
}

void Resolver::error(const compiler::SourceLoc& loc, const std::string& msg) {
    m_errors.emplace_back(msg, loc.line, loc.column);
}

// =============================================================================
// Pass 1: Type Registration
// =============================================================================

void Resolver::register_types() {
    for (const auto& ns : m_ast.namespaces) {
        // Register enums
        for (const auto& e : ns.enums) {
            register_type(e.name, TypeKind::Enum, e.loc);
        }

        // Register structs
        for (const auto& s : ns.structs) {
            register_type(s.name, TypeKind::Struct, s.loc, s.base);
        }

        // Register classes
        for (const auto& c : ns.classes) {
            register_type(c.name, TypeKind::Class, c.loc, c.base);
        }

        // Register services
        for (const auto& s : ns.services) {
            register_type(s.name, TypeKind::Service, s.loc);
        }

        // Register errors
        for (const auto& e : ns.errors) {
            register_type(e.name, TypeKind::Error, e.loc, e.base);
        }
    }
}

void Resolver::register_type(const std::string& name, TypeKind kind,
                             const compiler::SourceLoc& loc,
                             const std::optional<std::string>& base) {
    // Check for duplicate
    auto it = m_types.find(name);
    if (it != m_types.end()) {
        error(loc, "Duplicate type '" + name + "' (first defined at line " +
              std::to_string(it->second.loc.line) + ")");
        return;
    }

    m_types[name] = TypeInfo{name, kind, loc, base};
}

// =============================================================================
// Pass 2: Validation
// =============================================================================

void Resolver::validate_types() {
    for (const auto& ns : m_ast.namespaces) {
        // Validate enums
        for (const auto& e : ns.enums) {
            validate_enum(e);
        }

        // Validate structs
        for (const auto& s : ns.structs) {
            validate_struct(s);
        }

        // Validate classes
        for (const auto& c : ns.classes) {
            validate_class(c);
        }

        // Validate services
        for (const auto& s : ns.services) {
            validate_service(s);
        }

        // Validate errors
        for (const auto& e : ns.errors) {
            validate_error_def(e);
        }
    }
}

void Resolver::validate_enum(const compiler::EnumDef& e) {
    check_duplicate_enum_items(e.items, e.name);
}

void Resolver::validate_struct(const compiler::StructDef& s) {
    // Check inheritance
    if (s.base) {
        validate_inheritance(s.name, *s.base, TypeKind::Struct, s.loc);
    }

    // Check for inheritance cycles
    if (has_inheritance_cycle(s.name)) {
        error(s.loc, "Inheritance cycle detected for struct '" + s.name + "'");
    }

    // Validate field types
    for (const auto& field : s.fields) {
        validate_type(field.type, field.loc);
    }

    // Check for duplicate field names
    check_duplicate_fields(s.fields, s.name);
}

void Resolver::validate_class(const compiler::ClassDef& c) {
    // Check inheritance
    if (c.base) {
        validate_inheritance(c.name, *c.base, TypeKind::Class, c.loc);
    }

    // Check for inheritance cycles
    if (has_inheritance_cycle(c.name)) {
        error(c.loc, "Inheritance cycle detected for class '" + c.name + "'");
    }

    // Validate property types
    for (const auto& prop : c.properties) {
        validate_type(prop.type, prop.loc);
    }

    // Validate constructor param types
    for (const auto& ctor : c.constructors) {
        for (const auto& param : ctor.params) {
            validate_type(param.type, param.loc);
        }
    }

    // Validate methods
    for (const auto& method : c.methods) {
        validate_method(method);
    }

    // Check for duplicate method names
    check_duplicate_methods(c.methods, c.name);

    // Check for duplicate property names (symmetric with struct field checking)
    check_duplicate_properties(c.properties, c.name);
}

void Resolver::validate_service(const compiler::ServiceDef& s) {
    // Validate methods
    for (const auto& method : s.methods) {
        validate_method(method);
    }

    // Check for duplicate method names
    check_duplicate_methods(s.methods, s.name);
}

void Resolver::validate_error_def(const compiler::ErrorDef& e) {
    // Check inheritance
    if (e.base) {
        validate_inheritance(e.name, *e.base, TypeKind::Error, e.loc);
    }

    // Check for inheritance cycles
    if (has_inheritance_cycle(e.name)) {
        error(e.loc, "Inheritance cycle detected for error '" + e.name + "'");
    }

    // Validate field types
    for (const auto& field : e.fields) {
        validate_type(field.type, field.loc);
    }

    // Check for duplicate field names
    check_duplicate_fields(e.fields, e.name);
}

void Resolver::validate_type(const compiler::Type& type, const compiler::SourceLoc& loc) {
    // Primitive types are always valid
    if (compiler::is_primitive(type)) {
        return;
    }

    // User-defined type must exist in symbol table
    const std::string& type_name = compiler::get_user_type(type);
    if (!type_exists(type_name)) {
        error(loc, "Undefined type '" + type_name + "'");
    }
}

void Resolver::validate_method(const compiler::Method& m) {
    // Validate parameter types
    for (const auto& param : m.params) {
        validate_type(param.type, param.loc);
    }

    // Validate return type
    validate_type(m.return_type, m.loc);

    // Validate throws clause - each must be an error type
    for (const auto& error_name : m.throws) {
        const TypeInfo* info = lookup(error_name);
        if (!info) {
            error(m.loc, "Undefined error type '" + error_name + "' in throws clause");
        } else if (info->kind != TypeKind::Error) {
            error(m.loc, "Type '" + error_name + "' in throws clause is not an error type");
        }
    }
}

void Resolver::validate_inheritance(const std::string& type_name,
                                    const std::string& base_name,
                                    TypeKind expected_kind,
                                    const compiler::SourceLoc& loc) {
    // Check base type exists
    const TypeInfo* base_info = lookup(base_name);
    if (!base_info) {
        error(loc, "Undefined base type '" + base_name + "' for " +
              type_kind_to_string(expected_kind) + " '" + type_name + "'");
        return;
    }

    // Check base type is the correct kind
    if (base_info->kind != expected_kind) {
        error(loc, "Type '" + type_name + "' cannot inherit from " +
              type_kind_to_string(base_info->kind) + " '" + base_name +
              "' (expected " + type_kind_to_string(expected_kind) + ")");
    }
}

// =============================================================================
// Duplicate Detection
// =============================================================================

void Resolver::check_duplicate_fields(const std::vector<compiler::Field>& fields,
                                      const std::string& parent_name) {
    std::unordered_set<std::string> seen;
    for (const auto& field : fields) {
        if (seen.count(field.name)) {
            error(field.loc, "Duplicate field '" + field.name +
                  "' in " + parent_name);
        }
        seen.insert(field.name);
    }

    // Check for shadowing of inherited fields
    const TypeInfo* info = lookup(parent_name);
    if (!info || !info->base) {
        return;
    }

    // Collect all ancestor field names (with cycle guard)
    std::unordered_set<std::string> ancestor_fields;
    std::unordered_set<std::string> visited;
    std::string current = *info->base;
    while (true) {
        if (visited.count(current)) { // cycle — already reported elsewhere
            break;
        }
        visited.insert(current);

        // Find this ancestor's fields in the AST
        for (const auto& ns : m_ast.namespaces) {
            for (const auto& s : ns.structs) {
                if (s.name == current) {
                    for (const auto& f : s.fields) {
                        ancestor_fields.insert(f.name);
                    }
                }
            }
            for (const auto& e : ns.errors) {
                if (e.name == current) {
                    for (const auto& f : e.fields) {
                        ancestor_fields.insert(f.name);
                    }
                }
            }
        }
        // Walk up the chain
        const TypeInfo* ancestor = lookup(current);
        if (!ancestor || !ancestor->base) {
            break;
        }
        current = *ancestor->base;
    }

    // Report any shadowed fields
    for (const auto& field : fields) {
        if (ancestor_fields.count(field.name)) {
            error(field.loc, "Field '" + field.name + "' in " + parent_name +
                  " shadows inherited field from base type");
        }
    }
}

void Resolver::check_duplicate_methods(const std::vector<compiler::Method>& methods,
                                       const std::string& parent_name) {
    std::unordered_set<std::string> seen;
    for (const auto& method : methods) {
        if (seen.count(method.name)) {
            error(method.loc, "Duplicate method '" + method.name +
                  "' in " + parent_name);
        }
        seen.insert(method.name);
    }
}

void Resolver::check_duplicate_properties(const std::vector<compiler::Property>& properties,
                                          const std::string& parent_name) {
    std::unordered_set<std::string> seen;
    for (const auto& prop : properties) {
        if (seen.count(prop.name)) {
            error(prop.loc, "Duplicate property '" + prop.name +
                  "' in " + parent_name);
        }
        seen.insert(prop.name);
    }
}

void Resolver::check_duplicate_enum_items(const std::vector<compiler::EnumItem>& items,
                                          const std::string& parent_name) {
    std::unordered_set<std::string> seen;
    for (const auto& item : items) {
        if (seen.count(item.name)) {
            error(item.loc, "Duplicate enum item '" + item.name +
                  "' in " + parent_name);
        }
        seen.insert(item.name);
    }

    // Check for duplicate explicit values
    std::unordered_map<int64_t, std::string> seen_values;
    int64_t auto_value = 0;
    for (const auto& item : items) {
        int64_t val = item.value.value_or(auto_value);
        auto it = seen_values.find(val);
        if (it != seen_values.end()) {
            error(item.loc, "Enum value " + std::to_string(val) + " for '" +
                  item.name + "' collides with '" + it->second +
                  "' in " + parent_name);
        }
        seen_values[val] = item.name;
        auto_value = val + 1;
    }
}

// =============================================================================
// Cycle Detection
// =============================================================================

bool Resolver::has_inheritance_cycle(const std::string& name) {
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    return detect_cycle(name, visiting, visited);
}

bool Resolver::detect_cycle(const std::string& name,
                            std::unordered_set<std::string>& visiting,
                            std::unordered_set<std::string>& visited) {
    // Already fully processed - no cycle through this node
    if (visited.count(name)) {
        return false;
    }

    // Currently visiting - cycle detected
    if (visiting.count(name)) {
        return true;
    }

    const TypeInfo* info = lookup(name);
    if (!info || !info->base) {
        // No base type - no cycle possible
        visited.insert(name);
        return false;
    }

    // Mark as visiting
    visiting.insert(name);

    // Recursively check base
    bool has_cycle = detect_cycle(*info->base, visiting, visited);

    // Mark as visited
    visiting.erase(name);
    visited.insert(name);

    return has_cycle;
}

} // namespace song
