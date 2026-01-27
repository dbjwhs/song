// MIT License
// Copyright (c) 2026 dbjwhs

#include "scaffold.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <map>
#include <ctime>
#include <iomanip>

namespace song::compiler {

// =============================================================================
// Type Conversion Helpers (duplicated from codegen.cpp for independence)
// =============================================================================

std::string ScaffoldGenerator::type_to_cpp(const Type& t) {
    std::string base_type;

    if (is_primitive(t)) {
        base_type = primitive_to_cpp(get_primitive(t));
    } else {
        base_type = get_user_type(t);
    }

    // Handle arrays
    if (t.is_array) {
        int dims = t.array_dimensions > 0 ? t.array_dimensions : 1;
        std::string result;
        for (int i = 0; i < dims; ++i) {
            result += "std::vector<";
        }
        result += base_type;
        for (int i = 0; i < dims; ++i) {
            result += ">";
        }
        return result;
    }

    // Handle optional
    if (t.is_optional) {
        return "std::optional<" + base_type + ">";
    }

    return base_type;
}

std::string ScaffoldGenerator::type_to_param(const Type& t, const std::string& name) {
    std::string cpp_type = type_to_cpp(t);

    // Pass by const ref for complex types
    bool by_ref = t.is_array || t.is_optional;
    if (!by_ref && is_primitive(t)) {
        auto p = get_primitive(t);
        by_ref = (p == PrimitiveType::string || p == PrimitiveType::bytes);
    } else if (!by_ref && !is_primitive(t)) {
        by_ref = true;  // User-defined types always by ref
    }

    if (by_ref) {
        return "const " + cpp_type + "& " + name;
    }
    return cpp_type + " " + name;
}

std::string ScaffoldGenerator::params_to_string(const std::vector<Param>& params) {
    std::ostringstream out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) out << ", ";
        out << type_to_param(params[i].type, params[i].name);
    }
    return out.str();
}

std::string ScaffoldGenerator::fields_to_string(const std::vector<Field>& fields) {
    std::ostringstream out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out << ", ";
        out << type_to_cpp(fields[i].type) << " " << fields[i].name;
    }
    return out.str();
}

std::string ScaffoldGenerator::normalize_fields(const std::vector<ExistingField>& fields) {
    std::ostringstream out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out << ";";
        std::string type = fields[i].type;
        type = std::regex_replace(type, std::regex("\\s+"), " ");
        out << type << " " << fields[i].name;
    }
    return out.str();
}

std::string ScaffoldGenerator::normalize_fields(const std::vector<Field>& fields) {
    std::ostringstream out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out << ";";
        out << type_to_cpp(fields[i].type) << " " << fields[i].name;
    }
    return out.str();
}

std::string ScaffoldGenerator::normalize_enum_values(const std::vector<ExistingEnumValue>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ";";
        out << values[i].name;
        if (values[i].value.has_value()) {
            out << "=" << values[i].value.value();
        }
    }
    return out.str();
}

std::string ScaffoldGenerator::normalize_enum_values(const std::vector<EnumItem>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ";";
        out << values[i].name;
        if (values[i].value.has_value()) {
            out << "=" << values[i].value.value();
        }
    }
    return out.str();
}

std::string ScaffoldGenerator::normalize_signature(const std::string& return_type,
                                                   const std::string& params) {
    // Remove extra whitespace for comparison
    std::string sig = return_type + "(" + params + ")";
    // Normalize whitespace
    sig = std::regex_replace(sig, std::regex("\\s+"), " ");
    sig = std::regex_replace(sig, std::regex("\\s*,\\s*"), ", ");
    sig = std::regex_replace(sig, std::regex("\\s*\\(\\s*"), "(");
    sig = std::regex_replace(sig, std::regex("\\s*\\)\\s*"), ")");
    sig = std::regex_replace(sig, std::regex("\\s*&\\s*"), "& ");
    return sig;
}

// =============================================================================
// Skeleton Generation
// =============================================================================

