// MIT License
// Copyright (c) 2026 dbjwhs

#include "rust_codegen.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace song::compiler {

namespace {

// Rust keywords, strict and reserved. An IDL name that is one is emitted as
// a raw identifier (r#type); the four that cannot be raw get a trailing
// underscore.
const std::set<std::string> kKeywords = {
    "as", "break", "const", "continue", "crate", "else", "enum", "extern", "false", "fn",
    "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref",
    "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe",
    "use", "where", "while", "async", "await", "dyn", "abstract", "become", "box", "do",
    "final", "macro", "override", "priv", "typeof", "unsized", "virtual", "yield", "try", "gen",
};

const std::set<std::string> kNeverRaw = {"self", "Self", "super", "crate"};

// Names the generated code uses unqualified. An IDL type with one of these
// names would change the meaning of every signature in the module.
const std::set<std::string> kReservedTypeNames = {
    "String", "Vec", "Option", "Some", "None", "Ok", "Err", "Box", "Self",
};

std::string ident(const std::string& name) {
    if (kNeverRaw.count(name) != 0) {
        return name + "_";
    }
    if (kKeywords.count(name) != 0) {
        return "r#" + name;
    }
    return name;
}

// A doc comment with the prefix on EVERY line: the parser joins consecutive
// /// lines with '\n', and a continuation line without its prefix would be
// bare text in the generated module.
void emit_doc(std::ostream& out, const std::string& doc, const std::string& indent = "") {
    if (doc.empty()) {
        return;
    }
    size_t start = 0;
    for (;;) {
        size_t nl = doc.find('\n', start);
        bool last = (nl == std::string::npos);
        std::string line = doc.substr(start, last ? std::string::npos : nl - start);
        out << indent << "///";
        if (!line.empty()) {
            out << " " << line;
        }
        out << "\n";
        if (last) {
            break;
        }
        start = nl + 1;
    }
}

const char* primitive_to_rust(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::bool_: return "bool";
        case PrimitiveType::i8: return "i8";
        case PrimitiveType::i16: return "i16";
        case PrimitiveType::i32: return "i32";
        case PrimitiveType::i64: return "i64";
        case PrimitiveType::u8: return "u8";
        case PrimitiveType::u16: return "u16";
        case PrimitiveType::u32: return "u32";
        case PrimitiveType::u64: return "u64";
        case PrimitiveType::f32: return "f32";
        case PrimitiveType::f64: return "f64";
        case PrimitiveType::string: return "String";
        case PrimitiveType::bytes: return "Vec<u8>";
        case PrimitiveType::void_: return "()";
    }
    return "()";
}

bool is_string_or_bytes(const Type& t) {
    if (!is_primitive(t)) {
        return false;
    }
    auto p = get_primitive(t);
    return p == PrimitiveType::string || p == PrimitiveType::bytes;
}

// The element type of an array type: one dimension peeled off. `T[]?`
// generates as a plain array (as in the C++ generator), so the optional
// flag does not carry to the element.
Type element_of(const Type& t) {
    Type elem = t;
    int dims = elem.array_dimensions > 0 ? elem.array_dimensions : 1;
    elem.array_dimensions = dims - 1;
    elem.is_array = (elem.array_dimensions > 0);
    elem.is_optional = false;
    return elem;
}

Type without_optional(const Type& t) {
    Type inner = t;
    inner.is_optional = false;
    return inner;
}

// `buf` as a function argument: the proxies' request and response buffers
// are owned locals, every other buffer is already a &mut Buffer.
std::string buf_arg(const std::string& buf) {
    return (buf == "song_req" || buf == "song_resp") ? "&mut " + buf : buf;
}

bool has_stream(const ServiceDef& s) {
    return std::any_of(s.methods.begin(), s.methods.end(), [](const Method& m) { return m.is_stream; });
}

bool has_unary(const ServiceDef& s) {
    return std::any_of(s.methods.begin(), s.methods.end(), [](const Method& m) { return !m.is_stream; });
}

// The item values of an enum as the C++ generator numbers them: an explicit
// value, else one more than the item before (0 for the first).
std::vector<int64_t> item_values(const EnumDef& e) {
    std::vector<int64_t> values;
    int64_t next = 0;
    for (const auto& item : e.items) {
        int64_t value = item.value.has_value() ? item.value.value() : next;
        next = value + 1;
        values.push_back(value);
    }
    return values;
}

} // namespace

