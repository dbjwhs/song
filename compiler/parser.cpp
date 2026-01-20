// MIT License
// Copyright (c) 2026 dbjwhs

#include "parser.hpp"

namespace song {

// ParserError implementation
ParserError::ParserError(const std::string& message, size_t line, size_t column)
    : std::runtime_error("Parse error at line " + std::to_string(line) +
                         ", column " + std::to_string(column) + ": " + message),
      m_line(line), m_column(column) {}

// Parser implementation
Parser::Parser(std::string_view input)
    : m_lexer(input) {
    advance();  // Prime the parser with first token
}

void Parser::advance() {
    // Skip doc comments and accumulate them
    while (true) {
        m_current_token = m_lexer.next_token();
        if (m_current_token && m_current_token->m_type == TokenType::DocComment) {
            if (!m_pending_doc.empty()) {
                m_pending_doc += "\n";
            }
            m_pending_doc += m_current_token->m_value;
        } else {
            break;
        }
    }
}

bool Parser::check(TokenType type) const {
    return m_current_token && m_current_token->m_type == type;
}

bool Parser::at_end() const {
    return !m_current_token || m_current_token->m_type == TokenType::Eof;
}

Token Parser::expect(TokenType type, const std::string& msg) {
    if (!check(type)) {
        size_t line = m_current_token ? m_current_token->m_line : 0;
        size_t col = m_current_token ? m_current_token->m_column : 0;
        std::string actual = m_current_token ? token_type_to_string(m_current_token->m_type) : "EOF";
        throw ParserError(msg + " (got " + actual + ")", line, col);
    }
    Token token = *m_current_token;
    advance();
    return token;
}

std::string Parser::take_doc() {
    std::string doc = std::move(m_pending_doc);
    m_pending_doc.clear();
    return doc;
}

compiler::SourceLoc Parser::current_loc() {
    return compiler::SourceLoc{
        m_current_token ? static_cast<int>(m_current_token->m_line) : 0,
        m_current_token ? static_cast<int>(m_current_token->m_column) : 0,
        ""
    };
}

compiler::AST Parser::parse() {
    compiler::AST ast;

    // Parse namespace declaration
    compiler::Namespace ns = parse_namespace();

    // Parse definitions until EOF
    while (!at_end()) {
        std::string doc = take_doc();

        if (check(TokenType::KwStruct)) {
            auto def = parse_struct();
            if (def.doc.empty()) def.doc = doc;
            ns.structs.push_back(std::move(def));
        } else if (check(TokenType::KwEnum)) {
            auto def = parse_enum(false);
            if (def.doc.empty()) def.doc = doc;
            ns.enums.push_back(std::move(def));
        } else if (check(TokenType::KwFlags)) {
            auto def = parse_enum(true);
            if (def.doc.empty()) def.doc = doc;
            ns.enums.push_back(std::move(def));
        } else if (check(TokenType::KwClass)) {
            auto def = parse_class();
            if (def.doc.empty()) def.doc = doc;
            ns.classes.push_back(std::move(def));
        } else if (check(TokenType::KwService)) {
            auto def = parse_service();
            if (def.doc.empty()) def.doc = doc;
            ns.services.push_back(std::move(def));
        } else if (check(TokenType::KwError)) {
            auto def = parse_error();
            if (def.doc.empty()) def.doc = doc;
            ns.errors.push_back(std::move(def));
        } else {
            throw ParserError("Expected definition (struct, enum, flags, class, service, or error)",
                              m_current_token->m_line, m_current_token->m_column);
        }
    }

    ast.namespaces.push_back(std::move(ns));
    return ast;
}

compiler::Namespace Parser::parse_namespace() {
    compiler::Namespace ns;
    ns.loc = current_loc();

    expect(TokenType::KwNamespace, "Expected 'namespace'");
    Token name = expect(TokenType::Identifier, "Expected namespace name");
    ns.name = name.m_value;
    expect(TokenType::Semicolon, "Expected ';' after namespace");

    return ns;
}

compiler::StructDef Parser::parse_struct() {
    compiler::StructDef def;
    def.loc = current_loc();
    def.doc = take_doc();

    expect(TokenType::KwStruct, "Expected 'struct'");
    Token name = expect(TokenType::Identifier, "Expected struct name");
    def.name = name.m_value;

    // Optional inheritance
    if (check(TokenType::Colon)) {
        advance();
        Token base = expect(TokenType::Identifier, "Expected base type name");
        def.base = base.m_value;
    }

    expect(TokenType::LBrace, "Expected '{' after struct name");

    // Parse fields
    while (!check(TokenType::RBrace) && !at_end()) {
        def.fields.push_back(parse_field());
    }

    expect(TokenType::RBrace, "Expected '}' at end of struct");

    return def;
}

compiler::EnumDef Parser::parse_enum(bool is_flags) {
    compiler::EnumDef def;
    def.loc = current_loc();
    def.doc = take_doc();
    def.is_flags = is_flags;

    if (is_flags) {
        expect(TokenType::KwFlags, "Expected 'flags'");
    } else {
        expect(TokenType::KwEnum, "Expected 'enum'");
    }

    Token name = expect(TokenType::Identifier, "Expected enum name");
    def.name = name.m_value;

    expect(TokenType::LBrace, "Expected '{' after enum name");

    // Parse enum items
    if (!check(TokenType::RBrace)) {
        def.items.push_back(parse_enum_item());

        while (check(TokenType::Comma)) {
            advance();  // consume comma
            if (check(TokenType::RBrace)) break;  // trailing comma
            def.items.push_back(parse_enum_item());
        }
    }

    expect(TokenType::RBrace, "Expected '}' at end of enum");

    return def;
}

compiler::ClassDef Parser::parse_class() {
    compiler::ClassDef def;
    def.loc = current_loc();
    def.doc = take_doc();

    expect(TokenType::KwClass, "Expected 'class'");
    Token name = expect(TokenType::Identifier, "Expected class name");
    def.name = name.m_value;

    // Optional inheritance
    if (check(TokenType::Colon)) {
        advance();
        Token base = expect(TokenType::Identifier, "Expected base type name");
        def.base = base.m_value;
    }

    expect(TokenType::LBrace, "Expected '{' after class name");

    // Parse members (properties, constructors, methods)
    while (!check(TokenType::RBrace) && !at_end()) {
        std::string member_doc = take_doc();

        // Check for readonly property
        if (check(TokenType::KwReadonly)) {
            auto prop = parse_property();
            if (prop.doc.empty()) prop.doc = member_doc;
            def.properties.push_back(std::move(prop));
        }
        // Check for constructor (name matches class name, followed by '(')
        else if (check(TokenType::Identifier) && m_current_token->m_value == def.name) {
            // Need to peek ahead to distinguish constructor from method
            // Constructor: ClassName(params);
            // Method: identifier(params) -> type;
            // Save position - actually we can check: if name == class name, it's constructor
            auto ctor = parse_constructor(def.name);
            if (ctor.doc.empty()) ctor.doc = member_doc;
            def.constructors.push_back(std::move(ctor));
        }
        // Check for stream/optional method
        else if (check(TokenType::KwStream) || check(TokenType::KwOptional)) {
            auto method = parse_method();
            if (method.doc.empty()) method.doc = member_doc;
            def.methods.push_back(std::move(method));
        }
        // Could be a property (type name;) or method (name(params) -> type;)
        // Methods start with identifier followed by '('
        // Properties start with type followed by identifier followed by ';'
        else if (check(TokenType::Identifier)) {
            // Check if this is a method: identifier followed by '('
            // We need to check if current identifier is followed by '(' to distinguish
            // Method: method_name(params) -> type;
            // Property: TypeName property_name;
            //
            // Save current state and peek ahead
            std::string first_ident = m_current_token->m_value;
            advance();  // consume first identifier

            if (check(TokenType::LParen)) {
                // It's a method - we already consumed the name, now parse the rest
                compiler::Method method;
                method.loc = current_loc();
                method.doc = member_doc;
                method.name = first_ident;

                expect(TokenType::LParen, "Expected '(' after method name");

                // Parse parameters
                if (!check(TokenType::RParen)) {
                    method.params.push_back(parse_param());
                    while (check(TokenType::Comma)) {
                        advance();
                        method.params.push_back(parse_param());
                    }
                }

                expect(TokenType::RParen, "Expected ')' after parameters");
                expect(TokenType::Arrow, "Expected '->' after parameters");

                method.return_type = parse_type();

                // Check for throws clause
                if (check(TokenType::KwThrows)) {
                    advance();
                    Token error_type = expect(TokenType::Identifier, "Expected error type");
                    method.throws.push_back(error_type.m_value);

                    while (check(TokenType::Comma)) {
                        advance();
                        Token next_error = expect(TokenType::Identifier, "Expected error type");
                        method.throws.push_back(next_error.m_value);
                    }
                }

                expect(TokenType::Semicolon, "Expected ';' after method");
                def.methods.push_back(std::move(method));
            } else {
                // It's a property - first_ident was the type name
                // Now we need the property name
                Token prop_name = expect(TokenType::Identifier, "Expected property name");

                compiler::Property prop;
                prop.loc = current_loc();
                prop.doc = member_doc;
                prop.name = prop_name.m_value;
                prop.type.base = first_ident;  // user-defined type
                prop.readonly = false;

                // Check for array brackets
                while (check(TokenType::LBracket)) {
                    advance();
                    expect(TokenType::RBracket, "Expected ']' after '['");
                    prop.type.is_array = true;
                    prop.type.array_dimensions++;
                }

                // Check for optional marker
                if (check(TokenType::Question)) {
                    advance();
                    prop.type.is_optional = true;
                }

                expect(TokenType::Semicolon, "Expected ';' after property");
                def.properties.push_back(std::move(prop));
            }
        }
        // Primitive type property
        else if (is_primitive_type()) {
            auto type = parse_type();
            Token id = expect(TokenType::Identifier, "Expected property name");

            compiler::Property prop;
            prop.loc = current_loc();
            prop.doc = member_doc;
            prop.name = id.m_value;
            prop.type = type;
            prop.readonly = false;

            expect(TokenType::Semicolon, "Expected ';' after property");
            def.properties.push_back(std::move(prop));
        }
        else {
            throw ParserError("Expected property, constructor, or method in class",
                              m_current_token->m_line, m_current_token->m_column);
        }
    }

    expect(TokenType::RBrace, "Expected '}' at end of class");

    return def;
}

compiler::ServiceDef Parser::parse_service() {
    compiler::ServiceDef def;
    def.loc = current_loc();
    def.doc = take_doc();

    expect(TokenType::KwService, "Expected 'service'");
    Token name = expect(TokenType::Identifier, "Expected service name");
    def.name = name.m_value;

    expect(TokenType::LBrace, "Expected '{' after service name");

    // Parse methods
    while (!check(TokenType::RBrace) && !at_end()) {
        std::string method_doc = take_doc();
        auto method = parse_method();
        if (method.doc.empty()) method.doc = method_doc;
        def.methods.push_back(std::move(method));
    }

    expect(TokenType::RBrace, "Expected '}' at end of service");

    return def;
}

compiler::ErrorDef Parser::parse_error() {
    compiler::ErrorDef def;
    def.loc = current_loc();
    def.doc = take_doc();

    expect(TokenType::KwError, "Expected 'error'");
    Token name = expect(TokenType::Identifier, "Expected error name");
    def.name = name.m_value;

    // Optional inheritance
    if (check(TokenType::Colon)) {
        advance();
        Token base = expect(TokenType::Identifier, "Expected base error type name");
        def.base = base.m_value;
    }

    expect(TokenType::LBrace, "Expected '{' after error name");

    // Parse fields
    while (!check(TokenType::RBrace) && !at_end()) {
        def.fields.push_back(parse_field());
    }

    expect(TokenType::RBrace, "Expected '}' at end of error");

    return def;
}

compiler::Field Parser::parse_field() {
    compiler::Field field;
    field.loc = current_loc();
    field.doc = take_doc();

    field.type = parse_type();
    Token name = expect(TokenType::Identifier, "Expected field name");
    field.name = name.m_value;
    expect(TokenType::Semicolon, "Expected ';' after field");

    return field;
}

compiler::EnumItem Parser::parse_enum_item() {
    compiler::EnumItem item;
    item.loc = current_loc();
    item.doc = take_doc();

    Token name = expect(TokenType::Identifier, "Expected enum item name");
    item.name = name.m_value;

    // Optional value
    if (check(TokenType::Equals)) {
        advance();
        Token value = expect(TokenType::Integer, "Expected integer value");
        item.value = value.m_int_value;
    }

    return item;
}

compiler::Property Parser::parse_property() {
    compiler::Property prop;
    prop.loc = current_loc();
    prop.doc = take_doc();

    // Check for readonly
    if (check(TokenType::KwReadonly)) {
        advance();
        prop.readonly = true;
    }

    prop.type = parse_type();
    Token name = expect(TokenType::Identifier, "Expected property name");
    prop.name = name.m_value;
    expect(TokenType::Semicolon, "Expected ';' after property");

    return prop;
}

compiler::Constructor Parser::parse_constructor([[maybe_unused]] const std::string& class_name) {
    compiler::Constructor ctor;
    ctor.loc = current_loc();
    ctor.doc = take_doc();

    expect(TokenType::Identifier, "Expected constructor name");  // Already verified it matches class_name
    expect(TokenType::LParen, "Expected '(' after constructor name");

    // Parse parameters
    if (!check(TokenType::RParen)) {
        ctor.params.push_back(parse_param());
        while (check(TokenType::Comma)) {
            advance();
            ctor.params.push_back(parse_param());
        }
    }

    expect(TokenType::RParen, "Expected ')' after parameters");
    expect(TokenType::Semicolon, "Expected ';' after constructor");

    return ctor;
}

compiler::Method Parser::parse_method() {
    compiler::Method method;
    method.loc = current_loc();
    method.doc = take_doc();

    // Check for optional or stream modifiers
    if (check(TokenType::KwOptional)) {
        advance();
        method.is_optional = true;
    } else if (check(TokenType::KwStream)) {
        advance();
        method.is_stream = true;
    }

    Token name = expect(TokenType::Identifier, "Expected method name");
    method.name = name.m_value;

    expect(TokenType::LParen, "Expected '(' after method name");

    // Parse parameters
    if (!check(TokenType::RParen)) {
        method.params.push_back(parse_param());
        while (check(TokenType::Comma)) {
            advance();
            method.params.push_back(parse_param());
        }
    }

    expect(TokenType::RParen, "Expected ')' after parameters");
    expect(TokenType::Arrow, "Expected '->' after parameters");

    method.return_type = parse_type();

    // Check for throws clause
    if (check(TokenType::KwThrows)) {
        advance();
        Token error_type = expect(TokenType::Identifier, "Expected error type");
        method.throws.push_back(error_type.m_value);

        while (check(TokenType::Comma)) {
            advance();
            Token next_error = expect(TokenType::Identifier, "Expected error type");
            method.throws.push_back(next_error.m_value);
        }
    }

    expect(TokenType::Semicolon, "Expected ';' after method");

    return method;
}

compiler::Param Parser::parse_param() {
    compiler::Param param;
    param.loc = current_loc();

    param.type = parse_type();
    Token name = expect(TokenType::Identifier, "Expected parameter name");
    param.name = name.m_value;

    return param;
}

compiler::Type Parser::parse_type() {
    compiler::Type type;

    // Parse base type
    if (is_primitive_type()) {
        type.base = token_to_primitive();
        advance();
    } else if (check(TokenType::Identifier)) {
        type.base = m_current_token->m_value;
        advance();
    } else {
        throw ParserError("Expected type",
                          m_current_token ? m_current_token->m_line : 0,
                          m_current_token ? m_current_token->m_column : 0);
    }

    // Check for array brackets
    while (check(TokenType::LBracket)) {
        advance();
        expect(TokenType::RBracket, "Expected ']' after '['");
        type.is_array = true;
        type.array_dimensions++;
    }

    // Check for optional marker
    if (check(TokenType::Question)) {
        advance();
        type.is_optional = true;
    }

    return type;
}

bool Parser::is_type_start() const {
    return is_primitive_type() || check(TokenType::Identifier);
}

bool Parser::is_primitive_type() const {
    if (!m_current_token) return false;
    switch (m_current_token->m_type) {
        case TokenType::KwBool:
        case TokenType::KwI8:
        case TokenType::KwI16:
        case TokenType::KwI32:
        case TokenType::KwI64:
        case TokenType::KwU8:
        case TokenType::KwU16:
        case TokenType::KwU32:
        case TokenType::KwU64:
        case TokenType::KwF32:
        case TokenType::KwF64:
        case TokenType::KwString:
        case TokenType::KwBytes:
        case TokenType::KwVoid:
            return true;
        default:
            return false;
    }
}

compiler::PrimitiveType Parser::token_to_primitive() const {
    switch (m_current_token->m_type) {
        case TokenType::KwBool:   return compiler::PrimitiveType::bool_;
        case TokenType::KwI8:     return compiler::PrimitiveType::i8;
        case TokenType::KwI16:    return compiler::PrimitiveType::i16;
        case TokenType::KwI32:    return compiler::PrimitiveType::i32;
        case TokenType::KwI64:    return compiler::PrimitiveType::i64;
        case TokenType::KwU8:     return compiler::PrimitiveType::u8;
        case TokenType::KwU16:    return compiler::PrimitiveType::u16;
        case TokenType::KwU32:    return compiler::PrimitiveType::u32;
        case TokenType::KwU64:    return compiler::PrimitiveType::u64;
        case TokenType::KwF32:    return compiler::PrimitiveType::f32;
        case TokenType::KwF64:    return compiler::PrimitiveType::f64;
        case TokenType::KwString: return compiler::PrimitiveType::string;
        case TokenType::KwBytes:  return compiler::PrimitiveType::bytes;
        case TokenType::KwVoid:   return compiler::PrimitiveType::void_;
        default:
            throw ParserError("Not a primitive type",
                              m_current_token->m_line, m_current_token->m_column);
    }
}

} // namespace song
