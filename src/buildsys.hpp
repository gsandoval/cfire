#pragma once
// src/buildsys.hpp — C++23 Unified Build System, Live CC/CMake Builder & Multi-Package Resolver

#include "version.hpp"
#include "build/toolchain_layout.hpp"
#include "build/manifest.hpp"
#include "build/pch.hpp"
#include "build/binary_cache.hpp"
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_BUILDSYS_IMPL)
#include "sema.hpp"
#include "codegen.hpp"
#include "build/deps.hpp"
#include "build/package_loader.hpp"
#include "build/wasm_shell.hpp"
#include "build/apple_bundle.hpp"
#include "build/windows_resources.hpp"
#include "build/syntax_check.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#endif
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_BUILDSYS_INLINE
#else
#define CFIRE_BUILDSYS_INLINE inline
#endif

namespace cfire::build {

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto init_build_plan_flags(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool test_mode,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::string& web_profile,
    const std::string& subsystem,
    bool sanitize = false
) -> std::expected<BuildPlan, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto discover_and_apply_build_cf(
    BuildPlan& plan,
    const std::filesystem::path& cf_file,
    const std::filesystem::path& dir,
    bool test_mode,
    bool run_pre_build = true
) -> std::expected<void, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto build_external_deps_for_plan(
    const std::filesystem::path& default_dir,
    BuildPlan& plan
) -> std::expected<void, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto populate_checked_build_plan(
    frontend::CheckedFile& checked,
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast
) -> std::expected<void, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto analyze_and_emit(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast,
    bool test_mode = false,
    const std::string& target_triple = "",
    const std::string& sysroot = "",
    const std::string& web_profile = "auto",
    const std::string& subsystem = "auto",
    bool sanitize = false
) -> std::expected<std::pair<std::string, BuildPlan>, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto build_native(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_bin,
    bool test_mode = false,
    bool sanitize = false,
    const std::string& target_triple = "",
    const std::string& sysroot = "",
    const std::vector<std::string>& extra_link_libs = {},
    const std::vector<std::string>& extra_includes = {},
    const std::string& web_profile = "auto",
    const std::string& subsystem = "auto",
    const std::string& app_name = "",
    const std::string& bundle_id = "",
    const std::string& icon_path = ""
) -> std::expected<BuildPlan, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto generate_cmake_content(const BuildPlan& plan) -> std::string;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto eject_cmake(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_dir
) -> std::expected<void, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_BUILDSYS_IMPL)

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto init_build_plan_flags(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool test_mode,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::string& web_profile,
    const std::string& subsystem,
    bool sanitize
) -> std::expected<BuildPlan, std::string> {
    BuildPlan plan;
    plan.entry_file = cf_file.string();
    plan.is_ios = is_ios_target(target_triple);
    plan.is_ios_sim = is_ios_simulator_target(target_triple);
    plan.target_triple = plan.is_ios ? normalize_ios_triple(target_triple) : target_triple;
    plan.sysroot = sysroot;
    if (plan.is_ios && plan.sysroot.empty()) {
        auto sdk_res = resolve_ios_sdk_path(plan.is_ios_sim);
        if (!sdk_res) return std::unexpected(sdk_res.error());
        plan.sysroot = *sdk_res;
    }
    plan.web_profile = web_profile;
    plan.subsystem = subsystem;
    auto install_root = derive_install_root(runtime_dir);
    plan.cflags = {"-std=c++2b", "-Wno-c++11-narrowing", "-I" + runtime_dir.string()};
    if (std::filesystem::is_directory(runtime_dir / "vendor")) {
        plan.cflags.push_back("-I" + (runtime_dir / "vendor").string());
    }
    if (!install_root.empty() && install_root != "/usr") {
        std::error_code root_ec;
        if (std::filesystem::is_directory(install_root / "include", root_ec)) {
            plan.cflags.push_back("-I" + (install_root / "include").string());
        }
        if (std::filesystem::is_directory(install_root / "lib", root_ec)) {
            plan.ldflags.push_back("-L" + (install_root / "lib").string());
        }
    }
    bool is_wasm = plan.target_triple.starts_with("wasm");
#if defined(__linux__)
    if (!is_wasm) {
        plan.cflags.push_back("-Wno-builtin-macro-redefined");
        plan.cflags.push_back("-D__cpp_concepts=202002L");
        plan.cflags.push_back("-pthread");
        plan.ldflags.push_back("-pthread");
    }
#endif
    if (!plan.target_triple.empty()) {
        if (!is_wasm) {
            plan.cflags.push_back("--target=" + plan.target_triple);
            plan.ldflags.push_back("--target=" + plan.target_triple);
        }
    }
    if (!plan.sysroot.empty()) {
        plan.cflags.push_back("-isysroot");
        plan.cflags.push_back("\"" + plan.sysroot + "\"");
        plan.ldflags.push_back("-isysroot");
        plan.ldflags.push_back("\"" + plan.sysroot + "\"");
    }
    if (is_wasm) {
        plan.cflags.push_back("-DPLATFORM_WEB");
    }
    if (plan.is_ios) {
        plan.cflags.push_back("-DPLATFORM_IOS=1");
    }
    bool is_dev_debug = is_dev_debug_profile();
    if (test_mode) {
        plan.cflags.push_back("-DCFIRE_TEST_MODE=1");
        if (!is_wasm && !g_no_trap) {
            plan.cflags.push_back("-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero");
            plan.cflags.push_back("-fsanitize-trap=all");
        } else {
            plan.cflags.push_back("-fwrapv");
        }
    } else if (is_dev_debug && !is_wasm && !g_no_trap && !sanitize) {
        plan.cflags.push_back("-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero");
        plan.cflags.push_back("-fsanitize-trap=all");
    } else {
        plan.cflags.push_back("-fwrapv");
    }
    bool is_win_target = is_windows_target(plan.target_triple);
    if (is_win_target) {
        plan.ldflags.push_back("-lws2_32");
        plan.ldflags.push_back("-ladvapi32");
        plan.ldflags.push_back("-lshell32");
        plan.ldflags.push_back("-static");
    }
    return plan;
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto discover_and_apply_build_cf(
    BuildPlan& plan,
    const std::filesystem::path& cf_file,
    const std::filesystem::path& dir,
    bool test_mode,
    bool run_pre_build
) -> std::expected<void, std::string> {
    if (cf_file.filename() == "build.cf") return {};
    bool is_wasm = plan.target_triple.starts_with("wasm");

    std::filesystem::path found_bcf;
    std::error_code bcf_ec;
    auto effective_dir = dir.empty() ? std::filesystem::path(".") : dir;
    auto search_d = std::filesystem::weakly_canonical(effective_dir, bcf_ec);
    if (bcf_ec || search_d.empty()) search_d = std::filesystem::absolute(effective_dir, bcf_ec);
    for (int depth = 0; depth < 8 && !search_d.empty(); ++depth) {
        auto cand_bcf = search_d / "build.cf";
        if (std::filesystem::exists(cand_bcf, bcf_ec)) {
            found_bcf = cand_bcf;
            break;
        }
        if (search_d == search_d.parent_path()) break;
        search_d = search_d.parent_path();
    }
    if (found_bcf.empty()) return {};

    auto parsed = parse_build_cf(found_bcf);
    if (!parsed) return std::unexpected(parsed.error());
    auto bcf_parent = found_bcf.parent_path();
    if (!parsed->executables.empty()) {
        const ExecutableSpec* matched_exe = &parsed->executables[0];
        bool exact_entry_match = false;
        auto cf_canon = std::filesystem::weakly_canonical(cf_file, bcf_ec);
        for (const auto& ex : parsed->executables) {
            if (!ex.entry.empty()) {
                auto ex_canon = std::filesystem::weakly_canonical(bcf_parent / ex.entry, bcf_ec);
                if (!bcf_ec && ex_canon == cf_canon) {
                    matched_exe = &ex;
                    exact_entry_match = true;
                    break;
                }
            }
        }
        if (plan.app_name.empty()) plan.app_name = matched_exe->app_name;
        if (plan.bundle_id.empty()) plan.bundle_id = matched_exe->bundle_id;
        if (plan.url_schemes.empty()) plan.url_schemes = matched_exe->url_schemes;
        if (plan.sign_identity.empty()) plan.sign_identity = matched_exe->sign_identity;
        if (plan.icon_path.empty() && !matched_exe->icon.empty()) {
            auto icon_cand = bcf_parent / matched_exe->icon;
            if (std::filesystem::exists(icon_cand, bcf_ec)) {
                plan.icon_path = icon_cand.string();
            } else {
                plan.icon_path = matched_exe->icon;
            }
        }
        std::vector<std::string> fws_to_link;
        std::vector<std::string> libs_to_link;
        auto add_unique = [](std::vector<std::string>& vec, const std::string& item) {
            if (item.empty()) return;
            for (const auto& existing : vec) {
                if (existing == item) return;
            }
            vec.push_back(item);
        };
        if (test_mode || !exact_entry_match) {
            for (const auto& ex : parsed->executables) {
                for (const auto& fw : ex.frameworks) add_unique(fws_to_link, fw);
                for (const auto& lib : ex.libraries) add_unique(libs_to_link, lib);
            }
        } else {
            for (const auto& fw : matched_exe->frameworks) add_unique(fws_to_link, fw);
            for (const auto& lib : matched_exe->libraries) add_unique(libs_to_link, lib);
        }
#if defined(__APPLE__)
        if (!is_wasm) {
            for (const auto& fw : fws_to_link) {
                plan.ldflags.push_back("-framework");
                plan.ldflags.push_back(fw);
            }
        }
#endif
        if (!is_wasm) {
            for (const auto& lib : libs_to_link) {
                plan.ldflags.push_back(lib.starts_with("-l") ? lib : ("-l" + lib));
            }
        }
    }
    for (auto& dep : parsed->external_deps) {
        bool exists_already = false;
        for (const auto& ed : plan.external_deps) {
            if (ed.name == dep.name) { exists_already = true; break; }
        }
        if (!exists_already) {
            plan.external_deps.push_back(std::move(dep));
        }
    }
    for (auto& cmd : parsed->pre_build_commands) {
        if (cmd.work_dir.empty()) cmd.work_dir = bcf_parent.string();
        if (run_pre_build) {
            if (auto r = execute_pre_build_command(cmd, bcf_parent); !r) {
                return std::unexpected(r.error());
            }
        }
        plan.pre_build_commands.push_back(std::move(cmd));
    }
    return {};
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto build_external_deps_for_plan(
    const std::filesystem::path& default_dir,
    BuildPlan& plan
) -> std::expected<void, std::string> {
    for (auto& dep : plan.external_deps) {
        auto dep_base = dep.base_dir.empty() ? default_dir : dep.base_dir;
        if (auto r = build_local_dep(dep_base, dep, plan); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto populate_checked_build_plan(
    frontend::CheckedFile& checked,
    BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast
) -> std::expected<void, std::string> {
    auto install_root = derive_install_root(runtime_dir);
    bool is_wasm = plan.target_triple.starts_with("wasm");
    bool is_win_target = is_windows_target(plan.target_triple);
    std::filesystem::path entry_p = plan.entry_file.empty()
        ? std::filesystem::path(checked.ast.path)
        : std::filesystem::path(plan.entry_file);
    auto dir = entry_p.parent_path();
    if (dir.empty()) dir = ".";
    auto cache_dir = dir / ".cfire" / "cache";

    static const std::unordered_map<std::string, std::string> core_to_pc = {
        {"core:compress/zstd", "libzstd"},
        {"ext:compress/zstd", "libzstd"},
        {"core:json", "simdjson"},
        {"ext:json", "simdjson"},
        {"core:ai", "simdjson"},
        {"ext:ai", "simdjson"},
        {"core:tako", "simdjson"},
        {"ext:tako", "simdjson"},
        {"core:webui", "simdjson"},
        {"ext:webui", "simdjson"},
    };

    auto has_sqlcipher = [&]() -> bool {
        if (is_wasm || plan.is_ios) return false;
        std::error_code ec;
        if (const char* home = std::getenv("HOME")) {
            auto loc = std::filesystem::path(home) / ".local";
            if ((std::filesystem::exists(loc / "include" / "sqlcipher" / "sqlite3.h", ec) ||
                 std::filesystem::exists(loc / "include" / "sqlite3.h", ec)) &&
                (std::filesystem::exists(loc / "lib" / "libsqlcipher.a", ec) ||
                 std::filesystem::exists(loc / "lib" / "libsqlcipher.so", ec) ||
                 std::filesystem::exists(loc / "lib" / "libsqlcipher.dylib", ec))) {
                return true;
            }
        }
        if (ProbeCache::instance().probe_pkg_config("sqlcipher").found) return true;
        for (const char* inc_p : {
            "/opt/homebrew/opt/sqlcipher/include/sqlcipher/sqlite3.h",
            "/usr/local/opt/sqlcipher/include/sqlcipher/sqlite3.h",
            "/opt/homebrew/include/sqlcipher/sqlite3.h",
            "/usr/local/include/sqlcipher/sqlite3.h",
            "/usr/include/sqlcipher/sqlite3.h"
        }) {
            if (std::filesystem::exists(inc_p, ec)) return true;
        }
        return false;
    };

    std::unordered_set<std::string> seen_pc(plan.pkg_config_deps.begin(), plan.pkg_config_deps.end());
    auto add_pc = [&](const std::string& pc) {
        if (is_wasm) return;
        if (plan.is_ios) {
            if (pc == "sqlite3") {
                if (!seen_pc.contains(pc)) {
                    seen_pc.insert(pc);
                    plan.ldflags.push_back("-lsqlite3");
                }
            } else if (pc == "libzstd") {
                if (!seen_pc.contains(pc)) {
                    seen_pc.insert(pc);
                    plan.ldflags.push_back("-lcompression");
                }
            }
            return;
        }
        if (seen_pc.contains(pc)) return;
        seen_pc.insert(pc);
        plan.pkg_config_deps.push_back(pc);
        query_pkg_config(pc, plan.cflags, plan.ldflags, install_root, is_win_target);
    };

    for (const auto& [sname, tags] : checked.struct_tags) {
        bool has_j = false;
        for (const auto& t : tags) {
            if (!t.json_key.empty()) { has_j = true; break; }
        }
        if (has_j) {
            add_pc("simdjson");
            break;
        }
    }
    for (const auto& imp : checked.ast.imports) {
        if (!imp.is_cpp) {
            plan.auto_linked.push_back(imp.path);
            if (auto it = core_to_pc.find(imp.path); it != core_to_pc.end()) {
                add_pc(it->second);
            }
            if (imp.path == "core:db/sqlite" || imp.path == "ext:db/sqlite" ||
                imp.path == "core:sqlite" || imp.path == "ext:sqlite") {
                if (has_sqlcipher()) {
                    add_pc("sqlcipher");
                    plan.cflags.push_back("-DSQLITE_HAS_CODEC=1");
                } else {
                    add_pc("sqlite3");
                }
            }
            if (imp.path == "core:tako" || imp.path == "ext:tako") {
                if (const char* env_tako = std::getenv("TAKO_SDK_DIR")) {
                    std::error_code tec;
                    if (std::filesystem::is_directory(env_tako, tec)) {
                        plan.cflags.insert(plan.cflags.begin() + 2, "-I" + std::string(env_tako));
                    }
                }
            }
            if (imp.path == "core:ai" || imp.path == "ext:ai") {
                if (!is_wasm && plan.is_ios) {
                    plan.ldflags.insert(plan.ldflags.end(), {
                        "-framework", "Foundation",
                        "-framework", "NaturalLanguage",
                        "-framework", "Vision",
                        "-framework", "PDFKit",
                        "-framework", "CFNetwork",
                        "-lobjc"
                    });
                }
            }
            if (imp.path == "core:audio" || imp.path == "ext:audio") {
                if (!is_wasm) {
                    if (plan.is_ios) {
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "AudioToolbox",
                            "-framework", "AVFAudio",
                            "-framework", "Foundation",
                            "-lobjc"
                        });
                    } else {
#if defined(__APPLE__)
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "AudioToolbox",
                            "-framework", "CoreFoundation"
                        });
#elif defined(__linux__)
                        plan.ldflags.insert(plan.ldflags.end(), {"-ldl", "-lpthread"});
#endif
                    }
                }
            }
            if (imp.path == "core:webui" || imp.path == "ext:webui") {
                plan.is_gui = true;
                if (!is_wasm) {
                    if (plan.is_ios) {
                        plan.uses_objc = true;
                        plan.cflags.push_back("-fobjc-arc");
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "UIKit",
                            "-framework", "WebKit",
                            "-framework", "Foundation",
                            "-framework", "CoreGraphics"
                        });
                    } else if (is_win_target) {
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-luser32", "-lgdi32", "-lshell32", "-lole32", "-ldwmapi", "-ladvapi32", "-lcomdlg32", "-lshlwapi"
                        });
                    } else {
#if defined(__APPLE__)
                        plan.uses_objc = true;
                        plan.cflags.push_back("-fobjc-arc");
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "Cocoa",
                            "-framework", "WebKit"
                        });
#elif defined(__linux__)
                        if (ProbeCache::instance().probe_pkg_config("webkit2gtk-4.1").found) {
                            add_pc("webkit2gtk-4.1");
                            add_pc("gtk+-3.0");
                        } else if (ProbeCache::instance().probe_pkg_config("webkit2gtk-4.0").found) {
                            add_pc("webkit2gtk-4.0");
                            add_pc("gtk+-3.0");
                        }
#endif
                    }
                }
            }
            if (imp.path == "ext:crypto/sodium" || imp.path == "ext:sodium") {
                if (!is_wasm && !plan.is_ios) {
                    bool found_sodium = ProbeCache::instance().probe_pkg_config("libsodium").found;
                    if (!found_sodium && std::getenv("HOME")) {
                        auto loc_inc = std::filesystem::path(std::getenv("HOME")) / ".local" / "include" / "sodium.h";
                        if (std::filesystem::exists(loc_inc)) found_sodium = true;
                    }
                    if (!found_sodium && std::getenv("MINGW_PREFIX")) {
                        auto mp_inc = std::filesystem::path(std::getenv("MINGW_PREFIX")) / "include" / "sodium.h";
                        if (std::filesystem::exists(mp_inc)) found_sodium = true;
                    }
                    if (!found_sodium) {
                        for (const auto& inc_dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include", "/clang64/include", "C:/msys64/clang64/include"}) {
                            if (std::filesystem::exists(std::filesystem::path(inc_dir) / "sodium.h")) {
                                found_sodium = true;
                                break;
                            }
                        }
                    }
                    if (found_sodium) {
                        add_pc("libsodium");
                        plan.cflags.push_back("-DCFIRE_HAS_LIBSODIUM=1");
                        if (is_win_target) {
                            plan.cflags.push_back("-DSODIUM_STATIC=1");
                        }
                    } else if (inspect_clang_ast) {
                        return std::unexpected(
                            "ext:crypto/sodium requires libsodium (install via 'brew install libsodium' or 'apt install libsodium-dev')");
                    }
                }
            }
            if (imp.path == "core:keychain" || imp.path == "ext:keychain") {
                if (!is_wasm) {
                    if (is_win_target) {
                        plan.ldflags.push_back("-ladvapi32");
                    } else {
#if defined(__APPLE__)
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "Security",
                            "-framework", "CoreFoundation"
                        });
#endif
                    }
                }
            }
            if (imp.path == "core:net/ssh" || imp.path == "ext:net/ssh" || imp.path == "core:ssh") {
                if (!is_wasm && !plan.is_ios) {
                    bool found_libssh = ProbeCache::instance().probe_pkg_config("libssh").found;
                    if (!found_libssh && std::getenv("HOME")) {
                        auto loc_inc = std::filesystem::path(std::getenv("HOME")) / ".local" / "include" / "libssh" / "libssh.h";
                        if (std::filesystem::exists(loc_inc)) found_libssh = true;
                    }
                    if (!found_libssh) {
                        for (const auto& inc_dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include"}) {
                            if (std::filesystem::exists(std::filesystem::path(inc_dir) / "libssh" / "libssh.h")) {
                                found_libssh = true;
                                break;
                            }
                        }
                    }
                    if (found_libssh) {
                        add_pc("libssh");
                        plan.cflags.push_back("-DCFIRE_HAS_LIBSSH=1");
                        if (is_win_target) {
                            plan.cflags.push_back("-DLIBSSH_STATIC=1");
                        }
                    }
                }
            }
            continue;
        }
        for (const auto& attr : imp.attrs) {
            if (attr.name == "pkg" && !attr.args.empty()) {
                add_pc(attr.args[0]);
            }
        }
        if (inspect_clang_ast && !plan.cpp_headers.contains(imp.path)) {
            auto idx = bridge::inspect_header(imp.path, plan.cflags, cache_dir, resolve_cxx_compiler(plan.target_triple));
            if (!idx) return std::unexpected(idx.error());
            for (const auto& [sym_name, sym] : idx->symbols) {
                if (sym.kind == "FunctionDecl") {
                    checked.cpp_fn_params[sym_name] = sym.param_types;
                }
            }
            plan.cpp_headers[imp.path] = std::move(*idx);
        }
    }

    if (inspect_clang_ast && !is_wasm && seen_pc.contains("simdjson")) {
        bool has_local_simdjson = false;
        if (const char* home = std::getenv("HOME")) {
            auto loc = std::filesystem::path(home) / ".local";
            std::error_code ec;
            if (std::filesystem::exists(loc / "include" / "simdjson.h", ec)) {
                has_local_simdjson = true;
            }
        }
        if (!has_local_simdjson) {
            auto sj_probe = ProbeCache::instance().probe_pkg_config("simdjson");
            auto sj_ver = trim_whitespace(sj_probe.modversion);
            if (sj_probe.found && !sj_ver.empty() && (sj_ver.starts_with("1.") || sj_ver.starts_with("2.") || sj_ver.starts_with("3."))) {
                auto cc_probe = ProbeCache::instance().probe_compiler(resolve_cxx_compiler(plan.target_triple));
                int cl_maj = cc_probe.major_version;
                if (cl_maj >= 19) {
                    return std::unexpected(std::format(
                        "system simdjson {} is incompatible with Clang {} (requires simdjson >= 4.0; install simdjson >= 4.0 to ~/.local or upgrade libsimdjson-dev)",
                        sj_ver, cl_maj));
                }
            }
        }
    }
    return {};
}