// =============================================================================
// Types
// =============================================================================

void RustCodeGenerator::index_types(const Namespace& ns) {
    m_kinds.clear();
    auto check_name = [](const std::string& name) {
        if (kReservedTypeNames.count(name) != 0) {
            throw std::runtime_error("Rust target: a type cannot be named '" + name +
                                     "' (the generated module uses that name for the Rust type)");
        }
    };
    for (const auto& e : ns.enums) {
        check_name(e.name);
        // An enum with no items has no Rust enum form; it travels as its
        // integer, like a flags type.
        m_kinds[e.name] = (e.is_flags || e.items.empty()) ? UserKind::flags : UserKind::enum_;
    }
    for (const auto& s : ns.structs) {
        check_name(s.name);
        m_kinds[s.name] = UserKind::struct_;
    }
    for (const auto& s : ns.services) {
        check_name(s.name);
        if (m_kinds.count(s.name) != 0) {
            throw std::runtime_error("Rust target: service '" + s.name +
                                     "' has the name of a type (the service trait takes that name)");
        }
    }
}

RustCodeGenerator::UserKind RustCodeGenerator::kind_of(const std::string& name) const {
    auto it = m_kinds.find(name);
    if (it == m_kinds.end()) {
        throw std::runtime_error("Rust target: type '" + name +
                                 "' is not a struct or enum of this namespace (classes and error "
                                 "types are not supported)");
    }
    return it->second;
}

bool RustCodeGenerator::is_void(const Type& t) const {
    return !t.is_array && !t.is_optional && is_primitive(t) && get_primitive(t) == PrimitiveType::void_;
}

// Whether a value of this type is passed by value (it is Copy): a scalar or
// an enum. Everything else is passed by reference.
bool RustCodeGenerator::by_value(const Type& t) const {
    if (t.is_array || t.is_optional) {
        return false;
    }
    if (is_primitive(t)) {
        return !is_string_or_bytes(t);
    }
    return kind_of(get_user_type(t)) != UserKind::struct_;
}

std::string RustCodeGenerator::base_type(const Type& t) const {
    if (is_primitive(t)) {
        return primitive_to_rust(get_primitive(t));
    }
    kind_of(get_user_type(t));  // throws for a type this target cannot express
    return get_user_type(t);
}

// The type that owns a value: a struct field, a return value, a chunk.
std::string RustCodeGenerator::owned_type(const Type& t) const {
    if (t.is_array) {
        int dims = t.array_dimensions > 0 ? t.array_dimensions : 1;
        std::string result;
        for (int ndx = 0; ndx < dims; ++ndx) {
            result += "Vec<";
        }
        result += base_type(t);
        for (int ndx = 0; ndx < dims; ++ndx) {
            result += ">";
        }
        return result;
    }
    if (t.is_optional) {
        return "Option<" + base_type(t) + ">";
    }
    return base_type(t);
}

// The type of a method parameter: scalars and enums by value, the rest
// borrowed (&str, &[u8], &[T], &Struct), an optional as Option of that.
std::string RustCodeGenerator::param_type(const Type& t) const {
    if (t.is_array) {
        return "&[" + owned_type(element_of(t)) + "]";
    }
    if (t.is_optional) {
        return "Option<" + param_type(without_optional(t)) + ">";
    }
    if (is_primitive(t)) {
        auto p = get_primitive(t);
        if (p == PrimitiveType::string) {
            return "&str";
        }
        if (p == PrimitiveType::bytes) {
            return "&[u8]";
        }
        return primitive_to_rust(p);
    }
    return by_value(t) ? base_type(t) : "&" + base_type(t);
}

// =============================================================================
// Codecs
// =============================================================================

