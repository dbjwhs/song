// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <cstdint>

namespace song {

enum class TokenType {
    // Literals
    Identifier,
    Integer,
    DocComment,

    // Keywords - definitions
    KwNamespace,
    KwStruct,
    KwClass,
    KwEnum,
    KwFlags,
    KwService,
    KwError,

    // Keywords - modifiers
    KwReadonly,
    KwOptional,
    KwStream,
    KwThrows,

    // Keywords - primitive types
    KwBool,
    KwI8,
    KwI16,
    KwI32,
    KwI64,
    KwU8,
    KwU16,
    KwU32,
    KwU64,
    KwF32,
    KwF64,
    KwString,
    KwBytes,
    KwVoid,

    // Punctuation
    Semicolon,    // ;
    Comma,        // ,
    Colon,        // :
    Dot,          // .
    LBrace,       // {
    RBrace,       // }
    LParen,       // (
    RParen,       // )
    LBracket,     // [
    RBracket,     // ]
    Question,     // ?
    Arrow,        // ->
    Equals,       // =

    // Special
    Eof
};

/**
 * Convert token type to string representation (for debugging)
 */
std::string token_type_to_string(TokenType type);

/**
 * Token structure containing type, value, and source location
 */
struct Token {
    TokenType m_type;
    std::string m_value;
    size_t m_line;
    size_t m_column;
    int64_t m_int_value;  // for Integer tokens

    Token(TokenType type, std::string value, size_t line, size_t col);
    Token(TokenType type, std::string value, size_t line, size_t col, int64_t int_val);

    [[nodiscard]] std::string to_string() const;
};

/**
 * Lexical analyzer for Song IDL files
 */
class Lexer {
public:
    explicit Lexer(std::string_view input);

    std::optional<Token> next_token();

    [[nodiscard]] size_t current_line() const { return m_line; }
    [[nodiscard]] size_t current_column() const { return m_column; }

private:
    std::string_view m_input;
    size_t m_current;
    size_t m_line;
    size_t m_column;

    void advance();
    [[nodiscard]] char peek() const;
    [[nodiscard]] char peek_next() const;
    [[nodiscard]] bool at_end() const;
    void skip_whitespace();
    void skip_line_comment();

    std::optional<Token> lex_identifier_or_keyword();
    std::optional<Token> lex_integer();
    std::optional<Token> lex_doc_comment();
};

/**
 * Exception for lexer errors with source location
 */
class LexerError : public std::runtime_error {
public:
    LexerError(const std::string& message, size_t line, size_t column);

    [[nodiscard]] size_t line() const { return m_line; }
    [[nodiscard]] size_t column() const { return m_column; }

private:
    size_t m_line;
    size_t m_column;
};

} // namespace song
