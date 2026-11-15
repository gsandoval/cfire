#pragma once
// src/build/manifest.hpp — `build.cf` & `CMakeLists.txt` Manifest Parsing & Target Resolution

#include "toolchain_layout.hpp"

#ifndef CFIRE_INFRA_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_INFRA_INLINE
#else
#define CFIRE_INFRA_INLINE inline
#endif
#endif

namespace cfire::build {

struct ExternalDepSpec {
    std::string name{};
    std::string driver{}; // "CMake", "CC", "Autotools", "Git", "Archive"
    std::string source{};
    std::string target{};
    std::filesystem::path base_dir{}; // Directory of the build.cf that declared this dependency
    std::vector<std::string> sources{};
    std::vector<std::string> includes{};
    std::unordered_map<std::string, std::string> defines{};
    std::string built_archive{}; // Path to compiled .a or shared (.so / .dylib) library
    bool is_shared{false};

    // Remote fetching & pinning fields (BS-EXT-01)
    std::string fetch_kind{}; // "git" | "archive"
    std::string url{};
    std::string ref{};       // branch, tag, or commit
    std::string sha256{};    // integrity hash for archives
    std::string policy{};    // "fetch" | "system" | "vendor" | "if-missing"
    bool is_remote{false};
};

struct LockedPackage {
    std::string name;
    std::string url;
    std::string ref;
    std::string commit;
    std::string sha256;
    std::string driver;
};

struct PreBuildCommandSpec {
    std::string command{};
    std::vector<std::string> args{};
    std::vector<std::string> inputs{};
    std::vector<std::string> outputs{};
    std::string work_dir{};
};

struct ExecutableSpec {
    std::string var_name{};
    std::string name{};
    std::string entry{};
    std::string subsystem{"auto"};
    std::string app_name{};
    std::string bundle_id{};
    std::string icon{};
    std::vector<std::string> targets{"native"};
    std::string out_dir{};
    std::string web_profile{"auto"};
    std::vector<std::string> frameworks{};
    std::vector<std::string> libraries{};
    std::vector<std::string> pkg_config{};
    std::vector<std::string> includes{};
    std::vector<std::string> defines{};
    std::vector<std::string> url_schemes{};
    std::string sign_identity{};
    bool portable{false};
    bool static_link{false};
    bool universal{false};
    std::string macos_min{};
};

struct ParsedBuildCf {
    std::vector<ExternalDepSpec> external_deps{};
    std::vector<PreBuildCommandSpec> pre_build_commands{};
    std::vector<ExecutableSpec> executables{};
    std::vector<std::string> pkg_config{};
    std::vector<std::string> libraries{};
    std::vector<std::string> frameworks{};
    std::vector<std::string> includes{};
    std::vector<std::string> defines{};
};

struct BuildPlan {
    std::string entry_file{};
    std::string generated_cpp{};
    std::vector<std::string> warnings{};
    std::vector<std::string> local_cf_packages{};
    std::vector<std::string> auto_linked{};
    std::vector<std::string> pkg_config_deps{};
    std::vector<std::string> queried_pkg_configs{};
    std::vector<std::string> extra_pkg_configs{};
    std::vector<std::string> defines{};
    std::vector<std::string> extra_defines{};
    bool has_impl_define{false};
    std::vector<std::string> cflags{};
    std::vector<std::string> ldflags{};
    std::unordered_map<std::string, bridge::HeaderIndex> cpp_headers{};
    std::vector<ExternalDepSpec> external_deps{};
    std::vector<PreBuildCommandSpec> pre_build_commands{};
    bool used_pch = false;
    bool sanitized = false;
    std::string target_triple{};
    std::string sysroot{};
    std::vector<std::string> extra_link_libs{};
    std::vector<std::string> extra_includes{};
    bool is_gui{false};
    std::string web_profile{"auto"};
    std::string subsystem{"auto"};
    std::string app_name{};
    std::string bundle_id{};
    std::string icon_path{};
    std::vector<std::string> url_schemes{};
    std::string sign_identity{};
    std::string macos_app_bundle{};
    std::string ios_app_bundle{};
    bool is_ios{false};
    bool is_ios_sim{false};
    bool uses_objc{false};
    std::string emscripten_compiler{};
    bool used_sharded = false;
    std::size_t sharded_total_units = 0;
    std::size_t sharded_recompiled_units = 0;
    std::vector<std::string> sharded_object_files{};
    bool portable{false};
    bool static_link{false};
    bool universal{false};
    std::string macos_min{};
    bool use_vendored_sqlite3{false};
    bool use_vendored_simdjson{false};
    std::vector<std::string> portable_notes{};
};

struct TargetResolution {
    bool found = false;
    ExecutableSpec exe{};
    std::filesystem::path build_cf_path{};
    std::filesystem::path entry_file{};
    std::filesystem::path base_out_dir{};
    std::filesystem::path native_bin{};
    std::filesystem::path macos_app_bundle{};
    std::filesystem::path wasm_html{};
    std::filesystem::path wasm_js{};
    std::filesystem::path wasm_wasm{};
    bool has_native_target = false;
    bool has_wasm_target = false;
};

[[nodiscard]] CFIRE_INFRA_INLINE auto parse_build_cf(const std::filesystem::path& build_cf_path)
    -> std::expected<ParsedBuildCf, std::string>;

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_build_target(
    const std::filesystem::path& base_dir_or_build_cf,
    std::string_view target_name = "",
    bool prefer_wasm = false
) -> std::optional<TargetResolution>;

CFIRE_INFRA_INLINE void parse_cmake_manifest(
    const std::filesystem::path& cmake_lists,
    ExternalDepSpec& dep
);

[[nodiscard]] inline auto is_valid_c_macro_ident(std::string_view s) -> bool {
    if (s.empty()) return false;
    auto is_alpha = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (!is_alpha(static_cast<unsigned char>(s[0]))) return false;
    for (std::size_t i = 1; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (!is_alpha(c) && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}

[[nodiscard]] inline auto normalize_define_spec(std::string_view raw) -> std::expected<std::string, std::string> {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
    if (raw.starts_with("-D")) raw.remove_prefix(2);
    if (raw.empty()) {
        return std::unexpected("empty C/C++ preprocessor define; expected MACRO or MACRO=VAL");
    }
    auto eq = raw.find('=');
    std::string_view key = (eq == std::string_view::npos) ? raw : raw.substr(0, eq);
    if (!is_valid_c_macro_ident(key)) {
        return std::unexpected(std::format(
            "invalid C macro identifier '{}' in define '{}' (expected [A-Za-z_][A-Za-z0-9_]*)", key, raw));
    }
    return std::string(raw);
}

CFIRE_INFRA_INLINE void apply_manifest_pkg_fields(
    BuildPlan& plan,
    const std::filesystem::path& bcf_dir,
    const std::vector<std::string>& pkg_configs,
    const std::vector<std::string>& libraries,
    const std::vector<std::string>& frameworks,
    const std::vector<std::string>& includes,
    const std::vector<std::string>& defines = {}
);

CFIRE_INFRA_INLINE void resolve_plan_pkg_configs(
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    const std::string& pc_override = ""
);

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_plan_defines(
    BuildPlan& plan
) -> std::expected<void, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

[[nodiscard]] CFIRE_INFRA_INLINE auto parse_build_cf(const std::filesystem::path& build_cf_path)
    -> std::expected<ParsedBuildCf, std::string> {
    auto raw = read_file_text(build_cf_path);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(build_cf_path.string(), *raw);
    if (!ast) return std::unexpected(ast.error());

    std::error_code bcf_ec;
    auto bcf_abs = std::filesystem::weakly_canonical(build_cf_path, bcf_ec);
    auto bcf_dir = (bcf_ec ? std::filesystem::absolute(build_cf_path, bcf_ec) : bcf_abs).parent_path();

    auto collect_str_or_arr = [](const frontend::Expr& expr, std::vector<std::string>& out, bool clear_arr = false) {
        if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&expr.node)) {
            if (clear_arr) out.clear();
            for (const auto& el : arr->elements) {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                    out.push_back(lit->value);
                }
            }
        } else if (const auto* lit = std::get_if<frontend::LiteralExpr>(&expr.node)) {
            if (clear_arr) out = {lit->value};
            else out.push_back(lit->value);
        }
    };