// Statements that encode `e` into `buf`. In param_mode `e` has the
// parameter type (an Option holds borrowed values); otherwise it is the
// owned type, by reference or as a place.
std::string RustCodeGenerator::encode(const Type& t, const Expr& e, const std::string& buf,
                                      const std::string& indent, bool param_mode) const {
    std::string as_ref = e.is_ref ? e.text : "&" + e.text;
    std::string as_place = e.is_ref ? "*" + e.text : e.text;
    std::ostringstream out;

    if (t.is_array) {
        out << indent << buf << ".encode_array(" << as_ref << ", " << encode_fn(element_of(t), indent) << ")?;\n";
        return out.str();
    }

    if (t.is_optional) {
        Type inner = without_optional(t);
        // A parameter is an Option<&T> or Option<scalar>, matched by value;
        // an owned Option<T> is matched through a reference.
        Expr some = param_mode ? Expr{"song_v", !by_value(inner)} : Expr{"song_v", true};
        out << indent << "match " << (param_mode ? e.text : as_ref) << " {\n";
        out << indent << "    Some(song_v) => {\n";
        out << indent << "        " << buf << ".encode_bool(true);\n";
        out << encode(inner, some, buf, indent + "        ");
        out << indent << "    }\n";
        out << indent << "    None => " << buf << ".encode_bool(false),\n";
        out << indent << "}\n";
        return out.str();
    }

    if (is_primitive(t)) {
        auto p = get_primitive(t);
        if (p == PrimitiveType::void_) {
            return "";
        }
        if (p == PrimitiveType::string || p == PrimitiveType::bytes) {
            out << indent << buf << ".encode_" << primitive_to_string(p) << "(" << as_ref << ")?;\n";
        } else {
            out << indent << buf << ".encode_" << primitive_to_string(p) << "(" << as_place << ");\n";
        }
        return out.str();
    }

    const std::string& name = get_user_type(t);
    if (kind_of(name) == UserKind::struct_) {
        out << indent << "encode_" << name << "(" << buf_arg(buf) << ", " << as_ref << ")?;\n";
    } else {
        out << indent << "encode_" << name << "(" << buf_arg(buf) << ", " << as_place << ");\n";
    }
    return out.str();
}

// A function value of type fn(&mut Buffer, &T) -> Result<()> that encodes
// one owned value of this type: an array element, a stream chunk.
std::string RustCodeGenerator::encode_fn(const Type& t, const std::string& indent) const {
    if (!t.is_array && !t.is_optional && !is_primitive(t) && kind_of(get_user_type(t)) == UserKind::struct_) {
        return "encode_" + get_user_type(t);
    }
    if (is_void(t)) {
        return "|_song_buf: &mut song::Buffer, _song_v: &()| Ok(())";
    }
    std::ostringstream out;
    out << "|song_buf: &mut song::Buffer, song_v: &" << owned_type(t) << "| {\n";
    out << encode(t, Expr{"song_v", true}, "song_buf", indent + "    ");
    out << indent << "    Ok(())\n";
    out << indent << "}";
    return out.str();
}

// An expression of type Result<owned type> that decodes from `buf`.
std::string RustCodeGenerator::decode(const Type& t, const std::string& buf) const {
    if (t.is_array) {
        return buf + ".decode_array(" + decode_fn(element_of(t)) + ")";
    }
    if (t.is_optional) {
        return buf + ".decode_optional(" + decode_fn(without_optional(t)) + ")";
    }
    if (is_primitive(t)) {
        auto p = get_primitive(t);
        if (p == PrimitiveType::void_) {
            return "Ok(())";
        }
        return buf + ".decode_" + primitive_to_string(p) + "()";
    }
    kind_of(get_user_type(t));
    return "decode_" + get_user_type(t) + "(" + buf_arg(buf) + ")";
}

// A function value of type fn(&mut Buffer) -> Result<T> for this type.
std::string RustCodeGenerator::decode_fn(const Type& t) const {
    if (t.is_array || t.is_optional) {
        return "|song_buf: &mut song::Buffer| " + decode(t, "song_buf");
    }
    if (is_primitive(t)) {
        auto p = get_primitive(t);
        if (p == PrimitiveType::void_) {
            return "|_song_buf: &mut song::Buffer| Ok(())";
        }
        return std::string("song::Buffer::decode_") + primitive_to_string(p);
    }
    kind_of(get_user_type(t));
    return "decode_" + get_user_type(t);
}

// =============================================================================
// Enums
// =============================================================================