std::string ScaffoldGenerator::generate_skeleton(const Namespace& ns, const ServiceDef& service) {
    std::ostringstream out;

    out << "// " << service.name << " Implementation\n";
    out << "// Generated scaffold - safe to edit\n";
    out << "// Source: " << ns.name << ".song\n\n";

    out << "#include \"" << ns.name << ".hpp\"\n\n";

    out << "namespace song::" << ns.name << " {\n\n";

    // Implementation class
    out << "class " << service.name << "Impl : public I" << service.name << " {\n";
    out << "public:\n";

    for (const auto& m : service.methods) {
        std::string return_type = type_to_cpp(m.return_type);
        bool is_void = is_primitive(m.return_type) &&
                       get_primitive(m.return_type) == PrimitiveType::void_;

        if (!m.doc.empty()) {
            out << "    /// " << m.doc << "\n";
        }

        out << "    " << return_type << " " << m.name << "(";
        out << params_to_string(m.params);
        out << ") override {\n";
        out << "        // TODO: implement\n";

        if (is_void) {
            out << "        throw std::runtime_error(\"" << m.name << " not implemented\");\n";
        } else {
            out << "        throw std::runtime_error(\"" << m.name << " not implemented\");\n";
        }

        out << "    }\n\n";
    }

    out << "};\n\n";

    out << "} // namespace song::" << ns.name << "\n";

    return out.str();
}

// =============================================================================
// Parse Existing Implementation
// =============================================================================

std::vector<ExistingMethod> ScaffoldGenerator::parse_existing_impl(
    const std::string& content, const std::string& /*interface_name*/) {

    std::vector<ExistingMethod> methods;

    // Pattern to match method declarations with override keyword
    // Captures: return_type, method_name, params
    // Handles multi-line by first removing newlines within method signatures
    std::string normalized = content;

    // Parse line by line to find method signatures with override
    // This avoids issues with multi-line matching and doc comments
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;

    // Regex for method with override on a single line
    // Match: <return_type> <name>(<params>) override
    std::regex method_regex(
        R"(^\s*([\w:<>]+(?:\s*<[^>]+>)?)\s+(\w+)\s*\(([^)]*)\)\s*(?:const\s*)?override)",
        std::regex::ECMAScript
    );

    while (std::getline(stream, line)) {
        ++line_num;

        // Skip comment lines
        std::string trimmed = line;
        size_t first_non_space = trimmed.find_first_not_of(" \t");
        if (first_non_space != std::string::npos) {
            trimmed = trimmed.substr(first_non_space);
        }
        if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '*') {
            continue;
        }

        std::smatch match;
        if (std::regex_search(line, match, method_regex)) {
            ExistingMethod m;
            m.return_type = match[1].str();
            m.name = match[2].str();
            m.params = match[3].str();
            m.line_number = line_num;

            // Clean up whitespace
            m.return_type = std::regex_replace(m.return_type, std::regex("^\\s+|\\s+$"), "");
            m.params = std::regex_replace(m.params, std::regex("^\\s+|\\s+$"), "");

            methods.push_back(m);
        }
    }

    return methods;
}

// =============================================================================
// Parse Existing Structs from Header
// =============================================================================

std::vector<ExistingStruct> ScaffoldGenerator::parse_existing_structs(const std::string& content) {
    std::vector<ExistingStruct> structs;

    // Pattern to match struct definitions
    // Match: struct Name { ... };
    std::regex struct_regex(R"(struct\s+(\w+)\s*\{([^}]*)\})");

    auto begin = std::sregex_iterator(content.begin(), content.end(), struct_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch match = *it;
        ExistingStruct s;
        s.name = match[1].str();
        std::string body = match[2].str();

        // Parse fields from body
        // Match: type name;
        std::regex field_regex(R"(([^;]+?)\s+(\w+)\s*;)");
        auto field_begin = std::sregex_iterator(body.begin(), body.end(), field_regex);
        auto field_end = std::sregex_iterator();

        for (auto fit = field_begin; fit != field_end; ++fit) {
            std::smatch field_match = *fit;
            ExistingField f;
            f.type = field_match[1].str();
            f.name = field_match[2].str();

            // Clean up type (remove leading/trailing whitespace)
            f.type = std::regex_replace(f.type, std::regex("^\\s+|\\s+$"), "");

            // Skip if this looks like a comment or empty
            if (!f.type.empty() && f.type[0] != '/' && f.type.find("//") == std::string::npos) {
                s.fields.push_back(f);
            }
        }

        structs.push_back(s);
    }

    return structs;
}

