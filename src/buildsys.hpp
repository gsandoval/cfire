#pragma once
// src/buildsys.hpp — C++23 Unified Build System, Live CC/CMake Builder & Multi-Package Resolver

#include "version.hpp"
#include "build/toolchain_layout.hpp"
#include "build/manifest.hpp"
#include "build/deps.hpp"
#include "build/package_loader.hpp"
#include "build/wasm_shell.hpp"
#include "build/apple_bundle.hpp"

namespace cfire::build {

[[nodiscard]] inline auto analyze_and_emit(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast,
    bool test_mode = false,
    const std::string& target_triple = "",
    const std::string& sysroot = "",
    const std::string& web_profile = "auto",
    const std::string& subsystem = "auto"
) -> std::expected<std::pair<std::string, BuildPlan>, std::string> {
    auto dir = cf_file.parent_path();
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
    if (test_mode) {
        plan.cflags.push_back("-DCFIRE_TEST_MODE=1");
    }
    bool is_win_target = (plan.target_triple.find("windows") != std::string::npos ||
                          plan.target_triple.find("win32") != std::string::npos ||
                          plan.target_triple.find("mingw") != std::string::npos);
#if defined(_WIN32)
    if (!is_wasm) {
        is_win_target = true;
    }
#endif
    if (is_win_target) {
        plan.ldflags.push_back("-lws2_32");
        plan.ldflags.push_back("-ladvapi32");
    }

    // 1. Parse & execute any pre-build commands declared in sibling or ancestor `build.cf` (BS-08, CF-031)
    if (cf_file.filename() != "build.cf") {
        std::filesystem::path found_bcf;
        std::error_code bcf_ec;
        auto search_d = std::filesystem::weakly_canonical(dir, bcf_ec);
        if (bcf_ec || search_d.empty()) search_d = std::filesystem::absolute(dir, bcf_ec);
        for (int depth = 0; depth < 8 && !search_d.empty(); ++depth) {
            auto cand_bcf = search_d / "build.cf";
            if (std::filesystem::exists(cand_bcf, bcf_ec)) {
                found_bcf = cand_bcf;
                break;
            }
            if (search_d == search_d.parent_path()) break;
            search_d = search_d.parent_path();
        }
        if (!found_bcf.empty()) {
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
                if (auto r = execute_pre_build_command(cmd, bcf_parent); !r) {
                    return std::unexpected(r.error());
                }
                plan.pre_build_commands.push_back(std::move(cmd));
            }
        }
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
    for (auto& dep : plan.external_deps) {
        auto dep_base = dep.base_dir.empty() ? dir : dep.base_dir;
        if (auto r = build_local_dep(dep_base, dep, plan); !r) {
            return std::unexpected(r.error());
        }
    }

    // 3. Semantic Analysis
    auto checked = frontend::analyze(std::move(*ast));
    if (!checked) return std::unexpected(checked.error());

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
        int sc_rc = 0;
        (void)bridge::run_command_capture("pkg-config --exists sqlcipher 2>/dev/null", sc_rc);
        if (sc_rc == 0) return true;
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

    std::unordered_set<std::string> seen_pc;
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
        query_pkg_config(pc, plan.cflags, plan.ldflags, install_root);
    };

    auto cache_dir = dir / ".cfire" / "cache";
    for (const auto& [sname, tags] : checked->struct_tags) {
        bool has_j = false;
        for (const auto& t : tags) {
            if (!t.json_key.empty()) { has_j = true; break; }
        }
        if (has_j) {
            add_pc("simdjson");
            break;
        }
    }
    for (const auto& imp : checked->ast.imports) {
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
                    } else {
#if defined(__APPLE__)
                        plan.uses_objc = true;
                        plan.cflags.push_back("-fobjc-arc");
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "Cocoa",
                            "-framework", "WebKit"
                        });
