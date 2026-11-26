#pragma once
// src/sema/json_support.hpp — Shared Sema/Codegen JSON Serializability & Cross-Package Type Resolution

#include "types.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cfire::frontend::json_support {

struct JsonSupportError {
    std::string field_path{};
    std::string reason{};
    bool suggest_json_ignore = false;
};

[[nodiscard]] inline auto normalize_type_lookup_name(std::string_view raw) -> std::string {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i + 1 < raw.size() && raw[i] == ':' && raw[i + 1] == ':') {
            out.push_back('.');
            ++i;
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

[[nodiscard]] inline auto strip_pkg_mangle(std::string_view name) -> std::string_view {
    if (name.starts_with("cfire_pkg_")) {
        if (auto sep = name.find("__"); sep != std::string_view::npos) {
            return name.substr(sep + 2);
        }
    }
    return name;
}

[[nodiscard]] inline auto find_struct_decl(
    const CheckedFile& cf,
    std::string_view raw_name,
    std::string_view owner_pkg = {}
) -> const StructDecl* {
    std::string norm = normalize_type_lookup_name(raw_name);
    if (norm.empty()) return nullptr;
    if (auto dot = norm.find('.'); dot != std::string::npos) {
        std::string_view pkg(norm.data(), dot);
        std::string_view orig(norm.data() + dot + 1, norm.size() - dot - 1);
        if (auto it = cf.struct_defs.find(norm); it != cf.struct_defs.end()) {
            return &it->second;
        }
        std::string mangled = std::format("cfire_pkg_{}__{}", pkg, orig);
        if (auto it = cf.struct_defs.find(mangled); it != cf.struct_defs.end()) {
            return &it->second;
        }
        for (const auto& st : cf.ast.structs) {
            if (st.package_name == pkg && strip_pkg_mangle(st.name) == orig) {
                return &st;
            }
        }
        return nullptr;
    }
    if (!owner_pkg.empty()) {
        std::string mangled = std::format("cfire_pkg_{}__{}", owner_pkg, norm);
        if (auto it = cf.struct_defs.find(mangled); it != cf.struct_defs.end()) {
            return &it->second;
        }
        std::string qualified = std::format("{}.{}", owner_pkg, norm);
        if (auto it = cf.struct_defs.find(qualified); it != cf.struct_defs.end()) {
            return &it->second;
        }
    }
    if (auto it = cf.struct_defs.find(norm); it != cf.struct_defs.end()) {
        return &it->second;
    }
    for (const auto& st : cf.ast.structs) {
        if (st.name == norm) {
            return &st;
        }
    }
    return nullptr;
}

[[nodiscard]] inline auto find_enum_decl(
    const CheckedFile& cf,
    std::string_view raw_name,
    std::string_view owner_pkg = {}
) -> const EnumDecl* {
    std::string norm = normalize_type_lookup_name(raw_name);
    if (norm.empty()) return nullptr;
    if (auto dot = norm.find('.'); dot != std::string::npos) {
        std::string_view pkg(norm.data(), dot);
        std::string_view orig(norm.data() + dot + 1, norm.size() - dot - 1);
        std::string mangled = std::format("cfire_pkg_{}__{}", pkg, orig);
        if (auto it = cf.enums.find(mangled); it != cf.enums.end()) {
            return &it->second;
        }
        for (const auto& en : cf.ast.enums) {
            if (en.package_name == pkg && strip_pkg_mangle(en.name) == orig) {
                return &en;
            }
        }
        return nullptr;
    }
    if (!owner_pkg.empty()) {
        std::string mangled = std::format("cfire_pkg_{}__{}", owner_pkg, norm);
        if (auto it = cf.enums.find(mangled); it != cf.enums.end()) {
            return &it->second;
        }
        for (const auto& en : cf.ast.enums) {
            if (en.package_name == owner_pkg && strip_pkg_mangle(en.name) == norm) {
                return &en;
            }
        }
    }
    if (auto it = cf.enums.find(norm); it != cf.enums.end()) {
        return &it->second;
    }
    for (const auto& en : cf.ast.enums) {
        if (en.name == norm) {
            return &en;
        }
    }
    return nullptr;
}

[[nodiscard]] inline auto is_unit_enum(const EnumDecl& en) -> bool {
    return std::all_of(en.variants.begin(), en.variants.end(), [](const EnumVariant& v) {
        return v.payload_types.empty();
    });
}

[[nodiscard]] inline auto is_json_primitive_name(std::string_view name) -> bool {
    return name == "str" || name == "bool" ||
           name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "isize" ||
           name == "u8" || name == "byte" || name == "u16" || name == "u32" || name == "u64" || name == "usize" ||
           name == "f32" || name == "f64";
}

[[nodiscard]] inline auto is_json_dynamic_value_name(std::string_view name) -> bool {
    return name == "json.Value" || name == "Value";
}

[[nodiscard]] inline auto is_field_json_ignored(const CheckedFile& cf, const StructDecl& st, std::size_t idx) -> bool {
    if (idx >= st.fields.size()) return false;
    const auto& fld = st.fields[idx];
    if (auto it = cf.struct_tags.find(st.name); it != cf.struct_tags.end()) {
        for (const auto& t : it->second) {
            if (t.field_name == fld.name) {
                return t.json_key == "-";
            }
        }
    }
    if (!fld.raw_tag.empty()) {
        for (const auto& [k, v] : parse_struct_tags(fld.raw_tag)) {
            if (k == "json") {
                auto comma = v.find(',');
                std::string_view sv = comma != std::string::npos ? std::string_view(v).substr(0, comma) : std::string_view(v);
                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
                if (sv == "-") return true;
            }
        }
    }
    return false;
}

inline auto set_json_err(JsonSupportError* err, std::string_view path, std::string reason, bool suggest_ignore) -> bool {
    if (err && err->reason.empty()) {
        err->field_path = std::string(path);
        err->reason = std::move(reason);
        err->suggest_json_ignore = suggest_ignore;
    }
    return false;
}

[[nodiscard]] inline auto check_json_type_ref(
    const CheckedFile& cf,
    const TypeRef& tr,
    std::string_view path,
    std::string_view owner_pkg,
    std::unordered_set<std::string>& visiting,
    JsonSupportError* err = nullptr
) -> bool {
    if (tr.is_ptr || tr.is_mut_ptr) {
        return set_json_err(err, path, "pointer type cannot be serialized to JSON", !path.empty());
    }
    if (tr.is_borrow || tr.is_mut_borrow) {
        return set_json_err(err, path, "borrow reference type cannot be serialized to JSON", !path.empty());
    }
    if (tr.is_fn) {
        return set_json_err(err, path, "function/closure type cannot be serialized to JSON", !path.empty());
    }
    if (!tr.smart_wrap.empty()) {
        return set_json_err(err, path, std::format("smart pointer '{}[...]' cannot be serialized to JSON", tr.smart_wrap), !path.empty());
    }
    if (tr.is_map) {
        return set_json_err(err, path, "map type is not supported by typed core:json reflection", !path.empty());
    }
    if (tr.is_tuple) {
        return set_json_err(err, path, "tuple type cannot be serialized to JSON", !path.empty());
    }
    if (tr.is_array) {
        return set_json_err(err, path, "fixed-size array type is not supported by core:json serialization (use a slice '[]T')", !path.empty());
    }
    if (tr.is_fallible) {
        return set_json_err(err, path, "fallible 'T!' type cannot be serialized to JSON", !path.empty());
    }
    if (tr.is_optional) {
        TypeRef inner = tr;
        inner.is_optional = false;
        return check_json_type_ref(cf, inner, path, owner_pkg, visiting, err);
    }
    if (tr.is_slice) {
        if (!tr.elem_type) {
            return set_json_err(err, path, "untyped slice cannot be serialized to JSON", !path.empty());
        }
        return check_json_type_ref(cf, *tr.elem_type, path, owner_pkg, visiting, err);
    }

    std::string n = normalize_type_lookup_name(tr.name);
    if (is_json_primitive_name(n)) {
        return true;
    }

    // Check user-defined structs and enums FIRST so user types named e.g. `File`, `App`, `Server`,
    // `Session`, `Conn`, `Listener`, `Mutex`, `Channel` shadow stdlib handles consistently.
    if (const StructDecl* st = find_struct_decl(cf, n, owner_pkg)) {
        if (!st->type_params.empty() || !tr.type_args.empty()) {
            return set_json_err(err, path, std::format("generic struct '{}' is not supported by compile-time JSON reflection", n), !path.empty());
        }
        if (visiting.contains(st->name)) {
            return true;
        }
        visiting.insert(st->name);
        for (std::size_t i = 0; i < st->fields.size(); ++i) {
            if (is_field_json_ignored(cf, *st, i)) continue;
            const auto& fld = st->fields[i];
            std::string child_path = path.empty() ? fld.name : std::format("{}.{}", path, fld.name);
            if (!check_json_type_ref(cf, fld.type, child_path, st->package_name, visiting, err)) {
                visiting.erase(st->name);
                return false;
            }
        }
        visiting.erase(st->name);
        return true;
    }

    if (const EnumDecl* en = find_enum_decl(cf, n, owner_pkg)) {
        if (!en->type_params.empty() || !tr.type_args.empty()) {
            return set_json_err(err, path, std::format("generic enum '{}' is not supported by compile-time JSON reflection", n), !path.empty());
        }
        if (!is_unit_enum(*en)) {
            return set_json_err(err, path, std::format("enum '{}' has variant payloads and cannot be serialized to JSON", n), !path.empty());
        }
        return true;
    }

    if (is_json_dynamic_value_name(n)) {
        return true;
    }

    return set_json_err(err, path, std::format("type '{}' is not supported by core:json serialization", n.empty() ? "<unknown>" : n), !path.empty());
}

[[nodiscard]] inline auto can_emit_json_struct(const CheckedFile& cf, const StructDecl& st) -> bool {
    if (!st.type_params.empty()) return false;
    std::unordered_set<std::string> visiting;
    visiting.insert(st.name);
    for (std::size_t i = 0; i < st.fields.size(); ++i) {
        if (is_field_json_ignored(cf, st, i)) continue;
        if (!check_json_type_ref(cf, st.fields[i].type, st.fields[i].name, st.package_name, visiting, nullptr)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto check_json_type_id(
    const CheckedFile& cf,
    TypeId tid,
    std::string_view path,
    bool allow_top_level_borrow,
    std::unordered_set<std::string>& visiting,
    JsonSupportError* err = nullptr
) -> bool {
    if (!tid.is_valid() || tid.is_unknown() || tid.is_error()) return true;
    if (cf.type_table.contains_unknown(tid) || cf.type_table.contains_error(tid) || cf.type_table.contains_type_param(tid)) {
        return true;
    }

    const TypeDesc& d = cf.type_table.get(tid);
    switch (d.kind) {
        case TypeKind::Unknown:
        case TypeKind::TypeParam:
        case TypeKind::Error:
            return true;
        case TypeKind::Never:
        case TypeKind::Void:
        case TypeKind::Foreign:
        case TypeKind::Interface:
        case TypeKind::Fallible:
            return set_json_err(err, path, std::format("type '{}' is not supported by core:json serialization", cf.type_table.to_string(tid)), !path.empty());
        case TypeKind::Bool:
        case TypeKind::Int8:
        case TypeKind::Int16:
        case TypeKind::Int32:
        case TypeKind::Int64:
        case TypeKind::Uint8:
        case TypeKind::Uint16:
        case TypeKind::Uint32:
        case TypeKind::Uint64:
        case TypeKind::Isize:
        case TypeKind::Usize:
        case TypeKind::Float32:
        case TypeKind::Float64:
        case TypeKind::Str:
        case TypeKind::Byte:
            return true;
        case TypeKind::Borrow:
        case TypeKind::MutBorrow:
            if (allow_top_level_borrow && d.elem_type.is_valid()) {
                return check_json_type_id(cf, d.elem_type, path, false, visiting, err);
            }
            return set_json_err(err, path, "borrow reference type cannot be serialized to JSON", !path.empty());
        case TypeKind::Optional:
        case TypeKind::Slice:
            if (!d.elem_type.is_valid()) return true;
            return check_json_type_id(cf, d.elem_type, path, false, visiting, err);
        case TypeKind::Array:
            return set_json_err(err, path, std::format("fixed-size array '{}' is not supported by core:json serialization (use a slice '[]T')", cf.type_table.to_string(tid)), !path.empty());
        case TypeKind::Map:
            return set_json_err(err, path, std::format("map type '{}' is not supported by typed core:json reflection", cf.type_table.to_string(tid)), !path.empty());
        case TypeKind::Tuple:
            return set_json_err(err, path, std::format("tuple type '{}' cannot be serialized to JSON", cf.type_table.to_string(tid)), !path.empty());
        case TypeKind::Function:
            return set_json_err(err, path, "function/closure type cannot be serialized to JSON", !path.empty());
        case TypeKind::RawPtr:
        case TypeKind::MutPtr:
            return set_json_err(err, path, "pointer type cannot be serialized to JSON", !path.empty());
        case TypeKind::Own:
        case TypeKind::Shared:
            return set_json_err(err, path, std::format("smart pointer '{}' cannot be serialized to JSON", cf.type_table.to_string(tid)), !path.empty());
        case TypeKind::GenericInst:
            return set_json_err(err, path, std::format("generic type '{}' is not supported by compile-time JSON reflection", cf.type_table.to_string(tid)), !path.empty());
        case TypeKind::NominalStruct:
        case TypeKind::NominalEnum: {
            std::string n = normalize_type_lookup_name(d.name);
            if (n.empty()) n = normalize_type_lookup_name(cf.type_table.to_string(tid));
            if (const StructDecl* st = find_struct_decl(cf, n, d.package_name)) {
                if (!st->type_params.empty()) {
                    return set_json_err(err, path, std::format("generic struct '{}' is not supported by compile-time JSON reflection", n), !path.empty());
                }
                if (visiting.contains(st->name)) return true;
                visiting.insert(st->name);
                for (std::size_t i = 0; i < st->fields.size(); ++i) {
                    if (is_field_json_ignored(cf, *st, i)) continue;
                    const auto& fld = st->fields[i];
                    std::string child_path = path.empty() ? fld.name : std::format("{}.{}", path, fld.name);
                    if (!check_json_type_ref(cf, fld.type, child_path, st->package_name, visiting, err)) {
                        visiting.erase(st->name);
                        return false;
                    }
                }
                visiting.erase(st->name);
                return true;
            }
            if (const EnumDecl* en = find_enum_decl(cf, n, d.package_name)) {
                if (!en->type_params.empty()) {
                    return set_json_err(err, path, std::format("generic enum '{}' is not supported by compile-time JSON reflection", n), !path.empty());
                }
                if (!is_unit_enum(*en)) {
                    return set_json_err(err, path, std::format("enum '{}' has variant payloads and cannot be serialized to JSON", n), !path.empty());
                }
                return true;
            }
            if (is_json_dynamic_value_name(n)) {
                return true;
            }
            return set_json_err(err, path, std::format("type '{}' is not supported by core:json serialization", n), !path.empty());
        }
    }
    return true;
}

} // namespace cfire::frontend::json_support
