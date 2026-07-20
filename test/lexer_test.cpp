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

// A byte >= 0x80 is a negative value in a signed char; passing it to std::isXXX
// without an unsigned cast is undefined behavior. The lexer must classify it
// safely and raise a clean LexerError rather than invoking UB or crashing.
TEST(LexerTest, HighBitByteThrowsLexerErrorNotUB) {
    std::string src = "struct ";
    src.push_back(static_cast<char>(0x80));
    Lexer lexer(src);

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

// =============================================================================
// Non-ASCII / High-Byte Input (defined, deterministic error behavior)
// =============================================================================

// A raw high byte (0x80) is not whitespace, punctuation, digit, or an
// identifier start, so it must surface as a located LexerError rather than
// silently matching or crashing on the ctype UB path.
TEST(LexerTest, HighByte0x80ThrowsLexerError) {
  std::string input(1, static_cast<char>(0x80));
  Lexer lexer(input);
  try {
    lexer.next_token();
    FAIL() << "Expected LexerError";
  } catch (const LexerError& e) {
    EXPECT_EQ(e.line(), 1u);
    EXPECT_EQ(e.column(), 1u);
  }
}

TEST(LexerTest, HighByte0xFFThrowsLexerError) {
  std::string input(1, static_cast<char>(0xFF));
  Lexer lexer(input);
  EXPECT_THROW(lexer.next_token(), LexerError);
}

// A UTF-8 multibyte sequence embedded in identifier position: the ASCII prefix
// tokenizes as an Identifier and the first non-ASCII byte then raises a located
// LexerError. Documents that identifiers are ASCII-only.
TEST(LexerTest, Utf8IdentifierBytesTokenizePrefixThenThrow) {
  std::string input = "caf\xC3\xA9";  // "cafe" with a UTF-8 e-acute
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Identifier);
  EXPECT_EQ(t1->m_value, "caf");

  try {
    lexer.next_token();
    FAIL() << "Expected LexerError at first non-ASCII byte";
  } catch (const LexerError& e) {
    EXPECT_EQ(e.line(), 1u);
    EXPECT_EQ(e.column(), 4u);
  }
}

// Doc-comment bodies are copied byte-for-byte until end of line, so raw UTF-8
// bytes round-trip unchanged (the ctype path is not involved here).
TEST(LexerTest, DocCommentPreservesUtf8Bytes) {
  std::string input = "/// \xE2\x9C\x93 done";  // U+2713 CHECK MARK
  Lexer lexer(input);

  auto token = lexer.next_token();
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->m_type, TokenType::DocComment);
  EXPECT_EQ(token->m_value, std::string("\xE2\x9C\x93 done"));

  auto eof = lexer.next_token();
  ASSERT_TRUE(eof.has_value());
  EXPECT_EQ(eof->m_type, TokenType::Eof);
}

// =============================================================================
// CRLF / Carriage-Return Handling
// =============================================================================

// Current behavior: a doc comment on CRLF input retains the trailing '\r' in its
// value (lex_doc_comment collects until '\n' only). This locks that behavior so
// any future CRLF normalization is an intentional change.
TEST(LexerTest, DocCommentCrlfRetainsTrailingCr) {
  std::string input = "/// hello\r\nstruct";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::DocComment);
  EXPECT_EQ(t1->m_value, std::string("hello\r"));

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::KwStruct);
  EXPECT_EQ(t2->m_line, 2u);
}

// A regular (non-doc) line comment discards the trailing '\r' along with the
// rest of the line, so the CR never leaks into the following token.
TEST(LexerTest, LineCommentCrlfDoesNotLeakCarriageReturn) {
  std::string input = "struct // comment\r\nPoint";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::KwStruct);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Identifier);
  EXPECT_EQ(t2->m_value, "Point");
  EXPECT_EQ(t2->m_line, 2u);
}

// A lone '\r' is treated as ordinary whitespace and does NOT advance the line
// counter (only '\n' does). Two identifiers separated by a bare CR stay on
// line 1.
TEST(LexerTest, LoneCarriageReturnSeparatesIdentifiersSameLine) {
  std::string input = "a\rb";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Identifier);
  EXPECT_EQ(t1->m_value, "a");
  EXPECT_EQ(t1->m_line, 1u);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Identifier);
  EXPECT_EQ(t2->m_value, "b");
  EXPECT_EQ(t2->m_line, 1u);
}

