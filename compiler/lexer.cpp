// MIT License
// Copyright (c) 2026 dbjwhs

#include "lexer.hpp"
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace song {

// Keyword lookup table
static const std::unordered_map<std::string_view, TokenType> keywords = {
    // Definition keywords
    {"namespace", TokenType::KwNamespace},
    {"struct", TokenType::KwStruct},
    {"class", TokenType::KwClass},
    {"enum", TokenType::KwEnum},
    {"flags", TokenType::KwFlags},
    {"service", TokenType::KwService},
    {"error", TokenType::KwError},

    // Modifier keywords
    {"readonly", TokenType::KwReadonly},
    {"optional", TokenType::KwOptional},
    {"stream", TokenType::KwStream},
    {"throws", TokenType::KwThrows},

    // Primitive type keywords
    {"bool", TokenType::KwBool},
    {"i8", TokenType::KwI8},
    {"i16", TokenType::KwI16},
    {"i32", TokenType::KwI32},
    {"i64", TokenType::KwI64},
    {"u8", TokenType::KwU8},
    {"u16", TokenType::KwU16},
    {"u32", TokenType::KwU32},
    {"u64", TokenType::KwU64},
    {"f32", TokenType::KwF32},
    {"f64", TokenType::KwF64},
    {"string", TokenType::KwString},
    {"bytes", TokenType::KwBytes},
    {"void", TokenType::KwVoid},
};

std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::Identifier:   return "Identifier";
        case TokenType::Integer:      return "Integer";
        case TokenType::DocComment:   return "DocComment";
        case TokenType::KwNamespace:  return "namespace";
        case TokenType::KwStruct:     return "struct";
        case TokenType::KwClass:      return "class";
        case TokenType::KwEnum:       return "enum";
        case TokenType::KwFlags:      return "flags";
        case TokenType::KwService:    return "service";
        case TokenType::KwError:      return "error";
        case TokenType::KwReadonly:   return "readonly";
        case TokenType::KwOptional:   return "optional";
        case TokenType::KwStream:     return "stream";
        case TokenType::KwThrows:     return "throws";
        case TokenType::KwBool:       return "bool";
        case TokenType::KwI8:         return "i8";
        case TokenType::KwI16:        return "i16";
        case TokenType::KwI32:        return "i32";
        case TokenType::KwI64:        return "i64";
        case TokenType::KwU8:         return "u8";
        case TokenType::KwU16:        return "u16";
        case TokenType::KwU32:        return "u32";
        case TokenType::KwU64:        return "u64";
        case TokenType::KwF32:        return "f32";
        case TokenType::KwF64:        return "f64";
        case TokenType::KwString:     return "string";
        case TokenType::KwBytes:      return "bytes";
        case TokenType::KwVoid:       return "void";
        case TokenType::Semicolon:    return ";";
        case TokenType::Comma:        return ",";
        case TokenType::Colon:        return ":";
        case TokenType::Dot:          return ".";
        case TokenType::LBrace:       return "{";
        case TokenType::RBrace:       return "}";
        case TokenType::LParen:       return "(";
        case TokenType::RParen:       return ")";
        case TokenType::LBracket:     return "[";
        case TokenType::RBracket:     return "]";
        case TokenType::Question:     return "?";
        case TokenType::Arrow:        return "->";
        case TokenType::Equals:       return "=";
        case TokenType::Eof:          return "EOF";
        default:                      return "UNKNOWN";
    }
}

// Token implementation
Token::Token(TokenType type, std::string value, size_t line, size_t col)
    : m_type(type), m_value(std::move(value)), m_line(line), m_column(col), m_int_value(0) {}

Token::Token(TokenType type, std::string value, size_t line, size_t col, int64_t int_val)
    : m_type(type), m_value(std::move(value)), m_line(line), m_column(col), m_int_value(int_val) {}

std::string Token::to_string() const {
    std::ostringstream oss;
    oss << "Token{type=" << token_type_to_string(m_type)
        << ", value='" << m_value << "'";
    if (m_type == TokenType::Integer) {
        oss << ", int_value=" << m_int_value;
    }
    oss << ", line=" << m_line
        << ", column=" << m_column << "}";
    return oss.str();
}

// Lexer implementation
Lexer::Lexer(std::string_view input)
    : m_input(input), m_current(0), m_line(1), m_column(1) {}

void Lexer::advance() {
    if (!at_end()) {
        m_current++;
        m_column++;
    }
}

char Lexer::peek() const {
    if (at_end()) {
        return '\0';
    }
    return m_input[m_current];
}

char Lexer::peek_next() const {
    if (m_current + 1 >= m_input.length()) {
        return '\0';
    }
    return m_input[m_current + 1];
}

bool Lexer::at_end() const {
    return m_current >= m_input.length();
}

void Lexer::skip_whitespace() {
    while (!at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
        } else if (c == '\n') {
            advance();
            m_line++;
            m_column = 1;
        } else {
            break;
        }
    }
}