[[nodiscard]] inline auto write_file_if_changed(
    const std::filesystem::path& path,
    std::string_view content
) -> std::expected<bool, std::string> {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && !ec) {
        auto sz = std::filesystem::file_size(path, ec);
        if (!ec && sz == content.size()) {
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::string existing(static_cast<std::size_t>(sz), '\0');
                if (sz == 0 || in.read(existing.data(), static_cast<std::streamsize>(sz))) {
                    if (existing == content) {
                        return false;
                    }
                }
            }
        }
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected("failed to create directory for " + path.string() + ": " + ec.message());
        }
    }
    static std::atomic<uint64_t> seq{0};
    uint64_t id = seq.fetch_add(1, std::memory_order_relaxed);
    auto tmp = std::filesystem::path(path.string() + std::format(".tmp.{}", id));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected("failed to open temp file for writing: " + tmp.string());
        }
        if (!content.empty()) {
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        out.flush();
        if (!out.good()) {
            std::filesystem::remove(tmp, ec);
            return std::unexpected("failed to write temp file: " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                return std::unexpected("failed to open generated file for writing: " + path.string());
            }
            if (!content.empty()) {
                out.write(content.data(), static_cast<std::streamsize>(content.size()));
            }
            out.flush();
            if (!out.good()) {
                return std::unexpected("failed to write generated file: " + path.string());
            }
        }
    }
    return true;
}

