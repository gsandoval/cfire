#pragma once
// src/sema/json_support.hpp — Shared Sema/Codegen JSON Serializability & Cross-Package Type Resolution

#include "types.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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

[[nodiscard]] inline auto is_json_dynamic_value_name(std::string_view raw_name, const CheckedFile* cf = nullptr) -> bool {
    std::string norm = normalize_type_lookup_name(raw_name);
    if (norm == "json.Value" || norm == "Value") return true;
    if (cf) {
        if (auto dot = norm.find('.'); dot != std::string::npos) {
            std::string alias = norm.substr(0, dot);
            if (auto it = cf->core_aliases.find(alias); it != cf->core_aliases.end() && it->second == "json") {
                return norm.substr(dot + 1) == "Value";
            }
        }
    }
    return false;
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

    if (is_json_dynamic_value_name(n, &cf)) {
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
            if (is_json_dynamic_value_name(n, &cf)) {
                return true;
            }
            return set_json_err(err, path, std::format("type '{}' is not supported by core:json serialization", n), !path.empty());
        }
    }
    return true;
}

[[nodiscard]] inline auto compute_json_needed_types(const CheckedFile& cf) -> std::optional<std::unordered_set<std::string>> {
    // Template runtime headers (`webui`, `ai`, `tako`) invoke `cfire::json::{parse,stringify,schema}`
    // inside C++ templates on arbitrary user structs (including handler signature types), so skip filtering.
    for (const auto& imp : cf.ast.imports) {
        if (imp.is_cpp) continue;
        auto res = cfire::modules::resolve_import(imp.path);
        if (res.is_vendor && res.vendor_key == "tako") {
            return std::nullopt;
        }
        for (const auto& ck : res.core_keys) {
            if (ck == "ai" || ck == "webui") {
                return std::nullopt;
            }
        }
    }

    std::unordered_set<std::string> needed;
    std::vector<std::string> worklist;
    auto push_type_name = [&](const std::string& name) {
        if (!name.empty() && needed.insert(name).second) {
            worklist.push_back(name);
        }
    };
    auto add_named_decl = [&](std::string_view raw_name, std::string_view owner_pkg) {
        if (raw_name.empty()) return;
        if (const StructDecl* st = find_struct_decl(cf, raw_name, owner_pkg)) {
            if (st->type_params.empty()) push_type_name(st->name);
            return;
        }
        if (const EnumDecl* en = find_enum_decl(cf, raw_name, owner_pkg)) {
            if (en->type_params.empty()) push_type_name(en->name);
            return;
        }
    };

    // 1. Seed structs with explicit JSON tags.
    for (const auto& st : cf.ast.structs) {
        if (!st.type_params.empty()) continue;
        auto it = cf.struct_tags.find(st.name);
        if (it == cf.struct_tags.end()) continue;
        for (const auto& t : it->second) {
            if (!t.json_key.empty()) {
                push_type_name(st.name);
                break;
            }
        }
    }

    bool bail_to_all = false;
    auto seed_typeref = [&](const auto& self, const TypeRef& tr, std::string_view owner_pkg,
                            const std::unordered_set<std::string>& tparams) -> void {
        if (bail_to_all) return;
        if (!tr.name.empty() && tparams.contains(tr.name)) {
            bail_to_all = true;
            return;
        }
        if (tr.elem_type) self(self, *tr.elem_type, owner_pkg, tparams);
        if (tr.key_type) self(self, *tr.key_type, owner_pkg, tparams);
        for (const auto& ta : tr.type_args) self(self, ta, owner_pkg, tparams);
        for (const auto& tt : tr.tuple_types) self(self, tt, owner_pkg, tparams);
        for (const auto& fp : tr.fn_params) self(self, fp, owner_pkg, tparams);
        if (tr.fn_ret) self(self, *tr.fn_ret, owner_pkg, tparams);
        if (!tr.name.empty() && !is_json_primitive_name(tr.name)) {
            add_named_decl(tr.name, owner_pkg);
        }
    };

    auto seed_typeid = [&](const auto& self, TypeId tid, const std::unordered_set<std::string>& tparams) -> void {
        if (bail_to_all) return;
        if (!tid.is_valid() || tid.is_unknown() || tid.is_error()) {
            bail_to_all = true;
            return;
        }
        if (cf.type_table.contains_unknown(tid) || cf.type_table.contains_type_param(tid)) {
            bail_to_all = true;
            return;
        }
        const TypeDesc& d = cf.type_table.get(tid);
        switch (d.kind) {
            case TypeKind::Unknown:
            case TypeKind::TypeParam:
                bail_to_all = true;
                return;
            case TypeKind::Borrow:
            case TypeKind::MutBorrow:
            case TypeKind::Optional:
            case TypeKind::Slice:
            case TypeKind::Array:
            case TypeKind::Own:
            case TypeKind::Shared:
                if (d.elem_type.is_valid()) self(self, d.elem_type, tparams);
                return;
            case TypeKind::Map:
                if (d.key_type.is_valid()) self(self, d.key_type, tparams);
                if (d.elem_type.is_valid()) self(self, d.elem_type, tparams);
                return;
            case TypeKind::Tuple:
                for (TypeId pt : d.param_types) self(self, pt, tparams);
                return;
            case TypeKind::GenericInst:
                if (!d.base_name.empty()) {
                    if (tparams.contains(d.base_name)) { bail_to_all = true; return; }
                    add_named_decl(d.base_name, d.package_name);
                }
                for (TypeId ta : d.type_args) self(self, ta, tparams);
                return;
            case TypeKind::NominalStruct:
            case TypeKind::NominalEnum:
                if (!d.name.empty()) {
                    if (tparams.contains(d.name)) { bail_to_all = true; return; }
                    add_named_decl(d.name, d.package_name);
                }
                return;
            default:
                return;
        }
    };

    auto is_json_ns = [&](std::string_view ns) -> bool {
        if (ns == "json") return true;
        auto it = cf.core_aliases.find(std::string(ns));
        return it != cf.core_aliases.end() && it->second == "json";
    };

    auto scan_expr_node = [&](const ExprPtr& e, std::string_view cur_pkg,
                              const std::unordered_set<std::string>& tparams) -> bool {
        if (bail_to_all) return false;
        const auto* call = std::get_if<CallExpr>(&e->node);
        if (!call || !call->callee) return true;
        std::string method;
        if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node);
            mem && mem->object) {
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node);
                obj_id && is_json_ns(obj_id->name)) {
                method = mem->field;
            }
        } else if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
            std::string norm = normalize_type_lookup_name(id->name);
            if (auto dot = norm.find('.'); dot != std::string::npos && is_json_ns(norm.substr(0, dot))) {
                method = norm.substr(dot + 1);
            }
        }
        if (method == "stringify") {
            if (!call->args.empty() && call->args[0].value) {
                if (call->args[0].value->resolved_type_id == 0) {
                    bail_to_all = true;
                    return false;
                }
                seed_typeid(seed_typeid, TypeId{call->args[0].value->resolved_type_id}, tparams);
            }
        } else if (method == "parse" || method == "schema") {
            if (!call->type_args.empty()) {
                seed_typeref(seed_typeref, call->type_args[0], cur_pkg, tparams);
            }
        }
        return true;
    };

    for (const auto& st : cf.ast.structs) {
        std::unordered_set<std::string> st_tparams;
        for (const auto& tp : st.type_params) st_tparams.insert(tp.name);
        for (const auto& fld : st.fields) {
            if (fld.default_val) {
                visit_expr_tree(fld.default_val, [&](const ExprPtr& e) {
                    return scan_expr_node(e, st.package_name, st_tparams);
                });
            }
            if (bail_to_all) return std::nullopt;
        }
    }
    for (const auto& gv : cf.ast.global_vars) {
        std::unordered_set<std::string> empty_tp;
        if (gv.value) {
            visit_expr_tree(gv.value, [&](const ExprPtr& e) {
                return scan_expr_node(e, gv.package_name, empty_tp);
            });
        }
        if (bail_to_all) return std::nullopt;
    }
    for (const auto& fn : cf.ast.functions) {
        std::unordered_set<std::string> fn_tp;
        for (const auto& tp : fn.type_params) fn_tp.insert(tp.name);
        if (fn.receiver) {
            for (const auto& ta : fn.receiver->type.type_args) {
                if (!ta.name.empty() && !is_json_primitive_name(ta.name) &&
                    !find_struct_decl(cf, ta.name, fn.package_name) &&
                    !find_enum_decl(cf, ta.name, fn.package_name)) {
                    fn_tp.insert(ta.name);
                }
            }
        }
        visit_exprs(fn.body, [&](const ExprPtr& e) {
            return scan_expr_node(e, fn.package_name, fn_tp);
        });
        if (bail_to_all) return std::nullopt;
    }
    for (const auto& td : cf.ast.tests) {
        std::unordered_set<std::string> empty_tp;
        visit_exprs(td.body, [&](const ExprPtr& e) {
            return scan_expr_node(e, td.package_name, empty_tp);
        });
        if (bail_to_all) return std::nullopt;
    }

    // 3. Transitive closure through struct fields and enum variant payloads.
    const std::unordered_set<std::string> empty_tp;
    std::size_t cursor = 0;
    while (cursor < worklist.size()) {
        std::string cur_name = worklist[cursor++];
        if (const StructDecl* st = find_struct_decl(cf, cur_name)) {
            for (std::size_t i = 0; i < st->fields.size(); ++i) {
                if (is_field_json_ignored(cf, *st, i)) continue;
                seed_typeref(seed_typeref, st->fields[i].type, st->package_name, empty_tp);
            }
        } else if (const EnumDecl* en = find_enum_decl(cf, cur_name)) {
            for (const auto& v : en->variants) {
                for (const auto& pt : v.payload_types) {
                    seed_typeref(seed_typeref, pt, en->package_name, empty_tp);
                }
            }
        }
    }

    return needed;
}

} // namespace cfire::frontend::json_support