std::string RustCodeGenerator::generate_enum(const EnumDef& e) const {
    std::ostringstream out;
    std::vector<int64_t> values = item_values(e);

    // Rust forbids two variants with one value: the first item with a value
    // is the variant, later ones are associated constants naming it.
    std::vector<size_t> variants;
    std::vector<std::pair<size_t, size_t>> aliases;  // (item, the variant it names)
    for (size_t ndx = 0; ndx < e.items.size(); ++ndx) {
        if (values[ndx] < INT32_MIN || values[ndx] > INT32_MAX) {
            throw std::runtime_error("Rust target: enum " + e.name + " item " + e.items[ndx].name +
                                     " does not fit the i32 an enum travels as");
        }
        auto first = std::find_if(variants.begin(), variants.end(),
                                  [&](size_t v) { return values[v] == values[ndx]; });
        if (first == variants.end()) {
            variants.push_back(ndx);
        } else {
            aliases.emplace_back(ndx, *first);
        }
    }

    emit_doc(out, e.doc);
    out << "#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]\n";
    out << "#[repr(i32)]\n";
    out << "pub enum " << e.name << " {\n";
    for (size_t pos = 0; pos < variants.size(); ++pos) {
        const auto& item = e.items[variants[pos]];
        emit_doc(out, item.doc, "    ");
        if (pos == 0) {
            out << "    #[default]\n";
        }
        out << "    " << ident(item.name) << " = " << values[variants[pos]] << ",\n";
    }
    out << "}\n\n";

    out << "impl " << e.name << " {\n";
    for (const auto& [item, target] : aliases) {
        emit_doc(out, e.items[item].doc, "    ");
        out << "    pub const " << ident(e.items[item].name) << ": " << e.name << " = " << e.name
            << "::" << ident(e.items[target].name) << ";\n";
    }
    if (!aliases.empty()) {
        out << "\n";
    }
    out << "    /// The item with this wire value, if there is one.\n";
    out << "    pub fn from_i32(value: i32) -> Option<" << e.name << "> {\n";
    out << "        match value {\n";
    for (size_t v : variants) {
        out << "            " << values[v] << " => Some(" << e.name << "::" << ident(e.items[v].name) << "),\n";
    }
    out << "            _ => None,\n";
    out << "        }\n";
    out << "    }\n\n";
    out << "    /// The item's name as the IDL spells it.\n";
    out << "    pub fn name(self) -> &'static str {\n";
    out << "        match self {\n";
    for (size_t v : variants) {
        out << "            " << e.name << "::" << ident(e.items[v].name) << " => \"" << e.items[v].name << "\",\n";
    }
    out << "        }\n";
    out << "    }\n";
    out << "}\n\n";

    out << "pub fn encode_" << e.name << "(buf: &mut song::Buffer, val: " << e.name << ") {\n";
    out << "    buf.encode_i32(val as i32);\n";
    out << "}\n\n";
    out << "/// A value that is no item of the enum is a decode error.\n";
    out << "pub fn decode_" << e.name << "(buf: &mut song::Buffer) -> song::Result<" << e.name << "> {\n";
    out << "    let value = buf.decode_i32()?;\n";
    out << "    " << e.name << "::from_i32(value).ok_or_else(|| song::Error::decode(format!(\"unknown "
        << e.name << " value: {value}\")))\n";
    out << "}\n";
    return out.str();
}