[[nodiscard]] inline auto analyze_and_emit_impl(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast,
    bool test_mode,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::string& web_profile,
    const std::string& subsystem,
    bool sanitize,
    frontend::ShardedEmission* sharded_out = nullptr
) -> std::expected<std::pair<std::string, BuildPlan>, std::string> {
    auto dir = cf_file.parent_path();
    if (dir.empty()) dir = ".";
    auto plan_res = init_build_plan_flags(cf_file, runtime_dir, test_mode, target_triple, sysroot, web_profile, subsystem, sanitize);
    if (!plan_res) return std::unexpected(plan_res.error());
    BuildPlan plan = std::move(*plan_res);

    if (auto bcf_res = discover_and_apply_build_cf(plan, cf_file, dir, test_mode); !bcf_res) {
        return std::unexpected(bcf_res.error());
    }

    // 2. Read and parse root .cf source
    auto raw = read_file_text(cf_file);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(cf_file.string(), *raw);
    if (!ast) return std::unexpected(ast.error());
    if (test_mode) {
        frontend::append_doctests(cf_file.string(), *raw, *ast);
    }

    // 3. Resolve local `.cf` package imports (`import "./..."`, `import "pkg:..."`, and same-directory package siblings) recursively
    if (auto r = resolve_local_cf_imports(dir, *ast, plan, runtime_dir, test_mode); !r) {
        return std::unexpected(r.error());
    }

    // 4. Build all external dependencies (anchored to each dependency's declaring build.cf directory)
    if (auto r = build_external_deps_for_plan(dir, plan); !r) {
        return std::unexpected(r.error());
    }

    // Inspect C++ headers before Sema so Sema knows declared C/C++ symbols
    auto cache_dir = dir / ".cfire" / "cache";
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    if (inspect_clang_ast) {
        for (const auto& imp : ast->imports) {
            if (imp.is_cpp) {
                auto idx = bridge::inspect_header(imp.path, plan.cflags, cache_dir, resolve_cxx_compiler(plan.target_triple));
                if (idx) {
                    for (const auto& [sym_name, sym] : idx->symbols) {
                        if (sym.kind == "FunctionDecl") {
                            pre_cpp_fn_params[sym_name] = sym.param_types;
                        }
                    }
                    plan.cpp_headers[imp.path] = std::move(*idx);
                }
            }
        }
    }

    // 3. Semantic Analysis
    auto checked = frontend::analyze(std::move(*ast), std::move(pre_cpp_fn_params));
    if (!checked) return std::unexpected(checked.error());
    plan.warnings = checked->warnings;

    if (auto r = populate_checked_build_plan(*checked, plan, runtime_dir, inspect_clang_ast); !r) {
        return std::unexpected(r.error());
    }

    // 4. C++23 Code Generation (with Clang AST boundary coercions)
    std::string cpp_code = frontend::emit_cpp(*checked, test_mode);
    if (sharded_out != nullptr && !g_no_shard) {
        *sharded_out = frontend::emit_sharded_cpp(*checked, test_mode);
    }

    return std::make_pair(std::move(cpp_code), std::move(plan));
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto analyze_and_emit(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast,
    bool test_mode,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::string& web_profile,
    const std::string& subsystem,
    bool sanitize
) -> std::expected<std::pair<std::string, BuildPlan>, std::string> {
    return analyze_and_emit_impl(
        cf_file, runtime_dir, inspect_clang_ast, test_mode,
        target_triple, sysroot, web_profile, subsystem, sanitize, nullptr
    );
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto build_native(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_bin,
    bool test_mode,
    bool sanitize,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::vector<std::string>& extra_link_libs,
    const std::vector<std::string>& extra_includes,
    const std::string& web_profile,
    const std::string& subsystem,
    const std::string& app_name,
    const std::string& bundle_id,
    const std::string& icon_path
) -> std::expected<BuildPlan, std::string> {
    frontend::ShardedEmission sharded;
    auto res = analyze_and_emit_impl(
        cf_file, runtime_dir, true, test_mode, target_triple, sysroot, web_profile, subsystem, sanitize, &sharded
    );
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);
    for (const auto& w : plan.warnings) {
        std::println(stderr, "{}", w);
    }
    plan.sanitized = sanitize;
    plan.extra_link_libs = extra_link_libs;
    plan.extra_includes = extra_includes;
    plan.web_profile = web_profile;
    plan.subsystem = subsystem;
    if (!app_name.empty()) plan.app_name = app_name;
    if (!bundle_id.empty()) plan.bundle_id = bundle_id;
    if (!icon_path.empty()) plan.icon_path = icon_path;
    auto resolve_icon_candidate = [&](const std::string& p_str) -> std::filesystem::path {
        std::filesystem::path icon_resolved = p_str;
        std::error_code ec;
        if (icon_resolved.is_relative() && !std::filesystem::exists(icon_resolved, ec)) {
            if (std::filesystem::exists(cf_file.parent_path() / icon_resolved, ec)) {
                icon_resolved = std::filesystem::absolute(cf_file.parent_path() / icon_resolved, ec);
            } else if (std::filesystem::exists(cf_file.parent_path().parent_path() / icon_resolved, ec)) {
                icon_resolved = std::filesystem::absolute(cf_file.parent_path().parent_path() / icon_resolved, ec);
            }
        }
        return icon_resolved;
    };
    if (!plan.icon_path.empty()) {
        std::filesystem::path icon_resolved = resolve_icon_candidate(plan.icon_path);
        plan.icon_path = icon_resolved.string();
        plan.cflags.push_back(std::format("-DCFIRE_APP_ICON_PATH=\\\"{}\\\"", icon_resolved.string()));
    }

    if (g_profile_mode) {
        plan.cflags.push_back("-g");
        plan.cflags.push_back("-fno-omit-frame-pointer");
    }

    bool is_wasm = target_triple.starts_with("wasm");
    if (is_wasm) {
        auto empp_res = resolve_emscripten_compiler();
        if (!empp_res) {
            return std::unexpected(empp_res.error());
        }
        plan.emscripten_compiler = *empp_res;
        std::string effective_profile = (plan.web_profile == "auto") ? (plan.is_gui ? "canvas" : "cli") : plan.web_profile;
        plan.web_profile = effective_profile;

        auto cf_parent = cf_file.parent_path().empty() ? std::filesystem::path(".") : cf_file.parent_path();
        auto gen_dir = cf_parent / ".cfire" / "gen";
        std::filesystem::create_directories(gen_dir);
        auto shell_path = gen_dir / (effective_profile == "canvas" ? "cfire_canvas_shell.html" : "cfire_console_shell.html");
        std::string shell_content = std::string(effective_profile == "canvas" ? kCanvasShellHtml : kConsoleShellHtml);
        if (!plan.app_name.empty()) {
            auto title_pos = shell_content.find("<title>cfire Web Application</title>");
            if (title_pos != std::string::npos) {
                shell_content.replace(title_pos, 36, std::format("<title>{}</title>", plan.app_name));
            }
        }
        if (!plan.icon_path.empty()) {
            std::filesystem::path icon_src = plan.icon_path;
            if (icon_src.is_relative() && !std::filesystem::exists(icon_src)) {
                if (std::filesystem::exists(cf_file.parent_path() / icon_src)) {
                    icon_src = cf_file.parent_path() / icon_src;
                }
            }
            std::error_code ec;
            if (std::filesystem::exists(icon_src, ec)) {
                auto out_dir = out_bin.parent_path();
                auto favicon_dest = out_dir / "favicon.png";
                std::filesystem::copy_file(icon_src, favicon_dest, std::filesystem::copy_options::overwrite_existing, ec);
                auto head_pos = shell_content.find("</head>");
                if (head_pos != std::string::npos) {
                    shell_content.insert(head_pos, "    <link rel=\"icon\" type=\"image/png\" href=\"favicon.png\">\n  ");
                }
            }
        }
        {
            std::ofstream sf(shell_path);
            sf << shell_content;
        }

        plan.cflags.push_back("-sWASM=1");
        plan.cflags.push_back("-msimd128");
        plan.cflags.push_back("-sALLOW_MEMORY_GROWTH=1");
        plan.ldflags.push_back(std::format("--shell-file \"{}\"", shell_path.string()));
        if (effective_profile == "canvas") {
            plan.cflags.push_back("-sUSE_GLFW=3");
            plan.cflags.push_back("-sASYNCIFY=1");
        }
    } else if (plan.is_ios) {
        if (!test_mode) {
            std::string bin_stem = out_bin.extension() == ".app" ? out_bin.stem().string() : out_bin.filename().string();
            auto app_bundle = out_bin.extension() == ".app" ? out_bin : (out_bin.parent_path() / (bin_stem + ".app"));
            plan.ios_app_bundle = app_bundle.string();
        }
    } else {
        bool gui_subsystem = (plan.subsystem == "gui") || (plan.subsystem == "auto" && plan.is_gui);
        bool is_windows = is_windows_target(target_triple);
        if (is_windows && !test_mode && !plan.icon_path.empty()) {
            auto res_obj = compile_windows_icon(plan.icon_path, out_bin, target_triple);
            if (!res_obj.empty()) {
                plan.ldflags.push_back("\"" + res_obj + "\"");
            }
        }
        if (gui_subsystem) {
            if (is_windows) {
                plan.ldflags.push_back("-mwindows");
            } else {
#if defined(__APPLE__)
                if ((target_triple.empty() || target_triple.find("apple") != std::string::npos) && !test_mode) {
                    std::string bin_stem = out_bin.extension() == ".app" ? out_bin.stem().string() : out_bin.filename().string();
                    auto app_bundle = out_bin.extension() == ".app" ? out_bin : (out_bin.parent_path() / (bin_stem + ".app"));
                    plan.macos_app_bundle = app_bundle.string();
                }
#endif
            }
        }
    }

    for (const auto& lib : extra_link_libs) {
        if (lib.find("libtako") != std::string::npos) {
            plan.cflags.push_back("-DCFIRE_HAS_LIBTAKO=1");
        }
        plan.ldflags.push_back(lib);
        plan.ldflags.push_back("-lpthread");
        plan.ldflags.push_back("-ldl");
        plan.ldflags.push_back("-lm");
#if defined(__APPLE__)
        if (target_triple.empty()) {
            plan.ldflags.insert(plan.ldflags.end(), {
                "-framework", "CoreFoundation",
                "-framework", "Security",
                "-framework", "SystemConfiguration",
                "-lresolv"
            });
        }
#endif
    }
    for (const auto& inc : extra_includes) {
        plan.cflags.push_back("-I \"" + inc + "\"");
    }

    auto cf_parent = cf_file.parent_path().empty() ? std::filesystem::path(".") : cf_file.parent_path();
    auto gen_dir = cf_parent / ".cfire" / "gen";
    std::filesystem::create_directories(gen_dir);
    if (out_bin.has_parent_path()) {
        std::error_code out_ec;
        std::filesystem::create_directories(out_bin.parent_path(), out_ec);
    }
    auto stem = test_mode ? (cf_file.stem().string() + "_test") : cf_file.stem().string();
    auto cpp_path = gen_dir / (stem + ((plan.uses_objc || (plan.is_ios && plan.is_gui)) ? ".mm" : ".cpp"));
    plan.generated_cpp = cpp_path.string();

    if (auto wr = write_file_if_changed(cpp_path, cpp_code); !wr) {
        return std::unexpected(wr.error());
    }

    for (auto& l : plan.ldflags) {
        if (l == "-lsimdjson") {
            std::error_code simd_ec;
            for (const auto& d : plan.ldflags) {
                if (d.starts_with("-L")) {
                    auto p = std::filesystem::path(d.substr(2)) / "libsimdjson_static.a";
                    if (std::filesystem::exists(p, simd_ec)) {
                        l = p.string();
                        break;
                    }
                }
            }
        }
    }

    const bool dev_debug = is_dev_debug_profile();
    std::string opt_flags = sanitize
        ? sanitizer_opt_flags()
        : (dev_debug ? "-O0 -g" : "-O2");

    std::string compiler = is_wasm ? plan.emscripten_compiler : resolve_cxx_compiler(plan.target_triple);
    std::string linker_flag;
    if (!is_wasm && !plan.is_ios && plan.target_triple.empty()) {
        linker_flag = resolve_effective_linker_flag(compiler);
    }

    const std::string compiler_fp = compiler_binary_fingerprint(compiler);
    const std::string rt_fp = runtime_headers_fingerprint(runtime_dir);
    std::string local_cpp_hdr_fp;
    if (!plan.cpp_headers.empty()) {
        std::vector<std::string> hdr_entries;
        std::error_code hec;
        for (const auto& [hdr_key, _] : plan.cpp_headers) {
            std::filesystem::path hp = hdr_key;
            if (!std::filesystem::is_regular_file(hp, hec) || hec) {
                hec.clear();
                hp = cf_parent / hdr_key;
            }
            if (std::filesystem::is_regular_file(hp, hec) && !hec) {
                hdr_entries.push_back(hp.lexically_normal().string() + "=" + file_fingerprint_string(hp));
            }
        }
        std::sort(hdr_entries.begin(), hdr_entries.end());
        for (std::size_t i = 0; i < hdr_entries.size(); ++i) {
            if (i > 0) local_cpp_hdr_fp.push_back(';');
            local_cpp_hdr_fp += hdr_entries[i];
        }
    }

    const bool will_shard = !g_no_shard &&
                            !is_wasm &&
                            !plan.uses_objc &&
                            !(plan.is_ios && plan.is_gui) &&
                            !sharded.has_cpp_impl_define &&
                            sharded.units.size() >= 2;

    const bool use_persistent_obj = (!will_shard && dev_debug && !is_wasm);
    std::filesystem::path obj_path;
    auto obj_dir = cf_parent / ".cfire" / "obj";
    std::filesystem::path shard_dir;
    std::vector<std::filesystem::path> unit_cpp_paths;
    std::vector<std::filesystem::path> unit_obj_paths;

    if (will_shard) {
        shard_dir = gen_dir / "shards" / stem;
        std::error_code dir_ec;
        std::filesystem::create_directories(shard_dir, dir_ec);
        std::filesystem::create_directories(obj_dir, dir_ec);
        auto decls_path = shard_dir / "decls.hpp";
        if (auto wr = write_file_if_changed(decls_path, sharded.decls_hpp); !wr) {
            return std::unexpected(wr.error());
        }
        const std::string decls_hash = sha256_hex(sharded.decls_hpp);
        unit_cpp_paths.reserve(sharded.units.size());
        unit_obj_paths.reserve(sharded.units.size());
        for (const auto& unit : sharded.units) {
            auto u_cpp = shard_dir / unit.filename;
            if (auto wr = write_file_if_changed(u_cpp, unit.cpp); !wr) {
                return std::unexpected(wr.error());
            }
            detail::Sha256State st;
            auto feed = [&](std::string_view tag, std::string_view val) {
                st.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
                const char sep1 = ':';
                st.update(reinterpret_cast<const uint8_t*>(&sep1), 1);
                st.update(reinterpret_cast<const uint8_t*>(val.data()), val.size());
                const char sep2 = '\0';
                st.update(reinterpret_cast<const uint8_t*>(&sep2), 1);
            };
            feed("schema", "cf202-v1");
            feed("compiler", compiler_fp);
            feed("opt", opt_flags);
            feed("target", plan.target_triple);
            for (const auto& cf_flag : plan.cflags) {
                feed("cf", cf_flag);
            }
            feed("rt", rt_fp);
            feed("cpp_hdr", local_cpp_hdr_fp);
            feed("decls", decls_hash);
            feed("unit_path", u_cpp.string());
            feed("unit_cpp", unit.cpp);
            std::string h16 = st.final_hex().substr(0, 16);
            auto u_obj = obj_dir / std::format("{}__pkg_{}_{}.o", stem, unit.id, h16);
            unit_cpp_paths.push_back(std::move(u_cpp));
            unit_obj_paths.push_back(std::move(u_obj));
        }
        plan.used_sharded = true;
        plan.sharded_total_units = sharded.units.size();
        for (const auto& u_obj : unit_obj_paths) {
            plan.sharded_object_files.push_back(u_obj.string());
        }
    } else if (use_persistent_obj) {
        std::error_code obj_ec;
        std::filesystem::create_directories(obj_dir, obj_ec);
        std::string h12 = sha256_hex(
            std::format("{}|{}|{}", cpp_path.filename().string(), opt_flags, plan.target_triple)
        ).substr(0, 12);
        obj_path = obj_dir / std::format("{}_{}.o", stem, h12);
    }

    auto compute_active_obj_fp = [&]() -> std::string {
        std::error_code oec;
        if (will_shard && dev_debug) {
            std::string joined;
            for (std::size_t i = 0; i < unit_obj_paths.size(); ++i) {
                const auto& p = unit_obj_paths[i];
                if (!std::filesystem::exists(p, oec) || oec) {
                    return {};
                }
                if (i > 0) joined.push_back(';');
                joined += p.filename().string() + "=" + file_fingerprint_string(p);
            }
            return joined;
        }
        if (!will_shard && use_persistent_obj) {
            if (!std::filesystem::exists(obj_path, oec) || oec) {
                return {};
            }
            return file_fingerprint_string(obj_path);
        }
        return {};
    };

    const bool allow_bin_cache = !g_no_binary_cache && !is_wasm && !(plan.is_ios && plan.is_gui);
    std::string bin_cache_key;
    if (allow_bin_cache) {
        std::string extra_fp = std::format(
            "objc={}|ios={}|sanitize={}|test={}|dsym={}|dev={}|sharded={}",
            plan.uses_objc ? 1 : 0,
            plan.is_ios ? 1 : 0,
            sanitize ? 1 : 0,
            test_mode ? 1 : 0,
            g_emit_dsym ? 1 : 0,
            dev_debug ? 1 : 0,
            will_shard ? 1 : 0
        );
        if (will_shard && dev_debug) {
            for (const auto& uo : unit_obj_paths) {
                extra_fp += "|sobj=" + uo.string();
            }
        } else if (use_persistent_obj) {
            extra_fp += "|obj=" + obj_path.string();
        }
        if (!local_cpp_hdr_fp.empty()) {
            extra_fp += "|cpp_hdr=" + local_cpp_hdr_fp;
        }
        std::error_code lfec;
        for (const auto& l : plan.ldflags) {
            if (!l.empty() && l.front() != '-' &&
                (l.ends_with(".a") || l.ends_with(".o") || l.ends_with(".so") || l.ends_with(".dylib") || l.ends_with(".lib"))) {
                std::string stripped = l;
                if (stripped.size() >= 2 && stripped.front() == '"' && stripped.back() == '"') {
                    stripped = stripped.substr(1, stripped.size() - 2);
                }
                if (std::filesystem::is_regular_file(stripped, lfec)) {
                    extra_fp += "|lf:" + stripped + "=" + file_fingerprint_string(stripped);
                }
            }
        }
        bin_cache_key = BinaryCache::compute_key(
            cpp_code,
            compiler_fp,
            opt_flags,
            plan.cflags,
            plan.ldflags,
            linker_flag,
            plan.target_triple,
            rt_fp,
            extra_fp
        );

        bool can_restore = true;
#if defined(__APPLE__)
        if ((will_shard && dev_debug) || (!will_shard && use_persistent_obj)) {
            std::string cur_fp = compute_active_obj_fp();
            BinaryCacheMeta pre_meta;
            if (cur_fp.empty() ||
                !BinaryCache::read_meta(bin_cache_key, pre_meta) ||
                pre_meta.obj_fp.empty() ||
                cur_fp != pre_meta.obj_fp) {
                can_restore = false;
            }
        }
#endif
        if (can_restore) {
            BinaryCacheMeta cached_meta;
            if (BinaryCache::try_restore(bin_cache_key, out_bin, &cached_meta)) {
                plan.used_pch = cached_meta.used_pch;
                if (will_shard) {
                    plan.sharded_recompiled_units = 0;
                }
#if defined(__APPLE__)
                if (g_emit_dsym && ((will_shard && dev_debug) || (!will_shard && use_persistent_obj)) && !plan.is_ios) {
                    auto dsym_path = std::filesystem::path(out_bin.string() + ".dSYM");
                    std::error_code dec;
                    if (!std::filesystem::exists(dsym_path, dec)) {
                        int dsym_rc = 0;
                        (void)bridge::run_command_capture(
                            std::format("dsymutil {} 2>&1", bridge::shell_quote(out_bin.string())),
                            dsym_rc
                        );
                    }
                }
#endif
                package_apple_app_bundles(cf_file, out_bin, plan);
                return plan;
            }
        }
    }

    // Only use host PCH if compiling natively for the host (empty target_triple) and not in Objective-C++ mode
    std::string pch_path;
    if (plan.target_triple.empty() && !plan.uses_objc && !(plan.is_ios && plan.is_gui)) {
        std::string umbrella = extract_pch_umbrella_header(will_shard ? sharded.decls_hpp : cpp_code);
        pch_path = ensure_composite_pch(runtime_dir, umbrella, opt_flags, plan.cflags);
    }

    auto build_compile_cflags = [&](std::string& out_cmd) {
        if ((plan.is_ios && plan.is_gui) && !plan.uses_objc) {
            out_cmd += " -fobjc-arc";
        }
        for (const auto& f : plan.cflags) {
            if (is_wasm && f.find("simdjson") != std::string::npos) continue;
            out_cmd += " " + f;
        }
    };

    auto build_link_ldflags = [&](std::string& out_cmd) {
        if (!linker_flag.empty()) {
            out_cmd += " " + linker_flag;
        }
        for (const auto& l : plan.ldflags) {
            if (is_wasm && (l.find(".dylib") != std::string::npos || l.find(".so") != std::string::npos || l == "-lsimdjson" || l == "-lsqlite3" || l == "-lzstd")) {
                continue;
            }
            out_cmd += " " + l;
        }
    };

    auto append_link_relevant_cflags = [&](std::string& out_cmd) {
        for (std::size_t i = 0; i < plan.cflags.size(); ++i) {
            const auto& f = plan.cflags[i];
            if (f == "-isysroot" || f == "-arch" || f == "--target" || f == "--sysroot") {
                out_cmd += " " + f;
                if (i + 1 < plan.cflags.size()) {
                    out_cmd += " " + plan.cflags[++i];
                }
            } else if (f.starts_with("--target=") || f.starts_with("--sysroot=") ||
                       f.starts_with("-isysroot") || f == "-pthread" ||
                       f.starts_with("-stdlib=") || f.starts_with("-m") ||
                       f == "-static" || f.starts_with("-fuse-ld=")) {
                out_cmd += " " + f;
            }
        }
    };

    int exit_code = 0;
    std::string output;
    std::string cmd;

    if (will_shard) {
        std::vector<std::size_t> dirty_indices;
        std::error_code oec;
        for (std::size_t u = 0; u < unit_obj_paths.size(); ++u) {
            auto sz = std::filesystem::exists(unit_obj_paths[u], oec) && !oec
                ? std::filesystem::file_size(unit_obj_paths[u], oec)
                : 0;
            if (oec || sz == 0 || !std::filesystem::exists(unit_obj_paths[u], oec)) {
                dirty_indices.push_back(u);
                oec.clear();
            }
        }

        plan.sharded_recompiled_units = dirty_indices.size();
        plan.used_pch = !pch_path.empty();

        if (!dirty_indices.empty()) {
            auto make_unit_compile_cmd = [&](std::size_t u, const std::filesystem::path& out_obj, bool with_pch) {
                std::string c = compiler;
                build_compile_cflags(c);
                c += std::format(" -iquote \"{}\"", shard_dir.string());
                if (with_pch && !pch_path.empty()) {
                    c += std::format(" -include-pch \"{}\"{}", pch_path, pch_static_flag());
                }
                c += std::format(" {} -c \"{}\" -o \"{}\"", opt_flags, unit_cpp_paths[u].string(), out_obj.string());
                return c;
            };

            const std::size_t worker_count = std::min<std::size_t>(
                dirty_indices.size(),
                std::max<std::size_t>(1, static_cast<std::size_t>(std::thread::hardware_concurrency()))
            );
            std::atomic<std::size_t> next_job{0};
            std::atomic<bool> failed{false};
            std::atomic<bool> pch_fell_back{false};
            std::atomic<uint64_t> obj_tmp_seq{0};
            std::mutex err_mu;

            auto worker = [&]() {
                while (!failed.load(std::memory_order_acquire)) {
                    std::size_t j = next_job.fetch_add(1, std::memory_order_relaxed);
                    if (j >= dirty_indices.size()) break;
                    const std::size_t u = dirty_indices[j];
                    const auto& target_obj = unit_obj_paths[u];
                    const uint64_t tmp_id = obj_tmp_seq.fetch_add(1, std::memory_order_relaxed);
                    const auto tmp_obj = std::filesystem::path(std::format("{}.tmp.{}.o", target_obj.string(), tmp_id));

                    std::string unit_cmd = make_unit_compile_cmd(u, tmp_obj, true);
                    int rc = 0;
                    std::string out_txt = bridge::run_command_capture(unit_cmd, rc);
                    if (rc != 0 && !pch_path.empty()) {
                        if (is_verbose_build()) {
                            std::lock_guard<std::mutex> lk(err_mu);
                            std::println(stderr, "note: precompiled header compilation failed for {}; retrying without PCH", sharded.units[u].filename);
                        }
                        std::string fb_cmd = make_unit_compile_cmd(u, tmp_obj, false);
                        int fb_rc = 0;
                        std::string fb_out = bridge::run_command_capture(fb_cmd, fb_rc);
                        if (fb_rc == 0) {
                            rc = 0;
                            out_txt = std::move(fb_out);
                            unit_cmd = std::move(fb_cmd);
                            pch_fell_back.store(true, std::memory_order_relaxed);
                        }
                    }

                    std::error_code fec;
                    if (rc == 0) {
                        std::filesystem::rename(tmp_obj, target_obj, fec);
                        if (fec) {
                            fec.clear();
                            std::filesystem::copy_file(tmp_obj, target_obj, std::filesystem::copy_options::overwrite_existing, fec);
                            std::error_code rm_ec;
                            std::filesystem::remove(tmp_obj, rm_ec);
                            if (fec) {
                                rc = 1;
                                out_txt = "failed to stage compiled object file: " + target_obj.string() + " (" + fec.message() + ")";
                            }
                        }
                    } else {
                        std::filesystem::remove(tmp_obj, fec);
                    }

                    if (rc != 0) {
                        std::lock_guard<std::mutex> lk(err_mu);
                        if (!failed.exchange(true, std::memory_order_acq_rel)) {
                            exit_code = rc;
                            output = std::move(out_txt);
                            cmd = std::move(unit_cmd);
                        }
                        break;
                    }
                }
            };

            std::vector<std::thread> threads;
            threads.reserve(worker_count > 1 ? worker_count - 1 : 0);
            for (std::size_t t = 1; t < worker_count; ++t) {
                threads.emplace_back(worker);
            }
            worker();
            for (auto& th : threads) {
                th.join();
            }

            if (pch_fell_back.load(std::memory_order_relaxed)) {
                plan.used_pch = false;
            }

            if (exit_code == 0) {
                // Prune stale `.o` files per unit, keeping at most the 4 newest by mtime (never deleting active unit .o).
                std::error_code pec;
                for (std::size_t u = 0; u < sharded.units.size(); ++u) {
                    const std::string prefix = std::format("{}__pkg_{}_", stem, sharded.units[u].id);
                    const std::string keep_name = unit_obj_paths[u].filename().string();
                    struct ObjEntry {
                        std::filesystem::path path;
                        std::filesystem::file_time_type mtime{};
                    };
                    std::vector<ObjEntry> stale;
                    for (const auto& de : std::filesystem::directory_iterator(obj_dir, pec)) {
                        if (pec) break;
                        std::string fname = de.path().filename().string();
                        if (fname.starts_with(prefix) && fname.ends_with(".o") && !fname.contains(".tmp.") && fname != keep_name) {
                            auto mt = de.last_write_time(pec);
                            if (!pec) stale.push_back(ObjEntry{de.path(), mt});
                            pec.clear();
                        }
                    }
                    if (stale.size() > 3) {
                        std::sort(stale.begin(), stale.end(), [](const ObjEntry& a, const ObjEntry& b) {
                            return a.mtime > b.mtime;
                        });
                        for (std::size_t s = 3; s < stale.size(); ++s) {
                            std::filesystem::remove(stale[s].path, pec);
                        }
                    }
                }
            }
        }

        if (exit_code == 0) {
            std::string link_cmd = compiler;
            append_link_relevant_cflags(link_cmd);
            if (sanitize) {
                link_cmd += " " + sanitizer_opt_flags();
            }
            for (const auto& u_obj : unit_obj_paths) {
                link_cmd += std::format(" \"{}\"", u_obj.string());
            }
            link_cmd += std::format(" -o \"{}\"", out_bin.string());
            build_link_ldflags(link_cmd);
            int link_rc = 0;
            std::string link_out = bridge::run_command_capture(link_cmd, link_rc);
            if (link_rc != 0) {
                exit_code = link_rc;
                output = std::move(link_out);
                cmd = std::move(link_cmd);
            }
        }
    } else {
        auto make_compile_cmd = [&](bool with_pch) {
            std::string c = compiler;
            build_compile_cflags(c);
            if (with_pch && !pch_path.empty()) {
                c += std::format(" -include-pch \"{}\"{}", pch_path, pch_static_flag());
            }
            if (use_persistent_obj) {
                c += std::format(" {} -c \"{}\" -o \"{}\"", opt_flags, cpp_path.string(), obj_path.string());
            } else {
                c += std::format(" {} \"{}\" -o \"{}\"", opt_flags, cpp_path.string(), out_bin.string());
                build_link_ldflags(c);
            }
            return c;
        };

        plan.used_pch = !pch_path.empty();
        cmd = make_compile_cmd(true);
        output = bridge::run_command_capture(cmd, exit_code);
        if (exit_code != 0 && plan.used_pch) {
            if (is_verbose_build()) {
                std::println(stderr, "note: precompiled header compilation failed; retrying without PCH");
            }
            // Fallback: retry compilation without -include-pch in case of toolchain or thread-model divergence
            std::string fallback_cmd = make_compile_cmd(false);
            int fb_code = 0;
            std::string fb_out = bridge::run_command_capture(fallback_cmd, fb_code);
            if (fb_code == 0) {
                exit_code = 0;
                output = std::move(fb_out);
                cmd = std::move(fallback_cmd);
                plan.used_pch = false;
            }
        }

        if (exit_code == 0 && use_persistent_obj) {
            std::string link_cmd = compiler;
            append_link_relevant_cflags(link_cmd);
            if (sanitize) {
                link_cmd += " " + sanitizer_opt_flags();
            }
            link_cmd += std::format(" \"{}\" -o \"{}\"", obj_path.string(), out_bin.string());
            build_link_ldflags(link_cmd);
            int link_rc = 0;
            std::string link_out = bridge::run_command_capture(link_cmd, link_rc);
            if (link_rc != 0) {
                exit_code = link_rc;
                output = output.empty() ? link_out : (output + "\n" + link_out);
                cmd = std::move(link_cmd);
            }
        }
    }

    if (exit_code != 0) {
        std::string clean_out = cfire::diag::humanize_cpp_compiler_output(output);
        if (is_verbose_build()) {
            return std::unexpected(std::format("{} compilation failed:\n{}\nCommand: {}", compiler, clean_out, cmd));
        }
        return std::unexpected(std::format("compilation failed:\n{}", clean_out));
    }

#if defined(__APPLE__)
    if (g_emit_dsym && ((will_shard && dev_debug) || (!will_shard && use_persistent_obj)) && !plan.is_ios) {
        int dsym_rc = 0;
        (void)bridge::run_command_capture(
            std::format("dsymutil {} 2>&1", bridge::shell_quote(out_bin.string())),
            dsym_rc
        );
    }
#endif

    if (allow_bin_cache && !bin_cache_key.empty()) {
        BinaryCacheMeta store_meta;
        store_meta.used_pch = plan.used_pch;
        if ((will_shard && dev_debug) || (!will_shard && use_persistent_obj)) {
            store_meta.obj_fp = compute_active_obj_fp();
        }
        BinaryCache::store(bin_cache_key, out_bin, store_meta);
    }

    package_apple_app_bundles(cf_file, out_bin, plan);

    return plan;
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto generate_cmake_content(const BuildPlan& plan) -> std::string {
    std::ostringstream cmake;
    cmake << "cmake_minimum_required(VERSION 3.25)\n";
    cmake << "project(cfire_ejected_app LANGUAGES C CXX)\n";
    cmake << "set(CMAKE_CXX_STANDARD 23)\n";
    cmake << "set(CMAKE_CXX_STANDARD_REQUIRED OFF)\n\n";
    cmake << "include_directories(\"${CMAKE_CURRENT_SOURCE_DIR}\")\n\n";
    if (!plan.pkg_config_deps.empty()) {
        cmake << "find_package(PkgConfig REQUIRED)\n";
        for (std::size_t i = 0; i < plan.pkg_config_deps.size(); ++i) {
            cmake << std::format("pkg_check_modules(DEP_{} REQUIRED IMPORTED_TARGET {})\n", i, plan.pkg_config_deps[i]);
        }
        cmake << "\n";
    }
    for (const auto& dep : plan.external_deps) {
        std::string vendor_prefix = std::format("vendor/{}", dep.name);
        if (!dep.sources.empty()) {
            std::string srcs;
            for (const auto& s : dep.sources) {
                if (!srcs.empty()) srcs += " ";
                srcs += std::format("\"{}/{}\"", vendor_prefix, s);
            }
            cmake << std::format("add_library({} STATIC {})\n", dep.name, srcs);
            cmake << std::format("target_include_directories({} PUBLIC \"${{CMAKE_CURRENT_SOURCE_DIR}}/{}\")\n", dep.name, vendor_prefix);
            for (const auto& [k, v] : dep.defines) {
                cmake << std::format("target_compile_definitions({} PRIVATE {}={})\n", dep.name, k, v);
            }
            cmake << "\n";
        } else if (dep.driver == "CMake" || dep.driver == "cmake") {
            cmake << std::format("if (EXISTS \"${{CMAKE_CURRENT_SOURCE_DIR}}/{}/CMakeLists.txt\")\n", vendor_prefix);
            cmake << std::format("    add_subdirectory({})\n", vendor_prefix);
            cmake << "endif()\n\n";
        }
    }
    cmake << "add_executable(app main.cpp)\n";
    cmake << "if (CMAKE_CXX_COMPILER_ID MATCHES \"Clang|AppleClang\")\n";
    cmake << "    target_compile_options(app PRIVATE -std=c++2b)\n";
    cmake << "endif()\n";
    cmake << "target_include_directories(app PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}\")\n";
    for (std::size_t i = 0; i < plan.pkg_config_deps.size(); ++i) {
        cmake << std::format("target_link_libraries(app PRIVATE PkgConfig::DEP_{})\n", i);
    }
    for (const auto& dep : plan.external_deps) {
        cmake << std::format("target_link_libraries(app PRIVATE {})\n", dep.name);
    }
    for (const auto& flag : plan.ldflags) {
        cmake << std::format("target_link_libraries(app PRIVATE \"{}\")\n", flag);
    }
    return cmake.str();
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto eject_cmake(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_dir
) -> std::expected<void, std::string> {
    auto res = analyze_and_emit(cf_file, runtime_dir, true);
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);

    std::filesystem::create_directories(out_dir);
    {
        std::ofstream out(out_dir / "main.cpp", std::ios::binary | std::ios::trunc);
        out << cpp_code;
    }
    for (const char* hdr : {"cfire_prelude.hpp", "cfire_test_runtime.hpp", "cfire_core_stdlib.hpp"}) {
        std::error_code ec;
        std::filesystem::copy_file(runtime_dir / hdr, out_dir / hdr,
            std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (std::filesystem::exists(runtime_dir / "core")) {
        std::error_code ec;
        std::filesystem::copy(runtime_dir / "core", out_dir / "core",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (std::filesystem::exists(runtime_dir / "vendor")) {
        std::error_code ec;
        std::filesystem::copy(runtime_dir / "vendor", out_dir / "vendor",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::filesystem::path cf_dir = std::filesystem::absolute(cf_file).parent_path();
    for (const auto& dep : plan.external_deps) {
        if (!dep.source.empty()) {
            std::filesystem::path src_dir = dep.source.starts_with("/") ? std::filesystem::path(dep.source) : (cf_dir / dep.source);
            if (std::filesystem::exists(src_dir)) {
                std::filesystem::path dest_vendor = out_dir / "vendor" / dep.name;
                std::error_code ec;
                std::filesystem::create_directories(dest_vendor, ec);
                std::filesystem::copy(src_dir, dest_vendor,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    std::string cmake_text = generate_cmake_content(plan);
    std::ofstream cm_out(out_dir / "CMakeLists.txt", std::ios::binary | std::ios::trunc);
    cm_out << cmake_text;
    return {};
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_BUILDSYS_IMPL)
#undef CFIRE_BUILDSYS_INLINE

} // namespace cfire::build
