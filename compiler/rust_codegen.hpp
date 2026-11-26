// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "ast.hpp"
#include <map>
#include <string>

namespace song::compiler {

// Rust code generator for Song IDL.
//
// One namespace becomes one module file (<namespace>.rs) for a crate that
// depends on the `song` runtime crate (rust/): structs and enums with their
// codecs, service and method ids, a client proxy per service, a service
// trait, dispatch functions and a register function.
//
// Throws std::runtime_error for IDL the Rust target cannot express (a type
// named like a Rust prelude type, a reference to a class or error type).
class RustCodeGenerator {
public:
    // Generate the complete module (types, client and service side)
    std::string generate_module(const Namespace& ns);

private:
    enum class UserKind { enum_, flags, struct_ };

    // An expression to encode: `text` is a reference to the value when
    // is_ref, else a place holding it.
    struct Expr {
        std::string text;
        bool is_ref;
    };

    std::map<std::string, UserKind> m_kinds;

    void index_types(const Namespace& ns);
    UserKind kind_of(const std::string& name) const;

    // Types
    bool is_void(const Type& t) const;
    bool by_value(const Type& t) const;
    std::string base_type(const Type& t) const;
    std::string owned_type(const Type& t) const;
    std::string param_type(const Type& t) const;

    // Codecs
    std::string encode(const Type& t, const Expr& e, const std::string& buf,
                       const std::string& indent, bool param_mode = false) const;
    std::string encode_fn(const Type& t, const std::string& indent) const;
    std::string decode(const Type& t, const std::string& buf) const;
    std::string decode_fn(const Type& t) const;

    // Definitions
    std::string generate_enum(const EnumDef& e) const;
    std::string generate_flags(const EnumDef& e) const;
    std::string generate_struct(const StructDef& s) const;
    std::string generate_service_ids(const Namespace& ns) const;
    std::string generate_proxy(const ServiceDef& s) const;
    std::string generate_trait(const ServiceDef& s) const;
    std::string generate_dispatch(const ServiceDef& s) const;
    std::string generate_register(const ServiceDef& s) const;

    // Method helpers
    std::string method_params(const Method& m) const;
    std::string pass_arg(const Param& p) const;
};

} // namespace song::compiler