// A flags type (and an enum with no items) is its integer in a newtype: any
// combination of bits is a value, so there is nothing to refuse on decode.
std::string RustCodeGenerator::generate_flags(const EnumDef& e) const {
    std::ostringstream out;
    const char* wire = e.is_flags ? "u32" : "i32";
    std::vector<int64_t> values = item_values(e);

    emit_doc(out, e.doc);
    out << "#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]\n";
    out << "pub struct " << e.name << "(pub " << wire << ");\n\n";

    out << "impl " << e.name << " {\n";
    for (size_t ndx = 0; ndx < e.items.size(); ++ndx) {
        if (values[ndx] < 0 || values[ndx] > UINT32_MAX) {
            throw std::runtime_error("Rust target: flags " + e.name + " item " + e.items[ndx].name +
                                     " does not fit the u32 a flags type travels as");
        }
        emit_doc(out, e.items[ndx].doc, "    ");
        out << "    pub const " << ident(e.items[ndx].name) << ": " << e.name << " = " << e.name << "("
            << values[ndx] << ");\n";
    }
    if (!e.items.empty()) {
        out << "\n";
    }
    out << "    pub fn bits(self) -> " << wire << " {\n";
    out << "        self.0\n";
    out << "    }\n\n";
    out << "    /// Whether every bit of `other` is set.\n";
    out << "    pub fn contains(self, other: " << e.name << ") -> bool {\n";
    out << "        self.0 & other.0 == other.0\n";
    out << "    }\n";
    out << "}\n\n";

    for (const char* op : {"BitOr", "BitAnd"}) {
        std::string fn = std::string(op) == "BitOr" ? "bitor" : "bitand";
        const char* sym = std::string(op) == "BitOr" ? "|" : "&";
        out << "impl std::ops::" << op << " for " << e.name << " {\n";
        out << "    type Output = " << e.name << ";\n\n";
        out << "    fn " << fn << "(self, rhs: " << e.name << ") -> " << e.name << " {\n";
        out << "        " << e.name << "(self.0 " << sym << " rhs.0)\n";
        out << "    }\n";
        out << "}\n\n";
    }

    out << "pub fn encode_" << e.name << "(buf: &mut song::Buffer, val: " << e.name << ") {\n";
    out << "    buf.encode_" << wire << "(val.0);\n";
    out << "}\n\n";
    out << "pub fn decode_" << e.name << "(buf: &mut song::Buffer) -> song::Result<" << e.name << "> {\n";
    out << "    Ok(" << e.name << "(buf.decode_" << wire << "()?))\n";
    out << "}\n";
    return out.str();
}

// =============================================================================
// Structs
// =============================================================================

std::string RustCodeGenerator::generate_struct(const StructDef& s) const {
    std::ostringstream out;
    bool empty = s.fields.empty();

    emit_doc(out, s.doc);
    out << "#[derive(Debug, Clone, PartialEq, Default)]\n";
    out << "pub struct " << s.name << " {\n";
    for (const auto& f : s.fields) {
        emit_doc(out, f.doc, "    ");
        out << "    pub " << ident(f.name) << ": " << owned_type(f.type) << ",\n";
    }
    out << "}\n\n";

    out << "pub fn encode_" << s.name << "(" << (empty ? "_buf" : "buf") << ": &mut song::Buffer, "
        << (empty ? "_val" : "val") << ": &" << s.name << ") -> song::Result<()> {\n";
    for (const auto& f : s.fields) {
        out << encode(f.type, Expr{"val." + ident(f.name), false}, "buf", "    ");
    }
    out << "    Ok(())\n";
    out << "}\n\n";

    out << "pub fn decode_" << s.name << "(" << (empty ? "_buf" : "buf") << ": &mut song::Buffer) -> song::Result<"
        << s.name << "> {\n";
    out << "    Ok(" << s.name << " {\n";
    for (const auto& f : s.fields) {
        out << "        " << ident(f.name) << ": " << decode(f.type, "buf") << "?,\n";
    }
    out << "    })\n";
    out << "}\n";
    return out.str();
}

// =============================================================================
// Services
// =============================================================================

// The same numbering as the C++ and Python generators: services from 1, a
// service with any stream method takes a second id for its stream
// dispatcher, methods from 1 within each service.
std::string RustCodeGenerator::generate_service_ids(const Namespace& ns) const {
    std::ostringstream out;
    out << "// Service and method IDs\n";
    int service_id = 1;
    for (const auto& s : ns.services) {
        out << "pub const SERVICE_" << s.name << ": u16 = " << service_id++ << ";\n";
        if (has_stream(s)) {
            out << "pub const SERVICE_" << s.name << "_STREAM: u16 = " << service_id++ << ";\n";
        }
        int method_id = 1;
        for (const auto& m : s.methods) {
            out << "pub const METHOD_" << s.name << "_" << m.name << ": u16 = " << method_id++ << ";\n";
        }
        out << "\n";
    }
    return out.str();
}

std::string RustCodeGenerator::method_params(const Method& m) const {
    std::ostringstream out;
    for (const auto& p : m.params) {
        out << ", " << ident(p.name) << ": " << param_type(p.type);
    }
    return out.str();
}

