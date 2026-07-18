// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include "lexer.hpp"

using namespace song;

// =============================================================================
// Basic Tokenization
// =============================================================================

TEST(LexerTest, EmptyInput) {
    Lexer lexer("");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Eof);
}

TEST(LexerTest, WhitespaceOnly) {
    Lexer lexer("   \t\n\r  ");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Eof);
}

// =============================================================================
// Punctuation
// =============================================================================

TEST(LexerTest, Semicolon) {
    Lexer lexer(";");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Semicolon);
    EXPECT_EQ(token->m_value, ";");
}

TEST(LexerTest, AllPunctuation) {
    Lexer lexer("; , : . { } ( ) [ ] ? = ->");

    std::vector<TokenType> expected = {
        TokenType::Semicolon, TokenType::Comma, TokenType::Colon, TokenType::Dot,
        TokenType::LBrace, TokenType::RBrace, TokenType::LParen, TokenType::RParen,
        TokenType::LBracket, TokenType::RBracket, TokenType::Question, TokenType::Equals,
        TokenType::Arrow, TokenType::Eof
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp) << "Expected " << token_type_to_string(exp);
    }
}

TEST(LexerTest, ArrowToken) {
    Lexer lexer("->");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Arrow);
    EXPECT_EQ(token->m_value, "->");
}

// =============================================================================
// Keywords
// =============================================================================

TEST(LexerTest, DefinitionKeywords) {
    Lexer lexer("namespace struct class enum flags service error");

    std::vector<TokenType> expected = {
        TokenType::KwNamespace, TokenType::KwStruct, TokenType::KwClass,
        TokenType::KwEnum, TokenType::KwFlags, TokenType::KwService, TokenType::KwError
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp);
    }
}

TEST(LexerTest, ModifierKeywords) {
    Lexer lexer("readonly optional stream throws");

    std::vector<TokenType> expected = {
        TokenType::KwReadonly, TokenType::KwOptional,
        TokenType::KwStream, TokenType::KwThrows
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp);
    }
}

TEST(LexerTest, PrimitiveTypeKeywords) {
    Lexer lexer("bool i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 string bytes void");

    std::vector<TokenType> expected = {
        TokenType::KwBool, TokenType::KwI8, TokenType::KwI16, TokenType::KwI32, TokenType::KwI64,
        TokenType::KwU8, TokenType::KwU16, TokenType::KwU32, TokenType::KwU64,
        TokenType::KwF32, TokenType::KwF64, TokenType::KwString, TokenType::KwBytes, TokenType::KwVoid
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp);
    }
}

// =============================================================================
// Identifiers
// =============================================================================

TEST(LexerTest, SimpleIdentifier) {
    Lexer lexer("foo");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Identifier);
    EXPECT_EQ(token->m_value, "foo");
}

TEST(LexerTest, IdentifierWithUnderscore) {
    Lexer lexer("foo_bar _private __dunder__");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::Identifier);
    EXPECT_EQ(t1->m_value, "foo_bar");

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::Identifier);
    EXPECT_EQ(t2->m_value, "_private");

    auto t3 = lexer.next_token();
    EXPECT_EQ(t3->m_type, TokenType::Identifier);
    EXPECT_EQ(t3->m_value, "__dunder__");
}

TEST(LexerTest, IdentifierWithNumbers) {
    Lexer lexer("foo123 bar2baz");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::Identifier);
    EXPECT_EQ(t1->m_value, "foo123");

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::Identifier);
    EXPECT_EQ(t2->m_value, "bar2baz");
}

TEST(LexerTest, IdentifierVsKeyword) {
    Lexer lexer("struct StructName");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::KwStruct);

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::Identifier);
    EXPECT_EQ(t2->m_value, "StructName");
}

// =============================================================================
// Integer Literals
// =============================================================================

TEST(LexerTest, DecimalInteger) {
    Lexer lexer("42");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Integer);
    EXPECT_EQ(token->m_value, "42");
    EXPECT_EQ(token->m_int_value, 42);
}

TEST(LexerTest, ZeroInteger) {
    Lexer lexer("0");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Integer);
    EXPECT_EQ(token->m_int_value, 0);
}