    std::string def_err;
    auto push_define_raw = [&](std::vector<std::string>& out, std::string_view spec) {
        if (!def_err.empty()) return;
        auto norm = normalize_define_spec(spec);
        if (!norm) {
            def_err = norm.error();
            return;
        }
        if (std::find(out.begin(), out.end(), *norm) == out.end()) {
            out.push_back(std::move(*norm));
        }
    };
    auto collect_defines_expr = [&](const frontend::Expr& expr, std::vector<std::string>& out) {
        auto push_kv = [&](std::string_view key, const frontend::Expr* val) {
            if (!def_err.empty()) return;
            if (!is_valid_c_macro_ident(key)) {
                def_err = std::format(
                    "invalid C macro identifier '{}' in build.cf Defines (expected [A-Za-z_][A-Za-z0-9_]*)", key);
                return;
            }
            if (!val) {
                push_define_raw(out, key);
                return;
            }
            if (const auto* lit = std::get_if<frontend::LiteralExpr>(&val->node)) {
                if (lit->kind == frontend::TokenKind::KwTrue || lit->kind == frontend::TokenKind::KwFalse) {
                    if (lit->kind == frontend::TokenKind::KwTrue) push_define_raw(out, key);
                    return;
                }
                push_define_raw(out, std::format("{}={}", key, lit->value));
            } else if (const auto* un = std::get_if<frontend::UnaryExpr>(&val->node)) {
                if (un->op == "-" && un->operand) {
                    if (const auto* u_lit = std::get_if<frontend::LiteralExpr>(&un->operand->node)) {
                        push_define_raw(out, std::format("{}=-{}", key, u_lit->value));
                    }
                }
            }
        };
        if (const auto* map_lit = std::get_if<frontend::StructLitExpr>(&expr.node)) {
            for (const auto& mf : map_lit->fields) {
                push_kv(mf.name, mf.value);
            }
        } else if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&expr.node)) {
            for (const auto& el : arr->elements) {
                if (!el) continue;
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                    push_define_raw(out, lit->value);
                }
            }
        } else if (const auto* lit = std::get_if<frontend::LiteralExpr>(&expr.node)) {
            push_define_raw(out, lit->value);
        }
    };

    auto populate_pkg_fields = [&](const frontend::StructLitExpr& st_lit,
                                   std::vector<std::string>& pkg_config,
                                   std::vector<std::string>& libraries,
                                   std::vector<std::string>& frameworks,
                                   std::vector<std::string>& includes,
                                   std::vector<std::string>& defines) {
        for (const auto& fld : st_lit.fields) {
            if (!fld.value) continue;
            if (fld.name == "Frameworks" || fld.name == "frameworks") {
                collect_str_or_arr(*fld.value, frameworks);
            } else if (fld.name == "Libraries" || fld.name == "libraries" || fld.name == "Libs" || fld.name == "libs") {
                collect_str_or_arr(*fld.value, libraries);
            } else if (fld.name == "PkgConfig" || fld.name == "pkg_config") {
                collect_str_or_arr(*fld.value, pkg_config);
            } else if (fld.name == "Includes" || fld.name == "includes") {
                collect_str_or_arr(*fld.value, includes);
            } else if (fld.name == "Defines" || fld.name == "defines") {
                collect_defines_expr(*fld.value, defines);
            }
        }
    };

    auto populate_exe_fields = [&](ExecutableSpec& exe_spec, const frontend::StructLitExpr& st_lit) {
        populate_pkg_fields(
            st_lit, exe_spec.pkg_config, exe_spec.libraries, exe_spec.frameworks, exe_spec.includes, exe_spec.defines);
        for (const auto& fld : st_lit.fields) {
            if (!fld.value) continue;
            if (fld.name == "Entry" || fld.name == "entry") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.entry = lit->value;
                }
            } else if (fld.name == "Subsystem" || fld.name == "subsystem") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.subsystem = lit->value;
                }
            } else if (fld.name == "Name" || fld.name == "name") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    if (exe_spec.name.empty()) exe_spec.name = lit->value;
                    if (exe_spec.app_name.empty()) exe_spec.app_name = lit->value;
                }
            } else if (fld.name == "AppName" || fld.name == "app_name") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.app_name = lit->value;
                }
            } else if (fld.name == "BundleId" || fld.name == "BundleID" || fld.name == "Identifier" || fld.name == "bundle_id") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.bundle_id = lit->value;
                }
            } else if (fld.name == "Icon" || fld.name == "icon") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.icon = lit->value;
                }
            } else if (fld.name == "Targets" || fld.name == "targets") {
                collect_str_or_arr(*fld.value, exe_spec.targets, true);
            } else if (fld.name == "OutDir" || fld.name == "out_dir") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.out_dir = lit->value;
                }
            } else if (fld.name == "WebProfile" || fld.name == "web_profile") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.web_profile = lit->value;
                }
            } else if (fld.name == "UrlSchemes" || fld.name == "url_schemes") {
                collect_str_or_arr(*fld.value, exe_spec.url_schemes);
            } else if (fld.name == "SignIdentity" || fld.name == "sign_identity") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.sign_identity = lit->value;
                }
            } else if (fld.name == "Portable" || fld.name == "portable") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.portable = (lit->value == "true");
                }
            } else if (fld.name == "Static" || fld.name == "static" || fld.name == "static_link") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.static_link = (lit->value == "true");
                }
            } else if (fld.name == "Universal" || fld.name == "universal") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.universal = (lit->value == "true");
                }
            } else if (fld.name == "MacosMin" || fld.name == "macos_min") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.macos_min = lit->value;
                }
            }
        }
    };

    ParsedBuildCf parsed;
    for (const auto& gv : ast->global_vars) {
        if (!gv.value) continue;
        if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&gv.value->node)) {
            if (st_lit->type_name == "Package" || st_lit->type_name == "build.Package") {
                populate_pkg_fields(
                    *st_lit, parsed.pkg_config, parsed.libraries, parsed.frameworks, parsed.includes, parsed.defines);
                continue;
            }
            if (st_lit->type_name == "Executable" || st_lit->type_name == "App" ||
                st_lit->type_name == "build.Executable" || st_lit->type_name == "build.App") {
                ExecutableSpec exe_spec;
                exe_spec.var_name = gv.name;
                populate_exe_fields(exe_spec, *st_lit);
                if (exe_spec.name.empty()) exe_spec.name = gv.name;
                if (exe_spec.targets.empty()) exe_spec.targets.push_back("native");
                if (exe_spec.app_name.empty()) exe_spec.app_name = exe_spec.name;
                if (exe_spec.bundle_id.empty()) exe_spec.bundle_id = "com.cfire." + exe_spec.name;
                parsed.executables.push_back(std::move(exe_spec));
                continue;
            }
        }
        const auto* call = std::get_if<frontend::CallExpr>(&gv.value->node);
        if (!call || !call->callee) continue;
        const auto* mem = std::get_if<frontend::MemberExpr>(&call->callee->node);
        if (!mem) continue;

        if (mem->field == "Package") {
            for (const auto& arg : call->args) {
                if (!arg.value) continue;
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&arg.value->node)) {
                    populate_pkg_fields(
                        *st_lit, parsed.pkg_config, parsed.libraries, parsed.frameworks, parsed.includes, parsed.defines);
                }
            }
            continue;
        }
        if (mem->field == "PkgConfig") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.pkg_config);
            }
            continue;
        }
        if (mem->field == "Link" || mem->field == "Libraries") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.libraries);
            }
            continue;
        }
        if (mem->field == "Include" || mem->field == "Includes") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.includes);
            }
            continue;
        }
        if (mem->field == "Frameworks") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.frameworks);
            }
            continue;
        }
        if (mem->field == "Define") {
            if (call->args.size() == 1 && call->args[0].value) {
                collect_defines_expr(*call->args[0].value, parsed.defines);
            } else if (call->args.size() >= 2 && call->args[0].value && call->args[1].value) {
                if (const auto* kl = std::get_if<frontend::LiteralExpr>(&call->args[0].value->node)) {
                    if (!is_valid_c_macro_ident(kl->value)) {
                        def_err = std::format(
                            "invalid C macro identifier '{}' in build.Define (expected [A-Za-z_][A-Za-z0-9_]*)",
                            kl->value);
                    } else if (const auto* vl = std::get_if<frontend::LiteralExpr>(&call->args[1].value->node)) {
                        if (vl->kind == frontend::TokenKind::KwTrue || vl->kind == frontend::TokenKind::KwFalse) {
                            if (vl->kind == frontend::TokenKind::KwTrue) push_define_raw(parsed.defines, kl->value);
                        } else {
                            push_define_raw(parsed.defines, std::format("{}={}", kl->value, vl->value));
                        }
                    } else if (const auto* un = std::get_if<frontend::UnaryExpr>(&call->args[1].value->node)) {
                        if (un->op == "-" && un->operand) {
                            if (const auto* u_lit = std::get_if<frontend::LiteralExpr>(&un->operand->node)) {
                                push_define_raw(parsed.defines, std::format("{}=-{}", kl->value, u_lit->value));
                            }
                        }
                    }
                }
            }
            continue;
        }
        if (mem->field == "Defines") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_defines_expr(*arg.value, parsed.defines);
            }
            continue;
        }

        if (mem->field == "Executable" || mem->field == "App") {
            ExecutableSpec exe_spec;
            exe_spec.var_name = gv.name;
            if (!call->args.empty() && call->args[0].value) {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&call->args[0].value->node)) {
                    exe_spec.name = lit->value;
                }
            }
            if (exe_spec.name.empty()) {
                exe_spec.name = gv.name;
            }
            if (call->args.size() > 1 && call->args[1].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                    populate_exe_fields(exe_spec, *st_lit);
                }
            }
            if (exe_spec.targets.empty()) {
                exe_spec.targets.push_back("native");
            }
            if (exe_spec.app_name.empty()) {
                exe_spec.app_name = exe_spec.name;
            }
            if (exe_spec.bundle_id.empty()) {
                exe_spec.bundle_id = "com.cfire." + exe_spec.name;
            }
            parsed.executables.push_back(std::move(exe_spec));
            continue;
        }

        if (mem->field == "Command") {
            PreBuildCommandSpec cmd_spec;
            if (!call->args.empty() && call->args[0].value) {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&call->args[0].value->node)) {
                    cmd_spec.command = lit->value;
                }
            }
            if (call->args.size() > 1 && call->args[1].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                    for (const auto& fld : st_lit->fields) {
                        if (!fld.value) continue;
                        if (fld.name == "Args") {
                            if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                                for (const auto& el : arr->elements) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                        cmd_spec.args.push_back(lit->value);
                                    }
                                }
                            }
                        } else if (fld.name == "Inputs") {
                            if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                                for (const auto& el : arr->elements) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                        cmd_spec.inputs.push_back(lit->value);
                                    }
                                }
                            }
                        } else if (fld.name == "Outputs") {
                            if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                                for (const auto& el : arr->elements) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                        cmd_spec.outputs.push_back(lit->value);
                                    }
                                }
                            }
                        } else if (fld.name == "WorkDir") {
                            if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                                cmd_spec.work_dir = lit->value;
                            }
                        }
                    }
                }
            }
            parsed.pre_build_commands.push_back(std::move(cmd_spec));
            continue;
        }

        ExternalDepSpec spec;
        spec.name = gv.name;
        spec.driver = mem->field;
        spec.base_dir = bcf_dir;
        if (mem->field == "Git") {
            spec.fetch_kind = "git";
            spec.is_remote = true;
            spec.driver = "CC";
        } else if (mem->field == "Archive") {
            spec.fetch_kind = "archive";
            spec.is_remote = true;
            spec.driver = "CC";
        }
        if (!call->args.empty() && call->args[0].value) {
            if (const auto* lit = std::get_if<frontend::LiteralExpr>(&call->args[0].value->node)) {
                spec.source = lit->value;
            }
        }
        if (call->args.size() > 1 && call->args[1].value) {
            if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                for (const auto& fld : st_lit->fields) {
                    if (!fld.value) continue;
                    if (fld.name == "Target") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.target = lit->value;
                        }
                    } else if (fld.name == "Sources") {
                        if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                            for (const auto& el : arr->elements) {
                                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                    spec.sources.push_back(lit->value);
                                }
                            }
                        }
                    } else if (fld.name == "Includes") {
                        if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                            for (const auto& el : arr->elements) {
                                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                    spec.includes.push_back(lit->value);
                                }
                            }
                        }
                    } else if (fld.name == "Defines" || fld.name == "defines" || fld.name == "Flags") {
                        const bool validate_keys = (fld.name == "Defines" || fld.name == "defines");
                        auto record_dep_def = [&](std::string_view key, std::string val) {
                            if (validate_keys && !is_valid_c_macro_ident(key)) {
                                def_err = std::format(
                                    "invalid C macro identifier '{}' in build.cf Defines (expected [A-Za-z_][A-Za-z0-9_]*)",
                                    key);
                                return;
                            }
                            spec.defines[std::string(key)] = std::move(val);
                        };
                        if (const auto* map_lit = std::get_if<frontend::StructLitExpr>(&fld.value->node)) {
                            for (const auto& mf : map_lit->fields) {
                                if (!mf.value) continue;
                                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&mf.value->node)) {
                                    if (lit->kind == frontend::TokenKind::KwTrue || lit->kind == frontend::TokenKind::KwFalse) {
                                        if (lit->kind == frontend::TokenKind::KwTrue) record_dep_def(mf.name, "1");
                                    } else {
                                        record_dep_def(mf.name, lit->value);
                                    }
                                } else if (const auto* un = std::get_if<frontend::UnaryExpr>(&mf.value->node)) {
                                    if (un->op == "-" && un->operand) {
                                        if (const auto* u_lit = std::get_if<frontend::LiteralExpr>(&un->operand->node)) {
                                            record_dep_def(mf.name, "-" + u_lit->value);
                                        }
                                    }
                                }
                            }
                        } else if (validate_keys) {
                            std::vector<std::string> arr_defs;
                            collect_defines_expr(*fld.value, arr_defs);
                            for (const auto& d : arr_defs) {
                                auto eq = d.find('=');
                                if (eq == std::string::npos) {
                                    record_dep_def(d, "1");
                                } else {
                                    record_dep_def(std::string_view(d).substr(0, eq), d.substr(eq + 1));
                                }
                            }
                        }
                    } else if (fld.name == "Url" || fld.name == "URL") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.url = lit->value;
                            spec.is_remote = true;
                        }
                    } else if (fld.name == "Tag" || fld.name == "Ref" || fld.name == "Commit" || fld.name == "Branch") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.ref = lit->value;
                        }
                    } else if (fld.name == "Sha256" || fld.name == "Hash" || fld.name == "Checksum") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.sha256 = lit->value;
                        }
                    } else if (fld.name == "Policy") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.policy = lit->value;
                        }
                    } else if (fld.name == "Driver") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.driver = lit->value;
                        }
                    }
                }
            }
        }
        if (spec.fetch_kind == "git" || spec.fetch_kind == "archive" ||
            spec.driver == "Git" || spec.driver == "Archive" ||
            spec.source.starts_with("https://") || spec.source.starts_with("http://") ||
            spec.source.ends_with(".git") || spec.source.starts_with("git@") ||
            !spec.url.empty()) {
            spec.is_remote = true;
            if (spec.url.empty()) spec.url = spec.source;
            if (spec.fetch_kind.empty()) {
                if (spec.driver == "Git" || spec.url.ends_with(".git") || spec.url.starts_with("git@")) {
                    spec.fetch_kind = "git";
                    if (spec.driver == "Git") spec.driver = "CC";
                } else if (spec.driver == "Archive" || spec.url.ends_with(".zip") || spec.url.ends_with(".tar.gz") || spec.url.ends_with(".tgz") || spec.url.ends_with(".tar.xz")) {
                    spec.fetch_kind = "archive";
                    if (spec.driver == "Archive") spec.driver = "CC";
                }
            }
        }
        parsed.external_deps.push_back(std::move(spec));
    }
    if (!def_err.empty()) {
        return std::unexpected(def_err);
    }
    return parsed;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_build_target(
    const std::filesystem::path& base_dir_or_build_cf,
    std::string_view target_name,
    bool prefer_wasm) -> std::optional<TargetResolution> {
    std::filesystem::path bcf = base_dir_or_build_cf;
    std::error_code ec;
    if (std::filesystem::is_directory(bcf, ec)) {
        bcf = bcf / "build.cf";
    }
    if (!std::filesystem::exists(bcf, ec)) {
        return std::nullopt;
    }
    auto parsed = parse_build_cf(bcf);
    if (!parsed || parsed->executables.empty()) {
        return std::nullopt;
    }

    const ExecutableSpec* chosen = nullptr;
    if (!target_name.empty() && target_name != "all" && target_name != ".") {
        for (const auto& exe : parsed->executables) {
            if (exe.var_name == target_name) {
                chosen = &exe;
                break;
            }
        }
        if (!chosen) {
            for (const auto& exe : parsed->executables) {
                if (exe.name == target_name || exe.app_name == target_name) {
                    chosen = &exe;
                    break;
                }
            }
        }
    } else {
        if (prefer_wasm) {
            for (const auto& exe : parsed->executables) {
                for (const auto& t : exe.targets) {
                    if (t == "wasm" || t == "web" || t.starts_with("wasm32")) {
                        chosen = &exe;
                        break;
                    }
                }
                if (chosen) break;
            }
        }
        if (!chosen && !parsed->executables.empty()) {
            chosen = &parsed->executables.front();
        }
    }

    if (!chosen) return std::nullopt;

    TargetResolution res;
    res.found = true;
    res.exe = *chosen;
    res.build_cf_path = std::filesystem::canonical(bcf, ec);
    auto proj_root = res.build_cf_path.parent_path();

    res.entry_file = chosen->entry;
    if (res.entry_file.is_relative()) {
        res.entry_file = proj_root / res.entry_file;
    }

    res.base_out_dir = chosen->out_dir.empty() ? proj_root : (proj_root / chosen->out_dir);

    for (const auto& t : chosen->targets) {
        if (t == "wasm" || t == "web" || t.starts_with("wasm32")) {
            res.has_wasm_target = true;
        }
        if (t == "native" || (!t.starts_with("wasm") && t != "web")) {
            res.has_native_target = true;
        }
    }

    // WASM paths
    res.wasm_html = res.base_out_dir / (chosen->name + ".html");
    res.wasm_js = res.base_out_dir / (chosen->name + ".js");
    res.wasm_wasm = res.base_out_dir / (chosen->name + ".wasm");

    // Native paths
    auto candidate = res.base_out_dir / chosen->name;
    if (std::filesystem::is_directory(candidate, ec)) {
        res.native_bin = candidate / chosen->name;
    } else {
        res.native_bin = candidate;
    }

    if (chosen->subsystem == "gui" || chosen->subsystem == "auto") {
        res.macos_app_bundle = res.native_bin.parent_path() / (chosen->name + ".app");
    }

    return res;
}