// A decoded (owned) parameter as the argument of a trait method.
std::string RustCodeGenerator::pass_arg(const Param& p) const {
    std::string name = ident(p.name);
    if (p.type.is_array) {
        return "&" + name;
    }
    if (p.type.is_optional) {
        Type inner = without_optional(p.type);
        if (by_value(inner)) {
            return name;
        }
        return name + (is_string_or_bytes(inner) ? ".as_deref()" : ".as_ref()");
    }
    return by_value(p.type) ? name : "&" + name;
}

std::string RustCodeGenerator::generate_proxy(const ServiceDef& s) const {
    std::ostringstream out;

    emit_doc(out, s.doc);
    if (s.doc.empty()) {
        out << "/// Client proxy for the " << s.name << " service.\n";
    }
    out << "pub struct " << s.name << "Proxy<'a> {\n";
    out << "    conn: &'a mut song::ServiceConnection,\n";
    out << "}\n\n";
    out << "impl<'a> " << s.name << "Proxy<'a> {\n";
    out << "    pub fn new(conn: &'a mut song::ServiceConnection) -> Self {\n";
    out << "        " << s.name << "Proxy { conn }\n";
    out << "    }\n";

    for (const auto& m : s.methods) {
        bool encodes = std::any_of(m.params.begin(), m.params.end(),
                                   [&](const Param& p) { return !is_void(p.type); });
        out << "\n";
        emit_doc(out, m.doc, "    ");

        if (m.is_stream) {
            // The declared return type is the type of each chunk. The
            // iterator yields a chunk as it arrives; chunk_timeout is the
            // longest wait for the next one (None: no limit).
            if (!m.doc.empty()) {
                out << "    ///\n";
            }
            out << "    /// Streams: yields each chunk as it arrives. `chunk_timeout` is the longest\n";
            out << "    /// wait for the next chunk (`None` waits without limit).\n";
            out << "    pub fn " << ident(m.name) << "(&mut self" << method_params(m)
                << ", chunk_timeout: Option<std::time::Duration>) -> song::Result<song::TypedStream<'_, "
                << owned_type(m.return_type) << ">> {\n";
        } else {
            out << "    pub fn " << ident(m.name) << "(&mut self" << method_params(m) << ") -> song::Result<"
                << owned_type(m.return_type) << "> {\n";
        }

        out << "        let " << (encodes ? "mut " : "") << "song_req = song::Buffer::new();\n";
        for (const auto& p : m.params) {
            out << encode(p.type, Expr{ident(p.name), !by_value(p.type)}, "song_req", "        ", true);
        }

        if (m.is_stream) {
            out << "        let song_stream = self.conn.call_streaming(SERVICE_" << s.name << "_STREAM, METHOD_"
                << s.name << "_" << m.name << ", &song_req, chunk_timeout)?;\n";
            out << "        Ok(song_stream.decoded(" << decode_fn(m.return_type) << "))\n";
        } else if (is_void(m.return_type)) {
            out << "        self.conn.call(SERVICE_" << s.name << ", METHOD_" << s.name << "_" << m.name
                << ", &song_req)?;\n";
            out << "        Ok(())\n";
        } else {
            out << "        let mut song_resp = self.conn.call(SERVICE_" << s.name << ", METHOD_" << s.name << "_"
                << m.name << ", &song_req)?;\n";
            out << "        " << decode(m.return_type, "song_resp") << "\n";
        }
        out << "    }\n";
    }

    out << "}\n";
    return out.str();
}

