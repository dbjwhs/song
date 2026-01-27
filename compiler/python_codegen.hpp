// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "ast.hpp"
#include <string>

namespace song::compiler {

// Python code generator for Song IDL
class PythonCodeGenerator {
    std::string m_namespace;  // Current namespace being generated

public:
    // Generate complete Python module (client + types)
    std::string generate_module(const Namespace& ns);

    // Generate just the client proxy classes
    std::string generate_client(const Namespace& ns);

private:
    // Type conversion helpers
    std::string type_to_python(const Type& t);
    std::string type_to_type_hint(const Type& t);
    std::string encode_call(const Type& t, const std::string& expr);
    std::string decode_call(const Type& t);

    // Struct generation
    std::string generate_struct_def(const StructDef& s);

    // Enum generation
    std::string generate_enum_def(const EnumDef& e);

    // Service generation
    std::string generate_service_ids(const Namespace& ns);
    std::string generate_service_proxy(const ServiceDef& s);

    // Method helpers
    std::string generate_method_params(const std::vector<Param>& params);
    std::string generate_type_hints(const std::vector<Param>& params);
};

} // namespace song::compiler
