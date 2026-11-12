#pragma once
// src/sema/analyze.hpp — Top-Level AST Semantic Analysis Drivers

#include "types.hpp"
#include "stdlib_index.hpp"
#include "fn_checker.hpp"
#include "builtin_methods.hpp"
#include <algorithm>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfire::frontend {

[[nodiscard]] inline auto analyze_resilient(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params = {}
) -> CheckedFile {
    CheckedFile cf;
    cf.ast = std::move(ast);
    cf.cpp_fn_params = std::move(pre_cpp_fn_params);

    // Populate import qualifiers, aliases, and shadowed default qualifiers
    std::unordered_set<std::string> unaliased_core_modules;
    bool has_unaliased_cpp = false;
    for (const auto& imp : cf.ast.imports) {
        if (imp.is_cpp) {
            if (!imp.alias.empty() && imp.alias != "cpp") {
                cf.cpp_qualifiers.insert(imp.alias);
                cf.cpp_alias_to_header[imp.alias] = imp.path;
            } else {
                has_unaliased_cpp = true;
                cf.cpp_qualifiers.insert("cpp");
                cf.cpp_alias_to_header["cpp"] = imp.path;
            }
        } else {
            auto [ns_alias, is_vendor] = cfire::modules::canonical_ns_for_import(imp.path);
            if (is_vendor) {
                std::string vendor_key = (ns_alias == "rl") ? "raylib" : (ns_alias == "rg") ? "raygui" : ns_alias;
                if (!imp.alias.empty() && imp.alias != vendor_key && imp.alias != ns_alias) {
                    cf.vendor_aliases[imp.alias] = vendor_key;
                    cf.shadowed_default_qualifiers[vendor_key] = imp.alias;
                    if (ns_alias != imp.alias) {
                        cf.shadowed_default_qualifiers[ns_alias] = imp.alias;
                    }
                } else {
                    std::string active = imp.alias.empty() ? vendor_key : imp.alias;
                    cf.vendor_aliases[active] = vendor_key;
                    cf.vendor_aliases[vendor_key] = vendor_key;
                    cf.vendor_aliases[ns_alias] = vendor_key;
                    unaliased_core_modules.insert(vendor_key);
                    unaliased_core_modules.insert(ns_alias);
                }
            } else {
                std::string canon_ns = ns_alias;
                if (canon_ns.empty()) {
                    auto slash = imp.path.rfind('/');
                    auto colon = imp.path.rfind(':');
                    std::size_t sep = (colon != std::string::npos && slash != std::string::npos) ? std::max(colon, slash) : (colon != std::string::npos ? colon : slash);
                    canon_ns = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                }
                if (!imp.alias.empty() && imp.alias != canon_ns) {
                    cf.core_aliases[imp.alias] = canon_ns;
                    cf.shadowed_default_qualifiers[canon_ns] = imp.alias;
                } else {
                    std::string active = imp.alias.empty() ? canon_ns : imp.alias;
                    cf.core_aliases[active] = canon_ns;
                    unaliased_core_modules.insert(canon_ns);
                }
            }
        }
    }
    for (const auto& un : unaliased_core_modules) {
        cf.shadowed_default_qualifiers.erase(un);
    }
    if (has_unaliased_cpp) {
        cf.shadowed_default_qualifiers.erase("cpp");
    } else if (!cf.cpp_qualifiers.empty() && !cf.cpp_qualifiers.contains("cpp")) {
        cf.shadowed_default_qualifiers["cpp"] = *cf.cpp_qualifiers.begin();
    }

    // 0a. Check Module Imports (CF-006 / CF-086 / CF-105)
    for (const auto& imp : cf.ast.imports) {
        if (!imp.is_cpp) {
            if (auto canon = cfire::modules::canonical_import_for_wrong_prefix(imp.path); !canon.empty()) {
                if (canon == "ext:crypto/sodium") {
                    cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/INVALID-IMPORT", std::format("module '{}' requires native libsodium and must be imported as 'ext:crypto/sodium'", imp.path), {{"from", imp.path}, {"to", "ext:crypto/sodium"}}));
                } else if (canon.starts_with("ext:")) {
                    cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/INVALID-IMPORT", std::format("module '{}' requires a native extension engine and must be imported as '{}'", imp.path, canon), {{"from", imp.path}, {"to", canon}}));
                } else {
                    cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/INVALID-IMPORT", std::format("module '{}' is part of the zero-setup core standard library and must be imported as '{}'", imp.path, canon), {{"from", imp.path}, {"to", canon}}));
                }
            } else if (std::find(kRetiredModules.begin(), kRetiredModules.end(), imp.path) != kRetiredModules.end()) {
                std::string_view what = imp.path == "core:str" ? "'str' methods (s.trim(), s.split(sep), parts.join(sep), i32.parse(s))"
                                                               : "slice methods (xs.sort(), xs.sort_by(f), xs.binary_search(v))";
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/RETIRED-API", std::format("module '{}' has been retired; use {}", imp.path, what), {{"rewrite", "delete_line"}, {"from", imp.path}}));
            } else if (!imp.path.starts_with("./") && !imp.path.starts_with("../") && !imp.path.ends_with(".cf") && !cfire::modules::is_known_module(imp.path)) {
                std::string best_match;
                std::size_t best_dist = 5;
                for (std::string_view km : cfire::modules::known_module_paths()) {
                    std::size_t d = StdlibCatalogIndex::edit_distance(imp.path, km);
                    if (d < best_dist) {
                        best_dist = d;
                        best_match = std::string(km);
                    }
                }
                std::string hint = best_match.empty() ? "" : std::format("; did you mean '{}'?", best_match);
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/UNKNOWN-MODULE", std::format("unknown module '{}'{}", imp.path, hint)));
            }
        }
    }

    // 0b. Check Duplicate Symbol Declarations (CF-104)
    struct DeclLocation {
        std::string path;
        int line = 1;
        int col = 1;
    };
    std::unordered_map<std::string, DeclLocation> declared_types;
    std::unordered_map<std::string, DeclLocation> declared_functions;
    std::unordered_map<std::string, DeclLocation> declared_methods;
    std::unordered_map<std::string, DeclLocation> declared_globals;

    for (const auto& st : cf.ast.structs) {
        std::string st_path = st.source_path.empty() ? cf.ast.path : st.source_path;
        if (auto it = declared_types.find(st.name); it != declared_types.end()) {
            cf.add_error(cfire::diag::diag(st_path, st.line, 0, "cfire/DUPLICATE-DECLARATION", std::format("duplicate declaration of type '{}'; previously declared at {}:{}", st.name, it->second.path, it->second.line)));
        } else {
            declared_types[st.name] = DeclLocation{st_path, st.line, 1};
        }
    }

    for (const auto& en : cf.ast.enums) {
        std::string en_path = en.source_path.empty() ? cf.ast.path : en.source_path;
        if (auto it = declared_types.find(en.name); it != declared_types.end()) {
            cf.add_error(cfire::diag::diag(en_path, en.line, 0, "cfire/DUPLICATE-DECLARATION", std::format("duplicate declaration of type '{}'; previously declared at {}:{}", en.name, it->second.path, it->second.line)));
        } else {
            declared_types[en.name] = DeclLocation{en_path, en.line, 1};
        }

        std::unordered_map<std::string, DeclLocation> seen_variants;
        for (const auto& v : en.variants) {
            if (auto vit = seen_variants.find(v.name); vit != seen_variants.end()) {
                cf.add_error(cfire::diag::diag(en_path, v.line, 0, "cfire/DUPLICATE-DECLARATION", std::format("duplicate declaration of enum variant '{}.{}'; previously declared at {}:{}", en.name, v.name, vit->second.path, vit->second.line)));
            } else {
                seen_variants[v.name] = DeclLocation{en_path, v.line, 1};
            }
        }
    }

    for (const auto& gv : cf.ast.global_vars) {
        std::string gv_path = gv.source_path.empty() ? cf.ast.path : gv.source_path;
        std::string pkg = gv.package_name.empty() ? cf.ast.package_name : gv.package_name;
        std::string gv_key = pkg + "::" + gv.name;
        if (auto it = declared_globals.find(gv_key); it != declared_globals.end()) {
            cf.add_error(cfire::diag::diag(gv_path, gv.line, 0, "cfire/DUPLICATE-DECLARATION", std::format("duplicate declaration of variable '{}'; previously declared at {}:{}", gv.name, it->second.path, it->second.line)));
        } else {
            declared_globals[gv_key] = DeclLocation{gv_path, gv.line, 1};
        }
    }

    // CF-123: Lint clusters of 3+ identically prefixed integer constants (VIEW_*, MODE_*)
    struct PrefixConstInfo {
        std::string source_path;
        int line = 1;
        std::vector<std::string> const_names;
    };
    std::unordered_map<std::string, PrefixConstInfo> prefix_clusters;
    for (const auto& gv : cf.ast.global_vars) {
        if (gv.is_mut || !gv.value) continue;
        const auto* lit = std::get_if<LiteralExpr>(&gv.value->node);
        if (!lit || lit->kind != TokenKind::Int) continue;
        auto us = gv.name.find('_');
        if (us != std::string::npos && us > 0) {
            std::string pfx = gv.name.substr(0, us);
            auto& entry = prefix_clusters[pfx];
            if (entry.const_names.empty()) {
                entry.source_path = gv.source_path.empty() ? cf.ast.path : gv.source_path;
                entry.line = gv.line;
            }
            entry.const_names.push_back(gv.name);
        }
    }
    for (const auto& [pfx, info] : prefix_clusters) {
        if (info.const_names.size() >= 3) {
            cf.add_warning(cfire::diag::diag(info.source_path, info.line, 0, "cfire/PREFER-ENUM", std::format("cluster of {} related integer constants with prefix '{}' detected; prefer an enum instead", info.const_names.size(), pfx), {{"prefix", pfx}}));
        }
    }

    for (const auto& fn : cf.ast.functions) {
        std::string fn_path = fn.source_path.empty() ? cf.ast.path : fn.source_path;
        if (fn.receiver) {
            std::string m_key = fn.receiver->type.name + "." + fn.name;
            if (auto it = declared_methods.find(m_key); it != declared_methods.end()) {
                cf.add_error(cfire::diag::diag(fn_path, fn.line, 0, "cfire/DUPLICATE-DECLARATION", std::format("duplicate declaration of method '{}.{}'; previously declared at {}:{}", fn.receiver->type.name, fn.name, it->second.path, it->second.line)));
            } else {
                declared_methods[m_key] = DeclLocation{fn_path, fn.line, 1};
            }
        } else {
            std::string pkg = fn.package_name.empty() ? cf.ast.package_name : fn.package_name;
            std::string fn_key = pkg + "::" + fn.name;
            if (auto it = declared_functions.find(fn_key); it != declared_functions.end()) {
                cf.add_error(cfire::diag::diag(fn_path, fn.line, 0, "cfire/DUPLICATE-DECLARATION", std::format("duplicate declaration of function '{}'; previously declared at {}:{}", fn.name, it->second.path, it->second.line)));
            } else {
                declared_functions[fn_key] = DeclLocation{fn_path, fn.line, 1};
            }
        }
    }

    // 1. Index Interfaces and Enums
    for (const auto& iface : cf.ast.interfaces) {
        cf.interfaces.insert(iface.name);
    }
    for (const auto& en : cf.ast.enums) {
        cf.enums[en.name] = en;
        for (const auto& v : en.variants) {
            if (cf.variant_to_enum.contains(v.name)) {
                cf.ambiguous_variants.insert(v.name);
            }
            cf.variant_to_enum[v.name] = en.name;
        }
    }

    // Populate type table with user-defined structs and enums (CF-116)
    for (const auto& st : cf.ast.structs) {
        TypeDesc d;
        d.kind = TypeKind::NominalStruct;
        d.name = st.name;
        d.package_name = st.package_name;
        (void)cf.type_table.intern(std::move(d));
    }
    for (const auto& en : cf.ast.enums) {
        TypeDesc d;
        d.kind = TypeKind::NominalEnum;
        d.name = en.name;
        d.package_name = en.package_name;
        (void)cf.type_table.intern(std::move(d));
    }

    std::unordered_map<std::string, std::size_t> fn_counts_sema;
    for (const auto& fn : cf.ast.functions) {
        if (!fn.receiver) fn_counts_sema[fn.name]++;
    }
    for (const auto& [name, count] : fn_counts_sema) {
        if (count > 1) cf.colliding_fn_names.insert(name);
    }
    std::unordered_map<std::string, std::size_t> gv_counts_sema;
    for (const auto& gv : cf.ast.global_vars) {
        gv_counts_sema[gv.name]++;
    }
    for (const auto& [name, count] : gv_counts_sema) {
        if (count > 1) cf.colliding_gv_names.insert(name);
    }

    // Validate no dynamic slices or arrays of structural interfaces (CF-108)
    auto check_type_ref_separators_ast = [&](auto& self, const TypeRef& tr, std::string_view src_path, int line, int col) -> void {
        if (!tr.raw_name.empty()) {
            auto dcol = tr.raw_name.find("::");
            auto dot = tr.raw_name.find('.');
            if (dcol != std::string::npos && (dot == std::string::npos || dcol < dot)) {
                std::string qual = tr.raw_name.substr(0, dcol);
                if (!cf.enums.contains(qual) &&
                    (qual == "cpp" || cf.cpp_qualifiers.contains(qual) || cf.vendor_aliases.contains(qual) ||
                     cf.core_aliases.contains(qual) || cf.shadowed_default_qualifiers.contains(qual))) {
                    std::string rest = tr.raw_name.substr(dcol + 2);
                    cf.add_error(cfire::diag::diag(src_path, line, col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", qual, rest, qual, rest), {{"kind", "qualifier"}}));
                }
            }
        }
        if (tr.elem_type) {
            self(self, *tr.elem_type, src_path, line, col);
        }
        if (tr.key_type) {
            self(self, *tr.key_type, src_path, line, col);
        }
        for (const auto& ga : tr.type_args) {
            self(self, ga, src_path, line, col);
        }
        for (const auto& tup : tr.tuple_types) {
            self(self, tup, src_path, line, col);
        }
    };
    // Q28: struct fields are snake_case; `cfire fix` renames the field and every use across the workspace.
    auto snake_case_of = [](std::string_view n) -> std::string {
        std::string out;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(n[i]);
            if (std::isupper(c)) {
                const bool prev_lower = i > 0 && (std::islower(static_cast<unsigned char>(n[i - 1])) || std::isdigit(static_cast<unsigned char>(n[i - 1])));
                const bool next_lower = i + 1 < n.size() && std::islower(static_cast<unsigned char>(n[i + 1]));
                const bool prev_upper = i > 0 && std::isupper(static_cast<unsigned char>(n[i - 1]));
                if (i > 0 && n[i - 1] != '_' && (prev_lower || (prev_upper && next_lower))) out.push_back('_');
                out.push_back(static_cast<char>(std::tolower(c)));
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return out;
    };
    std::unordered_set<std::string> field_case_seen;  // merged package views list a struct under `./f.cf` and `f.cf`
    for (const auto& st : cf.ast.structs) {
        std::string_view st_path = st.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(st.source_path);
        for (const auto& fld : st.fields) {
            std::string seen_key = std::format("{}:{}:{}.{}", std::filesystem::path(st_path).lexically_normal().string(), fld.line, st.name, fld.name);
            if (std::string snake = snake_case_of(fld.name); snake != fld.name && field_case_seen.insert(seen_key).second) {
                bool keyword_clash = cfire::frontend::find_keyword(snake).has_value();
                cf.add_warning(cfire::diag::diag(st_path, fld.line, 0, "cfire/FIELD-NAME-CASE",
                    std::format("field '{}.{}' should be snake_case ('{}'){}", st.name, fld.name, snake,
                        keyword_clash ? "; that name is a keyword, so rename it by hand" : "; untagged JSON/CLI keys follow the field name"),
                    {{"field", fld.name}, {"to", keyword_clash ? std::string() : snake}}));
            }
            check_type_ref_separators_ast(check_type_ref_separators_ast, fld.type, st_path, fld.line, 1);
            if (auto iface = find_interface_in_slice(fld.type, cf.interfaces)) {
                cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface)));
            }
        }
    }
    for (const auto& fn : cf.ast.functions) {
        std::string_view fn_path = fn.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(fn.source_path);
        if (fn.receiver) {
            check_type_ref_separators_ast(check_type_ref_separators_ast, fn.receiver->type, fn_path, fn.line, 1);
        }
        for (const auto& p : fn.params) {
            check_type_ref_separators_ast(check_type_ref_separators_ast, p.type, fn_path, p.line, p.col);
            if (auto iface = find_interface_in_slice(p.type, cf.interfaces)) {
                cf.add_error(cfire::diag::diag(fn_path, p.line, p.col, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface)));
            }
        }
        check_type_ref_separators_ast(check_type_ref_separators_ast, fn.return_type, fn_path, fn.line, 1);
        if (auto iface = find_interface_in_slice(fn.return_type, cf.interfaces)) {
            cf.add_error(cfire::diag::diag(fn_path, fn.line, 0, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface)));
        }
    }
    for (const auto& gv : cf.ast.global_vars) {
        if (gv.type) {
            std::string_view gv_path = gv.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(gv.source_path);
            check_type_ref_separators_ast(check_type_ref_separators_ast, *gv.type, gv_path, gv.line, 1);
            if (auto iface = find_interface_in_slice(*gv.type, cf.interfaces)) {
                cf.add_error(cfire::diag::diag(gv_path, gv.line, 0, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface)));
            }
        }
    }

    // 2. Index Struct Field Tags & Validate Tag Invariants (TEST-03a)
    for (const auto& st : cf.ast.structs) {
        std::string_view st_path = st.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(st.source_path);
        std::vector<StructTagInfo> tags;
        std::unordered_map<std::string, std::string> seen_json_keys;
        std::unordered_map<std::string, std::string> seen_db_cols;
        std::unordered_map<std::string, std::string> seen_cli_longs;
        std::unordered_map<std::string, std::string> seen_cli_shorts;

        for (const auto& fld : st.fields) {
            StructTagInfo info;
            info.field_name = fld.name;
            info.field_type = fld.type;
            if (fld.raw_tag.empty()) {
                tags.push_back(std::move(info));
                continue;
            }

            for (const auto& [key, val] : parse_struct_tags(fld.raw_tag)) {
                if (key == "cli") {
                    auto comma = val.find(',');
                    if (comma != std::string::npos) {
                        info.cli_long = trim_str(std::string_view(val).substr(0, comma));
                        info.cli_short = trim_str(std::string_view(val).substr(comma + 1));
                    } else {
                        info.cli_long = trim_str(val);
                    }
                    if (!info.cli_long.empty() && info.cli_long != "-") {
                        if (auto prev = seen_cli_longs.find(info.cli_long); prev != seen_cli_longs.end()) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("duplicate cli flag '--{}' on field '{}.{}' (already used by field '{}')", info.cli_long, st.name, fld.name, prev->second)));
                        } else {
                            seen_cli_longs[info.cli_long] = fld.name;
                        }
                    }
                    if (!info.cli_short.empty() && info.cli_short != "-") {
                        if (auto prev = seen_cli_shorts.find(info.cli_short); prev != seen_cli_shorts.end()) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("duplicate cli short flag '-{}' on field '{}.{}' (already used by field '{}')", info.cli_short, st.name, fld.name, prev->second)));
                        } else {
                            seen_cli_shorts[info.cli_short] = fld.name;
                        }
                    }
                } else if (key == "help") {
                    info.cli_help = val;
                } else if (key == "json") {
                    auto comma = val.find(',');
                    info.json_key = trim_str(comma != std::string_view::npos ? std::string_view(val).substr(0, comma) : std::string_view(val));
                    if (!info.json_key.empty() && info.json_key != "-") {
                        if (auto prev = seen_json_keys.find(info.json_key); prev != seen_json_keys.end()) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("duplicate json tag key '{}' on field '{}.{}' (already used by field '{}')", info.json_key, st.name, fld.name, prev->second)));
                        } else {
                            seen_json_keys[info.json_key] = fld.name;
                        }
                    }
                } else if (key == "db" || key == "sql") {
                    info.db_column = trim_str(val);
                    if (!info.db_column.empty() && info.db_column != "-") {
                        if (auto prev = seen_db_cols.find(info.db_column); prev != seen_db_cols.end()) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("duplicate {} column tag '{}' on field '{}.{}' (already used by field '{}')", key, info.db_column, st.name, fld.name, prev->second)));
                        } else {
                            seen_db_cols[info.db_column] = fld.name;
                        }
                    }
                } else if (key == "default") {
                    std::string dval = trim_str(val);
                    const std::string& tname = fld.type.name;
                    if (tname == "i8" || tname == "i16" || tname == "i32" || tname == "i64" || tname == "int") {
                        if (!cfire::detail::parse_int<std::int64_t>(dval)) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid default tag value '{}' for integer field '{}.{}' of type '{}'", dval, st.name, fld.name, tname)));
                        }
                    } else if (tname == "u8" || tname == "u16" || tname == "u32" || tname == "u64" || tname == "usize" || tname == "byte") {
                        if (dval.starts_with('-') || !cfire::detail::parse_int<std::uint64_t>(dval)) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid default tag value '{}' for unsigned field '{}.{}' of type '{}'", dval, st.name, fld.name, tname)));
                        }
                    } else if (tname == "bool") {
                        if (dval != "true" && dval != "false" && dval != "1" && dval != "0") {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid default tag value '{}' for bool field '{}.{}' (expected 'true' or 'false')", dval, st.name, fld.name)));
                        }
                    } else if (tname == "f32" || tname == "f64") {
                        char* endp = nullptr;
                        (void)std::strtod(dval.c_str(), &endp);
                        if (dval.empty() || endp != dval.c_str() + dval.size()) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid default tag value '{}' for float field '{}.{}' of type '{}'", dval, st.name, fld.name, tname)));
                        }
                    }
                } else if (key == "validate") {
                    const std::string& tname = fld.type.name;
                    bool is_int_type = (tname == "i8" || tname == "i16" || tname == "i32" || tname == "i64" || tname == "isize" || tname == "int");
                    bool is_uint_type = (tname == "u8" || tname == "u16" || tname == "u32" || tname == "u64" || tname == "usize" || tname == "byte");
                    bool is_float_type = (tname == "f32" || tname == "f64");
                    bool is_num_type = is_int_type || is_uint_type || is_float_type;
                    bool is_len_type = (tname == "str" || fld.type.is_slice);

                    std::size_t rpos = 0;
                    while (rpos <= val.size()) {
                        auto comma = val.find(',', rpos);
                        std::string r_tok = trim_str(comma == std::string::npos
                            ? std::string_view(val).substr(rpos)
                            : std::string_view(val).substr(rpos, comma - rpos));
                        rpos = (comma == std::string::npos) ? (val.size() + 1) : (comma + 1);
                        if (r_tok.empty()) continue;

                        if (r_tok == "non_empty") {
                            if (!is_len_type) {
                                cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("validate rule 'non_empty' on field '{}.{}' requires 'str' or slice type, got '{}'", st.name, fld.name, tname)));
                            } else {
                                info.validation.non_empty = true;
                                info.validation.has_rules = true;
                            }
                        } else if (r_tok.starts_with("min=") || r_tok.starts_with("max=")) {
                            bool is_min = r_tok.starts_with("min=");
                            std::string nstr = trim_str(std::string_view(r_tok).substr(4));
                            if (!is_num_type) {
                                cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("validate rule '{}' on field '{}.{}' requires a numeric field type, got '{}'", r_tok, st.name, fld.name, tname)));
                            } else {
                                char* endp = nullptr;
                                double num = std::strtod(nstr.c_str(), &endp);
                                bool valid_num = !nstr.empty() && endp == nstr.c_str() + nstr.size();
                                if (is_int_type && !cfire::detail::parse_int<std::int64_t>(nstr)) valid_num = false;
                                if (is_uint_type && (nstr.starts_with('-') || !cfire::detail::parse_int<std::uint64_t>(nstr))) valid_num = false;
                                if (!valid_num) {
                                    cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid numeric bound '{}' in validate tag on field '{}.{}' of type '{}'", r_tok, st.name, fld.name, tname)));
                                } else {
                                    info.validation.has_rules = true;
                                    if (is_min) {
                                        info.validation.has_min = true;
                                        info.validation.min_raw = nstr;
                                        info.validation.min_num = num;
                                    } else {
                                        info.validation.has_max = true;
                                        info.validation.max_raw = nstr;
                                        info.validation.max_num = num;
                                    }
                                }
                            }
                        } else if (r_tok.starts_with("min_len=") || r_tok.starts_with("max_len=")) {
                            bool is_min_l = r_tok.starts_with("min_len=");
                            std::string lstr = trim_str(std::string_view(r_tok).substr(8));
                            if (!is_len_type) {
                                cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("validate rule '{}' on field '{}.{}' requires 'str' or slice type, got '{}'", r_tok, st.name, fld.name, tname)));
                            } else {
                                auto parsed_len = (!lstr.starts_with('-')) ? cfire::detail::parse_int<std::uint64_t>(lstr) : std::nullopt;
                                if (!parsed_len) {
                                    cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid length bound '{}' in validate tag on field '{}.{}'", r_tok, st.name, fld.name)));
                                } else {
                                    info.validation.has_rules = true;
                                    if (is_min_l) {
                                        info.validation.has_min_len = true;
                                        info.validation.min_len = static_cast<std::size_t>(*parsed_len);
                                    } else {
                                        info.validation.has_max_len = true;
                                        info.validation.max_len = static_cast<std::size_t>(*parsed_len);
                                    }
                                }
                            }
                        } else if (r_tok.starts_with("oneof=")) {
                            std::string opts_str = trim_str(std::string_view(r_tok).substr(6));
                            if (tname != "str") {
                                cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("validate rule '{}' on field '{}.{}' requires 'str' field type, got '{}'", r_tok, st.name, fld.name, tname)));
                            } else {
                                std::vector<std::string> opts;
                                std::size_t opos = 0;
                                while (opos <= opts_str.size()) {
                                    auto bar = opts_str.find('|', opos);
                                    std::string item = trim_str(bar == std::string::npos
                                        ? std::string_view(opts_str).substr(opos)
                                        : std::string_view(opts_str).substr(opos, bar - opos));
                                    opos = (bar == std::string::npos) ? (opts_str.size() + 1) : (bar + 1);
                                    if (!item.empty()) opts.push_back(std::move(item));
                                }
                                if (opts.empty()) {
                                    cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("empty 'oneof=' option list in validate tag on field '{}.{}'", st.name, fld.name)));
                                } else {
                                    info.validation.has_rules = true;
                                    info.validation.oneof = std::move(opts);
                                }
                            }
                        } else {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("unknown validate tag rule '{}' on field '{}.{}' (expected min=, max=, min_len=, max_len=, non_empty, or oneof=)", r_tok, st.name, fld.name)));
                        }
                    }
                    if (info.validation.has_min && info.validation.has_max && info.validation.min_num > info.validation.max_num) {
                        cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("validate tag on field '{}.{}' has min ({}) > max ({})", st.name, fld.name, info.validation.min_raw, info.validation.max_raw)));
                    }
                    if (info.validation.has_min_len && info.validation.has_max_len && info.validation.min_len > info.validation.max_len) {
                        cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("validate tag on field '{}.{}' has min_len ({}) > max_len ({})", st.name, fld.name, info.validation.min_len, info.validation.max_len)));
                    }
                }
                info.custom_tags[key] = val;
            }
            tags.push_back(std::move(info));
        }
        cf.struct_tags[st.name] = std::move(tags);
    }

    // 3. Index Functions & Methods
    for (const auto& fn : cf.ast.functions) {
        if (fn.receiver) {
            std::string key = fn.receiver->type.name + "::" + fn.name;
            cf.methods[key] = fn;
            cf.method_owner[fn.name] = fn.receiver->type.name;
            cf.method_owners[fn.name].insert(fn.receiver->type.name);
        } else {
            if (!fn.package_name.empty()) {
                cf.fn_sigs[fn.package_name + "." + fn.name] = fn;
            }
            if (fn.package_name.empty() || !cf.fn_sigs.contains(fn.name)) {
                cf.fn_sigs[fn.name] = fn;
            }
        }
    }

    // 3b. Resolve Global Variables & Struct Default Value Expressions (CF-115)
    {
        FnDecl gv_dummy_fn;
        gv_dummy_fn.name = "__cfire_global_init";
        SemaFnChecker gv_checker(cf.ast.path, gv_dummy_fn, cf, cf.warnings, cf.errors, cf.diagnostics, true);
        for (const auto& gv : cf.ast.global_vars) {
            if (gv.value) {
                (void)gv_checker.check_expr(gv.value);
            }
        }
        for (const auto& st : cf.ast.structs) {
            for (const auto& fld : st.fields) {
                if (fld.default_val) {
                    (void)gv_checker.check_expr(fld.default_val);
                }
            }
        }
    }

    // 4. Validate Function & Method Bodies
    for (const auto& fn : cf.ast.functions) {
        std::string_view fn_path = fn.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(fn.source_path);
        SemaFnChecker checker(fn_path, fn, cf, cf.warnings, cf.errors, cf.diagnostics, true);
        for (const auto& st : fn.body) {
            (void)checker.check_stmt(st);
        }
        if (!fn.return_type.name.empty() && fn.return_type.name != "void" && fn.name != "main") {
            if (!SemaFnChecker::block_diverges(fn.body, &cf)) {
                cf.add_error(cfire::diag::diag(fn_path, fn.line, 0, "cfire/MISSING-RETURN", std::format("missing return statement on all execution paths in function '{}' returning '{}'", fn.name, fn.return_type.name)));
            }
        }
        std::string fn_key = fn.receiver ? (fn.receiver->type.name + "__" + fn.name) : fn.name;
        for (const auto& [p_name, eid] : checker.last_read_expr_id) {
            cf.last_use_move[fn_key + ":" + p_name] = eid;
            if (!fn.package_name.empty() && !fn.receiver) {
                cf.last_use_move[std::format("cfire_pkg_{}__{}:{}", fn.package_name, fn.name, p_name)] = eid;
            }
        }
    }

    // 5. Validate Test, Fuzz, and Bench Declarations
    for (std::size_t idx = 0; idx < cf.ast.tests.size(); ++idx) {
        const auto& td = cf.ast.tests[idx];
        std::string_view td_path = td.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(td.source_path);
        FnDecl dummy_fn;
        dummy_fn.name = std::format("__cfire_test_{}", idx);
        dummy_fn.return_type.is_fallible = true;
        dummy_fn.params = td.params;
        for (const auto& bv : td.bench_variations) {
            dummy_fn.params.push_back(ParamDecl{"in", bv.name, bv.inferred_type, bv.line, 1});
        }
        SemaFnChecker checker(td_path, dummy_fn, cf, cf.warnings, cf.errors, cf.diagnostics, true);
        for (const auto& bv : td.bench_variations) {
            for (const auto& vex : bv.values) {
                (void)checker.check_expr(vex);
            }
        }
        for (const auto& st : td.body) {
            (void)checker.check_stmt(st);
        }
    }

    return cf;
}

[[nodiscard]] inline auto analyze(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params = {}
) -> std::expected<CheckedFile, std::string> {
    auto cf = analyze_resilient(std::move(ast), std::move(pre_cpp_fn_params));
    if (!cf.errors.empty()) {
        return std::unexpected(cf.errors.front());
    }
    return cf;
}


} // namespace cfire::frontend