std::string RustCodeGenerator::generate_trait(const ServiceDef& s) const {
    std::ostringstream out;

    emit_doc(out, s.doc);
    if (!s.doc.empty()) {
        out << "///\n";
    }
    out << "/// The service side of " << s.name << ": implement this and hand it to `register_" << s.name
        << "`.\n";
    out << "/// An `Err` is sent to the client as an error";
    if (has_stream(s)) {
        out << "; from a streaming method it ends\n/// the stream with that error, after the chunks already written.\n";
    } else {
        out << ".\n";
    }
    out << "pub trait " << s.name << ": Send + Sync {\n";
    bool first = true;
    for (const auto& m : s.methods) {
        if (!first) {
            out << "\n";
        }
        first = false;
        emit_doc(out, m.doc, "    ");
        if (m.is_stream) {
            bool taken = std::any_of(m.params.begin(), m.params.end(),
                                     [](const Param& p) { return p.name == "writer"; });
            out << "    fn " << ident(m.name) << "(&self" << method_params(m) << ", "
                << (taken ? "song_writer" : "writer") << ": &mut song::TypedStreamWriter<'_, '_, "
                << owned_type(m.return_type) << ">) -> song::Result<()>;\n";
        } else {
            out << "    fn " << ident(m.name) << "(&self" << method_params(m) << ") -> song::Result<"
                << owned_type(m.return_type) << ">;\n";
        }
    }
    out << "}\n";
    return out.str();
}

std::string RustCodeGenerator::generate_dispatch(const ServiceDef& s) const {
    std::ostringstream out;

    auto decode_params = [&](const Method& m) {
        for (const auto& p : m.params) {
            out << "            let " << ident(p.name) << " = " << decode(p.type, "song_request") << "?;\n";
        }
    };
    auto call_args = [&](const Method& m) {
        std::string args;
        for (const auto& p : m.params) {
            if (!args.empty()) {
                args += ", ";
            }
            args += pass_arg(p);
        }
        return args;
    };

    // A parameter no arm uses is named with a leading underscore, so the
    // module compiles without an unused-variable warning.
    bool any_unary = has_unary(s);
    bool reads_request = false;
    bool writes_response = false;
    for (const auto& m : s.methods) {
        if (m.is_stream) {
            continue;
        }
        reads_request = reads_request || !m.params.empty();
        writes_response = writes_response || !is_void(m.return_type);
    }

    out << "/// Decode one unary call to " << s.name << ", run it on `song_impl`, encode the result.\n";
    out << "pub fn dispatch_" << s.name << "<T: " << s.name << " + ?Sized>(\n";
    out << "    " << (any_unary ? "" : "_") << "song_impl: &T,\n";
    out << "    song_method: u16,\n";
    out << "    " << (reads_request ? "" : "_") << "song_request: &mut song::Buffer,\n";
    out << "    " << (writes_response ? "" : "_") << "song_response: &mut song::Buffer,\n";
    out << ") -> song::Result<()> {\n";
    if (!any_unary) {
        // Every method streams: nothing to match on.
        out << "    Err(song::Error::service(format!(\"Unknown method ID: {song_method}\")))\n";
        out << "}\n";
    } else {
        out << "    match song_method {\n";
    }
    for (const auto& m : s.methods) {
        if (m.is_stream) {
            continue;  // stream dispatcher below
        }
        out << "        METHOD_" << s.name << "_" << m.name << " => {\n";
        decode_params(m);
        if (is_void(m.return_type)) {
            out << "            song_impl." << ident(m.name) << "(" << call_args(m) << ")?;\n";
        } else {
            out << "            let song_result = song_impl." << ident(m.name) << "(" << call_args(m) << ")?;\n";
            out << encode(m.return_type, Expr{"song_result", false}, "song_response", "            ");
        }
        out << "            Ok(())\n";
        out << "        }\n";
    }
    if (any_unary) {
        out << "        _ => Err(song::Error::service(format!(\"Unknown method ID: {song_method}\"))),\n";
        out << "    }\n";
        out << "}\n";
    }

    if (!has_stream(s)) {
        return out.str();
    }

    bool stream_reads_request = false;
    for (const auto& m : s.methods) {
        stream_reads_request = stream_reads_request || (m.is_stream && !m.params.empty());
    }

    // Streaming methods write chunks to a writer and are registered on
    // SERVICE_<Name>_STREAM, apart from the unary dispatcher above.
    out << "\n/// Decode one streaming call to " << s.name << " and run it on `song_impl`, which writes its\n";
    out << "/// chunks to the writer.\n";
    out << "pub fn dispatch_" << s.name << "_stream<T: " << s.name << " + ?Sized>(\n";
    out << "    song_impl: &T,\n";
    out << "    song_method: u16,\n";
    out << "    " << (stream_reads_request ? "" : "_") << "song_request: &mut song::Buffer,\n";
    out << "    song_writer: &mut song::StreamWriter<'_>,\n";
    out << ") -> song::Result<()> {\n";
    out << "    match song_method {\n";
    for (const auto& m : s.methods) {
        if (!m.is_stream) {
            continue;
        }
        out << "        METHOD_" << s.name << "_" << m.name << " => {\n";
        decode_params(m);
        out << "            let mut song_typed = song::TypedStreamWriter::new(song_writer, "
            << encode_fn(m.return_type, "            ") << ");\n";
        std::string args = call_args(m);
        out << "            song_impl." << ident(m.name) << "(" << args << (args.empty() ? "" : ", ")
            << "&mut song_typed)\n";
        out << "        }\n";
    }
    out << "        _ => Err(song::Error::service(format!(\"Unknown streaming method ID: {song_method}\"))),\n";
    out << "    }\n";
    out << "}\n";
    return out.str();
}