CFIRE_INFRA_INLINE void parse_cmake_manifest(
    const std::filesystem::path& cmake_lists,
    ExternalDepSpec& dep
) {
    auto txt = read_file_text(cmake_lists);
    if (!txt) return;
    std::string content = *txt;

    auto extract_cmd_args = [&](std::string_view cmd_name) -> std::vector<std::string> {
        std::vector<std::string> args;
        std::size_t pos = content.find(cmd_name);
        if (pos == std::string::npos) return args;
        auto lp = content.find('(', pos + cmd_name.size());
        auto rp = content.find(')', lp != std::string::npos ? lp : 0);
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp) return args;
        std::string inner = content.substr(lp + 1, rp - lp - 1);
        std::istringstream iss(inner);
        std::string tok;
        while (iss >> tok) {
            if (!tok.empty() && tok.front() == '"' && tok.back() == '"' && tok.size() >= 2) {
                tok = tok.substr(1, tok.size() - 2);
            }
            args.push_back(std::move(tok));
        }
        return args;
    };

    if (dep.sources.empty()) {
        auto lib_args = extract_cmd_args("add_library");
        for (std::size_t i = 1; i < lib_args.size(); ++i) {
            const auto& a = lib_args[i];
            if (a == "STATIC" || a == "SHARED" || a == "OBJECT" || a == "INTERFACE") continue;
            dep.sources.push_back(a);
        }
    }
    if (dep.includes.empty()) {
        auto inc_args = extract_cmd_args("target_include_directories");
        for (std::size_t i = 1; i < inc_args.size(); ++i) {
            const auto& a = inc_args[i];
            if (a == "PUBLIC" || a == "PRIVATE" || a == "INTERFACE") continue;
            dep.includes.push_back(a);
        }
    }
    auto def_args = extract_cmd_args("target_compile_definitions");
    for (std::size_t i = 1; i < def_args.size(); ++i) {
        std::string a = def_args[i];
        if (a == "PUBLIC" || a == "PRIVATE" || a == "INTERFACE") continue;
        if (a.starts_with("-D")) a = a.substr(2);
        if (auto eq = a.find('='); eq != std::string::npos) {
            dep.defines.try_emplace(a.substr(0, eq), a.substr(eq + 1));
        } else {
            dep.defines.try_emplace(a, "1");
        }
    }
}