TEST(LexerTest, MultipleDocCommentsCrlfRetainCr) {
  std::string input = "/// L1\r\n/// L2\r\n";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::DocComment);
  EXPECT_EQ(t1->m_value, std::string("L1\r"));

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::DocComment);
  EXPECT_EQ(t2->m_value, std::string("L2\r"));

  auto t3 = lexer.next_token();
  ASSERT_TRUE(t3.has_value());
  EXPECT_EQ(t3->m_type, TokenType::Eof);
}

// =============================================================================
// Slash / Minus Error Paths
// =============================================================================

// A single '/' not followed by another '/' is not a comment start and falls
// through to the unexpected-character error, with the column at the slash.
TEST(LexerTest, LoneSlashThrowsWithColumn) {
  std::string input = "a / b";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Identifier);
  EXPECT_EQ(t1->m_value, "a");

  try {
    lexer.next_token();
    FAIL() << "Expected LexerError at '/'";
  } catch (const LexerError& e) {
    EXPECT_EQ(e.line(), 1u);
    EXPECT_EQ(e.column(), 3u);
  }
}

TEST(LexerTest, LeadingSlashThrowsAtColumnOne) {
  std::string input = "/x";
  Lexer lexer(input);

  try {
    lexer.next_token();
    FAIL() << "Expected LexerError at '/'";
  } catch (const LexerError& e) {
    EXPECT_EQ(e.line(), 1u);
    EXPECT_EQ(e.column(), 1u);
  }
}

// Negative integer literals are not supported: after the '=' the lexer rejects
// the lone '-' (a '-' is only meaningful as part of '->').
TEST(LexerTest, NegativeLiteralNotSupported) {
  std::string input = "x = -1;";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Identifier);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Equals);

  try {
    lexer.next_token();
    FAIL() << "Expected LexerError at '-'";
  } catch (const LexerError& e) {
    EXPECT_EQ(e.line(), 1u);
    EXPECT_EQ(e.column(), 5u);
  }
}

// A lone '-' throws, but a complete '->' at end of input yields a single Arrow
// followed by Eof (regression guard for peek_next at the input boundary).
TEST(LexerTest, LoneMinusThrowsArrowAtEndSucceeds) {
  {
    std::string input = "-";
    Lexer lexer(input);
    EXPECT_THROW(lexer.next_token(), LexerError);
  }
  {
    std::string input = "->";
    Lexer lexer(input);
    auto t1 = lexer.next_token();
    ASSERT_TRUE(t1.has_value());
    EXPECT_EQ(t1->m_type, TokenType::Arrow);

    auto t2 = lexer.next_token();
    ASSERT_TRUE(t2.has_value());
    EXPECT_EQ(t2->m_type, TokenType::Eof);
  }
}

// =============================================================================
// EOF Idempotency and Position Tracking
// =============================================================================

// Calling next_token() repeatedly after Eof keeps returning Eof (never
// std::nullopt, never advancing) with a stable line/column.
TEST(LexerTest, RepeatedNextTokenPastEofReturnsStableEof) {
  std::string input = "x";
  Lexer lexer(input);

  auto ident = lexer.next_token();
  ASSERT_TRUE(ident.has_value());
  EXPECT_EQ(ident->m_type, TokenType::Identifier);

  size_t eof_line = 0;
  size_t eof_column = 0;
  for (int ndx = 0; ndx < 3; ++ndx) {
    auto eof = lexer.next_token();
    ASSERT_TRUE(eof.has_value());
    EXPECT_EQ(eof->m_type, TokenType::Eof);
    if (ndx == 0) {
      eof_line = eof->m_line;
      eof_column = eof->m_column;
    } else {
      EXPECT_EQ(eof->m_line, eof_line);
      EXPECT_EQ(eof->m_column, eof_column);
    }
  }
}

// Column resets to 1 at the start of each line across multiple newlines.
TEST(LexerTest, ColumnResetsToOneAfterEachNewline) {
  std::string input = "a\nbb\nccc";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_line, 1u);
  EXPECT_EQ(t1->m_column, 1u);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_line, 2u);
  EXPECT_EQ(t2->m_column, 1u);

  auto t3 = lexer.next_token();
  ASSERT_TRUE(t3.has_value());
  EXPECT_EQ(t3->m_line, 3u);
  EXPECT_EQ(t3->m_column, 1u);
}

