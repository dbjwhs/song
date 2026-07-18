// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "lexer.hpp"
#include "ast.hpp"
#include <optional>
#include <stdexcept>

namespace song {

/**
 * Recursive descent parser for Song IDL files.
 * Converts token stream from Lexer into AST nodes.
 */
class Parser {
public:
    explicit Parser(std::string_view input);

    compiler::AST parse();

private:
    Lexer m_lexer;
    std::optional<Token> m_current_token;
    std::string m_pending_doc;  // accumulated doc comments

    void advance();
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool at_end() const;
    Token expect(TokenType type, const std::string& msg);
    std::string take_doc();  // take and clear pending doc

    // Parse functions
    compiler::Namespace parse_namespace();
    compiler::StructDef parse_struct();
    compiler::EnumDef parse_enum(bool is_flags);
    compiler::ClassDef parse_class();
    compiler::ServiceDef parse_service();
    compiler::ErrorDef parse_error();
    compiler::Field parse_field();
    compiler::EnumItem parse_enum_item();
    compiler::Property parse_property();
    compiler::Method parse_method();
    compiler::Param parse_param();
    compiler::Type parse_type();
    compiler::SourceLoc current_loc();

    // Type helpers
    bool is_type_start() const;
    bool is_primitive_type() const;
    compiler::PrimitiveType token_to_primitive() const;
};

/**
 * Exception for parser errors with source location
 */
class ParserError : public std::runtime_error {
public:
    ParserError(const std::string& message, size_t line, size_t column);

    [[nodiscard]] size_t line() const { return m_line; }
    [[nodiscard]] size_t column() const { return m_column; }

private:
    size_t m_line;
    size_t m_column;
};

} // namespace song
