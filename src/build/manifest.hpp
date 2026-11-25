#pragma once
// src/build/manifest.hpp — `build.cf` & `CMakeLists.txt` Manifest Parsing & Target Resolution

#include "toolchain_layout.hpp"
#include <unordered_set>

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
    std::vector<std::string> extra_cflags{};
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
    std::vector<std::string> cflags{};
    std::vector<std::string> ldflags{};
    std::vector<std::string> sources{};
    std::vector<std::string> url_schemes{};
    std::string sign_identity{};
    bool portable{false};
    bool static_link{false};
    bool universal{false};
    std::string macos_min{};
    std::string version{};
    std::string build_number{};
    std::vector<std::string> resources{};
    bool has_explicit_resources{false};
    std::string ios_min{};
    std::vector<std::string> ios_orientations{};
    std::vector<int> ios_device_family{};
    std::string provisioning_profile{};
    std::string entitlements{};
    std::string privacy_manifest{};
    std::vector<std::pair<std::string, std::string>> info_plist_extras{};
    std::unordered_set<std::string> info_plist_bool_keys{};
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
    std::vector<std::string> cflags{};
    std::vector<std::string> ldflags{};
    std::vector<std::string> sources{};
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
    std::string version{};
    std::string build_number{};
    std::vector<std::string> resources{};
    bool has_explicit_resources{false};
    bool has_cpp_interop{false};
    std::string ios_min{};
    std::vector<std::string> ios_orientations{};
    std::vector<int> ios_device_family{};
    std::string provisioning_profile{};
    std::string entitlements{};
    std::string privacy_manifest{};
    std::vector<std::pair<std::string, std::string>> info_plist_extras{};
    std::unordered_set<std::string> info_plist_bool_keys{};
    std::string manifest_dir{};
    bool emit_ipa{false};
    std::string ios_ipa_path{};
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

[[nodiscard]] CFIRE_INFRA_INLINE auto parse_build_cf(
    const std::filesystem::path& build_cf_path,
    std::string_view target_triple = ""
) -> std::expected<ParsedBuildCf, std::string>;

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
    const std::vector<std::string>& defines = {},
    const std::vector<std::string>& cflags = {},
    const std::vector<std::string>& ldflags = {}
);

CFIRE_INFRA_INLINE auto resolve_plan_pkg_configs(
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    const std::string& pc_override = "",
    bool strict_missing = false
) -> std::expected<void, std::string>;

[[nodiscard]] CFIRE_INFRA_INLINE auto resolve_plan_defines(
    BuildPlan& plan
) -> std::expected<void, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