// A token following a skipped '//' line comment reports the correct line and a
// column of 1 (the columns consumed skipping the comment do not leak forward).
TEST(LexerTest, TokenPositionAfterSkippedLineComment) {
  std::string input = "struct // c\nPoint";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::KwStruct);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Identifier);
  EXPECT_EQ(t2->m_value, "Point");
  EXPECT_EQ(t2->m_line, 2u);
  EXPECT_EQ(t2->m_column, 1u);
}

// The Eof token's column reflects the trailing whitespace that was consumed.
TEST(LexerTest, EofColumnAfterTrailingSpace) {
  std::string input = "ab ";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Identifier);
  EXPECT_EQ(t1->m_column, 1u);

  auto eof = lexer.next_token();
  ASSERT_TRUE(eof.has_value());
  EXPECT_EQ(eof->m_type, TokenType::Eof);
  EXPECT_EQ(eof->m_column, 4u);
}

// =============================================================================
// Adjacent-Token Splitting
// =============================================================================

// A number immediately followed by letters splits into an Integer then an
// Identifier (no whitespace required, no lexical error). Locks the contract.
TEST(LexerTest, NumberFollowedByLettersSplits) {
  std::string input = "123abc";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Integer);
  EXPECT_EQ(t1->m_value, "123");
  EXPECT_EQ(t1->m_int_value, 123);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Identifier);
  EXPECT_EQ(t2->m_value, "abc");
}

// A hex literal stops at the first non-hex character; a trailing 'g' becomes a
// separate identifier.
TEST(LexerTest, HexLiteralFollowedByNonHexLetterSplits) {
  std::string input = "0xFFg";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::Integer);
  EXPECT_EQ(t1->m_value, "0xFF");
  EXPECT_EQ(t1->m_int_value, 0xFF);

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Identifier);
  EXPECT_EQ(t2->m_value, "g");
}

// '0x' with no following hex digit (here a non-hex letter) hits the
// missing-digits branch and throws.
TEST(LexerTest, HexPrefixWithoutDigitsThrows) {
  std::string input = "0xG";
  Lexer lexer(input);
  EXPECT_THROW(lexer.next_token(), LexerError);
}

// Leading zeros in a decimal literal are not interpreted as octal: 007 == 7.
TEST(LexerTest, LeadingZeroDecimalIsNotOctal) {
  std::string input = "007";
  Lexer lexer(input);

  auto token = lexer.next_token();
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->m_type, TokenType::Integer);
  EXPECT_EQ(token->m_value, "007");
  EXPECT_EQ(token->m_int_value, 7);
}

// =============================================================================
// Doc-Comment Boundary Shapes
// =============================================================================

// An empty doc comment body yields a DocComment with an empty value.
TEST(LexerTest, DocCommentEmptyBody) {
  std::string input = "///\nstruct";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::DocComment);
  EXPECT_EQ(t1->m_value, "");

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::KwStruct);
  EXPECT_EQ(t2->m_line, 2u);
}

// Only three slashes start the doc comment; a fourth slash is body content.
TEST(LexerTest, DocCommentFourSlashesFourthIsContent) {
  std::string input = "////text";
  Lexer lexer(input);

  auto token = lexer.next_token();
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->m_type, TokenType::DocComment);
  EXPECT_EQ(token->m_value, "/text");
}

// Exactly one leading space is stripped; a second space is preserved.
TEST(LexerTest, DocCommentStripsExactlyOneLeadingSpace) {
  std::string input = "///  double";
  Lexer lexer(input);

  auto token = lexer.next_token();
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->m_type, TokenType::DocComment);
  EXPECT_EQ(token->m_value, " double");
}

// A doc comment with no trailing newline (at EOF) still captures its body,
// then yields Eof.
TEST(LexerTest, DocCommentAtEofWithoutNewline) {
  std::string input = "/// tail";
  Lexer lexer(input);

  auto t1 = lexer.next_token();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->m_type, TokenType::DocComment);
  EXPECT_EQ(t1->m_value, "tail");

  auto t2 = lexer.next_token();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->m_type, TokenType::Eof);
}