// =============================================================================
// Parse Existing Enums from Header
// =============================================================================

std::vector<ExistingEnum> ScaffoldGenerator::parse_existing_enums(const std::string& content) {
    std::vector<ExistingEnum> enums;

    // Pattern to match enum class definitions
    // Match: enum class Name : type { ... };
    std::regex enum_regex(R"(enum\s+class\s+(\w+)\s*:\s*\w+\s*\{([^}]*)\})");

    auto begin = std::sregex_iterator(content.begin(), content.end(), enum_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch match = *it;
        ExistingEnum e;
        e.name = match[1].str();
        std::string body = match[2].str();

        // Check if it's a flags enum (has explicit values that look like powers of 2)
        e.is_flags = false;

        // Parse enum values from body
        // Match: name or name = value
        std::regex value_regex(R"((\w+)(?:\s*=\s*(-?\d+))?)");
        auto val_begin = std::sregex_iterator(body.begin(), body.end(), value_regex);
        auto val_end = std::sregex_iterator();

        for (auto vit = val_begin; vit != val_end; ++vit) {
            std::smatch val_match = *vit;
            ExistingEnumValue v;
            v.name = val_match[1].str();

            // Skip common non-value tokens
            if (v.name == "class" || v.name == "enum" || v.name == "struct") {
                continue;
            }

            if (val_match[2].matched) {
                v.value = std::stoll(val_match[2].str());
                // Check if value is a power of 2 (likely flags)
                if (v.value.value() > 0 && (v.value.value() & (v.value.value() - 1)) == 0) {
                    e.is_flags = true;
                }
            }

            e.values.push_back(v);
        }

        enums.push_back(e);
    }

    return enums;
}

// =============================================================================
// Compare IDL with Existing
// =============================================================================

SyncReport ScaffoldGenerator::compare(const ServiceDef& service,
                                      const std::vector<ExistingMethod>& existing) {
    SyncReport report;

    // Build map of existing methods by name
    std::map<std::string, ExistingMethod> existing_map;
    for (const auto& m : existing) {
        existing_map[m.name] = m;
    }

    // Check each IDL method
    for (const auto& m : service.methods) {
        auto it = existing_map.find(m.name);

        if (it == existing_map.end()) {
            // Method not found in implementation - it's new
            report.new_methods.push_back(&m);
        } else {
            // Method exists - check if signature matches
            std::string idl_return = type_to_cpp(m.return_type);
            std::string idl_params = params_to_string(m.params);
            std::string idl_sig = normalize_signature(idl_return, idl_params);

            std::string existing_sig = normalize_signature(it->second.return_type,
                                                           it->second.params);

            if (idl_sig != existing_sig) {
                // Signature changed
                report.modified_methods.push_back({it->second, &m});
            }

            // Remove from map (remaining are removed methods)
            existing_map.erase(it);
        }
    }

    // Remaining methods in map are removed from IDL
    for (const auto& [name, method] : existing_map) {
        report.removed_methods.push_back(method);
    }

    return report;
}

void ScaffoldGenerator::compare_structs(const Namespace& ns,
                                        const std::vector<ExistingStruct>& existing,
                                        SyncReport& report) {
    // Build map of existing structs by name
    std::map<std::string, ExistingStruct> existing_map;
    for (const auto& s : existing) {
        existing_map[s.name] = s;
    }

    // Check each IDL struct
    for (const auto& s : ns.structs) {
        auto it = existing_map.find(s.name);

        if (it == existing_map.end()) {
            // Struct not found in existing header - it's new
            report.new_structs.push_back(&s);
        } else {
            // Struct exists - check if fields match
            std::string idl_fields = normalize_fields(s.fields);
            std::string existing_fields = normalize_fields(it->second.fields);

            if (idl_fields != existing_fields) {
                // Fields changed
                report.modified_structs.push_back({it->second, &s});
            }

            // Remove from map (remaining are removed structs)
            existing_map.erase(it);
        }
    }

    // Remaining structs in map are removed from IDL
    for (const auto& [name, s] : existing_map) {
        report.removed_structs.push_back(s);
    }
}

