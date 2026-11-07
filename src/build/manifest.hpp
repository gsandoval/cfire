#pragma once
// src/build/manifest.hpp — `build.cf` & `CMakeLists.txt` Manifest Parsing & Target Resolution

#include "toolchain_layout.hpp"

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
    std::vector<std::string> url_schemes{};
    std::string sign_identity{};
};

struct ParsedBuildCf {
    std::vector<ExternalDepSpec> external_deps{};
    std::vector<PreBuildCommandSpec> pre_build_commands{};
    std::vector<ExecutableSpec> executables{};
};

struct BuildPlan {
    std::string entry_file{};
    std::string generated_cpp{};
    std::vector<std::string> local_cf_packages{};
    std::vector<std::string> auto_linked{};
    std::vector<std::string> pkg_config_deps{};
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
    int window_init_width{0};
    int window_init_height{0};
    std::string emscripten_compiler{};
};


[[nodiscard]] inline auto parse_build_cf(const std::filesystem::path& build_cf_path)
    -> std::expected<ParsedBuildCf, std::string>
{
    auto raw = read_file_text(build_cf_path);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(build_cf_path.string(), *raw);
    if (!ast) return std::unexpected(ast.error());

    std::error_code bcf_ec;
    auto bcf_abs = std::filesystem::weakly_canonical(build_cf_path, bcf_ec);
    auto bcf_dir = (bcf_ec ? std::filesystem::absolute(build_cf_path, bcf_ec) : bcf_abs).parent_path();

    auto populate_exe_fields = [](ExecutableSpec& exe_spec, const frontend::StructLitExpr& st_lit) {
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
                if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                    exe_spec.targets.clear();
                    for (const auto& el : arr->elements) {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                            exe_spec.targets.push_back(lit->value);
                        }
                    }
                } else if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.targets = {lit->value};
                }
            } else if (fld.name == "OutDir" || fld.name == "out_dir") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.out_dir = lit->value;
                }
            } else if (fld.name == "WebProfile" || fld.name == "web_profile") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.web_profile = lit->value;
                }
            } else if (fld.name == "Frameworks" || fld.name == "frameworks") {
                if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                    for (const auto& el : arr->elements) {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                            exe_spec.frameworks.push_back(lit->value);
                        }
                    }
                } else if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.frameworks.push_back(lit->value);
                }
            } else if (fld.name == "Libraries" || fld.name == "libraries" || fld.name == "Libs" || fld.name == "libs") {
                if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                    for (const auto& el : arr->elements) {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                            exe_spec.libraries.push_back(lit->value);
                        }
                    }
                } else if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.libraries.push_back(lit->value);
                }
            } else if (fld.name == "UrlSchemes" || fld.name == "url_schemes") {
                if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                    for (const auto& el : arr->elements) {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                            exe_spec.url_schemes.push_back(lit->value);
                        }
                    }
                } else if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.url_schemes.push_back(lit->value);
                }
            } else if (fld.name == "SignIdentity" || fld.name == "sign_identity") {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                    exe_spec.sign_identity = lit->value;
                }
            }
        }
    };

    ParsedBuildCf parsed;
    for (const auto& gv : ast->global_vars) {
        if (!gv.value) continue;
        if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&gv.value->node)) {
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
                    } else if (fld.name == "Defines" || fld.name == "Flags") {
                        if (const auto* map_lit = std::get_if<frontend::StructLitExpr>(&fld.value->node)) {
                            for (const auto& mf : map_lit->fields) {
                                if (mf.value) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&mf.value->node)) {
                                        spec.defines[mf.name] = lit->value;
                                    }
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
    return parsed;
}

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

[[nodiscard]] inline auto resolve_build_target(
    const std::filesystem::path& base_dir_or_build_cf,
    std::string_view target_name = "",
    bool prefer_wasm = false
) -> std::optional<TargetResolution> {
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

// Extracts tokens inside a CMake command like `add_library(kvstore STATIC src/kvstore.c)`
inline void parse_cmake_manifest(
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


} // namespace cfire::build