#elif defined(__linux__)
                        int wk_rc = 0;
                        (void)bridge::run_command_capture("pkg-config --exists webkit2gtk-4.1 2>/dev/null", wk_rc);
                        if (wk_rc == 0) {
                            add_pc("webkit2gtk-4.1");
                            add_pc("gtk+-3.0");
                        } else {
                            int wk40_rc = 0;
                            (void)bridge::run_command_capture("pkg-config --exists webkit2gtk-4.0 2>/dev/null", wk40_rc);
                            if (wk40_rc == 0) {
                                add_pc("webkit2gtk-4.0");
                                add_pc("gtk+-3.0");
                            }
                        }
#elif defined(_WIN32)
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-luser32", "-lgdi32", "-lshell32", "-lole32", "-ldwmapi", "-ladvapi32", "-lcomdlg32"
                        });
#endif
                        if (plan.target_triple.find("windows") != std::string::npos ||
                            plan.target_triple.find("win32") != std::string::npos ||
                            plan.target_triple.find("mingw") != std::string::npos) {
                            plan.ldflags.insert(plan.ldflags.end(), {
                                "-luser32", "-lgdi32", "-lshell32", "-lole32", "-ldwmapi", "-ladvapi32", "-lcomdlg32"
                            });
                        }
                    }
                }
            }
            if (imp.path == "vendor:raylib" || imp.path == "vendor:raygui") {
                plan.is_gui = true;
                if (imp.path == "vendor:raygui") {
                    for (auto cur = std::filesystem::absolute(dir); !cur.empty() && cur != cur.parent_path(); cur = cur.parent_path()) {
                        auto vg_dir = cur / "vendor" / "raygui";
                        if (std::filesystem::exists(vg_dir / "raygui.h")) {
                            plan.cflags.push_back("-I" + vg_dir.string());
                            break;
                        } else if (std::filesystem::exists(vg_dir / "src" / "raygui.h")) {
                            plan.cflags.push_back("-I" + (vg_dir / "src").string());
                            break;
                        }
                    }
                }

                if (is_wasm) {
                    bool found_raylib_wasm = false;
                    for (const auto& cache_raylib_wasm : {
                        get_package_cache_root() / "vendor_raylib_wasm",
                        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".cache" / "cfire" / "pkg" / "vendor_raylib_wasm",
                        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".cfire" / "cache" / "pkg" / "vendor_raylib_wasm",
                        std::filesystem::path("/tmp/cfire_cache/pkg/vendor_raylib_wasm")
                    }) {
                        if (std::filesystem::exists(cache_raylib_wasm / "include" / "raylib.h")) {
                            plan.cflags.push_back("-I" + (cache_raylib_wasm / "include").string());
                            if (std::filesystem::exists(cache_raylib_wasm / "lib" / "libraylib.a")) {
                                plan.ldflags.push_back((cache_raylib_wasm / "lib" / "libraylib.a").string());
                            }
                            found_raylib_wasm = true;
                            break;
                        }
                    }
                    if (!found_raylib_wasm) {
                        for (auto cur = std::filesystem::absolute(dir); !cur.empty() && cur != cur.parent_path(); cur = cur.parent_path()) {
                            auto v_web = cur / "vendor" / "raylib" / "web";
                            if (std::filesystem::exists(v_web / "libraylib.a")) {
                                plan.ldflags.push_back((v_web / "libraylib.a").string());
                                found_raylib_wasm = true;
                                break;
                            }
                        }
                    }
                    if (!found_raylib_wasm) {
                        for (const auto& inc_dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include"}) {
                            if (std::filesystem::exists(std::filesystem::path(inc_dir) / "raylib.h")) {
                                plan.cflags.push_back("-I" + std::string(inc_dir));
                                break;
                            }
                        }
                        for (const auto& cache_raylib : {get_package_cache_root() / "vendor_raylib",
                                                         std::filesystem::path("/tmp/cfire_cache/pkg/vendor_raylib")}) {
                            if (std::filesystem::exists(cache_raylib / "include" / "raylib.h")) {
                                plan.cflags.push_back("-I" + (cache_raylib / "include").string());
                                break;
                            }
                        }
                    }
                    plan.cflags.push_back("-DPLATFORM_WEB");
                    plan.ldflags.push_back("-sUSE_GLFW=3");
                    plan.ldflags.push_back("-sASYNCIFY=1");
                    plan.ldflags.push_back("-sALLOW_MEMORY_GROWTH=1");
                } else if (plan.is_ios) {
                    bool already_has_objc_arc = false;
                    for (const auto& cf : plan.cflags) {
                        if (cf == "-fobjc-arc") already_has_objc_arc = true;
                    }
                    if (!already_has_objc_arc) {
                        plan.cflags.push_back("-fobjc-arc");
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "UIKit",
                            "-framework", "Foundation",
                            "-framework", "OpenGLES",
                            "-framework", "QuartzCore",
                            "-framework", "CoreGraphics",
                            "-framework", "AudioToolbox"
                        });
                    }
                } else {
                auto is_readable = [](const std::filesystem::path& p) -> bool {
                    std::ifstream ifs(p, std::ios::binary);
                    return ifs.good();
                };
                bool found_raylib = false;

                // 1. Try ~/.local/include + ~/.local/lib first (CF-075)
                if (const char* home = std::getenv("HOME")) {
                    auto loc = std::filesystem::path(home) / ".local";
                    if (is_readable(loc / "include" / "raylib.h")) {
                        plan.cflags.push_back("-I" + (loc / "include").string());
                        if (is_readable(loc / "lib" / "libraylib.a")) {
                            plan.ldflags.push_back((loc / "lib" / "libraylib.a").string());
                        } else {
                            plan.ldflags.push_back("-L" + (loc / "lib").string());
                            plan.ldflags.push_back("-lraylib");
                        }
                        found_raylib = true;
                    }
                }

                // 2. Try system pkg-config
                if (!found_raylib) {
                    int chk_rc = 0;
                    (void)bridge::run_command_capture("pkg-config --exists raylib 2>/dev/null", chk_rc);
                    if (chk_rc == 0) {
                        add_pc("raylib");
                        found_raylib = true;
                    }
                }

                // 3. Try standard system directories
                if (!found_raylib) {
                    for (const auto& inc_dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include"}) {
                        if (is_readable(std::filesystem::path(inc_dir) / "raylib.h")) {
                            plan.cflags.push_back("-I" + std::string(inc_dir));
                            std::string lib_dir = (inc_dir == std::string("/opt/homebrew/include")) ? "/opt/homebrew/lib" :
                                                  (inc_dir == std::string("/usr/local/include")) ? "/usr/local/lib" : "/usr/lib";
                            if (is_readable(std::filesystem::path(lib_dir) / "libraylib.a")) {
                                plan.ldflags.push_back((std::filesystem::path(lib_dir) / "libraylib.a").string());
                            } else {
                                plan.ldflags.push_back("-L" + lib_dir);
                                plan.ldflags.push_back("-lraylib");
                            }
                            found_raylib = true;
                            break;
                        }
                    }
                }

                // 4. Try project-local vendor/raylib or ancestor vendor_raylib cache
                if (!found_raylib) {
                    for (auto cur = std::filesystem::absolute(dir); !cur.empty() && cur != cur.parent_path(); cur = cur.parent_path()) {
                        auto v_dir = cur / "vendor" / "raylib";
                        auto c_dir = cur / ".cfire" / "cache" / "vendor_raylib";
                        if (is_readable(v_dir / "include" / "raylib.h") && is_readable(v_dir / "lib" / "libraylib.a")) {
                            plan.cflags.push_back("-I" + (v_dir / "include").string());
                            plan.ldflags.push_back((v_dir / "lib" / "libraylib.a").string());
                            found_raylib = true;
                            break;
                        } else if (is_readable(c_dir / "include" / "raylib.h") && is_readable(c_dir / "lib" / "libraylib.a")) {
                            plan.cflags.push_back("-I" + (c_dir / "include").string());
                            plan.ldflags.push_back((c_dir / "lib" / "libraylib.a").string());
                            found_raylib = true;
                            break;
                        } else if (is_readable(v_dir / "raylib.h") && is_readable(v_dir / "libraylib.a")) {
                            plan.cflags.push_back("-I" + v_dir.string());
                            plan.ldflags.push_back((v_dir / "libraylib.a").string());
                            found_raylib = true;
                            break;
                        }
                    }
                }

                // 5. Try current working directory cache and global/local package cache (populated via `cfire setup --raylib`)
                if (!found_raylib) {
                    auto cwd_cache = std::filesystem::current_path() / ".cfire" / "cache" / "vendor_raylib";
                    if (is_readable(cwd_cache / "include" / "raylib.h") && is_readable(cwd_cache / "lib" / "libraylib.a")) {
                        plan.cflags.push_back("-I" + (cwd_cache / "include").string());
                        plan.ldflags.push_back((cwd_cache / "lib" / "libraylib.a").string());
                        found_raylib = true;
                    }
                }
                if (!found_raylib) {
                    for (const auto& cache_raylib : {get_package_cache_root() / "vendor_raylib",
                                                     std::filesystem::path("/tmp/cfire_cache/pkg/vendor_raylib")}) {
                        if (is_readable(cache_raylib / "include" / "raylib.h") &&
                            is_readable(cache_raylib / "include" / "raymath.h") &&
                            is_readable(cache_raylib / "lib" / "libraylib.a")) {
                            plan.cflags.push_back("-I" + (cache_raylib / "include").string());
                            plan.ldflags.push_back((cache_raylib / "lib" / "libraylib.a").string());
                            found_raylib = true;
                            break;
                        }
                    }
                }

                if (found_raylib) {
#if defined(__APPLE__)
                    plan.ldflags.insert(plan.ldflags.end(), {"-framework", "Cocoa", "-framework", "IOKit", "-framework", "OpenGL", "-framework", "AudioToolbox", "-framework", "CoreFoundation", "-framework", "ApplicationServices"});
#elif defined(__linux__)
                    plan.ldflags.insert(plan.ldflags.end(), {"-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11"});
#endif
                } else {
                    bool already_has_fallback = false;
                    for (const auto& cf : plan.cflags) {
                        if (cf == "-DCFIRE_RAYLIB_FALLBACK=1") already_has_fallback = true;
                    }
                    if (!already_has_fallback) {
                        plan.cflags.push_back("-DCFIRE_RAYLIB_FALLBACK=1");
#if defined(__APPLE__)
                        plan.ldflags.insert(plan.ldflags.end(), {"-framework", "AudioToolbox", "-framework", "CoreFoundation"});
#endif
                    }
                }
                } // !is_wasm
            }
            if (imp.path == "ext:crypto/sodium" || imp.path == "ext:sodium") {
                if (!is_wasm && !plan.is_ios) {
                    int sod_rc = 0;
                    (void)bridge::run_command_capture("pkg-config --exists libsodium 2>/dev/null", sod_rc);
                    bool found_sodium = (sod_rc == 0);
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
                    } else if (inspect_clang_ast) {
                        return std::unexpected(
                            "ext:crypto/sodium requires libsodium (install via 'brew install libsodium' or 'apt install libsodium-dev')");
                    }
                }
            }
            if (imp.path == "core:keychain" || imp.path == "ext:keychain") {
                if (!is_wasm) {
#if defined(__APPLE__)
                    plan.ldflags.insert(plan.ldflags.end(), {
                        "-framework", "Security",
                        "-framework", "CoreFoundation"
                    });
#elif defined(_WIN32)
                    plan.ldflags.push_back("-ladvapi32");
#endif
                    if (plan.target_triple.find("windows") != std::string::npos ||
                        plan.target_triple.find("win32") != std::string::npos ||
                        plan.target_triple.find("mingw") != std::string::npos) {
                        plan.ldflags.push_back("-ladvapi32");
                    }
                }
            }
            if (imp.path == "core:net/ssh" || imp.path == "ext:net/ssh" || imp.path == "core:ssh") {
                if (!is_wasm && !plan.is_ios) {
                    int ssh_rc = 0;
                    (void)bridge::run_command_capture("pkg-config --exists libssh 2>/dev/null", ssh_rc);
                    bool found_libssh = (ssh_rc == 0);
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
        if (inspect_clang_ast) {
            auto idx = bridge::inspect_header(imp.path, plan.cflags, cache_dir);
            if (!idx) return std::unexpected(idx.error());
            for (const auto& [sym_name, sym] : idx->symbols) {
                if (sym.kind == "FunctionDecl") {
                    checked->cpp_fn_params[sym_name] = sym.param_types;
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
            int sj_rc = 0;
            auto sj_ver = bridge::run_command_capture("pkg-config --modversion simdjson 2>/dev/null", sj_rc);
            sj_ver = trim_whitespace(sj_ver);
            if (sj_rc == 0 && !sj_ver.empty() && (sj_ver.starts_with("1.") || sj_ver.starts_with("2.") || sj_ver.starts_with("3."))) {
                int cl_rc = 0;
                auto cl_ver = bridge::run_command_capture("clang++ -dumpversion 2>/dev/null", cl_rc);
                cl_ver = trim_whitespace(cl_ver);
                int cl_maj = 0;
                if (auto dot = cl_ver.find('.'); dot != std::string::npos) {
                    if (auto parsed_maj = cfire::detail::parse_int<int>(cl_ver.substr(0, dot))) cl_maj = *parsed_maj;
                } else if (auto parsed_maj = cfire::detail::parse_int<int>(cl_ver)) {
                    cl_maj = *parsed_maj;
                }
                if (cl_maj >= 19) {
                    return std::unexpected(std::format(
                        "system simdjson {} is incompatible with Clang {} (requires simdjson >= 4.0; install simdjson >= 4.0 to ~/.local or upgrade libsimdjson-dev)",
                        sj_ver, cl_maj));
                }
            }
        }
    }

    // 4. C++23 Code Generation (with Clang AST boundary coercions)
    std::string cpp_code = frontend::emit_cpp(*checked, test_mode);

    if (auto pos = cpp_code.find("InitWindow("); pos != std::string::npos) {
        auto rp = cpp_code.find(')', pos + 11);
        if (rp != std::string::npos) {
            std::string args = cpp_code.substr(pos + 11, rp - (pos + 11));
            auto c1 = args.find(',');
            if (c1 != std::string::npos) {
                auto c2 = args.find(',', c1 + 1);
                std::string w_str = trim_whitespace(args.substr(0, c1));
                std::string h_str = trim_whitespace(c2 != std::string::npos ? args.substr(c1 + 1, c2 - (c1 + 1)) : args.substr(c1 + 1));
                if (auto w = cfire::detail::parse_int<int>(w_str)) plan.window_init_width = *w;
                if (auto h = cfire::detail::parse_int<int>(h_str)) plan.window_init_height = *h;
            }
        }
    }

    return std::make_pair(std::move(cpp_code), std::move(plan));
}


[[nodiscard]] inline auto resolve_cxx_compiler() -> std::string {
    if (const char* env_cxx = std::getenv("CFIRE_CXX"); env_cxx && *env_cxx) {
        return std::string(env_cxx);
    }
    return "clang++";
}

[[nodiscard]] inline auto prelude_pch_path(
    const std::filesystem::path& runtime_dir,
    bool sanitize
) -> std::filesystem::path {
    std::error_code ec;
    auto pch_dir = std::filesystem::temp_directory_path(ec) / "cfire_pch_cache";
    auto can_rt = std::filesystem::weakly_canonical(runtime_dir, ec);
    std::string cxx_bin = resolve_cxx_compiler();
    std::string key = std::format("{}|{}|{}|{}",
        (ec ? runtime_dir : can_rt).string(),
        cxx_bin,
        cfire::version::kVersion,
        cfire::version::kGitSha);
    std::size_t rt_hash = std::hash<std::string>{}(key);
    std::string pch_name = std::format("cfire_prelude_{}_{:08x}.pch",
        sanitize ? "san" : "opt",
        static_cast<std::uint32_t>(rt_hash & 0xFFFFFFFFu));
    return pch_dir / pch_name;
}

[[nodiscard]] inline auto ensure_prelude_pch(
    const std::filesystem::path& runtime_dir,
    bool sanitize
) -> std::string {
    auto prelude_hdr = runtime_dir / "cfire_prelude.hpp";
    std::error_code ec;
    if (!std::filesystem::exists(prelude_hdr, ec)) return "";

    auto pch_path = prelude_pch_path(runtime_dir, sanitize);
    std::filesystem::create_directories(pch_path.parent_path(), ec);
    std::string cxx_bin = resolve_cxx_compiler();

    bool needs_build = !std::filesystem::exists(pch_path, ec);
    if (!needs_build) {
        auto hdr_time = std::filesystem::last_write_time(prelude_hdr, ec);
        auto pch_time = std::filesystem::last_write_time(pch_path, ec);
        if (!ec && hdr_time > pch_time) {
            needs_build = true;
        }
        auto test_rt_hdr = runtime_dir / "cfire_test_runtime.hpp";
        if (!needs_build && std::filesystem::exists(test_rt_hdr, ec)) {
            auto tr_time = std::filesystem::last_write_time(test_rt_hdr, ec);
            if (!ec && tr_time > pch_time) {
                needs_build = true;
            }
        }
    }

    if (needs_build) {
        std::string opt_flags = sanitize
            ? "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
            : "-O2";
#if defined(__linux__)
        opt_flags += " -Wno-builtin-macro-redefined -D__cpp_concepts=202002L -pthread";
#endif
        std::string cmd = std::format(
            "{} -std=c++2b -Wno-c++11-narrowing {} -x c++-header \"{}\" -o \"{}\"",
            cxx_bin, opt_flags, prelude_hdr.string(), pch_path.string());
        int rc = 0;
        (void)bridge::run_command_capture(cmd, rc);
        if (rc != 0) {
            std::filesystem::remove(pch_path, ec);
            return "";
        }
    }
    return pch_path.string();
}

[[nodiscard]] inline auto verify_cpp_syntax_only(
    const std::filesystem::path& cf_file,
    std::string_view cpp_code,
    const BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    const std::vector<std::string>& extra_includes = {}
) -> std::expected<void, std::string> {
    auto gen_dir = cf_file.parent_path() / ".cfire" / "gen";
    std::error_code ec;
    std::filesystem::create_directories(gen_dir, ec);
    auto cpp_path = gen_dir / (cf_file.stem().string() + ".cpp");
    if (ec) {
        cpp_path = std::filesystem::temp_directory_path() / (cf_file.stem().string() + "_check.cpp");
    }
    {
        std::ofstream out(cpp_path, std::ios::binary | std::ios::trunc);
        out << cpp_code;
    }

    std::string pch_path;
    if (plan.target_triple.empty() && !plan.uses_objc) {
        pch_path = ensure_prelude_pch(runtime_dir, false);
    }
    std::string cmd = resolve_cxx_compiler() + " -O2";
    for (const auto& f : plan.cflags) cmd += " " + f;
    for (const auto& inc : extra_includes) cmd += " -I \"" + inc + "\"";
    if (!pch_path.empty()) {
        cmd += std::format(" -include-pch \"{}\"", pch_path);
    }
    if (plan.uses_objc) {
        cmd += " -x objective-c++";
    }
    cmd += std::format(" -fsyntax-only \"{}\" 2>&1", cpp_path.string());
    int rc = 0;
    auto output = bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        return std::unexpected(cfire::diag::humanize_cpp_compiler_output(output));
    }
    return {};
}


[[nodiscard]] inline auto build_native(
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
) -> std::expected<BuildPlan, std::string> {
    auto res = analyze_and_emit(cf_file, runtime_dir, true, test_mode, target_triple, sysroot, web_profile, subsystem);
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);
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

    bool is_wasm = target_triple.starts_with("wasm");
    if (is_wasm) {
        auto empp_res = resolve_emscripten_compiler();
        if (!empp_res) {
            return std::unexpected(empp_res.error());
        }
        plan.emscripten_compiler = *empp_res;
        std::string effective_profile = (plan.web_profile == "auto") ? (plan.is_gui ? "canvas" : "cli") : plan.web_profile;
        plan.web_profile = effective_profile;

        auto gen_dir = cf_file.parent_path() / ".cfire" / "gen";
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
        if (gui_subsystem) {
            bool is_windows = target_triple.find("windows") != std::string::npos ||
                              target_triple.find("win32") != std::string::npos ||
                              target_triple.find("mingw") != std::string::npos;
#if defined(_WIN32)
            is_windows = true;
#endif
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

    auto gen_dir = cf_file.parent_path() / ".cfire" / "gen";
    std::filesystem::create_directories(gen_dir);
    if (out_bin.has_parent_path()) {
        std::error_code out_ec;
        std::filesystem::create_directories(out_bin.parent_path(), out_ec);
    }
    auto stem = test_mode ? (cf_file.stem().string() + "_test") : cf_file.stem().string();
    auto cpp_path = gen_dir / (stem + ((plan.uses_objc || (plan.is_ios && plan.is_gui)) ? ".mm" : ".cpp"));
    plan.generated_cpp = cpp_path.string();

    {
        std::ofstream out(cpp_path, std::ios::binary | std::ios::trunc);
        out << cpp_code;
    }

    std::string opt_flags = sanitize
        ? "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
        : "-O2";
    // Only use host PCH if compiling natively for the host (empty target_triple) and not in Objective-C++ mode
    std::string pch_path;
    if (plan.target_triple.empty() && !plan.uses_objc) {
        pch_path = ensure_prelude_pch(runtime_dir, sanitize);
    }

    std::string compiler = is_wasm ? plan.emscripten_compiler : resolve_cxx_compiler();
    std::string cmd = compiler;
    if ((plan.is_ios && plan.is_gui) && !plan.uses_objc) {
        cmd += " -fobjc-arc";
    }
    for (const auto& f : plan.cflags) {
        if (is_wasm && f.find("simdjson") != std::string::npos) continue;
        cmd += " " + f;
    }
    if (!pch_path.empty()) {
        cmd += std::format(" -include-pch \"{}\"", pch_path);
        plan.used_pch = true;
    }
    for (auto& l : plan.ldflags) {
        if (l == "-lsimdjson") {
            for (const auto& d : plan.ldflags) {
                if (d.starts_with("-L")) {
                    auto p = std::filesystem::path(d.substr(2)) / "libsimdjson_static.a";
                    if (std::filesystem::exists(p)) {
                        l = p.string();
                        break;
                    }
                }
            }
        }
    }

    cmd += std::format(" {} \"{}\" -o \"{}\"", opt_flags, cpp_path.string(), out_bin.string());
    for (const auto& l : plan.ldflags) {
        if (is_wasm && (l.find(".dylib") != std::string::npos || l.find(".so") != std::string::npos || l == "-lsimdjson" || l == "-lsqlite3" || l == "-lzstd")) {
            continue;
        }
        cmd += " " + l;
    }

    int exit_code = 0;
    std::string output = bridge::run_command_capture(cmd, exit_code);
    if (exit_code != 0) {
        std::string clean_out = cfire::diag::humanize_cpp_compiler_output(output);
        if (const char* verb = std::getenv("CFIRE_VERBOSE"); verb && std::string_view(verb) == "1") {
            return std::unexpected(std::format("{} compilation failed:\n{}\nCommand: {}", compiler, clean_out, cmd));
        }
        return std::unexpected(std::format("compilation failed:\n{}", clean_out));
    }

    package_apple_app_bundles(cf_file, out_bin, plan);

    return plan;
}

[[nodiscard]] inline auto generate_cmake_content(const BuildPlan& plan) -> std::string {
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
    for (const auto& al : plan.auto_linked) {
        if (al.starts_with("vendor:raylib") || al.starts_with("vendor:raygui")) {
            cmake << "target_link_libraries(app PRIVATE raylib)\n";
        }
    }
    return cmake.str();
}

[[nodiscard]] inline auto eject_cmake(
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

} // namespace cfire::build