void ScaffoldGenerator::compare_enums(const Namespace& ns,
                                      const std::vector<ExistingEnum>& existing,
                                      SyncReport& report) {
    // Build map of existing enums by name
    std::map<std::string, ExistingEnum> existing_map;
    for (const auto& e : existing) {
        existing_map[e.name] = e;
    }

    // Check each IDL enum
    for (const auto& e : ns.enums) {
        auto it = existing_map.find(e.name);

        if (it == existing_map.end()) {
            // Enum not found in existing header - it's new
            report.new_enums.push_back(&e);
        } else {
            // Enum exists - check if values match
            std::string idl_values = normalize_enum_values(e.items);
            std::string existing_values = normalize_enum_values(it->second.values);

            if (idl_values != existing_values) {
                // Values changed
                report.modified_enums.push_back({it->second, &e});
            }

            // Remove from map (remaining are removed enums)
            existing_map.erase(it);
        }
    }

    // Remaining enums in map are removed from IDL
    for (const auto& [name, e] : existing_map) {
        report.removed_enums.push_back(e);
    }
}

// =============================================================================
// Generate Sync Report
// =============================================================================

std::string ScaffoldGenerator::generate_sync_report(const Namespace& ns,
                                                    const ServiceDef& /*service*/,
                                                    const SyncReport& report) {
    std::ostringstream out;

    // Get current timestamp
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);

    out << "\n";
    out << "// ============================================================\n";
    out << "// SCAFFOLD SYNC REPORT - " << ns.name << ".song\n";
    out << "// Generated: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
    out << "// ============================================================\n";

    // New methods
    if (!report.new_methods.empty()) {
        out << "//\n";
        out << "// ----- NEW METHODS (add to your class) ----------------------\n";
        out << "//\n";

        for (const auto* m : report.new_methods) {
            std::string return_type = type_to_cpp(m->return_type);

            if (!m->doc.empty()) {
                out << "//     /// " << m->doc << "\n";
            }

            out << "//     " << return_type << " " << m->name << "(";
            out << params_to_string(m->params);
            out << ") override {\n";
            out << "//         // TODO: implement\n";
            out << "//         throw std::runtime_error(\"" << m->name << " not implemented\");\n";
            out << "//     }\n";
            out << "//\n";
        }
    }

    // Removed methods
    if (!report.removed_methods.empty()) {
        out << "//\n";
        out << "// ----- REMOVED FROM IDL (consider deleting) -----------------\n";
        out << "//\n";

        for (const auto& m : report.removed_methods) {
            out << "//     " << m.return_type << " " << m.name << "(" << m.params << ")\n";
            out << "//     ^ Line " << m.line_number << " - consider removing\n";
            out << "//\n";
        }
    }

    // Modified signatures
    if (!report.modified_methods.empty()) {
        out << "//\n";
        out << "// ----- SIGNATURE CHANGED (update your implementation) -------\n";
        out << "//\n";

        for (const auto& [existing, idl_method] : report.modified_methods) {
            std::string new_return = type_to_cpp(idl_method->return_type);
            std::string new_params = params_to_string(idl_method->params);

            out << "//     " << idl_method->name << ":\n";
            out << "//       WAS: " << existing.return_type << " " << existing.name
                << "(" << existing.params << ")\n";
            out << "//       NOW: " << new_return << " " << idl_method->name
                << "(" << new_params << ")\n";
            out << "//       Line " << existing.line_number << " in your implementation\n";
            out << "//\n";
        }
    }

    // New structs
    if (!report.new_structs.empty()) {
        out << "//\n";
        out << "// ----- NEW STRUCTS -----------------------------------------\n";
        out << "//\n";

        for (const auto* s : report.new_structs) {
            out << "//     " << s->name << ":\n";
            for (const auto& f : s->fields) {
                out << "//         " << type_to_cpp(f.type) << " " << f.name << "\n";
            }
            out << "//\n";
        }
    }

    // Removed structs
    if (!report.removed_structs.empty()) {
        out << "//\n";
        out << "// ----- STRUCTS REMOVED FROM IDL ----------------------------\n";
        out << "//     ^ Your code may reference these types\n";
        out << "//\n";

        for (const auto& s : report.removed_structs) {
            out << "//     " << s.name << "\n";
        }
        out << "//\n";
    }

    // Modified structs
    if (!report.modified_structs.empty()) {
        out << "//\n";
        out << "// ----- STRUCT FIELDS CHANGED -------------------------------\n";
        out << "//\n";

        for (const auto& [existing, idl_struct] : report.modified_structs) {
            out << "//     " << idl_struct->name << ":\n";
            out << "//       WAS:\n";
            for (const auto& f : existing.fields) {
                out << "//         " << f.type << " " << f.name << "\n";
            }
            out << "//       NOW:\n";
            for (const auto& f : idl_struct->fields) {
                out << "//         " << type_to_cpp(f.type) << " " << f.name << "\n";
            }
            out << "//\n";
        }
    }

    // New enums
    if (!report.new_enums.empty()) {
        out << "//\n";
        out << "// ----- NEW ENUMS -------------------------------------------\n";
        out << "//\n";

        for (const auto* e : report.new_enums) {
            out << "//     " << (e->is_flags ? "flags " : "enum ") << e->name << ":\n";
            for (const auto& v : e->items) {
                out << "//         " << v.name;
                if (v.value.has_value()) {
                    out << " = " << v.value.value();
                }
                out << "\n";
            }
            out << "//\n";
        }
    }

    // Removed enums
    if (!report.removed_enums.empty()) {
        out << "//\n";
        out << "// ----- ENUMS REMOVED FROM IDL ------------------------------\n";
        out << "//     ^ Your code may reference these types\n";
        out << "//\n";

        for (const auto& e : report.removed_enums) {
            out << "//     " << e.name << "\n";
        }
        out << "//\n";
    }

    // Modified enums
    if (!report.modified_enums.empty()) {
        out << "//\n";
        out << "// ----- ENUM VALUES CHANGED ---------------------------------\n";
        out << "//\n";

        for (const auto& [existing, idl_enum] : report.modified_enums) {
            out << "//     " << idl_enum->name << ":\n";
            out << "//       WAS:\n";
            for (const auto& v : existing.values) {
                out << "//         " << v.name;
                if (v.value.has_value()) {
                    out << " = " << v.value.value();
                }
                out << "\n";
            }
            out << "//       NOW:\n";
            for (const auto& v : idl_enum->items) {
                out << "//         " << v.name;
                if (v.value.has_value()) {
                    out << " = " << v.value.value();
                }
                out << "\n";
            }
            out << "//\n";
        }
    }

    out << "// ============================================================\n";

    return out.str();
}