CFIRE_INFRA_INLINE void apply_manifest_pkg_fields(
    BuildPlan& plan,
    const std::filesystem::path& bcf_dir,
    const std::vector<std::string>& pkg_configs,
    const std::vector<std::string>& libraries,
    const std::vector<std::string>& frameworks,
    const std::vector<std::string>& includes,
    const std::vector<std::string>& defines
) {
    auto add_unique = [](std::vector<std::string>& vec, const std::string& item) {
        if (item.empty()) return;
        for (const auto& existing : vec) {
            if (existing == item) return;
        }
        vec.push_back(item);
    };
    const bool is_wasm = (plan.target_triple == "wasm" || plan.target_triple == "web" ||
                          plan.target_triple.find("wasm") != std::string::npos);

    for (const auto& pc : pkg_configs) {
        add_unique(plan.pkg_config_deps, pc);
    }
    for (const auto& def : defines) {
        add_unique(plan.defines, def);
    }
    for (const auto& inc : includes) {
        if (inc.empty()) continue;
        std::filesystem::path ip(inc);
        if (ip.is_relative() && !bcf_dir.empty()) {
            std::error_code ec;
            auto abs_inc = std::filesystem::weakly_canonical(bcf_dir / ip, ec);
            ip = ec ? (bcf_dir / ip) : abs_inc;
        }
        std::string resolved = ip.string();
        add_unique(plan.extra_includes, resolved);
        add_unique(plan.cflags, "-I" + resolved);
    }
    if (is_wasm) return;

#if defined(__APPLE__)
    for (const auto& fw : frameworks) {
        if (fw.empty()) continue;
        bool already = false;
        for (std::size_t i = 0; i + 1 < plan.ldflags.size(); ++i) {
            if (plan.ldflags[i] == "-framework" && plan.ldflags[i + 1] == fw) {
                already = true;
                break;
            }
        }
        if (!already) {
            plan.ldflags.push_back("-framework");
            plan.ldflags.push_back(fw);
        }
    }
#else
    (void)frameworks;
#endif
    for (const auto& lib : libraries) {
        if (lib.empty()) continue;
        add_unique(plan.ldflags, lib.starts_with("-l") ? lib : ("-l" + lib));
    }
}

