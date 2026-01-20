// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "ast.hpp"
#include <string>
#include <sstream>

namespace song::compiler {

// Code generator for Song IDL
class CodeGenerator {
    std::string m_namespace;  // Current namespace being generated

public:
    // Generate complete header (all-in-one for simplicity)
    std::string generate_header(const Namespace& ns);

    // Generate types header (structs, enums)
    std::string generate_types_header(const Namespace& ns);

    // Generate serialization implementation
    std::string generate_wire_impl(const Namespace& ns);

    // Generate client proxies header
    std::string generate_client_header(const Namespace& ns);

    // Generate server skeletons header
    std::string generate_server_header(const Namespace& ns);

private:
    // Struct generation
    std::string generate_struct_def(const StructDef& s);
    std::string generate_struct_encode(const StructDef& s);
    std::string generate_struct_decode(const StructDef& s);

    // Enum generation
    std::string generate_enum_def(const EnumDef& e);

    // Service generation
    std::string generate_service_ids(const Namespace& ns);
    std::string generate_service_proxy(const ServiceDef& s);
    std::string generate_service_interface(const ServiceDef& s);
    std::string generate_service_dispatcher(const ServiceDef& s);

    // Method helpers
    std::string generate_method_signature(const Method& m, bool is_interface);
    std::string generate_method_params(const std::vector<Param>& params);
    std::string generate_encode_params(const std::vector<Param>& params);
    std::string generate_decode_params(const std::vector<Param>& params);

    // Type conversion helpers
    std::string type_to_cpp(const Type& t);
    std::string type_to_param(const Type& t, const std::string& name);
    std::string encode_call(const Type& t, const std::string& expr);
    std::string decode_call(const Type& t);
};

} // namespace song::compiler
