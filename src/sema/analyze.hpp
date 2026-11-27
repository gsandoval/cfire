#pragma once
// src/sema/analyze.hpp — Top-Level AST Semantic Analysis Drivers

#include "types.hpp"
#include "json_support.hpp"
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

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_SEMA_ANALYZE_INLINE
#else
#define CFIRE_SEMA_ANALYZE_INLINE inline
#endif

namespace cfire::frontend {

[[nodiscard]] CFIRE_SEMA_ANALYZE_INLINE auto analyze_resilient(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params = {},
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns = {}
) -> CheckedFile;

[[nodiscard]] CFIRE_SEMA_ANALYZE_INLINE auto analyze(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params = {},
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns = {}
) -> std::expected<CheckedFile, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_SEMA_STMT_IMPL)
[[nodiscard]] CFIRE_SEMA_ANALYZE_INLINE auto analyze_resilient(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params,
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns
) -> CheckedFile {
    CheckedFile cf;
    cf.ast = std::move(ast);
    cf.cpp_fn_params = std::move(pre_cpp_fn_params);
    cf.cpp_fn_returns = std::move(pre_cpp_fn_returns);
    cf.has_cpp_interop = !cf.cpp_fn_params.empty();

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
                std::string vendor_key = ns_alias;
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
    if (!cf.cpp_qualifiers.empty()) {
        cf.has_cpp_interop = true;
    }
    if (has_unaliased_cpp) {
        cf.shadowed_default_qualifiers.erase("cpp");
    } else if (!cf.cpp_qualifiers.empty() && !cf.cpp_qualifiers.contains("cpp")) {
        cf.shadowed_default_qualifiers["cpp"] = *cf.cpp_qualifiers.begin();
    }

    // 0a. Check Module Imports
    for (const auto& imp : cf.ast.imports) {
        if (!imp.is_cpp) {
            if (auto canon = cfire::modules::canonical_import_for_wrong_prefix(imp.path); !canon.empty()) {
                if (imp.path.starts_with("ext:") == canon.starts_with("ext:")) {
                    cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/INVALID-IMPORT", std::format("module '{}' is a non-canonical import path and must be imported as '{}'", imp.path, canon), {{"from", imp.path}, {"to", canon}}));
                } else if (canon == "ext:crypto/sodium") {
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
            } else if (imp.path == "vendor:raylib" || imp.path == "vendor:raygui") {
                // raylib is no longer bundled. Native library linkage lives in build.cf or --pkg-config.
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/RETIRED-API", std::format(
                    "module '{}' has been retired; cfire no longer bundles raylib. Use 'ext:webui' for GUIs, or import \"raylib.h\" via 'import cpp \"raylib.h\"' with 'PkgConfig: [\"raylib\"]' in 'build.cf' (or '--pkg-config raylib')", imp.path)));
            } else if (imp.path.starts_with("pkg:")) {
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/RETIRED-API", std::format(
                    "import prefix 'pkg:' in '{}' has been retired; use built-in 'core:*' or 'ext:*' modules, or relative './...' paths for local packages", imp.path)));
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
        // Validate import attributes; retire legacy @pkg(...) and @define(...) onto build.cf / CLI flags.
        for (const auto& attr : imp.attrs) {
            if (attr.name == "pkg") {
                std::string pkg_hint = attr.args.empty() ? "\"libname\"" : std::format("\"{}\"", attr.args[0]);
                std::string cli_hint = attr.args.empty() ? "libname" : attr.args[0];
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/RETIRED-API", std::format(
                    "attribute '@pkg(...)' on imports has been retired; move native linkage into 'build.cf' (PkgConfig: [{}] or Libraries: [...]) or pass '--pkg-config {}' for single-file builds",
                    pkg_hint, cli_hint)));
            } else if (attr.name == "define") {
                std::string bcf_hint;
                std::string cli_hint;
                if (attr.args.empty()) {
                    bcf_hint = "Defines: [\"MACRO\"] or build.Define(\"MACRO\")";
                    cli_hint = "--define MACRO' ('-DMACRO";
                } else if (attr.args.size() == 1) {
                    bcf_hint = std::format("Defines: [\"{}\"] or build.Define(\"{}\")", attr.args[0], attr.args[0]);
                    cli_hint = std::format("--define {}' ('-D{}", attr.args[0], attr.args[0]);
                } else {
                    bcf_hint = std::format("Defines: {{ {}: \"{}\" }} or build.Define(\"{}\", \"{}\")",
                        attr.args[0], attr.args[1], attr.args[0], attr.args[1]);
                    cli_hint = std::format("--define {}={}' ('-D{}={}", attr.args[0], attr.args[1], attr.args[0], attr.args[1]);
                }
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/RETIRED-API", std::format(
                    "attribute '@define(...)' on imports has been retired; move preprocessor macro definitions into 'build.cf' ({}) or pass '{}' for single-file builds",
                    bcf_hint, cli_hint)));
            } else {
                cf.add_error(cfire::diag::diag(cf.ast.path, imp.line, 0, "cfire/INVALID-ATTRIBUTE",
                    std::format("unknown import attribute '@{}'; imports do not accept attributes (declare native linkage and preprocessor defines in 'build.cf')", attr.name)));
            }
        }
    }

    // 0b. Check Duplicate Symbol Declarations
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

    // Lint clusters of 3+ identically prefixed integer constants (VIEW_*, MODE_*)
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

    const bool is_build_manifest_file =
        std::filesystem::path(cf.ast.path).filename() == "build.cf" || cf.ast.package_name == "build";

    auto report_private_top = [&](std::string_view cur_pkg, const DeclVisInfo& dv,
                                  std::string_view use_path, int use_line, int use_col) {
        if (dv.is_pub || dv.name.empty()) return;
        std::string ncur = cf.normalize_pkg(cur_pkg);
        std::string ndec = cf.normalize_pkg(dv.package_name);
        if (ncur == ndec) return;
        std::string dedup = std::format("{}:{}:{}:{}.{}",
            cf.normalize_file_key(use_path), use_line, dv.kind, ndec, dv.name);
        if (!cf.reported_private_uses.insert(std::move(dedup)).second) return;
        cf.add_error(cfire::diag::diag(
            use_path, use_line, use_col, "cfire/PRIVATE-SYMBOL",
            std::format("{} '{}.{}' is private to package '{}'; mark it 'pub' to export it",
                dv.kind, ndec, dv.name, ndec),
            {
                {"symbol", dv.name},
                {"kind", dv.kind},
                {"package", ndec},
                {"target_file", dv.source_path},
                {"target_line", std::to_string(dv.line > 0 ? dv.line : 1)},
            }));
    };

    // 1. Index Interfaces and Enums (plus visibility tables & REDUNDANT-PUB checks)
    auto index_type_vis = [&](std::string_view ast_name, std::string_view pkg, std::string_view src_path,
                              int line, std::string_view kind, bool is_pub) {
        std::string npkg = cf.normalize_pkg(pkg);
        std::string orig = CheckedFile::strip_pkg_mangle(ast_name);
        DeclVisInfo dv{std::string(kind), orig, npkg, std::string(src_path), line, is_pub};
        cf.type_visibility[std::string(ast_name)] = dv;
        cf.type_visibility[npkg + "." + orig] = dv;
        if (!is_build_manifest_file && is_pub && npkg == "main") {
            cf.add_warning(cfire::diag::diag(
                src_path, line, 1, "cfire/REDUNDANT-PUB",
                std::format("'pub' on {} '{}' has no effect in package 'main' because 'main' cannot be imported", kind, orig)));
        }
    };

    for (const auto& st : cf.ast.structs) {
        std::string st_path = st.source_path.empty() ? cf.ast.path : st.source_path;
        index_type_vis(st.name, st.package_name, st_path, st.line, "struct", st.is_pub);
    }
    for (const auto& iface : cf.ast.interfaces) {
        cf.interfaces.insert(iface.name);
        cf.interface_defs[iface.name] = iface;
        std::string orig_name = CheckedFile::strip_pkg_mangle(iface.name);
        if (!iface.package_name.empty()) {
            cf.interface_defs[iface.package_name + "." + orig_name] = iface;
        }
        std::string ifc_path = iface.source_path.empty() ? cf.ast.path : iface.source_path;
        index_type_vis(iface.name, iface.package_name, ifc_path, iface.line, "interface", iface.is_pub);
    }
    for (const auto& en : cf.ast.enums) {
        cf.enums[en.name] = en;
        for (const auto& v : en.variants) {
            if (cf.variant_to_enum.contains(v.name)) {
                cf.ambiguous_variants.insert(v.name);
            }
            cf.variant_to_enum[v.name] = en.name;
        }
        std::string en_path = en.source_path.empty() ? cf.ast.path : en.source_path;
        index_type_vis(en.name, en.package_name, en_path, en.line, "enum", en.is_pub);
    }
    for (const auto& gv : cf.ast.global_vars) {
        std::string npkg = cf.normalize_pkg(gv.package_name);
        std::string gv_path = gv.source_path.empty() ? cf.ast.path : gv.source_path;
        if (!is_build_manifest_file && gv.is_pub && npkg == "main") {
            cf.add_warning(cfire::diag::diag(
                gv_path, gv.line, 1, "cfire/REDUNDANT-PUB",
                std::format("'pub' on global '{}' has no effect in package 'main' because 'main' cannot be imported", gv.name)));
        }
    }

    // Populate type table with user-defined structs and enums
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

    // A type two packages both declare is renamed `cfire_pkg_<pkg>__<Name>`; its spelling stays `Name`.
    auto declare_type_name = [&](const std::string& n) {
        cf.declared_type_names.insert(n);
        if (n.starts_with("cfire_pkg_")) {
            if (auto sep = n.find("__"); sep != std::string::npos) cf.declared_type_names.insert(n.substr(sep + 2));
        }
    };
    for (const auto& st : cf.ast.structs) declare_type_name(st.name);
    for (const auto& en : cf.ast.enums) declare_type_name(en.name);
    for (const auto& in : cf.ast.interfaces) declare_type_name(in.name);
    std::unordered_set<std::string> scope_type_params;  // type parameters of the declaration being checked
    std::string active_decl_pkg;
    auto check_type_params_bounds = [&](const std::vector<TypeParam>& tps, std::string_view decl_pkg, std::string_view decl_path, int decl_line) {
        for (const auto& tp : tps) {
            for (const auto& bound : tp.constraints) {
                if (bound.empty()) continue;
                if (cf.interface_defs.contains(bound)) {
                    if (auto dot = bound.find('.'); dot != std::string::npos) {
                        cf.mark_qualifier_used(decl_path, bound.substr(0, dot));
                    }
                    std::string mp = CheckedFile::extract_mangled_pkg(bound);
                    if (!mp.empty() && mp != cf.normalize_pkg(decl_pkg)) {
                        cf.mark_qualifier_used(decl_path, mp);
                    }
                    if (const DeclVisInfo* dv = cf.lookup_type_vis(bound, decl_pkg)) {
                        cf.mark_type_used(dv->package_name, dv->name);
                        report_private_top(decl_pkg, *dv, decl_path, decl_line, 1);
                    }
                    continue;
                }
                if (bound == "Comparable" || bound == "Ordered" ||
                    bound == "Numeric" || bound == "Integer") {
                    continue;
                }
                cf.add_error(cfire::diag::diag(decl_path, decl_line, 1, "cfire/UNDECLARED-TYPE",
                    std::format("unknown interface constraint '{}' on type parameter '{}'", bound, tp.name),
                    {{"name", bound}}));
            }
        }
    };
    // Validate no dynamic slices or arrays of structural interfaces, plus record import/type usage & visibility
    auto check_type_ref_separators_ast = [&](auto& self, const TypeRef& tr, std::string_view src_path, int line, int col, bool mark_root_used = true) -> void {
        if (!tr.raw_name.empty()) {
            auto dot = tr.raw_name.find('.');
            auto dcol = tr.raw_name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                cf.mark_qualifier_used(src_path, tr.raw_name.substr(0, sep));
            }
        }
        if (!tr.name.empty()) {
            std::string mp = CheckedFile::extract_mangled_pkg(tr.name);
            if (!mp.empty() && mp != cf.normalize_pkg(active_decl_pkg)) {
                cf.mark_qualifier_used(src_path, mp);
            }
            if (auto dot = tr.name.find('.'); dot != std::string::npos) {
                cf.mark_qualifier_used(src_path, tr.name.substr(0, dot));
            }
            if (const DeclVisInfo* dv = cf.lookup_type_vis(tr.name, active_decl_pkg)) {
                if (mark_root_used) cf.mark_type_used(dv->package_name, dv->name);
                report_private_top(active_decl_pkg, *dv, src_path, line, col);
            }
        }
        // no raw pointer types (C++ interop only) and no `&T` / `&mut T` reference types.
        if (tr.is_ptr || tr.is_mut_ptr) {
            cf.add_error(cfire::diag::diag(src_path, line, col, "cfire/RAW-POINTER",
                "raw pointer types ('*T', '*mut T') exist only in C++ interop: hold the value itself, an index into a slice, or 'own[T]'/'shared[T]'"));
            return;
        }
        if (tr.is_borrow || tr.is_mut_borrow) {
            cf.add_error(cfire::diag::diag(src_path, line, col, "cfire/REFERENCE-TYPE",
                tr.is_mut_borrow ? "'&mut T' is not a cfire type: a parameter the callee changes is 'mut name: T'"
                                 : "'&T' is not a cfire type: write 'T'; read-only parameters are already views",
                {{"kind", tr.is_mut_borrow ? "mut" : "view"}}));
            return;
        }
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
            // a struct field of a stdlib type the module does not export.
            if (dot != std::string::npos && (dcol == std::string::npos || dot < dcol)) {
                std::string qual = tr.raw_name.substr(0, dot);
                std::string ns;
                if (!cf.shadowed_default_qualifiers.contains(qual) && !cf.cpp_qualifiers.contains(qual) && !cf.vendor_aliases.contains(qual)) {
                    if (auto it = cf.core_aliases.find(qual); it != cf.core_aliases.end()) ns = it->second;
                    else if (StdlibCatalogIndex::instance().has_namespace(qual)) ns = qual;
                }
                if (!ns.empty()) {
                    const bool manifest = std::filesystem::path(src_path).filename() == "build.cf" || cf.ast.package_name == "build";
                    if (std::string why = stdlib_type_name_problem(ns, tr.raw_name.substr(dot + 1), manifest); !why.empty()) {
                        cf.add_error(cfire::diag::diag(src_path, line, col, "cfire/UNKNOWN-STDLIB-TYPE", why));
                    }
                }
            }
            // a bare type name resolves (builtin, package type, type parameter), or it is a cfire error.
            if (dot == std::string::npos && dcol == std::string::npos && !tr.is_fn && !tr.is_tuple) {
                if (std::string why = bare_type_name_problem(tr.raw_name, cf.declared_type_names, scope_type_params); !why.empty()) {
                    cf.add_error(cfire::diag::diag(src_path, line, col, "cfire/UNDECLARED-TYPE", why, {{"name", tr.raw_name}}));
                    return;
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
        for (const auto& fp : tr.fn_params) {
            self(self, fp, src_path, line, col);
        }
        if (tr.fn_ret) {
            self(self, *tr.fn_ret, src_path, line, col);
        }
    };
    // struct fields are snake_case; `cfire fix` renames the field and every use across the workspace.
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
        active_decl_pkg = st.package_name;
        scope_type_params.clear();
        for (const auto& tp : st.type_params) scope_type_params.insert(tp.name);
        check_type_params_bounds(st.type_params, st.package_name, st_path, st.line);
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

    // Reject direct or optional value-recursive struct cycles (`cfire/RECURSIVE-TYPE`)
    {
        std::unordered_map<std::string, const StructDecl*> struct_by_name;
        for (const auto& st : cf.ast.structs) struct_by_name.emplace(st.name, &st);

        auto type_mentions_tp = [&](auto& self, const TypeRef& tr, std::string_view tp_name) -> bool {
            if (tr.is_ptr || tr.is_mut_ptr || !tr.smart_wrap.empty() || tr.is_slice || tr.is_map || tr.is_fn) return false;
            if (tr.name == tp_name) return true;
            if (tr.elem_type && self(self, *tr.elem_type, tp_name)) return true;
            for (const auto& tt : tr.tuple_types) {
                if (self(self, tt, tp_name)) return true;
            }
            return false;
        };

        auto collect_value_deps = [&](auto& self, const TypeRef& tr, std::vector<std::string>& out) -> void {
            if (tr.is_ptr || tr.is_mut_ptr || !tr.smart_wrap.empty() || tr.is_slice || tr.is_map || tr.is_fn) return;
            if (tr.is_tuple) {
                for (const auto& tt : tr.tuple_types) self(self, tt, out);
                return;
            }
            if (tr.elem_type) {
                self(self, *tr.elem_type, out);
            } else if (!tr.name.empty() && struct_by_name.contains(tr.name)) {
                out.push_back(tr.name);
            }
            if (!tr.type_args.empty()) {
                if (auto sit = struct_by_name.find(tr.name); sit != struct_by_name.end()) {
                    const StructDecl* templ = sit->second;
                    for (std::size_t k = 0; k < templ->type_params.size() && k < tr.type_args.size(); ++k) {
                        bool embeds_k = std::any_of(templ->fields.begin(), templ->fields.end(), [&](const StructField& f) {
                            return type_mentions_tp(type_mentions_tp, f.type, templ->type_params[k].name);
                        });
                        if (embeds_k) self(self, tr.type_args[k], out);
                    }
                }
            }
        };

        std::unordered_map<std::string, int> color; // 0=unvisited, 1=visiting, 2=done
        std::vector<std::string> stack;
        std::unordered_set<std::string> reported_roots;
        auto dfs = [&](auto& self, const std::string& name) -> void {
            color[name] = 1;
            stack.push_back(name);
            if (auto sit = struct_by_name.find(name); sit != struct_by_name.end()) {
                const StructDecl* st = sit->second;
                std::string_view st_path = st->source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(st->source_path);
                for (const auto& fld : st->fields) {
                    std::vector<std::string> deps;
                    collect_value_deps(collect_value_deps, fld.type, deps);
                    for (const auto& dep : deps) {
                        int c = color[dep];
                        if (c == 1) {
                            auto cyc_it = std::find(stack.begin(), stack.end(), dep);
                            if (cyc_it != stack.end() && reported_roots.insert(dep).second) {
                                std::string cyc;
                                for (auto it = cyc_it; it != stack.end(); ++it) cyc += *it + " -> ";
                                cyc += dep;
                                cf.add_error(cfire::diag::diag(
                                    st_path, fld.line > 0 ? fld.line : st->line, 0,
                                    "cfire/RECURSIVE-TYPE",
                                    std::format("recursive value type cycle '{}' has infinite size; break the cycle with indirection ('own[T]', 'shared[T]', '*T', or '[]T')", cyc)));
                            }
                        } else if (c == 0) {
                            self(self, dep);
                        }
                    }
                }
            }
            stack.pop_back();
            color[name] = 2;
        };
        for (const auto& st : cf.ast.structs) {
            if (color[st.name] == 0) dfs(dfs, st.name);
        }
    }

    // Transitive fixed-point set of move-only structs containing `own[T]`
    {
        auto tr_is_move_only = [&](auto& self, const TypeRef& tr) -> bool {
            if (tr.is_ptr || tr.is_mut_ptr || tr.smart_wrap == "shared" || tr.is_borrow || tr.is_mut_borrow || tr.is_fn) {
                return false;
            }
            if (tr.smart_wrap == "own") return true;
            if (tr.is_tuple) {
                return std::any_of(tr.tuple_types.begin(), tr.tuple_types.end(), [&](const TypeRef& tt) { return self(self, tt); });
            }
            if (tr.elem_type && self(self, *tr.elem_type)) return true;
            if (tr.key_type && self(self, *tr.key_type)) return true;
            if (!tr.name.empty() && cf.move_only_structs.contains(tr.name)) return true;
            return std::any_of(tr.type_args.begin(), tr.type_args.end(), [&](const TypeRef& ta) { return self(self, ta); });
        };
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& st : cf.ast.structs) {
                if (cf.move_only_structs.contains(st.name)) continue;
                for (const auto& fld : st.fields) {
                    if (tr_is_move_only(tr_is_move_only, fld.type)) {
                        cf.move_only_structs.insert(st.name);
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    for (const auto& fn : cf.ast.functions) {
        std::string_view fn_path = fn.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(fn.source_path);
        if (!is_build_manifest_file && fn.is_pub && !fn.receiver && fn.ctor_type.empty() && cf.normalize_pkg(fn.package_name) == "main") {
            cf.add_warning(cfire::diag::diag(
                fn_path, fn.line, 1, "cfire/REDUNDANT-PUB",
                std::format("'pub' on function '{}' has no effect in package 'main' because 'main' cannot be imported", fn.name)));
        }
        active_decl_pkg = fn.package_name;
        scope_type_params.clear();
        for (const auto& tp : fn.type_params) scope_type_params.insert(tp.name);
        check_type_params_bounds(fn.type_params, fn.package_name, fn_path, fn.line);
        if (fn.receiver) {
            for (const auto& ta : fn.receiver->type.type_args) scope_type_params.insert(ta.name);  // `fn (b: Box[T])`
            for (const auto& st : cf.ast.structs) {
                if (st.name == fn.receiver->type.name) for (const auto& tp : st.type_params) scope_type_params.insert(tp.name);
            }
            check_type_ref_separators_ast(check_type_ref_separators_ast, fn.receiver->type, fn_path, fn.line, 1, false);
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
    for (const auto& en : cf.ast.enums) {
        std::string_view en_path = en.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(en.source_path);
        active_decl_pkg = en.package_name;
        scope_type_params.clear();
        for (const auto& tp : en.type_params) scope_type_params.insert(tp.name);
        check_type_params_bounds(en.type_params, en.package_name, en_path, en.line);
        for (const auto& v : en.variants) {
            for (const auto& pt : v.payload_types) check_type_ref_separators_ast(check_type_ref_separators_ast, pt, en_path, v.line, 1);
        }
    }
    for (const auto& iface : cf.ast.interfaces) {
        std::string_view ifc_path = iface.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(iface.source_path);
        active_decl_pkg = iface.package_name;
        scope_type_params.clear();
        for (const auto& m : iface.methods) {
            int mline = m.line > 0 ? m.line : iface.line;
            int mcol = m.col > 0 ? m.col : 1;
            for (const auto& pt : m.param_types) check_type_ref_separators_ast(check_type_ref_separators_ast, pt, ifc_path, mline, mcol);
            check_type_ref_separators_ast(check_type_ref_separators_ast, m.return_type, ifc_path, mline, mcol);
        }
    }
    scope_type_params.clear();
    for (const auto& gv : cf.ast.global_vars) {
        active_decl_pkg = gv.package_name;
        if (gv.type) {
            std::string_view gv_path = gv.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(gv.source_path);
            check_type_ref_separators_ast(check_type_ref_separators_ast, *gv.type, gv_path, gv.line, 1);
            if (auto iface = find_interface_in_slice(*gv.type, cf.interfaces)) {
                cf.add_error(cfire::diag::diag(gv_path, gv.line, 0, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface)));
            }
        }
    }
    for (const auto& td : cf.ast.tests) {
        std::string_view td_path = td.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(td.source_path);
        active_decl_pkg = td.package_name;
        for (const auto& p : td.params) {
            check_type_ref_separators_ast(check_type_ref_separators_ast, p.type, td_path, p.line, p.col);
        }
        for (const auto& bv : td.bench_variations) {
            check_type_ref_separators_ast(check_type_ref_separators_ast, bv.inferred_type, td_path, bv.line, 1);
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
                        if (!parse_int_literal<std::int64_t>(dval)) {
                            cf.add_error(cfire::diag::diag(st_path, fld.line, 0, "cfire/INVALID-STRUCT-TAG", std::format("invalid default tag value '{}' for integer field '{}.{}' of type '{}'", dval, st.name, fld.name, tname)));
                        }
                    } else if (tname == "u8" || tname == "u16" || tname == "u32" || tname == "u64" || tname == "usize" || tname == "byte") {
                        if (dval.starts_with('-') || !parse_int_literal<std::uint64_t>(dval)) {
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
                                if (is_int_type && !parse_int_literal<std::int64_t>(nstr)) valid_num = false;
                                if (is_uint_type && (nstr.starts_with('-') || !parse_int_literal<std::uint64_t>(nstr))) valid_num = false;
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
                                auto parsed_len = (!lstr.starts_with('-')) ? parse_int_literal<std::uint64_t>(lstr) : std::nullopt;
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
        cf.struct_defs[st.name] = st;
        if (!st.package_name.empty()) {
            std::string orig_name = st.name;
            if (orig_name.starts_with("cfire_pkg_")) {
                if (auto sep = orig_name.find("__"); sep != std::string::npos) {
                    orig_name = orig_name.substr(sep + 2);
                }
            }
            cf.struct_tags[st.package_name + "." + orig_name] = cf.struct_tags[st.name];
            cf.struct_defs[st.package_name + "." + orig_name] = st;
        }
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

    // 3b. Resolve Global Variables & Struct Default Value Expressions
    for (const auto& gv : cf.ast.global_vars) {
        if (!gv.value) {
            if (gv.type) {
                TypeId gt = cf.type_table.from_typeref(*gv.type);
                if (gt.is_valid() && !gt.is_unknown()) gv.resolved_type_id = gt.index;
            }
            continue;
        }
        std::string_view gv_path = gv.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(gv.source_path);
        FnDecl gv_dummy_fn;
        gv_dummy_fn.name = "__cfire_global_init";
        gv_dummy_fn.package_name = gv.package_name;
        SemaFnChecker gv_checker(gv_path, gv_dummy_fn, cf, cf.warnings, cf.errors, cf.diagnostics, true);
        (void)gv_checker.check_expr(gv.value);
        TypeId gt = gv.type
            ? gv_checker.resolve_typeref(*gv.type)
            : ((gv.value->resolved_type_id != 0) ? TypeId{gv.value->resolved_type_id} : gv_checker.infer_expr_type_id_sema(gv.value));
        if (gt.is_valid() && !gt.is_unknown()) {
            gv.resolved_type_id = gt.index;
        }
    }
    for (const auto& st : cf.ast.structs) {
        std::string_view st_path = st.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(st.source_path);
        FnDecl st_dummy_fn;
        st_dummy_fn.name = "__cfire_global_init";
        st_dummy_fn.package_name = st.package_name;
        SemaFnChecker st_checker(st_path, st_dummy_fn, cf, cf.warnings, cf.errors, cf.diagnostics, true);
        for (const auto& fld : st.fields) {
            if (fld.default_val) {
                (void)st_checker.check_expr(fld.default_val);
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
        checker.finish_unused_checks();
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
        dummy_fn.package_name = td.package_name;
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
        checker.finish_unused_checks();
    }

    // 6. Post-Pass Audits: Per-File Unused Imports & Unused Private Top-Level Symbols
    auto is_external_or_manifest = [](std::string_view sp) -> bool {
        if (sp.empty()) return false;
        std::string s = std::filesystem::path(sp).generic_string();
        return s.contains("/vendor/") || s.ends_with("/build.cf") || s == "build.cf";
    };

    {
        std::unordered_set<std::string> error_import_lines;
        for (const auto& d : cf.diagnostics) {
            if (d.severity == "error" &&
                (d.code == "cfire/UNKNOWN-MODULE" || d.code == "cfire/INVALID-IMPORT" ||
                 d.code == "cfire/RETIRED-API" || d.code == "cfire/UNRESOLVED-PACKAGE")) {
                error_import_lines.insert(std::format("{}:{}", cf.normalize_file_key(d.file), d.line));
            }
        }
        const std::vector<ImportDecl>& imports_to_audit = !cf.ast.file_imports.empty() ? cf.ast.file_imports : cf.ast.imports;
        std::unordered_set<std::string> seen_file_quals;
        std::unordered_set<std::string> emitted_unused_imp;
        for (const auto& imp : imports_to_audit) {
            std::string imp_file = cf.normalize_file_key(imp.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(imp.source_path));
            if (imp_file.contains("/vendor/") || imp_file.contains("\\vendor\\")) continue;
            std::string line_key = std::format("{}:{}", imp_file, imp.line);
            if (error_import_lines.contains(line_key)) continue;

            std::string qual;
            if (imp.is_cpp) {
                qual = imp.alias.empty() ? std::string("cpp") : imp.alias;
                if (cf.usage.files_with_cpp_block.contains(imp_file)) {
                    cf.mark_qualifier_used(imp_file, qual);
                }
            } else if (imp.path.starts_with("./") || imp.path.starts_with("../")) {
                if (!imp.alias.empty()) {
                    qual = imp.alias;
                } else {
                    std::string importer_pkg = cf.normalize_pkg(imp.package_name);
                    if (!imp.resolved_pkg_name.empty() && imp.resolved_pkg_name == importer_pkg) {
                        // Same-package relative file import exposes bare symbols cleanly; skip qualifier check.
                        continue;
                    }
                    qual = imp.resolved_pkg_name;
                }
            } else {
                auto [ns_alias, is_vendor] = cfire::modules::canonical_ns_for_import(imp.path);
                if (is_vendor) {
                    qual = imp.alias.empty() ? ns_alias : imp.alias;
                } else {
                    std::string canon_ns = ns_alias;
                    if (canon_ns.empty()) {
                        auto slash = imp.path.rfind('/');
                        auto colon = imp.path.rfind(':');
                        std::size_t sep = (colon != std::string::npos && slash != std::string::npos)
                            ? std::max(colon, slash)
                            : (colon != std::string::npos ? colon : slash);
                        canon_ns = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                    }
                    qual = imp.alias.empty() ? canon_ns : imp.alias;
                }
            }
            if (qual.empty()) continue;

            const bool is_dup = !seen_file_quals.insert(imp_file + "::" + qual).second;
            auto qit = cf.usage.used_qualifiers_by_file.find(imp_file);
            const bool used = !is_dup && (qit != cf.usage.used_qualifiers_by_file.end() && qit->second.contains(qual));
            if (!used && emitted_unused_imp.insert(line_key).second) {
                std::string raw_src = imp.source_path.empty() ? cf.ast.path : imp.source_path;
                cf.add_warning(cfire::diag::diag(
                    raw_src, imp.line, 1, "cfire/UNUSED-IMPORT",
                    is_dup ? std::format("duplicate unused import '{}'", imp.path)
                           : std::format("unused import '{}'", imp.path),
                    {{"path", imp.path}, {"qualifier", qual}, {"rewrite", "delete_line"}}));
            }
        }
    }

    if (!is_build_manifest_file) {
        std::unordered_set<std::string> pkgs_with_cpp_block;
        for (const auto& fn : cf.ast.functions) {
            std::string fkey = cf.normalize_file_key(fn.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(fn.source_path));
            if (cf.usage.files_with_cpp_block.contains(fkey)) {
                pkgs_with_cpp_block.insert(cf.normalize_pkg(fn.package_name));
            }
        }
        for (const auto& td : cf.ast.tests) {
            std::string fkey = cf.normalize_file_key(td.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(td.source_path));
            if (cf.usage.files_with_cpp_block.contains(fkey)) {
                pkgs_with_cpp_block.insert(cf.normalize_pkg(td.package_name));
            }
        }
        std::unordered_set<std::string> iface_method_names;
        for (const auto& ifc : cf.ast.interfaces) {
            for (const auto& m : ifc.methods) iface_method_names.insert(m.name);
        }
        auto in_sibling_tests = [&](const std::string& pkg, std::string_view ident) -> bool {
            if (auto it = cf.ast.sibling_test_idents.find(pkg); it != cf.ast.sibling_test_idents.end()) {
                return it->second.contains(std::string(ident));
            }
            return false;
        };
        std::unordered_set<std::string> emitted_unused_syms;
        auto emit_unused_sym = [&](std::string_view raw_path, int line, std::string_view pkg,
                                   std::string_view kind, std::string_view name, std::string msg) {
            std::string k = std::format("{}:{}:{}:{}", cf.normalize_file_key(raw_path), line, kind, name);
            if (!emitted_unused_syms.insert(std::move(k)).second) return;
            (void)pkg;
            cf.add_warning(cfire::diag::diag(
                raw_path, line, 1, "cfire/UNUSED-SYMBOL", std::move(msg),
                {{"kind", std::string(kind)}, {"name", std::string(name)}}));
        };

        auto check_unused_type_decl = [&](const auto& decl) {
            if (decl.is_pub) return;
            std::string orig = CheckedFile::strip_pkg_mangle(decl.name);
            if (orig.empty() || orig.starts_with('_')) return;
            std::string raw_path = decl.source_path.empty() ? cf.ast.path : decl.source_path;
            if (is_external_or_manifest(raw_path)) return;
            std::string pkg = cf.normalize_pkg(decl.package_name);
            if (pkgs_with_cpp_block.contains(pkg) || in_sibling_tests(pkg, orig)) return;
            if (!cf.usage.used_types.contains(pkg + "::" + orig)) {
                emit_unused_sym(raw_path, decl.line, pkg, "type", orig,
                    std::format("unused private type '{}'; prefix with '_' or remove it", orig));
            }
        };
        for (const auto& st : cf.ast.structs) check_unused_type_decl(st);
        for (const auto& en : cf.ast.enums) check_unused_type_decl(en);
        for (const auto& ifc : cf.ast.interfaces) check_unused_type_decl(ifc);

        for (const auto& gv : cf.ast.global_vars) {
            if (gv.is_pub || gv.name.empty() || gv.name.starts_with('_')) continue;
            std::string raw_path = gv.source_path.empty() ? cf.ast.path : gv.source_path;
            if (is_external_or_manifest(raw_path)) continue;
            std::string pkg = cf.normalize_pkg(gv.package_name);
            if (pkgs_with_cpp_block.contains(pkg) || in_sibling_tests(pkg, gv.name)) continue;
            if (!cf.usage.used_globals.contains(pkg + "::" + gv.name)) {
                emit_unused_sym(raw_path, gv.line, pkg, "global", gv.name,
                    std::format("unused private global '{}'; prefix with '_' or remove it", gv.name));
            }
        }

        for (const auto& fn : cf.ast.functions) {
            std::string raw_path = fn.source_path.empty() ? cf.ast.path : fn.source_path;
            if (is_external_or_manifest(raw_path)) continue;
            std::string pkg = cf.normalize_pkg(fn.package_name);
            if (pkgs_with_cpp_block.contains(pkg)) continue;
            if (!fn.receiver && fn.ctor_type.empty()) {
                if (fn.is_pub || fn.name == "main" || fn.name.empty() || fn.name.starts_with('_')) continue;
                if (in_sibling_tests(pkg, fn.name)) continue;
                if (!cf.usage.used_fns.contains(pkg + "::" + fn.name)) {
                    emit_unused_sym(raw_path, fn.line, pkg, "fn", fn.name,
                        std::format("unused private function '{}'; prefix with '_' or remove it", fn.name));
                }
            } else {
                std::string owner_raw = !fn.ctor_type.empty() ? fn.ctor_type : fn.receiver->type.name;
                const DeclVisInfo* dv = cf.lookup_type_vis(owner_raw, fn.package_name);
                if (dv && dv->is_pub) continue; // methods/constructors on pub types inherit pub visibility
                std::string orig_owner = dv ? dv->name : CheckedFile::strip_pkg_mangle(owner_raw);
                if (fn.name.empty() || fn.name.starts_with('_')) continue;
                if (!fn.ctor_type.empty()) {
                    if (orig_owner.starts_with('_') || in_sibling_tests(pkg, orig_owner)) continue;
                    if (!cf.usage.used_fns.contains(pkg + "::" + fn.name) &&
                        !cf.usage.used_methods.contains(orig_owner + "::" + fn.name) &&
                        !cf.usage.used_methods.contains(pkg + "." + orig_owner + "::" + fn.name)) {
                        emit_unused_sym(raw_path, fn.line, pkg, "method", orig_owner + ".make",
                            std::format("unused private constructor '{}.make'; prefix type with '_' or remove it", orig_owner));
                    }
                } else {
                    if (iface_method_names.contains(fn.name) || in_sibling_tests(pkg, fn.name)) continue;
                    if (!cf.usage.used_methods.contains(orig_owner + "::" + fn.name) &&
                        !cf.usage.used_methods.contains(pkg + "." + orig_owner + "::" + fn.name)) {
                        emit_unused_sym(raw_path, fn.line, pkg, "method", orig_owner + "." + fn.name,
                            std::format("unused private method '{}.{}'; prefix with '_' or remove it", orig_owner, fn.name));
                    }
                }
            }
        }
    }

    return cf;
}

[[nodiscard]] CFIRE_SEMA_ANALYZE_INLINE auto analyze(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params,
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns
) -> std::expected<CheckedFile, std::string> {
    auto cf = analyze_resilient(std::move(ast), std::move(pre_cpp_fn_params), std::move(pre_cpp_fn_returns));
    if (!cf.errors.empty()) {
        return std::unexpected(cf.errors.front());
    }
    return cf;
}
#endif

} // namespace cfire::frontend