CFIRE_INFRA_INLINE void resolve_plan_pkg_configs(
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    const std::string& pc_override
) {
    auto add_unique = [](std::vector<std::string>& vec, const std::string& item) {
        if (item.empty()) return;
        for (const auto& existing : vec) {
            if (existing == item) return;
        }
        vec.push_back(item);
    };
    for (const auto& epc : plan.extra_pkg_configs) {
        add_unique(plan.pkg_config_deps, epc);
    }
    if (!pc_override.empty()) {
        add_unique(plan.pkg_config_deps, pc_override);
    }

    const bool is_wasm = (plan.target_triple == "wasm" || plan.target_triple == "web" ||
                          plan.target_triple.find("wasm") != std::string::npos);
    if (is_wasm) return;

    bool is_win = (plan.target_triple.find("windows") != std::string::npos ||
                   plan.target_triple.find("win32") != std::string::npos ||
                   plan.target_triple.find("mingw") != std::string::npos);
#if defined(_WIN32)
    if (plan.target_triple.empty()) is_win = true;
#endif
    const bool prefer_static = plan.static_link || plan.portable || is_win;
    const auto install_root = runtime_dir.empty() ? std::filesystem::path{} : derive_install_root(runtime_dir);

    const std::vector<std::string> snapshot = plan.pkg_config_deps;
    for (const auto& pc : snapshot) {
        if (pc.empty()) continue;
        if (std::find(plan.queried_pkg_configs.begin(), plan.queried_pkg_configs.end(), pc) != plan.queried_pkg_configs.end()) {
            continue;
        }
        plan.queried_pkg_configs.push_back(pc);
        if (plan.is_ios) {
            if (pc == "sqlite3") {
                add_unique(plan.ldflags, "-lsqlite3");
            } else if (pc == "libzstd") {
                add_unique(plan.ldflags, "-lcompression");
            }
            continue;
        }
        query_pkg_config(pc, plan.cflags, plan.ldflags, install_root, prefer_static);
    }
}

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_plan_defines(
    BuildPlan& plan
) -> std::expected<void, std::string> {
    auto apply_one = [&](std::string_view raw) -> std::expected<void, std::string> {
        auto norm = normalize_define_spec(raw);
        if (!norm) return std::unexpected(norm.error());
        auto eq = norm->find('=');
        std::string_view key = (eq == std::string::npos) ? std::string_view(*norm) : std::string_view(*norm).substr(0, eq);
        if (key.contains("IMPLEMENTATION")) {
            plan.has_impl_define = true;
        }
        std::string flag = "-D" + *norm;
        if (std::find(plan.cflags.begin(), plan.cflags.end(), flag) == plan.cflags.end()) {
            plan.cflags.push_back(std::move(flag));
        }
        return {};
    };
    for (const auto& def : plan.defines) {
        if (auto r = apply_one(def); !r) return r;
    }
    for (const auto& def : plan.extra_defines) {
        if (auto r = apply_one(def); !r) return r;
    }
    return {};
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