std::string RustCodeGenerator::generate_register(const ServiceDef& s) const {
    std::ostringstream out;
    bool stream = has_stream(s);

    out << "/// Register " << s.name << " on a runtime: its dispatcher";
    out << (stream ? "s and its methods with their flags.\n" : " and its methods.\n");
    out << "pub fn register_" << s.name << "<T: " << s.name
        << " + ?Sized + 'static>(runtime: &mut song::ServiceRuntime, song_impl: std::sync::Arc<T>) {\n";
    if (stream) {
        out << "    let song_unary = std::sync::Arc::clone(&song_impl);\n";
        out << "    runtime.register_dispatcher(SERVICE_" << s.name << ", move |method, request, response| {\n";
        out << "        dispatch_" << s.name << "(song_unary.as_ref(), method, request, response)\n";
        out << "    });\n";
        out << "    runtime.register_stream_dispatcher(SERVICE_" << s.name
            << "_STREAM, move |method, request, writer| {\n";
        out << "        dispatch_" << s.name << "_stream(song_impl.as_ref(), method, request, writer)\n";
        out << "    });\n";
    } else {
        out << "    runtime.register_dispatcher(SERVICE_" << s.name << ", move |method, request, response| {\n";
        out << "        dispatch_" << s.name << "(song_impl.as_ref(), method, request, response)\n";
        out << "    });\n";
    }
    for (const auto& m : s.methods) {
        out << "    runtime.register_method(SERVICE_" << s.name << (m.is_stream ? "_STREAM" : "") << ", METHOD_"
            << s.name << "_" << m.name << ", song::wire::method_flags::" << (m.is_stream ? "STREAMING" : "NONE")
            << ");\n";
    }
    out << "}\n";
    return out.str();
}

// =============================================================================
// Module
// =============================================================================

std::string RustCodeGenerator::generate_module(const Namespace& ns) {
    index_types(ns);

    std::ostringstream out;
    out << "// Generated by songc - DO NOT EDIT\n";
    out << "// Source namespace: " << ns.name << "\n";
    out << "//\n";
    out << "// A module for a crate that depends on the `song` runtime crate:\n";
    out << "//     mod " << ns.name << ";\n";
    out << "\n";
    out << "// Names are the IDL's own (encode_Point, SERVICE_Echo), and a crate may use\n";
    out << "// only the client or only the service half of the module.\n";
    out << "#![allow(non_snake_case, non_camel_case_types, non_upper_case_globals, dead_code)]\n";
    out << "#![allow(clippy::too_many_arguments, clippy::upper_case_acronyms, clippy::enum_variant_names)]\n";

    for (const auto& e : ns.enums) {
        out << "\n" << (m_kinds.at(e.name) == UserKind::flags ? generate_flags(e) : generate_enum(e));
    }
    for (const auto& s : ns.structs) {
        out << "\n" << generate_struct(s);
    }
    if (!ns.services.empty()) {
        out << "\n" << generate_service_ids(ns);
        bool first = true;
        for (const auto& s : ns.services) {
            if (!first) {
                out << "\n";
            }
            first = false;
            out << generate_proxy(s) << "\n";
            out << generate_trait(s) << "\n";
            out << generate_dispatch(s) << "\n";
            out << generate_register(s);
        }
    }
    return out.str();
}

} // namespace song::compiler