TEST(LexerTest, HexInteger) {
    Lexer lexer("0xFF 0x01 0xDEAD");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::Integer);
    EXPECT_EQ(t1->m_int_value, 0xFF);

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::Integer);
    EXPECT_EQ(t2->m_int_value, 0x01);

    auto t3 = lexer.next_token();
    EXPECT_EQ(t3->m_type, TokenType::Integer);
    EXPECT_EQ(t3->m_int_value, 0xDEAD);
}

TEST(LexerTest, HexIntegerUpperCase) {
    Lexer lexer("0XAB");
    auto token = lexer.next_token();
    EXPECT_EQ(token->m_type, TokenType::Integer);
    EXPECT_EQ(token->m_int_value, 0xAB);
}

// Boundary: the largest representable literals lex to exactly INT64_MAX.
TEST(LexerTest, DecimalIntegerMaxInt64) {
    Lexer lexer("9223372036854775807");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_type, TokenType::Integer);
    EXPECT_EQ(token->m_int_value, 9223372036854775807LL);
}

TEST(LexerTest, HexIntegerMaxInt64) {
    Lexer lexer("0x7FFFFFFFFFFFFFFF");
    auto token = lexer.next_token();
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->m_int_value, 0x7FFFFFFFFFFFFFFFLL);
}

// An out-of-range literal must raise a LexerError (with a source location), not
// let std::stoll's std::out_of_range escape and crash songc.
TEST(LexerTest, DecimalIntegerOverflowThrowsLexerError) {
    Lexer lexer("9223372036854775808");  // INT64_MAX + 1
    EXPECT_THROW(lexer.next_token(), LexerError);
}

TEST(LexerTest, HexIntegerOverflowThrowsLexerError) {
    Lexer lexer("0xFFFFFFFFFFFFFFFF");  // exceeds INT64_MAX
    EXPECT_THROW(lexer.next_token(), LexerError);
}

TEST(LexerTest, ExtremeIntegerOverflowThrowsLexerError) {
    Lexer lexer("999999999999999999999999999999");
    EXPECT_THROW(lexer.next_token(), LexerError);
}

// =============================================================================
// Comments
// =============================================================================

TEST(LexerTest, LineCommentSkipped) {
    Lexer lexer("struct // this is a comment\nPoint");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::KwStruct);

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::Identifier);
    EXPECT_EQ(t2->m_value, "Point");
}

TEST(LexerTest, DocCommentPreserved) {
    Lexer lexer("/// This is documentation\nstruct");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::DocComment);
    EXPECT_EQ(t1->m_value, "This is documentation");

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::KwStruct);
}

TEST(LexerTest, DocCommentNoSpace) {
    Lexer lexer("///NoSpace");

    auto token = lexer.next_token();
    EXPECT_EQ(token->m_type, TokenType::DocComment);
    EXPECT_EQ(token->m_value, "NoSpace");
}

TEST(LexerTest, MultipleDocComments) {
    Lexer lexer("/// Line 1\n/// Line 2\nstruct");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::DocComment);
    EXPECT_EQ(t1->m_value, "Line 1");

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::DocComment);
    EXPECT_EQ(t2->m_value, "Line 2");

    auto t3 = lexer.next_token();
    EXPECT_EQ(t3->m_type, TokenType::KwStruct);
}

// =============================================================================
// Line/Column Tracking
// =============================================================================

TEST(LexerTest, LineTracking) {
    Lexer lexer("struct\nPoint\n{\n}");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_line, 1);

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_line, 2);

    auto t3 = lexer.next_token();
    EXPECT_EQ(t3->m_line, 3);

    auto t4 = lexer.next_token();
    EXPECT_EQ(t4->m_line, 4);
}

TEST(LexerTest, ColumnTracking) {
    Lexer lexer("struct Point");

    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_column, 1);

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_column, 8);  // "struct " is 7 chars
}

// =============================================================================
// Complete Examples
// =============================================================================

TEST(LexerTest, SimpleStruct) {
    Lexer lexer("struct Point { i32 x; i32 y; }");

    std::vector<std::pair<TokenType, std::string>> expected = {
        {TokenType::KwStruct, "struct"},
        {TokenType::Identifier, "Point"},
        {TokenType::LBrace, "{"},
        {TokenType::KwI32, "i32"},
        {TokenType::Identifier, "x"},
        {TokenType::Semicolon, ";"},
        {TokenType::KwI32, "i32"},
        {TokenType::Identifier, "y"},
        {TokenType::Semicolon, ";"},
        {TokenType::RBrace, "}"},
        {TokenType::Eof, ""}
    };

    for (const auto& [exp_type, exp_value] : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp_type) << "Expected " << exp_value;
        if (exp_type != TokenType::Eof) {
            EXPECT_EQ(token->m_value, exp_value);
        }
    }
}