[[nodiscard]] CFIRE_INFRA_INLINE auto parse_build_cf(
    const std::filesystem::path& build_cf_path,
    std::string_view target_triple)
    -> std::expected<ParsedBuildCf, std::string> {
    auto raw = read_file_text(build_cf_path);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(build_cf_path.string(), *raw);
    if (!ast) return std::unexpected(ast.error());

    std::error_code bcf_ec;
    auto bcf_abs = std::filesystem::weakly_canonical(build_cf_path, bcf_ec);
    auto bcf_dir = (bcf_ec ? std::filesystem::absolute(build_cf_path, bcf_ec) : bcf_abs).parent_path();

    // Determine deterministic target OS & arch for `build.os()` / `build.arch()` evaluation
    std::string target_os;
    std::string target_arch;
    {
        std::string tt(target_triple);
        if (tt == "wasm" || tt == "web" || tt.find("wasm") != std::string::npos) {
            target_os = "wasm";
            target_arch = "wasm32";
        } else if (tt == "ios" || tt == "ios-sim" || tt.find("apple-ios") != std::string::npos) {
            target_os = "ios";
            target_arch = "arm64";
        } else if (!tt.empty()) {
            if (tt.find("apple-macos") != std::string::npos || tt.find("apple-darwin") != std::string::npos) {
                target_os = "macos";
            } else if (tt.find("windows") != std::string::npos || tt.find("win32") != std::string::npos || tt.find("mingw") != std::string::npos) {
                target_os = "windows";
            } else if (tt.find("linux") != std::string::npos) {
                target_os = "linux";
            }
            if (tt.find("arm64") != std::string::npos || tt.find("aarch64") != std::string::npos) {
                target_arch = "arm64";
            } else if (tt.find("x86_64") != std::string::npos || tt.find("amd64") != std::string::npos) {
                target_arch = "x86_64";
            }
        }
        if (target_os.empty()) {
#if defined(__APPLE__)
            target_os = "macos";
#elif defined(_WIN32)
            target_os = "windows";
#elif defined(__linux__)
            target_os = "linux";
#else
            target_os = "unknown";
#endif
        }
        if (target_arch.empty()) {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64__)
            target_arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
            target_arch = "x86_64";
#else
            target_arch = "unknown";
#endif
        }
    }

    struct ManifestVal {
        enum class Kind { Bool, Str, List, Map };
        Kind kind = Kind::Str;
        bool b = false;
        std::string s{};
        std::vector<std::string> list{};
        std::vector<std::pair<std::string, std::pair<bool, std::string>>> map_entries{}; // key -> (is_bool, str_or_bool_val)
    };
    std::unordered_map<std::string, ManifestVal> env;

    auto invalid_val = [&](int line, std::string_view msg) -> std::unexpected<std::string> {
        return std::unexpected(std::format("{}:{}: {} (cfire/INVALID-BUILD-VALUE)", build_cf_path.string(), line, msg));
    };

    auto eval_expr = [&](auto& self, const frontend::Expr& expr) -> std::expected<ManifestVal, std::string> {
        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&expr.node)) {
            if (lit->kind == frontend::TokenKind::KwTrue) {
                return ManifestVal{ManifestVal::Kind::Bool, true, "true", {}, {}};
            }
            if (lit->kind == frontend::TokenKind::KwFalse) {
                return ManifestVal{ManifestVal::Kind::Bool, false, "false", {}, {}};
            }
            if (lit->kind == frontend::TokenKind::String || lit->kind == frontend::TokenKind::RawString ||
                lit->kind == frontend::TokenKind::Int || lit->kind == frontend::TokenKind::Float ||
                lit->kind == frontend::TokenKind::Char) {
                return ManifestVal{ManifestVal::Kind::Str, false, lit->value, {}, {}};
            }
            return invalid_val(expr.line, "unsupported literal kind in build.cf manifest value");
        }
        if (const auto* id = std::get_if<frontend::IdentExpr>(&expr.node)) {
            auto it = env.find(id->name);
            if (it == env.end()) {
                return invalid_val(expr.line, std::format("unknown or non-constant identifier '{}' in build.cf", id->name));
            }
            return it->second;
        }
        if (const auto* un = std::get_if<frontend::UnaryExpr>(&expr.node)) {
            if (!un->operand) return invalid_val(expr.line, "missing unary operand in build.cf");
            auto ov = self(self, *un->operand);
            if (!ov) return ov;
            if (un->op == "!") {
                if (ov->kind != ManifestVal::Kind::Bool) {
                    return invalid_val(expr.line, "'!' operand in build.cf must be bool");
                }
                return ManifestVal{ManifestVal::Kind::Bool, !ov->b, !ov->b ? "true" : "false", {}, {}};
            }
            if (un->op == "-") {
                if (ov->kind != ManifestVal::Kind::Str) {
                    return invalid_val(expr.line, "unary '-' operand in build.cf must be numeric literal");
                }
                return ManifestVal{ManifestVal::Kind::Str, false, "-" + ov->s, {}, {}};
            }
            return invalid_val(expr.line, std::format("unsupported unary operator '{}' in build.cf", un->op));
        }
        if (const auto* call = std::get_if<frontend::CallExpr>(&expr.node)) {
            if (call->callee) {
                if (const auto* mem = std::get_if<frontend::MemberExpr>(&call->callee->node)) {
                    if (mem->object) {
                        if (const auto* obj_id = std::get_if<frontend::IdentExpr>(&mem->object->node)) {
                            if (obj_id->name == "build" && call->args.empty()) {
                                if (mem->field == "os") {
                                    return ManifestVal{ManifestVal::Kind::Str, false, target_os, {}, {}};
                                }
                                if (mem->field == "arch") {
                                    return ManifestVal{ManifestVal::Kind::Str, false, target_arch, {}, {}};
                                }
                            }
                        }
                    }
                }
            }
            return invalid_val(expr.line, "call expression cannot be evaluated as a compile-time constant in build.cf");
        }
        if (const auto* bin = std::get_if<frontend::BinaryExpr>(&expr.node)) {
            if (!bin->left || !bin->right) {
                return invalid_val(expr.line, "incomplete binary expression in build.cf");
            }
            auto lv = self(self, *bin->left);
            if (!lv) return lv;
            if (bin->op == "&&") {
                if (lv->kind != ManifestVal::Kind::Bool) return invalid_val(expr.line, "'&&' requires bool operands in build.cf");
                if (!lv->b) return ManifestVal{ManifestVal::Kind::Bool, false, "false", {}, {}};
                auto rv = self(self, *bin->right);
                if (!rv) return rv;
                if (rv->kind != ManifestVal::Kind::Bool) return invalid_val(expr.line, "'&&' requires bool operands in build.cf");
                return rv;
            }
            if (bin->op == "||") {
                if (lv->kind != ManifestVal::Kind::Bool) return invalid_val(expr.line, "'||' requires bool operands in build.cf");
                if (lv->b) return ManifestVal{ManifestVal::Kind::Bool, true, "true", {}, {}};
                auto rv = self(self, *bin->right);
                if (!rv) return rv;
                if (rv->kind != ManifestVal::Kind::Bool) return invalid_val(expr.line, "'||' requires bool operands in build.cf");
                return rv;
            }
            auto rv = self(self, *bin->right);
            if (!rv) return rv;
            if (bin->op == "==" || bin->op == "!=") {
                if (lv->kind != rv->kind) {
                    return invalid_val(expr.line, std::format("cannot compare incompatible manifest value types with '{}'", bin->op));
                }
                bool eq = false;
                if (lv->kind == ManifestVal::Kind::Str) eq = (lv->s == rv->s);
                else if (lv->kind == ManifestVal::Kind::Bool) eq = (lv->b == rv->b);
                else if (lv->kind == ManifestVal::Kind::List) eq = (lv->list == rv->list);
                else return invalid_val(expr.line, "maps cannot be compared in build.cf");
                bool res = (bin->op == "==") ? eq : !eq;
                return ManifestVal{ManifestVal::Kind::Bool, res, res ? "true" : "false", {}, {}};
            }
            if (bin->op == "+") {
                if (lv->kind == ManifestVal::Kind::Str && rv->kind == ManifestVal::Kind::Str) {
                    return ManifestVal{ManifestVal::Kind::Str, false, lv->s + rv->s, {}, {}};
                }
                if (lv->kind == ManifestVal::Kind::List && rv->kind == ManifestVal::Kind::List) {
                    std::vector<std::string> joined = lv->list;
                    joined.insert(joined.end(), rv->list.begin(), rv->list.end());
                    return ManifestVal{ManifestVal::Kind::List, false, {}, std::move(joined), {}};
                }
                return invalid_val(expr.line, "'+' in build.cf requires two strings or two string lists");
            }
            return invalid_val(expr.line, std::format("unsupported operator '{}' in build.cf expression", bin->op));
        }
        if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&expr.node)) {
            std::vector<std::string> items;
            items.reserve(arr->elements.size());
            for (const auto& el : arr->elements) {
                if (!el) continue;
                auto ev = self(self, *el);
                if (!ev) return ev;
                if (ev->kind != ManifestVal::Kind::Str) {
                    return invalid_val(el->line, "list elements in build.cf must evaluate to constant strings");
                }
                items.push_back(std::move(ev->s));
            }
            return ManifestVal{ManifestVal::Kind::List, false, {}, std::move(items), {}};
        }
        if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&expr.node)) {
            if (!st_lit->type_name.empty()) {
                return invalid_val(expr.line, std::format("struct literal '{}' cannot be used as a field value in build.cf", st_lit->type_name));
            }
            ManifestVal mv;
            mv.kind = ManifestVal::Kind::Map;
            for (const auto& mf : st_lit->fields) {
                if (!mf.value) {
                    mv.map_entries.emplace_back(mf.name, std::pair<bool, std::string>{true, "true"});
                    continue;
                }
                auto fv = self(self, *mf.value);
                if (!fv) return fv;
                if (fv->kind == ManifestVal::Kind::Bool) {
                    mv.map_entries.emplace_back(mf.name, std::pair<bool, std::string>{true, fv->b ? "true" : "false"});
                } else if (fv->kind == ManifestVal::Kind::Str) {
                    mv.map_entries.emplace_back(mf.name, std::pair<bool, std::string>{false, fv->s});
                } else {
                    return invalid_val(mf.value->line, std::format("map field '{}' value in build.cf must be bool or string", mf.name));
                }
            }
            return mv;
        }
        if (const auto* me = std::get_if<frontend::MatchExpr>(&expr.node)) {
            if (!me->target) return invalid_val(expr.line, "missing condition/target in build.cf branch expression");
            auto eval_branch_body = [&](const std::vector<frontend::StmtPtr>& body) -> std::expected<ManifestVal, std::string> {
                if (body.size() != 1 || !body[0]) {
                    return invalid_val(expr.line, "conditional branch in build.cf must contain a single expression");
                }
                if (const auto* es = std::get_if<frontend::ExprStmt>(&body[0]->node); es && es->expr) {
                    return self(self, *es->expr);
                }
                if (const auto* rs = std::get_if<frontend::ReturnStmt>(&body[0]->node); rs && rs->value) {
                    return self(self, *rs->value);
                }
                return invalid_val(body[0]->line, "conditional branch statement cannot be evaluated at compile time in build.cf");
            };
            auto tv = self(self, *me->target);
            if (!tv) return tv;
            if (me->is_if) {
                if (tv->kind != ManifestVal::Kind::Bool) {
                    return invalid_val(me->target->line, "'if' condition in build.cf must evaluate to bool");
                }
                if (me->arms.size() < 2) {
                    return invalid_val(expr.line, "'if' expression in build.cf requires an 'else' branch");
                }
                return eval_branch_body(tv->b ? me->arms[0].body : me->arms[1].body);
            }
            auto pattern_matches = [&](const frontend::MatchPattern& pat) -> bool {
                if (pat.variant_name == "_") return true;
                if (tv->kind == ManifestVal::Kind::Str) {
                    std::string_view lit = pat.literal_val;
                    if (lit.size() >= 2 && lit.front() == '"' && lit.back() == '"') {
                        lit = lit.substr(1, lit.size() - 2);
                    } else if (lit.starts_with("R\"(") && lit.ends_with(")\"") && lit.size() >= 5) {
                        lit = lit.substr(3, lit.size() - 5);
                    }
                    return lit == tv->s;
                }
                if (tv->kind == ManifestVal::Kind::Bool) {
                    return (tv->b && pat.literal_val == "true") || (!tv->b && pat.literal_val == "false");
                }
                return false;
            };
            for (const auto& arm : me->arms) {
                if (arm.guard) {
                    return invalid_val(arm.line, "match guards are not supported in build.cf constant expressions");
                }
                if (arm.variant_name == "_") {
                    return eval_branch_body(arm.body);
                }
                bool matched = false;
                if (!arm.patterns.empty()) {
                    for (const auto& pat : arm.patterns) {
                        if (pattern_matches(pat)) {
                            matched = true;
                            break;
                        }
                    }
                } else {
                    matched = pattern_matches(arm.to_pattern());
                }
                if (matched) return eval_branch_body(arm.body);
            }
            return invalid_val(expr.line, "non-exhaustive match in build.cf constant expression");
        }
        return invalid_val(expr.line, "expression cannot be evaluated as a compile-time constant in build.cf");
    };

    std::string eval_err;
    auto eval_str = [&](const frontend::Expr& expr) -> std::string {
        if (!eval_err.empty()) return {};
        auto r = eval_expr(eval_expr, expr);
        if (!r) {
            eval_err = r.error();
            return {};
        }
        if (r->kind != ManifestVal::Kind::Str) {
            eval_err = invalid_val(expr.line, "expected constant string value in build.cf").error();
            return {};
        }
        return r->s;
    };
    auto eval_bool = [&](const frontend::Expr& expr) -> bool {
        if (!eval_err.empty()) return false;
        auto r = eval_expr(eval_expr, expr);
        if (!r) {
            eval_err = r.error();
            return false;
        }
        if (r->kind != ManifestVal::Kind::Bool) {
            eval_err = invalid_val(expr.line, "expected constant bool value in build.cf").error();
            return false;
        }
        return r->b;
    };
    auto collect_str_or_arr = [&](const frontend::Expr& expr, std::vector<std::string>& out, bool clear_arr = false) {
        if (!eval_err.empty()) return;
        auto r = eval_expr(eval_expr, expr);
        if (!r) {
            eval_err = r.error();
            return;
        }
        if (r->kind == ManifestVal::Kind::List) {
            if (clear_arr) out = r->list;
            else out.insert(out.end(), r->list.begin(), r->list.end());
        } else if (r->kind == ManifestVal::Kind::Str) {
            if (clear_arr) out = {r->s};
            else out.push_back(r->s);
        } else {
            eval_err = invalid_val(expr.line, "expected constant string or string list in build.cf").error();
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
        if (!eval_err.empty() || !def_err.empty()) return;
        auto r = eval_expr(eval_expr, expr);
        if (!r) {
            eval_err = r.error();
            return;
        }
        if (r->kind == ManifestVal::Kind::Map) {
            for (const auto& [k, val_pair] : r->map_entries) {
                if (!def_err.empty()) return;
                if (!is_valid_c_macro_ident(k)) {
                    def_err = std::format(
                        "invalid C macro identifier '{}' in build.cf Defines (expected [A-Za-z_][A-Za-z0-9_]*)", k);
                    return;
                }
                if (val_pair.first) {
                    if (val_pair.second == "true") push_define_raw(out, k);
                } else {
                    push_define_raw(out, std::format("{}={}", k, val_pair.second));
                }
            }
        } else if (r->kind == ManifestVal::Kind::List) {
            for (const auto& item : r->list) {
                push_define_raw(out, item);
            }
        } else if (r->kind == ManifestVal::Kind::Str) {
            push_define_raw(out, r->s);
        } else {
            eval_err = invalid_val(expr.line, "Defines field must be a map, string list, or string in build.cf").error();
        }
    };

    auto populate_pkg_fields = [&](const frontend::StructLitExpr& st_lit,
                                   std::vector<std::string>& pkg_config,
                                   std::vector<std::string>& libraries,
                                   std::vector<std::string>& frameworks,
                                   std::vector<std::string>& includes,
                                   std::vector<std::string>& defines,
                                   std::vector<std::string>& cflags,
                                   std::vector<std::string>& ldflags,
                                   std::vector<std::string>& sources) {
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
            } else if (fld.name == "CFlags" || fld.name == "cflags" || fld.name == "Flags" || fld.name == "flags") {
                collect_str_or_arr(*fld.value, cflags);
            } else if (fld.name == "LDFlags" || fld.name == "ldflags") {
                collect_str_or_arr(*fld.value, ldflags);
            } else if (fld.name == "Sources" || fld.name == "sources") {
                collect_str_or_arr(*fld.value, sources);
            }
        }
    };

    auto populate_exe_fields = [&](ExecutableSpec& exe_spec, const frontend::StructLitExpr& st_lit) {
        populate_pkg_fields(
            st_lit, exe_spec.pkg_config, exe_spec.libraries, exe_spec.frameworks,
            exe_spec.includes, exe_spec.defines, exe_spec.cflags, exe_spec.ldflags, exe_spec.sources);
        for (const auto& fld : st_lit.fields) {
            if (!fld.value) continue;
            if (fld.name == "Entry" || fld.name == "entry") {
                exe_spec.entry = eval_str(*fld.value);
            } else if (fld.name == "Subsystem" || fld.name == "subsystem") {
                exe_spec.subsystem = eval_str(*fld.value);
            } else if (fld.name == "Name" || fld.name == "name") {
                std::string nm = eval_str(*fld.value);
                if (exe_spec.name.empty()) exe_spec.name = nm;
                if (exe_spec.app_name.empty()) exe_spec.app_name = nm;
            } else if (fld.name == "AppName" || fld.name == "app_name") {
                exe_spec.app_name = eval_str(*fld.value);
            } else if (fld.name == "BundleId" || fld.name == "BundleID" || fld.name == "Identifier" || fld.name == "bundle_id") {
                exe_spec.bundle_id = eval_str(*fld.value);
            } else if (fld.name == "Icon" || fld.name == "icon") {
                exe_spec.icon = eval_str(*fld.value);
            } else if (fld.name == "Targets" || fld.name == "targets") {
                collect_str_or_arr(*fld.value, exe_spec.targets, true);
            } else if (fld.name == "OutDir" || fld.name == "out_dir") {
                exe_spec.out_dir = eval_str(*fld.value);
            } else if (fld.name == "WebProfile" || fld.name == "web_profile") {
                exe_spec.web_profile = eval_str(*fld.value);
            } else if (fld.name == "UrlSchemes" || fld.name == "url_schemes") {
                collect_str_or_arr(*fld.value, exe_spec.url_schemes);
            } else if (fld.name == "SignIdentity" || fld.name == "sign_identity") {
                exe_spec.sign_identity = eval_str(*fld.value);
            } else if (fld.name == "Portable" || fld.name == "portable") {
                exe_spec.portable = eval_bool(*fld.value);
            } else if (fld.name == "Static" || fld.name == "static" || fld.name == "static_link") {
                exe_spec.static_link = eval_bool(*fld.value);
            } else if (fld.name == "Universal" || fld.name == "universal") {
                exe_spec.universal = eval_bool(*fld.value);
            } else if (fld.name == "MacosMin" || fld.name == "macos_min") {
                exe_spec.macos_min = eval_str(*fld.value);
            } else if (fld.name == "Version" || fld.name == "version") {
                exe_spec.version = eval_str(*fld.value);
            } else if (fld.name == "BuildNumber" || fld.name == "build_number") {
                exe_spec.build_number = eval_str(*fld.value);
            } else if (fld.name == "Resources" || fld.name == "resources") {
                collect_str_or_arr(*fld.value, exe_spec.resources, true);
                exe_spec.has_explicit_resources = true;
            } else if (fld.name == "IosMin" || fld.name == "ios_min") {
                exe_spec.ios_min = eval_str(*fld.value);
            } else if (fld.name == "IosOrientations" || fld.name == "ios_orientations") {
                collect_str_or_arr(*fld.value, exe_spec.ios_orientations, true);
            } else if (fld.name == "IosDeviceFamily" || fld.name == "ios_device_family") {
                auto add_family = [&](std::string_view raw_fam) {
                    if (raw_fam == "1" || raw_fam == "iphone" || raw_fam == "iPhone") {
                        exe_spec.ios_device_family.push_back(1);
                    } else if (raw_fam == "2" || raw_fam == "ipad" || raw_fam == "iPad") {
                        exe_spec.ios_device_family.push_back(2);
                    }
                };
                exe_spec.ios_device_family.clear();
                std::vector<std::string> fam_items;
                collect_str_or_arr(*fld.value, fam_items, true);
                for (const auto& item : fam_items) add_family(item);
            } else if (fld.name == "ProvisioningProfile" || fld.name == "provisioning_profile") {
                exe_spec.provisioning_profile = eval_str(*fld.value);
            } else if (fld.name == "Entitlements" || fld.name == "entitlements") {
                exe_spec.entitlements = eval_str(*fld.value);
            } else if (fld.name == "PrivacyManifest" || fld.name == "privacy_manifest") {
                exe_spec.privacy_manifest = eval_str(*fld.value);
            } else if (fld.name == "InfoPlist" || fld.name == "info_plist") {
                auto r = eval_expr(eval_expr, *fld.value);
                if (!r) {
                    eval_err = r.error();
                    return;
                }
                if (r->kind != ManifestVal::Kind::Map) {
                    eval_err = invalid_val(fld.value->line, "InfoPlist must be a map literal in build.cf").error();
                    return;
                }
                auto upsert_plist = [&](const std::string& k, std::string v, bool is_bool) {
                    for (auto& kv : exe_spec.info_plist_extras) {
                        if (kv.first == k) {
                            kv.second = std::move(v);
                            if (is_bool) exe_spec.info_plist_bool_keys.insert(k);
                            else exe_spec.info_plist_bool_keys.erase(k);
                            return;
                        }
                    }
                    exe_spec.info_plist_extras.emplace_back(k, std::move(v));
                    if (is_bool) exe_spec.info_plist_bool_keys.insert(k);
                };
                for (const auto& [k, val_pair] : r->map_entries) {
                    upsert_plist(k, val_pair.second, val_pair.first);
                }
            }
        }
    };

    auto populate_step_fields = [&](PreBuildCommandSpec& cmd_spec, const frontend::StructLitExpr& st_lit) {
        for (const auto& fld : st_lit.fields) {
            if (!fld.value) continue;
            if (fld.name == "Command" || fld.name == "command") {
                cmd_spec.command = eval_str(*fld.value);
            } else if (fld.name == "Args" || fld.name == "args") {
                collect_str_or_arr(*fld.value, cmd_spec.args);
            } else if (fld.name == "Inputs" || fld.name == "inputs") {
                collect_str_or_arr(*fld.value, cmd_spec.inputs);
            } else if (fld.name == "Outputs" || fld.name == "outputs") {
                collect_str_or_arr(*fld.value, cmd_spec.outputs);
            } else if (fld.name == "WorkDir" || fld.name == "work_dir") {
                cmd_spec.work_dir = eval_str(*fld.value);
            }
        }
    };

    auto is_manifest_builder_call = [](const frontend::Expr& expr, std::string& field_out) -> const frontend::CallExpr* {
        const auto* call = std::get_if<frontend::CallExpr>(&expr.node);
        if (!call || !call->callee) return nullptr;
        const auto* mem = std::get_if<frontend::MemberExpr>(&call->callee->node);
        if (!mem) return nullptr;
        if (mem->object) {
            if (const auto* id = std::get_if<frontend::IdentExpr>(&mem->object->node)) {
                if (id->name == "build" && (mem->field == "os" || mem->field == "arch")) {
                    return nullptr; // query call, not a builder statement
                }
            }
        }
        field_out = mem->field;
        return call;
    };

    ParsedBuildCf parsed;
    for (const auto& gv : ast->global_vars) {
        if (!gv.value) continue;
        if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&gv.value->node)) {
            if (st_lit->type_name == "Package" || st_lit->type_name == "build.Package") {
                populate_pkg_fields(
                    *st_lit, parsed.pkg_config, parsed.libraries, parsed.frameworks,
                    parsed.includes, parsed.defines, parsed.cflags, parsed.ldflags, parsed.sources);
                if (!eval_err.empty()) return std::unexpected(eval_err);
                continue;
            }
            if (st_lit->type_name == "Executable" || st_lit->type_name == "App" ||
                st_lit->type_name == "build.Executable" || st_lit->type_name == "build.App") {
                ExecutableSpec exe_spec;
                exe_spec.var_name = gv.name;
                populate_exe_fields(exe_spec, *st_lit);
                if (!eval_err.empty()) return std::unexpected(eval_err);
                if (exe_spec.name.empty()) exe_spec.name = gv.name;
                if (exe_spec.targets.empty()) exe_spec.targets.push_back("native");
                if (exe_spec.app_name.empty()) exe_spec.app_name = exe_spec.name;
                if (exe_spec.bundle_id.empty()) exe_spec.bundle_id = "com.cfire." + exe_spec.name;
                parsed.executables.push_back(std::move(exe_spec));
                continue;
            }
            if (st_lit->type_name == "Step" || st_lit->type_name == "build.Step" ||
                st_lit->type_name == "Command" || st_lit->type_name == "build.Command") {
                PreBuildCommandSpec cmd_spec;
                populate_step_fields(cmd_spec, *st_lit);
                if (!eval_err.empty()) return std::unexpected(eval_err);
                parsed.pre_build_commands.push_back(std::move(cmd_spec));
                continue;
            }
        }

        std::string builder_field;
        const auto* call = is_manifest_builder_call(*gv.value, builder_field);
        if (!call) {
            // Top-level constant manifest binding (e.g., `is_mac := build.os() == "macos"` or `flags := if ...`)
            auto val_res = eval_expr(eval_expr, *gv.value);
            if (!val_res) return std::unexpected(val_res.error());
            if (!gv.name.empty() && gv.name != "_") {
                env[gv.name] = std::move(*val_res);
            }
            continue;
        }

        if (builder_field == "Package") {
            for (const auto& arg : call->args) {
                if (!arg.value) continue;
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&arg.value->node)) {
                    populate_pkg_fields(
                        *st_lit, parsed.pkg_config, parsed.libraries, parsed.frameworks,
                        parsed.includes, parsed.defines, parsed.cflags, parsed.ldflags, parsed.sources);
                } else {
                    return invalid_val(arg.value->line, "build.Package expects a configuration struct literal");
                }
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "PkgConfig") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.pkg_config);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "Link" || builder_field == "Libraries") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.libraries);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "Include" || builder_field == "Includes") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.includes);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "Frameworks") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.frameworks);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "CFlags" || builder_field == "Flags") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.cflags);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "LDFlags") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_str_or_arr(*arg.value, parsed.ldflags);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "Define") {
            if (call->args.size() == 1 && call->args[0].value) {
                collect_defines_expr(*call->args[0].value, parsed.defines);
            } else if (call->args.size() >= 2 && call->args[0].value && call->args[1].value) {
                std::string key = eval_str(*call->args[0].value);
                if (!eval_err.empty()) return std::unexpected(eval_err);
                if (!is_valid_c_macro_ident(key)) {
                    def_err = std::format(
                        "invalid C macro identifier '{}' in build.Define (expected [A-Za-z_][A-Za-z0-9_]*)", key);
                } else {
                    auto vr = eval_expr(eval_expr, *call->args[1].value);
                    if (!vr) return std::unexpected(vr.error());
                    if (vr->kind == ManifestVal::Kind::Bool) {
                        if (vr->b) push_define_raw(parsed.defines, key);
                    } else if (vr->kind == ManifestVal::Kind::Str) {
                        push_define_raw(parsed.defines, std::format("{}={}", key, vr->s));
                    } else {
                        return invalid_val(call->args[1].value->line, "build.Define value must be a constant bool or string");
                    }
                }
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }
        if (builder_field == "Defines") {
            for (const auto& arg : call->args) {
                if (arg.value) collect_defines_expr(*arg.value, parsed.defines);
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            continue;
        }

        if (builder_field == "Executable" || builder_field == "App") {
            ExecutableSpec exe_spec;
            exe_spec.var_name = gv.name;
            if (!call->args.empty() && call->args[0].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[0].value->node)) {
                    populate_exe_fields(exe_spec, *st_lit);
                } else {
                    exe_spec.name = eval_str(*call->args[0].value);
                }
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            if (exe_spec.name.empty()) {
                exe_spec.name = gv.name;
            }
            if (call->args.size() > 1 && call->args[1].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                    populate_exe_fields(exe_spec, *st_lit);
                } else {
                    return invalid_val(call->args[1].value->line, "expected configuration struct literal as second argument");
                }
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
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

        if (builder_field == "Command" || builder_field == "Step") {
            PreBuildCommandSpec cmd_spec;
            if (!call->args.empty() && call->args[0].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[0].value->node)) {
                    populate_step_fields(cmd_spec, *st_lit);
                } else {
                    cmd_spec.command = eval_str(*call->args[0].value);
                }
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            if (call->args.size() > 1 && call->args[1].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                    std::string step_or_cmd = cmd_spec.command;
                    cmd_spec.command.clear();
                    populate_step_fields(cmd_spec, *st_lit);
                    if (cmd_spec.command.empty()) cmd_spec.command = std::move(step_or_cmd);
                } else {
                    return invalid_val(call->args[1].value->line, "build.Step/Command expects a configuration struct literal");
                }
            }
            if (!eval_err.empty()) return std::unexpected(eval_err);
            parsed.pre_build_commands.push_back(std::move(cmd_spec));
            continue;
        }

        static const std::unordered_set<std::string_view> kKnownDepDrivers = {
            "CC", "CMake", "Make", "Ninja", "Autotools", "Git", "Archive"
        };
        if (!kKnownDepDrivers.contains(builder_field)) {
            return invalid_val(gv.line, std::format("unknown build manifest builder 'build.{}'", builder_field));
        }

        ExternalDepSpec spec;
        spec.name = gv.name;
        spec.driver = builder_field;
        spec.base_dir = bcf_dir;
        if (builder_field == "Git") {
            spec.fetch_kind = "git";
            spec.is_remote = true;
            spec.driver = "CC";
        } else if (builder_field == "Archive") {
            spec.fetch_kind = "archive";
            spec.is_remote = true;
            spec.driver = "CC";
        }
        auto populate_dep_fields = [&](const frontend::StructLitExpr& st_lit) {
            for (const auto& fld : st_lit.fields) {
                if (!fld.value) continue;
                if (fld.name == "Target") {
                    spec.target = eval_str(*fld.value);
                } else if (fld.name == "Sources") {
                    collect_str_or_arr(*fld.value, spec.sources);
                } else if (fld.name == "Includes") {
                    collect_str_or_arr(*fld.value, spec.includes);
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
                    auto dr = eval_expr(eval_expr, *fld.value);
                    if (!dr) {
                        eval_err = dr.error();
                        return;
                    }
                    if (dr->kind == ManifestVal::Kind::Map) {
                        for (const auto& [k, val_pair] : dr->map_entries) {
                            if (val_pair.first) {
                                if (val_pair.second == "true") record_dep_def(k, "1");
                            } else {
                                record_dep_def(k, val_pair.second);
                            }
                        }
                    } else if (validate_keys && (dr->kind == ManifestVal::Kind::List || dr->kind == ManifestVal::Kind::Str)) {
                        std::vector<std::string> items = (dr->kind == ManifestVal::Kind::List)
                            ? dr->list
                            : std::vector<std::string>{dr->s};
                        for (const auto& d : items) {
                            auto eq = d.find('=');
                            if (eq == std::string::npos) record_dep_def(d, "1");
                            else record_dep_def(std::string_view(d).substr(0, eq), d.substr(eq + 1));
                        }
                    } else if (validate_keys) {
                        eval_err = invalid_val(fld.value->line, "Defines must evaluate to a map, string list, or string").error();
                        return;
                    }
                } else if (fld.name == "Url" || fld.name == "URL") {
                    spec.url = eval_str(*fld.value);
                    spec.is_remote = true;
                } else if (fld.name == "Tag" || fld.name == "Ref" || fld.name == "Commit" || fld.name == "Branch") {
                    spec.ref = eval_str(*fld.value);
                } else if (fld.name == "Sha256" || fld.name == "Hash" || fld.name == "Checksum") {
                    spec.sha256 = eval_str(*fld.value);
                } else if (fld.name == "Policy") {
                    spec.policy = eval_str(*fld.value);
                } else if (fld.name == "Driver") {
                    spec.driver = eval_str(*fld.value);
                } else if (fld.name == "Static") {
                    (void)eval_bool(*fld.value);
                }
            }
        };
        if (!call->args.empty() && call->args[0].value) {
            if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[0].value->node)) {
                populate_dep_fields(*st_lit);
            } else {
                spec.source = eval_str(*call->args[0].value);
            }
        }
        if (!eval_err.empty()) return std::unexpected(eval_err);
        if (call->args.size() > 1 && call->args[1].value) {
            if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                populate_dep_fields(*st_lit);
            } else {
                return invalid_val(call->args[1].value->line, "expected configuration struct literal for dependency");
            }
        }
        if (!eval_err.empty()) return std::unexpected(eval_err);
        if (spec.fetch_kind == "git" || spec.fetch_kind == "archive" ||
            builder_field == "Git" || builder_field == "Archive" ||
            spec.source.starts_with("https://") || spec.source.starts_with("http://") ||
            spec.source.ends_with(".git") || spec.source.starts_with("git@") ||
            !spec.url.empty()) {
            spec.is_remote = true;
            if (spec.url.empty()) spec.url = spec.source;
            if (spec.fetch_kind.empty()) {
                if (builder_field == "Git" || spec.url.ends_with(".git") || spec.url.starts_with("git@")) {
                    spec.fetch_kind = "git";
                    if (spec.driver == "Git") spec.driver = "CC";
                } else if (builder_field == "Archive" || spec.url.ends_with(".zip") || spec.url.ends_with(".tar.gz") || spec.url.ends_with(".tgz") || spec.url.ends_with(".tar.xz")) {
                    spec.fetch_kind = "archive";
                    if (spec.driver == "Archive") spec.driver = "CC";
                }
            }
        }
        parsed.external_deps.push_back(std::move(spec));
    }
    if (!eval_err.empty()) {
        return std::unexpected(eval_err);
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
    const std::vector<std::string>& defines,
    const std::vector<std::string>& cflags,
    const std::vector<std::string>& ldflags
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
    for (const auto& cf : cflags) {
        for (auto& tok : split_ws(cf)) add_unique(plan.cflags, std::move(tok));
    }
    if (is_wasm) return;

    const bool is_apple_tgt = plan.target_triple.empty()
#if defined(__APPLE__)
        ? true
#else
        ? false
#endif
        : (plan.target_triple.find("apple") != std::string::npos ||
           plan.target_triple == "ios" || plan.target_triple == "ios-sim");
    if (is_apple_tgt) {
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
    }
    for (const auto& lib : libraries) {
        if (lib.empty()) continue;
        add_unique(plan.ldflags, lib.starts_with("-l") ? lib : ("-l" + lib));
    }
    for (const auto& lf : ldflags) {
        for (auto& tok : split_ws(lf)) add_unique(plan.ldflags, std::move(tok));
    }
}

CFIRE_INFRA_INLINE auto resolve_plan_pkg_configs(
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    const std::string& pc_override,
    bool strict_missing
) -> std::expected<void, std::string> {
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
    if (is_wasm) return {};

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
        if (auto qr = query_pkg_config(pc, plan.cflags, plan.ldflags, install_root, prefer_static, strict_missing); !qr) {
            return std::unexpected(qr.error());
        }
    }
    return {};
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