void Lexer::skip_line_comment() {
    // Skip until end of line
    while (!at_end() && peek() != '\n') {
        advance();
    }
}

std::optional<Token> Lexer::next_token() {
    skip_whitespace();

    if (at_end()) {
        return Token(TokenType::Eof, "", m_line, m_column);
    }

    char c = peek();
    size_t start_line = m_line;
    size_t start_column = m_column;

    // Check for comments
    if (c == '/') {
        if (peek_next() == '/') {
            // Check for doc comment (///)
            if (m_current + 2 < m_input.length() && m_input[m_current + 2] == '/') {
                return lex_doc_comment();
            }
            // Regular comment - skip it
            skip_line_comment();
            return next_token();
        }
    }

    // Single character punctuation
    switch (c) {
        case ';': advance(); return Token(TokenType::Semicolon, ";", start_line, start_column);
        case ',': advance(); return Token(TokenType::Comma, ",", start_line, start_column);
        case ':': advance(); return Token(TokenType::Colon, ":", start_line, start_column);
        case '.': advance(); return Token(TokenType::Dot, ".", start_line, start_column);
        case '{': advance(); return Token(TokenType::LBrace, "{", start_line, start_column);
        case '}': advance(); return Token(TokenType::RBrace, "}", start_line, start_column);
        case '(': advance(); return Token(TokenType::LParen, "(", start_line, start_column);
        case ')': advance(); return Token(TokenType::RParen, ")", start_line, start_column);
        case '[': advance(); return Token(TokenType::LBracket, "[", start_line, start_column);
        case ']': advance(); return Token(TokenType::RBracket, "]", start_line, start_column);
        case '?': advance(); return Token(TokenType::Question, "?", start_line, start_column);
        case '=': advance(); return Token(TokenType::Equals, "=", start_line, start_column);
        default: break;
    }

    // Arrow operator
    if (c == '-' && peek_next() == '>') {
        advance(); // -
        advance(); // >
        return Token(TokenType::Arrow, "->", start_line, start_column);
    }

    // Integer literals
    if (std::isdigit(c)) {
        return lex_integer();
    }

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return lex_identifier_or_keyword();
    }

    // Unknown character
    throw LexerError("Unexpected character: '" + std::string(1, c) + "'", m_line, m_column);
}

std::optional<Token> Lexer::lex_identifier_or_keyword() {
    size_t start_column = m_column;
    std::string value;

    while (!at_end() && (std::isalnum(peek()) || peek() == '_')) {
        value += peek();
        advance();
    }

    // Check if it's a keyword
    auto it = keywords.find(value);
    if (it != keywords.end()) {
        return Token(it->second, value, m_line, start_column);
    }

    return Token(TokenType::Identifier, value, m_line, start_column);
}

std::optional<Token> Lexer::lex_integer() {
    size_t start_column = m_column;
    std::string value;
    int64_t int_value = 0;
    int base = 10;

    // Check for hex prefix
    if (peek() == '0' && (peek_next() == 'x' || peek_next() == 'X')) {
        base = 16;
        value += peek(); advance(); // 0
        value += peek(); advance(); // x

        while (!at_end() && std::isxdigit(peek())) {
            value += peek();
            advance();
        }

        if (value.length() == 2) {
            throw LexerError("Invalid hex literal: missing digits", m_line, start_column);
        }
    } else {
        // Decimal
        while (!at_end() && std::isdigit(peek())) {
            value += peek();
            advance();
        }
    }

    // std::stoll throws std::out_of_range for a literal that does not fit in
    // int64_t. Surface that as a LexerError with a source location instead of
    // letting an uncaught std::out_of_range escape and crash songc. (Full-width
    // u64 values above INT64_MAX are not yet representable; they are reported as
    // an out-of-range error rather than silently truncated.)
    try {
        int_value = std::stoll(value, nullptr, base);
    } catch (const std::out_of_range&) {
        throw LexerError("Integer literal out of range (maximum is INT64_MAX): " + value,
                         m_line, start_column);
    } catch (const std::invalid_argument&) {
        throw LexerError("Invalid integer literal: " + value, m_line, start_column);
    }

    return Token(TokenType::Integer, value, m_line, start_column, int_value);
}

std::optional<Token> Lexer::lex_doc_comment() {
    size_t start_column = m_column;

    // Skip the ///
    advance(); // /
    advance(); // /
    advance(); // /

    // Skip optional leading space
    if (!at_end() && peek() == ' ') {
        advance();
    }

    // Collect the comment text until end of line
    std::string value;
    while (!at_end() && peek() != '\n') {
        value += peek();
        advance();
    }

    return Token(TokenType::DocComment, value, m_line, start_column);
}

// LexerError implementation
LexerError::LexerError(const std::string& message, size_t line, size_t column)
    : std::runtime_error("Lexer error at line " + std::to_string(line) +
                         ", column " + std::to_string(column) + ": " + message),
      m_line(line), m_column(column) {}

} // namespace song
