// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "ast.hpp"
#include <string>
#include <vector>
#include <optional>

namespace song::compiler {

// Represents a method signature extracted from existing implementation
struct ExistingMethod {
    std::string name;
    std::string return_type;
    std::string params;
    int line_number;
};

// Represents a struct field extracted from existing header
struct ExistingField {
    std::string name;
    std::string type;
};

// Represents a struct extracted from existing header
struct ExistingStruct {
    std::string name;
    std::vector<ExistingField> fields;
};

// Represents an enum value extracted from existing header
struct ExistingEnumValue {
    std::string name;
    std::optional<int64_t> value;
};

// Represents an enum extracted from existing header
struct ExistingEnum {
    std::string name;
    bool is_flags;
    std::vector<ExistingEnumValue> values;
};

// Result of comparing IDL with existing implementation
struct SyncReport {
    // Methods in IDL but not in implementation
    std::vector<const Method*> new_methods;

    // Methods in implementation but not in IDL
    std::vector<ExistingMethod> removed_methods;

    // Methods with changed signatures (pair: existing, new from IDL)
    std::vector<std::pair<ExistingMethod, const Method*>> modified_methods;

    // Structs in IDL but not in existing header
    std::vector<const StructDef*> new_structs;

    // Structs in existing header but not in IDL
    std::vector<ExistingStruct> removed_structs;

    // Structs with changed fields (pair: existing, new from IDL)
    std::vector<std::pair<ExistingStruct, const StructDef*>> modified_structs;

    // Enums in IDL but not in existing header
    std::vector<const EnumDef*> new_enums;

    // Enums in existing header but not in IDL
    std::vector<ExistingEnum> removed_enums;

    // Enums with changed values (pair: existing, new from IDL)
    std::vector<std::pair<ExistingEnum, const EnumDef*>> modified_enums;

    bool has_changes() const {
        return !new_methods.empty() || !removed_methods.empty() || !modified_methods.empty() ||
               !new_structs.empty() || !removed_structs.empty() || !modified_structs.empty() ||
               !new_enums.empty() || !removed_enums.empty() || !modified_enums.empty();
    }
};

// Scaffold generator for C++ service implementations
class ScaffoldGenerator {
public:
    // Generate full skeleton (for new file)
    std::string generate_skeleton(const Namespace& ns, const ServiceDef& service);

    // Generate sync report (for existing file)
    std::string generate_sync_report(const Namespace& ns, const ServiceDef& service,
                                     const SyncReport& report);

    // Parse existing implementation file to extract method signatures
    std::vector<ExistingMethod> parse_existing_impl(const std::string& content,
                                                    const std::string& interface_name);

    // Parse existing header file to extract struct definitions
    std::vector<ExistingStruct> parse_existing_structs(const std::string& content);

    // Parse existing header file to extract enum definitions
    std::vector<ExistingEnum> parse_existing_enums(const std::string& content);

    // Compare IDL service with existing methods to produce sync report
    SyncReport compare(const ServiceDef& service,
                       const std::vector<ExistingMethod>& existing);

    // Compare IDL structs with existing structs
    void compare_structs(const Namespace& ns,
                         const std::vector<ExistingStruct>& existing,
                         SyncReport& report);

    // Compare IDL enums with existing enums
    void compare_enums(const Namespace& ns,
                       const std::vector<ExistingEnum>& existing,
                       SyncReport& report);

    // Main entry point: generate skeleton or sync report as appropriate
    // Returns the content to write (full skeleton or original + appended report)
    // existing_impl_content: content of the _impl.cpp file (if exists)
    // existing_header_content: content of the .hpp file (if exists)
    std::string generate(const Namespace& ns, const ServiceDef& service,
                         const std::optional<std::string>& existing_impl_content,
                         const std::optional<std::string>& existing_header_content = std::nullopt);

private:
    // Helper functions (type_to_cpp/type_to_param are free functions in ast.hpp)
    std::string params_to_string(const std::vector<Param>& params);
    std::string fields_to_string(const std::vector<Field>& fields);

    // Normalize signature for comparison
    std::string normalize_signature(const std::string& return_type,
                                    const std::string& params);

    // Normalize struct fields for comparison
    std::string normalize_fields(const std::vector<ExistingField>& fields);
    std::string normalize_fields(const std::vector<Field>& fields);

    // Normalize enum values for comparison
    std::string normalize_enum_values(const std::vector<ExistingEnumValue>& values);
    std::string normalize_enum_values(const std::vector<EnumItem>& values);
};

} // namespace song::compiler