// =============================================================================
// Main Entry Point
// =============================================================================

std::string ScaffoldGenerator::generate(const Namespace& ns, const ServiceDef& service,
                                        const std::optional<std::string>& existing_impl_content,
                                        const std::optional<std::string>& existing_header_content) {
    if (!existing_impl_content.has_value()) {
        // No existing file - generate full skeleton
        return generate_skeleton(ns, service);
    }

    // Parse existing implementation
    std::vector<ExistingMethod> existing_methods = parse_existing_impl(
        existing_impl_content.value(), "I" + service.name);

    // Compare methods with IDL
    SyncReport report = compare(service, existing_methods);

    // If we have an existing header, also compare structs and enums
    if (existing_header_content.has_value()) {
        std::vector<ExistingStruct> existing_structs = parse_existing_structs(
            existing_header_content.value());
        std::vector<ExistingEnum> existing_enums = parse_existing_enums(
            existing_header_content.value());

        compare_structs(ns, existing_structs, report);
        compare_enums(ns, existing_enums, report);
    }

    if (!report.has_changes()) {
        // No changes - return original content with a note
        std::ostringstream out;
        out << existing_impl_content.value();
        out << "\n// Scaffold sync: all methods up to date\n";
        return out.str();
    }

    // Append sync report to existing content
    std::ostringstream out;
    out << existing_impl_content.value();
    out << generate_sync_report(ns, service, report);
    return out.str();
}

} // namespace song::compiler
