// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "ast.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <stdexcept>

namespace song {

/**
 * Type categories for symbol table entries
 */
enum class TypeKind {
    Enum,
    Struct,
    Class,
    Service,
    Error
};

/**
 * Symbol table entry for a user-defined type
 */
struct TypeInfo {
    std::string name;
    TypeKind kind;
    compiler::SourceLoc loc;
    std::optional<std::string> base;  // for inheritance tracking
};

/**
 * Exception for resolver errors with source location
 */
class ResolverError : public std::runtime_error {
public:
    ResolverError(const std::string& message, int line, int column);

    [[nodiscard]] int line() const { return m_line; }
    [[nodiscard]] int column() const { return m_column; }

private:
    int m_line;
    int m_column;
};

/**
 * Semantic analyzer for Song IDL.
 *
 * Two-pass algorithm:
 * 1. Register all type definitions in symbol table
 * 2. Validate all type references, inheritance, and constraints
 */
class Resolver {
public:
    explicit Resolver(compiler::AST& ast);

    /**
     * Run semantic analysis on the AST.
     * @return true if valid, false if errors found
     */
    bool resolve();

    /**
     * Get collected errors after resolve() returns false.
     */
    [[nodiscard]] const std::vector<ResolverError>& errors() const { return m_errors; }

    /**
     * Check if a type name exists in the symbol table.
     */
    [[nodiscard]] bool type_exists(const std::string& name) const;

    /**
     * Lookup a type in the symbol table.
     * @return pointer to TypeInfo or nullptr if not found
     */
    [[nodiscard]] const TypeInfo* lookup(const std::string& name) const;

private:
    compiler::AST& m_ast;
    std::unordered_map<std::string, TypeInfo> m_types;  // symbol table
    std::vector<ResolverError> m_errors;

    // Pass 1: Registration
    void register_types();
    void register_type(const std::string& name, TypeKind kind,
                       const compiler::SourceLoc& loc,
                       const std::optional<std::string>& base = std::nullopt);

    // Pass 2: Validation
    void validate_types();
    void validate_struct(const compiler::StructDef& s);
    void validate_enum(const compiler::EnumDef& e);
    void validate_class(const compiler::ClassDef& c);
    void validate_service(const compiler::ServiceDef& s);
    void validate_error_def(const compiler::ErrorDef& e);
    void validate_type(const compiler::Type& type, const compiler::SourceLoc& loc);
    void validate_method(const compiler::Method& m);
    void validate_inheritance(const std::string& type_name,
                              const std::string& base_name,
                              TypeKind expected_kind,
                              const compiler::SourceLoc& loc);

    // Duplicate detection within definitions
    void check_duplicate_fields(const std::vector<compiler::Field>& fields,
                                const std::string& parent_name);
    void check_duplicate_methods(const std::vector<compiler::Method>& methods,
                                 const std::string& parent_name);
    void check_duplicate_enum_items(const std::vector<compiler::EnumItem>& items,
                                    const std::string& parent_name);

    // Cycle detection
    bool has_inheritance_cycle(const std::string& name);
    bool detect_cycle(const std::string& name,
                      std::unordered_set<std::string>& visiting,
                      std::unordered_set<std::string>& visited);

    // Error reporting
    void error(const compiler::SourceLoc& loc, const std::string& msg);
};

/**
 * Convert TypeKind to string for error messages
 */
const char* type_kind_to_string(TypeKind kind);

} // namespace song