TEST(LexerTest, EnumWithValues) {
    Lexer lexer("enum Status { idle = 0, running = 1 }");

    std::vector<TokenType> expected = {
        TokenType::KwEnum, TokenType::Identifier, TokenType::LBrace,
        TokenType::Identifier, TokenType::Equals, TokenType::Integer, TokenType::Comma,
        TokenType::Identifier, TokenType::Equals, TokenType::Integer,
        TokenType::RBrace, TokenType::Eof
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp);
    }
}

TEST(LexerTest, MethodSignature) {
    Lexer lexer("get_pixel(i32 x, i32 y) -> u32;");

    std::vector<TokenType> expected = {
        TokenType::Identifier,  // get_pixel
        TokenType::LParen,
        TokenType::KwI32, TokenType::Identifier,  // i32 x
        TokenType::Comma,
        TokenType::KwI32, TokenType::Identifier,  // i32 y
        TokenType::RParen,
        TokenType::Arrow,
        TokenType::KwU32,
        TokenType::Semicolon,
        TokenType::Eof
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp);
    }
}

TEST(LexerTest, OptionalArrayType) {
    Lexer lexer("string[]? names;");

    std::vector<TokenType> expected = {
        TokenType::KwString, TokenType::LBracket, TokenType::RBracket,
        TokenType::Question, TokenType::Identifier, TokenType::Semicolon,
        TokenType::Eof
    };

    for (auto exp : expected) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->m_type, exp);
    }
}

TEST(LexerTest, ServiceDefinition) {
    Lexer lexer(R"(
/// Canvas factory service
service CanvasFactory {
    create(i32 width, i32 height) -> Canvas;
}
)");

    // Just verify it tokenizes without error and has expected structure
    auto t1 = lexer.next_token();
    EXPECT_EQ(t1->m_type, TokenType::DocComment);

    auto t2 = lexer.next_token();
    EXPECT_EQ(t2->m_type, TokenType::KwService);

    auto t3 = lexer.next_token();
    EXPECT_EQ(t3->m_type, TokenType::Identifier);
    EXPECT_EQ(t3->m_value, "CanvasFactory");

    // Continue until EOF
    while (true) {
        auto token = lexer.next_token();
        ASSERT_TRUE(token.has_value());
        if (token->m_type == TokenType::Eof) break;
    }
}

// =============================================================================
// Error Cases
// =============================================================================

TEST(LexerTest, InvalidCharacterThrows) {
    Lexer lexer("struct @ Point");

    lexer.next_token();  // struct

    EXPECT_THROW(lexer.next_token(), LexerError);
}

TEST(LexerTest, InvalidHexLiteralThrows) {
    Lexer lexer("0x");

    EXPECT_THROW(lexer.next_token(), LexerError);
}

TEST(LexerTest, LexerErrorHasLocation) {
    Lexer lexer("struct\n@");

    lexer.next_token();  // struct

    try {
        lexer.next_token();
        FAIL() << "Expected LexerError";
    } catch (const LexerError& e) {
        EXPECT_EQ(e.line(), 2);
        EXPECT_EQ(e.column(), 1);
    }
}

// =============================================================================
// Token to_string
// =============================================================================

TEST(LexerTest, TokenToString) {
    Token t(TokenType::Identifier, "foo", 1, 5);
    std::string str = t.to_string();
    EXPECT_TRUE(str.find("Identifier") != std::string::npos);
    EXPECT_TRUE(str.find("foo") != std::string::npos);
    EXPECT_TRUE(str.find("line=1") != std::string::npos);
}

TEST(LexerTest, IntegerTokenToString) {
    Token t(TokenType::Integer, "42", 1, 1, 42);
    std::string str = t.to_string();
    EXPECT_TRUE(str.find("Integer") != std::string::npos);
    EXPECT_TRUE(str.find("int_value=42") != std::string::npos);
}
