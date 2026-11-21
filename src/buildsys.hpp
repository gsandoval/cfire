#pragma once
// src/buildsys.hpp — C++23 Unified Build System, Live CC/CMake Builder & Multi-Package Resolver

#include "version.hpp"
#include "build/toolchain_layout.hpp"
#include "build/manifest.hpp"
#include "build/pch.hpp"
#include "build/binary_cache.hpp"
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_BUILDSYS_IMPL)
#include "sema.hpp"
#include "codegen.hpp"
#include "build/deps.hpp"
#include "build/lib_cache.hpp"
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
    bool sanitize = false,
    const std::vector<std::string>& extra_pkg_configs = {},
    const std::vector<std::string>& extra_defines = {}
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
    bool inspect_clang_ast,
    const std::vector<std::string>& extra_link_libs = {}
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
    bool sanitize = false,
    const std::vector<std::string>& extra_link_libs = {},
    const std::vector<std::string>& extra_pkg_configs = {},
    const std::vector<std::string>& extra_includes = {},
    const std::vector<std::string>& extra_defines = {}
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
    const std::string& icon_path = "",
    const std::vector<std::string>& extra_pkg_configs = {},
    const std::vector<std::string>& extra_defines = {}
) -> std::expected<BuildPlan, std::string>;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto generate_cmake_content(const BuildPlan& plan) -> std::string;

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto eject_cmake(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_dir,
    const std::vector<std::string>& extra_pkg_configs = {},
    const std::vector<std::string>& extra_defines = {}
) -> std::expected<void, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_BUILDSYS_IMPL)

inline void sync_macos_min_flags(BuildPlan& plan) {
    auto erase_min = [](std::vector<std::string>& flags) {
        std::erase_if(flags, [](const std::string& f) {
            return f.starts_with("-mmacosx-version-min=");
        });
    };
    erase_min(plan.cflags);
    erase_min(plan.ldflags);
    if (!plan.macos_min.empty() &&
        is_macos_target(plan.target_triple, plan.is_ios, is_wasm_triple(plan.target_triple))) {
        const std::string flag = "-mmacosx-version-min=" + plan.macos_min;
        plan.cflags.push_back(flag);
        plan.ldflags.push_back(flag);
    }
}

inline void sync_ios_min_flags(BuildPlan& plan) {
    if (!plan.is_ios) return;
    plan.target_triple = normalize_ios_triple(plan.target_triple, plan.ios_min);
    const std::string resolved_cc = resolve_cxx_compiler(plan.target_triple);
    const bool use_target_flag = !compiler_is_gcc_driver(resolved_cc);
    auto replace_target = [&](std::vector<std::string>& flags) {
        std::erase_if(flags, [](const std::string& f) {
            return f.starts_with("--target=");
        });
        if (use_target_flag) {
            flags.push_back("--target=" + plan.target_triple);
        }
    };
    replace_target(plan.cflags);
    replace_target(plan.ldflags);
}

[[nodiscard]] inline auto compute_packaging_cache_fingerprint(const BuildPlan& plan) -> std::string {
    detail::Sha256State st;
    auto feed = [&](std::string_view tag, std::string_view val) {
        st.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
        const char sep1 = ':';
        st.update(reinterpret_cast<const uint8_t*>(&sep1), 1);
        st.update(reinterpret_cast<const uint8_t*>(val.data()), val.size());
        const char sep2 = '\0';
        st.update(reinterpret_cast<const uint8_t*>(&sep2), 1);
    };
    feed("ios_min", plan.ios_min);
    feed("macos_min", plan.macos_min);
    feed("version", plan.version);
    feed("build_number", plan.build_number);
    feed("app_name", plan.app_name);
    feed("bundle_id", plan.bundle_id);
    feed("sign_identity", plan.sign_identity);
    feed("provisioning_profile", plan.provisioning_profile);
    feed("entitlements", plan.entitlements);
    feed("privacy_manifest", plan.privacy_manifest);
    feed("icon_path", plan.icon_path);
    feed("emit_ipa", plan.emit_ipa ? "1" : "0");
    feed("has_res", plan.has_explicit_resources ? "1" : "0");
    feed("mac_app", plan.macos_app_bundle);
    feed("ios_app", plan.ios_app_bundle);
    feed("manifest_dir", plan.manifest_dir);
    for (const auto& s : plan.resources) feed("res", s);
    for (const auto& s : plan.url_schemes) feed("scheme", s);
    for (const auto& s : plan.ios_orientations) feed("orient", s);
    for (int fam : plan.ios_device_family) feed("family", std::to_string(fam));
    std::vector<std::pair<std::string, std::string>> sorted_plist = plan.info_plist_extras;
    std::sort(sorted_plist.begin(), sorted_plist.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [k, v] : sorted_plist) {
        feed("plist_k", k);
        feed("plist_v", v);
        feed("plist_b", plan.info_plist_bool_keys.contains(k) ? "1" : "0");
    }
    auto feed_file = [&](std::string_view tag, const std::string& p_str) {
        if (p_str.empty()) return;
        std::filesystem::path p(p_str);
        if (p.is_relative() && !plan.manifest_dir.empty()) {
            std::error_code ec;
            auto cand = std::filesystem::path(plan.manifest_dir) / p;
            if (std::filesystem::exists(cand, ec)) p = cand;
        }
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec) && !ec) {
            feed(tag, file_fingerprint_string(p));
        }
    };
    feed_file("icon_fp", plan.icon_path);
    feed_file("prof_fp", plan.provisioning_profile);
    feed_file("ent_fp", plan.entitlements);
    feed_file("priv_fp", plan.privacy_manifest);
    return st.final_hex().substr(0, 16);
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto init_build_plan_flags(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool test_mode,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::string& web_profile,
    const std::string& subsystem,
    bool sanitize,
    const std::vector<std::string>& extra_pkg_configs,
    const std::vector<std::string>& extra_defines
) -> std::expected<BuildPlan, std::string> {
    BuildPlan plan;
    plan.entry_file = cf_file.string();
    plan.extra_pkg_configs = extra_pkg_configs;
    plan.extra_defines = extra_defines;
    plan.is_ios = is_ios_target(target_triple);
    plan.is_ios_sim = is_ios_simulator_target(target_triple);
    plan.ios_min = resolve_effective_ios_min(target_triple);
    plan.target_triple = plan.is_ios ? normalize_ios_triple(target_triple, plan.ios_min) : target_triple;
    if (!g_sign_identity_override.empty()) plan.sign_identity = g_sign_identity_override;
    if (!g_provisioning_profile_override.empty()) {
        std::error_code aec;
        std::filesystem::path p(g_provisioning_profile_override);
        plan.provisioning_profile = p.is_relative() ? std::filesystem::absolute(p, aec).string() : p.string();
    }
    if (!g_entitlements_override.empty()) {
        std::error_code aec;
        std::filesystem::path p(g_entitlements_override);
        plan.entitlements = p.is_relative() ? std::filesystem::absolute(p, aec).string() : p.string();
    }
    if (g_emit_ipa_mode) plan.emit_ipa = true;
    plan.sysroot = sysroot;
    if (plan.is_ios && plan.sysroot.empty()) {
        auto sdk_res = resolve_ios_sdk_path(plan.is_ios_sim);
        if (!sdk_res) return std::unexpected(sdk_res.error());
        plan.sysroot = *sdk_res;
    }
    plan.web_profile = web_profile;
    plan.subsystem = subsystem;
    auto install_root = derive_install_root(runtime_dir);
    plan.cflags = {"-std=c++2b", "-Wno-c++11-narrowing", "-DSIMDJSON_DEVELOPMENT_CHECKS=0", "-I" + runtime_dir.string()};
    if (!install_root.empty() && install_root != "/usr") {
        std::error_code root_ec;
        if (std::filesystem::is_directory(install_root / "include", root_ec)) {
            plan.cflags.push_back("-I" + (install_root / "include").string());
        }
        if (std::filesystem::is_directory(install_root / "lib", root_ec)) {
            plan.ldflags.push_back("-L" + (install_root / "lib").string());
        }
    }
    bool is_wasm = is_wasm_triple(plan.target_triple);
    plan.portable = g_portable_mode;
    plan.static_link = g_static_mode;
    plan.universal = g_universal_mode;
    plan.macos_min = resolve_effective_macos_min(plan.target_triple, plan.is_ios, is_wasm);
#if defined(__linux__)
    if (!is_wasm && !is_windows_target(plan.target_triple)) {
        plan.cflags.push_back("-Wno-builtin-macro-redefined");
        plan.cflags.push_back("-D__cpp_concepts=202002L");
        plan.cflags.push_back("-pthread");
        plan.ldflags.push_back("-pthread");
    }
#endif
    if (!plan.target_triple.empty()) {
        if (!is_wasm) {
            const std::string resolved_cc = resolve_cxx_compiler(plan.target_triple);
            if (!compiler_is_gcc_driver(resolved_cc)) {
                plan.cflags.push_back("--target=" + plan.target_triple);
                plan.ldflags.push_back("--target=" + plan.target_triple);
            }
        }
    }
    if (!plan.sysroot.empty()) {
        plan.cflags.push_back("-isysroot");
        plan.cflags.push_back("\"" + plan.sysroot + "\"");
        plan.ldflags.push_back("-isysroot");
        plan.ldflags.push_back("\"" + plan.sysroot + "\"");
    }
    sync_macos_min_flags(plan);
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
    if (!test_mode && !is_dev_debug) {
        plan.cflags.push_back("-DCFIRE_RELEASE_BUILD=1");
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
    plan.manifest_dir = bcf_parent.empty() ? std::string(".") : bcf_parent.string();
    apply_manifest_pkg_fields(
        plan,
        bcf_parent,
        parsed->pkg_config,
        parsed->libraries,
        parsed->frameworks,
        parsed->includes,
        parsed->defines
    );
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
        if (plan.version.empty()) plan.version = matched_exe->version;
        if (plan.build_number.empty()) plan.build_number = matched_exe->build_number;
        if (!plan.has_explicit_resources && matched_exe->has_explicit_resources) {
            plan.has_explicit_resources = true;
            plan.resources = matched_exe->resources;
        }
        if (plan.url_schemes.empty()) plan.url_schemes = matched_exe->url_schemes;
        if (plan.sign_identity.empty()) plan.sign_identity = matched_exe->sign_identity;
        if (plan.provisioning_profile.empty()) plan.provisioning_profile = matched_exe->provisioning_profile;
        if (plan.entitlements.empty()) plan.entitlements = matched_exe->entitlements;
        if (plan.privacy_manifest.empty()) plan.privacy_manifest = matched_exe->privacy_manifest;
        if (plan.ios_orientations.empty()) plan.ios_orientations = matched_exe->ios_orientations;
        if (plan.ios_device_family.empty()) plan.ios_device_family = matched_exe->ios_device_family;
        auto plan_has_plist = [&](std::string_view key) {
            return std::any_of(plan.info_plist_extras.begin(), plan.info_plist_extras.end(),
                [key](const auto& kv) { return kv.first == key; });
        };
        for (const auto& [k, v] : matched_exe->info_plist_extras) {
            if (!plan_has_plist(k)) {
                plan.info_plist_extras.emplace_back(k, v);
                if (matched_exe->info_plist_bool_keys.contains(k)) {
                    plan.info_plist_bool_keys.insert(k);
                }
            }
        }
        if (!matched_exe->macos_min.empty() && !is_macos_min_supported(matched_exe->macos_min)) {
            return std::unexpected(std::format(
                "unsupported macos_min '{}' in build.cf: cfire C++23 runtime (<format>/<print>) requires macOS >= {}",
                matched_exe->macos_min, kDefaultMacosMinVersion));
        }
        if (!matched_exe->ios_min.empty() && !is_ios_min_supported(matched_exe->ios_min)) {
            return std::unexpected(std::format(
                "unsupported ios_min '{}' in build.cf: cfire iOS runtime requires iOS >= {}",
                matched_exe->ios_min, kMinSupportedIosVersion));
        }
        if (plan.is_ios) {
            plan.ios_min = resolve_effective_ios_min(plan.target_triple, matched_exe->ios_min);
            sync_ios_min_flags(plan);
        }
        if (matched_exe->portable) plan.portable = true;
        if (matched_exe->static_link) plan.static_link = true;
        if (matched_exe->universal) plan.universal = true;
        plan.macos_min = resolve_effective_macos_min(
            plan.target_triple,
            plan.is_ios,
            is_wasm,
            matched_exe->macos_min
        );
        if (plan.macos_min.empty() &&
            (plan.portable || plan.universal) &&
            is_macos_target(plan.target_triple, plan.is_ios, is_wasm)) {
            plan.macos_min = std::string(kDefaultMacosMinVersion);
        }
        sync_macos_min_flags(plan);
        if (plan.icon_path.empty() && !matched_exe->icon.empty()) {
            auto icon_cand = bcf_parent / matched_exe->icon;
            if (std::filesystem::exists(icon_cand, bcf_ec)) {
                plan.icon_path = icon_cand.string();
            } else {
                plan.icon_path = matched_exe->icon;
            }
        }
        if (test_mode || !exact_entry_match) {
            for (const auto& ex : parsed->executables) {
                apply_manifest_pkg_fields(plan, bcf_parent, ex.pkg_config, ex.libraries, ex.frameworks, ex.includes, ex.defines);
            }
        } else {
            apply_manifest_pkg_fields(
                plan, bcf_parent, matched_exe->pkg_config, matched_exe->libraries, matched_exe->frameworks, matched_exe->includes, matched_exe->defines);
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
    bool inspect_clang_ast,
    const std::vector<std::string>& extra_link_libs
) -> std::expected<void, std::string> {
    if (!extra_link_libs.empty() && plan.extra_link_libs.empty()) {
        plan.extra_link_libs = extra_link_libs;
    }
    plan.has_cpp_interop = plan.has_cpp_interop || checked.has_cpp_interop ||
                           !checked.cpp_qualifiers.empty() || !checked.vendor_aliases.empty() ||
                           std::any_of(checked.ast.imports.begin(), checked.ast.imports.end(),
                               [](const auto& imp) { return imp.is_cpp || imp.path.starts_with("vendor:"); });
    const auto& effective_extra_links = extra_link_libs.empty() ? plan.extra_link_libs : extra_link_libs;

    auto install_root = derive_install_root(runtime_dir);
    bool is_wasm = is_wasm_triple(plan.target_triple);
    bool is_win_target = is_windows_target(plan.target_triple);
    std::filesystem::path entry_p = plan.entry_file.empty()
        ? std::filesystem::path(checked.ast.path)
        : std::filesystem::path(plan.entry_file);
    auto dir = entry_p.parent_path();
    if (dir.empty()) dir = ".";
    auto cache_dir = dir / ".cfire" / "cache";

    static const std::unordered_map<std::string, std::string> core_to_pc = {
        {"ext:compress/zstd", "libzstd"},
        {"core:json", "simdjson"},
        {"ext:ai", "simdjson"},
        {"ext:tako", "simdjson"},
        {"ext:webui", "simdjson"},
    };

    const bool has_explicit_pc_env =
        (std::getenv("PKG_CONFIG_LIBDIR") != nullptr && std::getenv("PKG_CONFIG_LIBDIR")[0] != '\0') ||
        (std::getenv("PKG_CONFIG_PATH") != nullptr && std::getenv("PKG_CONFIG_PATH")[0] != '\0');

    auto has_explicit_pkg_override = [&](std::string_view pkg_name) -> bool {
        for (const auto& pc : plan.pkg_config_deps) {
            if (pc == pkg_name) return true;
        }
        for (const auto& pc : plan.extra_pkg_configs) {
            if (pc == pkg_name) return true;
        }
        return false;
    };

    auto has_dep_or_link_override = [&](std::string_view token) -> bool {
        for (const auto& dep : plan.external_deps) {
            if (dep.name.find(token) != std::string::npos ||
                dep.target.find(token) != std::string::npos ||
                dep.built_archive.find(token) != std::string::npos) {
                return true;
            }
        }
        for (const auto& lf : plan.ldflags) {
            if (lf.find(token) != std::string::npos) return true;
        }
        for (const auto& lf : effective_extra_links) {
            if (lf.find(token) != std::string::npos) return true;
        }
        return false;
    };

    auto has_local_prefix_lib = [&](std::initializer_list<const char*> headers,
                                    std::initializer_list<const char*> libs) -> bool {
        auto home_dir = user_home_dir();
        if (home_dir.empty()) return false;
        std::error_code ec;
        auto loc = home_dir / ".local";
        bool hdr_ok = false;
        for (const char* h : headers) {
            if (std::filesystem::exists(loc / "include" / h, ec)) {
                hdr_ok = true;
                break;
            }
        }
        if (!hdr_ok) return false;
        for (const char* dir_name : {"lib", "lib64"}) {
            for (const char* lib_name : libs) {
                if (std::filesystem::exists(loc / dir_name / lib_name, ec)) {
                    return true;
                }
            }
        }
        return false;
    };

    auto append_unique_cflag = [&](const std::string& flag) {
        if (std::find(plan.cflags.begin(), plan.cflags.end(), flag) == plan.cflags.end()) {
            plan.cflags.push_back(flag);
        }
    };

    auto has_sqlcipher = [&]() -> bool {
        if (is_wasm || plan.is_ios) return false;
        if (has_explicit_pkg_override("sqlcipher") || has_dep_or_link_override("sqlcipher")) {
            return true;
        }
        if (has_local_prefix_lib({"sqlcipher/sqlite3.h", "sqlite3.h"},
                                 {"libsqlcipher.a", "libsqlcipher.so", "libsqlcipher.dylib",
                                  "libsqlcipher.dll.a", "sqlcipher.lib"})) {
            return true;
        }
        if (!ignore_system_libs() || has_explicit_pc_env) {
            if (ProbeCache::instance().probe_pkg_config("sqlcipher").found) return true;
        }
        if (!ignore_system_libs()) {
            std::error_code ec;
            for (const char* inc_p : {
                "/opt/homebrew/opt/sqlcipher/include/sqlcipher/sqlite3.h",
                "/usr/local/opt/sqlcipher/include/sqlcipher/sqlite3.h",
                "/opt/homebrew/include/sqlcipher/sqlite3.h",
                "/usr/local/include/sqlcipher/sqlite3.h",
                "/usr/include/sqlcipher/sqlite3.h"
            }) {
                if (std::filesystem::exists(inc_p, ec)) return true;
            }
        }
        return false;
    };

    std::unordered_set<std::string> seen_pc(plan.pkg_config_deps.begin(), plan.pkg_config_deps.end());
    auto add_pc = [&](const std::string& pc) {
        if (seen_pc.contains(pc) &&
            std::find(plan.queried_pkg_configs.begin(), plan.queried_pkg_configs.end(), pc) != plan.queried_pkg_configs.end()) {
            return;
        }
        seen_pc.insert(pc);
        resolve_plan_pkg_configs(plan, runtime_dir, pc);
    };

    auto request_sqlite3_dep = [&]() {
        if (has_sqlcipher()) {
            add_pc("sqlcipher");
            plan.cflags.push_back("-DSQLITE_HAS_CODEC=1");
            return;
        }
        bool overridden =
            has_explicit_pkg_override("sqlite3") ||
            has_dep_or_link_override("sqlite") ||
            has_local_prefix_lib({"sqlite3.h"},
                                 {"libsqlite3.a", "libsqlite3.so", "libsqlite3.dylib",
                                  "libsqlite3.dll.a", "sqlite3.lib"});
        if (!overridden && !plan.use_vendored_sqlite3) {
            std::error_code vec;
            auto v_sqlite_dir = runtime_dir / "vendor" / "sqlite3";
            if (std::filesystem::is_regular_file(v_sqlite_dir / "sqlite3.c", vec) &&
                std::filesystem::is_regular_file(v_sqlite_dir / "sqlite3.h", vec)) {
                plan.use_vendored_sqlite3 = true;
                append_unique_cflag("-I" + v_sqlite_dir.string());
                return;
            }
        }
        add_pc("sqlite3");
    };

    auto request_simdjson_dep = [&]() {
        if (plan.use_vendored_simdjson) return;
        bool explicit_override =
            has_explicit_pkg_override("simdjson") ||
            has_dep_or_link_override("simdjson") ||
            has_local_prefix_lib({"simdjson.h"},
                                 {"libsimdjson.a", "libsimdjson_static.a", "libsimdjson.so",
                                  "libsimdjson.dylib", "libsimdjson.dll.a", "simdjson.lib"});
        if (!explicit_override) {
            std::error_code vec;
            auto v_simd_dir = runtime_dir / "vendor" / "simdjson";
            if (std::filesystem::is_regular_file(v_simd_dir / "simdjson.cpp", vec) &&
                std::filesystem::is_regular_file(v_simd_dir / "simdjson.h", vec)) {
                plan.use_vendored_simdjson = true;
                append_unique_cflag("-I" + v_simd_dir.string());
                return;
            }
        }
        add_pc("simdjson");
    };

    for (const auto& [sname, tags] : checked.struct_tags) {
        bool has_j = false;
        for (const auto& t : tags) {
            if (!t.json_key.empty()) { has_j = true; break; }
        }
        if (has_j) {
            request_simdjson_dep();
            break;
        }
    }
    for (const auto& imp : checked.ast.imports) {
        if (!imp.is_cpp) {
            plan.auto_linked.push_back(imp.path);
            if (auto it = core_to_pc.find(imp.path); it != core_to_pc.end()) {
                if (it->second == "simdjson") {
                    request_simdjson_dep();
                } else {
                    add_pc(it->second);
                }
            }
            if (imp.path == "ext:db/sqlite") {
                request_sqlite3_dep();
            }
            if (imp.path == "ext:tako") {
                if (const char* env_tako = std::getenv("TAKO_SDK_DIR")) {
                    std::error_code tec;
                    if (std::filesystem::is_directory(env_tako, tec)) {
                        plan.cflags.insert(plan.cflags.begin() + 2, "-I" + std::string(env_tako));
                    }
                }
            }
            if (imp.path == "ext:ai") {
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
            if (imp.path == "ext:audio") {
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
            if (imp.path == "ext:webui") {
                plan.is_gui = true;
                if (!is_wasm) {
                    if (plan.is_ios) {
                        plan.uses_objc = true;
                        plan.cflags.push_back("-fobjc-arc");
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "UIKit",
                            "-framework", "WebKit",
                            "-framework", "Foundation",
                            "-framework", "CoreGraphics",
                            "-framework", "SafariServices",
                            "-framework", "PhotosUI",
                            "-framework", "UniformTypeIdentifiers",
                            "-framework", "UserNotifications"
                        });
                    } else if (is_win_target) {
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-luser32", "-lgdi32", "-lshell32", "-lole32", "-luuid", "-ldwmapi", "-ladvapi32", "-lcomdlg32", "-lshlwapi"
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
            if (imp.path == "ext:crypto/sodium") {
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
            if (imp.path == "core:os") {
                if (!is_wasm) {
                    if (is_win_target) {
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-lshell32", "-lole32", "-luuid"
                        });
                    } else if (plan.is_ios) {
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-lobjc", "-framework", "Foundation", "-framework", "UIKit"
                        });
                    }
                }
            }
            if (imp.path == "core:keychain") {
                if (!is_wasm) {
                    if (is_win_target) {
                        plan.ldflags.push_back("-ladvapi32");
                    } else if (plan.is_ios) {
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "Security",
                            "-framework", "CoreFoundation",
                            "-lobjc"
                        });
                        const bool has_face_id_desc = std::any_of(
                            plan.info_plist_extras.begin(), plan.info_plist_extras.end(),
                            [](const auto& kv) { return kv.first == "NSFaceIDUsageDescription"; });
                        if (!has_face_id_desc) {
                            const std::string kc_warn =
                                "warning: core:keychain on iOS uses LocalAuthentication/Face ID; set InfoPlist = {\"NSFaceIDUsageDescription\": \"...\"} in build.cf before App Store submission";
                            if (std::find(plan.warnings.begin(), plan.warnings.end(), kc_warn) == plan.warnings.end()) {
                                plan.warnings.push_back(kc_warn);
                            }
                        }
                    } else {
#if defined(__APPLE__)
                        plan.ldflags.insert(plan.ldflags.end(), {
                            "-framework", "Security",
                            "-framework", "CoreFoundation",
                            "-lobjc"
                        });
#endif
                    }
                }
            }
            if (imp.path == "ext:net/ssh") {
                if (!is_wasm && !plan.is_ios) {
                    bool found_libssh = ProbeCache::instance().probe_pkg_config("libssh").found;
                    if (!found_libssh && std::getenv("HOME")) {
                        auto loc_inc = std::filesystem::path(std::getenv("HOME")) / ".local" / "include" / "libssh" / "libssh.h";
                        if (std::filesystem::exists(loc_inc)) found_libssh = true;
                    }
                    if (!found_libssh && std::getenv("MINGW_PREFIX")) {
                        auto mp_inc = std::filesystem::path(std::getenv("MINGW_PREFIX")) / "include" / "libssh" / "libssh.h";
                        if (std::filesystem::exists(mp_inc)) found_libssh = true;
                    }
                    if (!found_libssh) {
                        for (const auto& inc_dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include", "/clang64/include", "C:/msys64/clang64/include"}) {
                            if (std::filesystem::exists(std::filesystem::path(inc_dir) / "libssh" / "libssh.h")) {
                                found_libssh = true;
                                break;
                            }
                        }
                    }
                    if (found_libssh) {
                        add_pc("libssh");
                        plan.cflags.push_back("-DCFIRE_HAS_LIBSSH=1");
                        if (is_win_target || plan.static_link || plan.portable) {
                            if (ProbeCache::instance().probe_pkg_config("libcrypto").found) {
                                add_pc("libcrypto");
                            }
                            if (ProbeCache::instance().probe_pkg_config("zlib").found) {
                                add_pc("zlib");
                            }
                        }
                        if (is_win_target) {
                            plan.cflags.push_back("-DLIBSSH_STATIC=1");
                            plan.ldflags.insert(plan.ldflags.end(), {
                                "-liphlpapi", "-lcrypt32", "-lsecur32", "-lbcrypt",
                                "-luserenv", "-lpthread", "-luser32", "-lgdi32",
                                "-lws2_32", "-ladvapi32", "-lshell32"
                            });
                        }
                    } else if (inspect_clang_ast) {
                        const bool is_test_plan =
                            std::find(plan.cflags.begin(), plan.cflags.end(), "-DCFIRE_TEST_MODE=1") != plan.cflags.end();
                        if (!is_test_plan) {
                            return std::unexpected(
                                "ext:net/ssh requires libssh (install via 'brew install libssh', 'apt install libssh-dev', or 'pacman -S mingw-w64-clang-x86_64-libssh')");
                        }
                    }
                }
            }
            continue;
        }
        if (inspect_clang_ast && !plan.cpp_headers.contains(imp.path)) {
            std::vector<std::string> hdr_cflags = plan.cflags;
            if (!dir.empty()) {
                std::string dir_inc = "-I" + dir.string();
                if (std::find(hdr_cflags.begin(), hdr_cflags.end(), dir_inc) == hdr_cflags.end()) {
                    hdr_cflags.push_back(std::move(dir_inc));
                }
            }
            auto idx = bridge::inspect_header(imp.path, hdr_cflags, cache_dir, resolve_cxx_compiler(plan.target_triple));
            if (!idx) return std::unexpected(idx.error());
            for (const auto& [sym_name, sym] : idx->symbols) {
                if (sym.kind == "FunctionDecl") {
                    std::string qual = imp.alias.empty() ? "cpp" : imp.alias;
                    checked.cpp_fn_params[sym_name] = sym.param_types;
                    checked.cpp_fn_params[qual + "." + sym_name] = sym.param_types;
                    if (!sym.return_type.empty()) {
                        checked.cpp_fn_returns[sym_name] = sym.return_type;
                        checked.cpp_fn_returns[qual + "." + sym_name] = sym.return_type;
                    }
                }
            }
            plan.cpp_headers[imp.path] = std::move(*idx);
        }
    }

    if (inspect_clang_ast && !is_wasm && !plan.use_vendored_simdjson && seen_pc.contains("simdjson")) {
        bool has_local_simdjson = false;
        if (auto home = user_home_dir(); !home.empty()) {
            auto loc = home / ".local";
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
    frontend::ShardedEmission* sharded_out = nullptr,
    const std::vector<std::string>& extra_link_libs = {},
    const std::vector<std::string>& extra_pkg_configs = {},
    const std::vector<std::string>& extra_includes = {},
    const std::vector<std::string>& extra_defines = {}
) -> std::expected<std::pair<std::string, BuildPlan>, std::string> {
    auto dir = cf_file.parent_path();
    if (dir.empty()) dir = ".";
    auto plan_res = init_build_plan_flags(
        cf_file, runtime_dir, test_mode, target_triple, sysroot, web_profile, subsystem, sanitize, extra_pkg_configs, extra_defines);
    if (!plan_res) return std::unexpected(plan_res.error());
    BuildPlan plan = std::move(*plan_res);
    if (!extra_link_libs.empty()) {
        plan.extra_link_libs = extra_link_libs;
    }
    for (const auto& inc : extra_includes) {
        if (inc.empty()) continue;
        if (std::find(plan.extra_includes.begin(), plan.extra_includes.end(), inc) == plan.extra_includes.end()) {
            plan.extra_includes.push_back(inc);
        }
        std::string iflag = "-I" + inc;
        if (std::find(plan.cflags.begin(), plan.cflags.end(), iflag) == plan.cflags.end()) {
            plan.cflags.push_back(std::move(iflag));
        }
    }

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

    // 3. Resolve local `.cf` package imports (`import "./..."` and same-directory package siblings) recursively
    if (auto r = resolve_local_cf_imports(dir, *ast, plan, runtime_dir, test_mode); !r) {
        return std::unexpected(r.error());
    }

    // 4. Build all external dependencies (anchored to each dependency's declaring build.cf directory)
    if (auto r = build_external_deps_for_plan(dir, plan); !r) {
        return std::unexpected(r.error());
    }

    // Resolve build.cf + subpackage + --pkg-config / --define settings BEFORE Sema & header inspection
    resolve_plan_pkg_configs(plan, runtime_dir);
    if (auto dr = resolve_plan_defines(plan); !dr) {
        return std::unexpected(dr.error());
    }

    // Inspect C++ headers before Sema so Sema knows declared C/C++ symbols
    auto cache_dir = dir / ".cfire" / "cache";
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns;
    if (inspect_clang_ast) {
        for (const auto& imp : ast->imports) {
            if (imp.is_cpp) {
                std::vector<std::string> hdr_cflags = plan.cflags;
                if (!dir.empty()) {
                    std::string dir_inc = "-I" + dir.string();
                    if (std::find(hdr_cflags.begin(), hdr_cflags.end(), dir_inc) == hdr_cflags.end()) {
                        hdr_cflags.push_back(std::move(dir_inc));
                    }
                }
                auto idx = bridge::inspect_header(imp.path, hdr_cflags, cache_dir, resolve_cxx_compiler(plan.target_triple));
                if (idx) {
                    std::string qual = imp.alias.empty() ? "cpp" : imp.alias;
                    for (const auto& [sym_name, sym] : idx->symbols) {
                        if (sym.kind == "FunctionDecl") {
                            pre_cpp_fn_params[sym_name] = sym.param_types;
                            pre_cpp_fn_params[qual + "." + sym_name] = sym.param_types;
                            if (!sym.return_type.empty()) {
                                pre_cpp_fn_returns[sym_name] = sym.return_type;
                                pre_cpp_fn_returns[qual + "." + sym_name] = sym.return_type;
                            }
                        }
                    }
                    plan.cpp_headers[imp.path] = std::move(*idx);
                }
            }
        }
    }

    // 3. Semantic Analysis
    auto checked = frontend::analyze(std::move(*ast), std::move(pre_cpp_fn_params), std::move(pre_cpp_fn_returns));
    if (!checked) return std::unexpected(checked.error());
    plan.warnings = checked->warnings;

    if (auto r = populate_checked_build_plan(*checked, plan, runtime_dir, inspect_clang_ast, extra_link_libs); !r) {
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
    bool sanitize,
    const std::vector<std::string>& extra_link_libs,
    const std::vector<std::string>& extra_pkg_configs,
    const std::vector<std::string>& extra_includes,
    const std::vector<std::string>& extra_defines
) -> std::expected<std::pair<std::string, BuildPlan>, std::string> {
    return analyze_and_emit_impl(
        cf_file, runtime_dir, inspect_clang_ast, test_mode,
        target_triple, sysroot, web_profile, subsystem, sanitize, nullptr,
        extra_link_libs, extra_pkg_configs, extra_includes, extra_defines
    );
}

[[nodiscard]] inline auto apply_portability_and_static_rules(
    BuildPlan& plan,
    const std::string& compiler
) -> std::expected<void, std::string> {
    const bool is_wasm = is_wasm_triple(plan.target_triple);
    if (is_wasm) return {};
    if (!plan.portable && !plan.static_link) return {};

    const bool is_win = is_windows_target(plan.target_triple);
    const bool is_macos = is_macos_target(plan.target_triple, plan.is_ios, is_wasm);
    const bool is_apple = plan.is_ios || is_apple_triple(plan.target_triple);

    if (plan.static_link && is_apple) {
        return std::unexpected(
            "--static full binary static linking is not supported on macOS/iOS (Apple does not ship a static libSystem); use --portable to statically link third-party libraries where available");
    }

    auto add_note = [&](std::string msg) {
        if (std::find(plan.portable_notes.begin(), plan.portable_notes.end(), msg) == plan.portable_notes.end()) {
            plan.portable_notes.push_back(std::move(msg));
        }
    };

    if (plan.static_link) {
        for (const auto& dep : plan.external_deps) {
            if (dep_is_shared_build(dep) ||
                dep.built_archive.ends_with(".so") ||
                dep.built_archive.ends_with(".dylib") ||
                dep.built_archive.ends_with(".dll")) {
                return std::unexpected(std::format(
                    "--static cannot link shared-only external dependency '{}'; switch it to static or use --portable",
                    dep.name));
            }
        }
    } else if (plan.portable) {
        for (const auto& dep : plan.external_deps) {
            if (dep_is_shared_build(dep) ||
                dep.built_archive.ends_with(".so") ||
                dep.built_archive.ends_with(".dylib")) {
                add_note(std::format("external dependency '{}' is built as shared library ({})", dep.name, dep.built_archive));
            }
        }
    }

    static const std::unordered_set<std::string_view> kSystemLibs = {
        "pthread", "dl", "m", "rt", "util", "c", "mvec", "atomic",
        "stdc++", "stdc++exp", "c++", "c++abi", "gcc", "gcc_s",
        "System", "resolv", "objc", "compression",
        // Windows MinGW system DLL import libraries:
        "ws2_32", "advapi32", "shell32", "user32", "gdi32",
        "ole32", "oleaut32", "uuid", "comdlg32", "shlwapi", "dwmapi",
        "bcrypt", "crypt32", "iphlpapi", "winmm", "kernel32",
        "secur32", "ncrypt", "userenv", "netapi32",
        "mingw32", "mingwex", "msvcrt", "ucrt", "psapi", "version"
    };

    std::vector<std::filesystem::path> search_dirs;
    auto push_search_dir = [&](const std::filesystem::path& p) {
        if (p.empty()) return;
        std::error_code ec;
        if (!std::filesystem::is_directory(p, ec)) return;
        for (const auto& existing : search_dirs) {
            if (existing == p) return;
        }
        search_dirs.push_back(p);
    };
    for (std::size_t i = 0; i < plan.ldflags.size(); ++i) {
        const auto& lf = plan.ldflags[i];
        if (lf == "-L" && i + 1 < plan.ldflags.size()) {
            push_search_dir(dep_strip_quotes(plan.ldflags[++i]));
        } else if (lf.size() > 2 && lf.starts_with("-L")) {
            push_search_dir(dep_strip_quotes(lf.substr(2)));
        }
    }
    if (auto home = user_home_dir(); !home.empty()) {
        push_search_dir(home / ".local" / "lib");
        push_search_dir(home / ".local" / "lib64");
    }

    std::string expected_macos_arch;
    if (is_macos) {
        if (plan.target_triple.starts_with("arm64") || plan.target_triple.starts_with("aarch64")) {
            expected_macos_arch = "arm64";
        } else if (plan.target_triple.starts_with("x86_64")) {
            expected_macos_arch = "x86_64";
        } else {
#if defined(__aarch64__) || defined(__arm64__)
            expected_macos_arch = "arm64";
#else
            expected_macos_arch = "x86_64";
#endif
        }
    }

    auto archive_matches_target = [&](const std::filesystem::path& cand) -> bool {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(cand, ec)) return false;
#if defined(__APPLE__)
        if (is_macos && !expected_macos_arch.empty()) {
            int lrc = 0;
            std::string archs = trim_whitespace(bridge::run_command_capture(
                std::format("/usr/bin/lipo -archs {} 2>/dev/null", bridge::shell_quote(cand.string())), lrc));
            if (lrc == 0 && !archs.empty()) {
                std::string padded = " " + archs + " ";
                if (padded.find(" " + expected_macos_arch + " ") == std::string::npos) {
                    return false;
                }
            }
        }
#endif
        return true;
    };

    auto find_static_archive = [&](std::string_view lib_name) -> std::string {
        const std::string a_name = "lib" + std::string(lib_name) + ".a";
        const std::string win_lib_name = std::string(lib_name) + ".lib";
        for (const auto& dir : search_dirs) {
            auto cand = dir / a_name;
            if (archive_matches_target(cand)) return cand.string();
            if (is_win) {
                auto wcand = dir / win_lib_name;
                if (archive_matches_target(wcand)) return wcand.string();
            }
        }
        if (!ignore_system_libs() && !compiler.empty()) {
            std::string probe_cmd = compiler;
            if (!plan.target_triple.empty() && !compiler_is_gcc_driver(compiler)) {
                probe_cmd += " --target=" + plan.target_triple;
            }
            if (!plan.sysroot.empty()) {
                if (is_apple) probe_cmd += " -isysroot \"" + plan.sysroot + "\"";
                else probe_cmd += " --sysroot=\"" + plan.sysroot + "\"";
            }
            for (const auto& dir : search_dirs) {
                probe_cmd += " -L" + bridge::shell_quote(dir.string());
            }
            probe_cmd += " --print-file-name=" + bridge::shell_quote(a_name) + " 2>/dev/null";
            int prc = 0;
            std::string found = trim_whitespace(bridge::run_command_capture(probe_cmd, prc));
            if (prc == 0 && !found.empty() && found != a_name &&
                (found.find('/') != std::string::npos || found.find('\\') != std::string::npos)) {
                std::filesystem::path fp(found);
                if (archive_matches_target(fp)) return fp.string();
            }
        }
        return {};
    };

    for (std::size_t i = 0; i < plan.ldflags.size(); ++i) {
        if (i > 0 && plan.ldflags[i - 1] == "-framework") continue;
        std::string& tok = plan.ldflags[i];
        std::string unq = dep_strip_quotes(tok);
        if (unq.size() > 2 && unq.starts_with("-l")) {
            std::string lib_name = unq.substr(2);
            if (kSystemLibs.contains(lib_name)) continue;
            std::string static_path = find_static_archive(lib_name);
            if (!static_path.empty()) {
                tok = static_path;
            } else if (plan.static_link) {
                return std::unexpected(std::format(
                    "--static: could not find static archive 'lib{}.a' for '-l{}'", lib_name, lib_name));
            } else if (plan.portable) {
                add_note(std::format(
                    "library '{}' has no static archive for this target; falling back to dynamic linking", lib_name));
            }
        } else if (!unq.empty() && unq.front() != '-' &&
                   (unq.ends_with(".so") || unq.ends_with(".dylib") || unq.find(".so.") != std::string::npos)) {
            if (plan.static_link) {
                return std::unexpected(std::format(
                    "--static cannot link shared library '{}'", unq));
            } else if (plan.portable) {
                add_note(std::format("shared library '{}' linked dynamically under --portable", unq));
            }
        }
    }

    auto has_flag = [&](std::string_view needle) {
        for (const auto& lf : plan.ldflags) {
            if (lf.find(needle) != std::string::npos) return true;
        }
        return false;
    };

    if (plan.static_link && !is_apple && !is_wasm) {
        if (!has_flag("-static")) {
            plan.ldflags.push_back("-static");
        }
    } else if (plan.portable && !plan.static_link) {
        if (is_macos) {
            if (!has_flag("@executable_path")) {
                plan.ldflags.push_back("-Wl,-rpath,@executable_path");
            }
        } else if (!is_win && !is_apple) {
            if (!has_flag("$ORIGIN")) {
                plan.ldflags.push_back("-Wl,-rpath,'$ORIGIN'");
            }
        }
    }

    return {};
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
    const std::string& icon_path,
    const std::vector<std::string>& extra_pkg_configs,
    const std::vector<std::string>& extra_defines
) -> std::expected<BuildPlan, std::string> {
    static thread_local bool in_universal_slice = false;
    if (!in_universal_slice && g_universal_mode) {
        if (is_ios_target(target_triple)) {
            return std::unexpected("--universal is only supported for macOS desktop targets, not iOS");
        }
#if !defined(__APPLE__)
        return std::unexpected("--universal (macOS fat binary) requires a macOS host");
#else
        if (!target_triple.empty()) {
            return std::unexpected(
                "--universal builds both arm64 and x86_64 macOS slices automatically and cannot be combined with --target");
        }
#endif
    }

    auto run_universal_build = [&](const std::string& raw_eff_min) -> std::expected<BuildPlan, std::string> {
#if !defined(__APPLE__)
        (void)raw_eff_min;
        return std::unexpected("universal = true / --universal (macOS fat binary) requires a macOS host");
#else
        std::string eff_min = !g_macos_min_version.empty()
            ? g_macos_min_version
            : (!raw_eff_min.empty() ? raw_eff_min : std::string(kDefaultMacosMinVersion));
        if (!is_macos_min_supported(eff_min)) {
            return std::unexpected(std::format(
                "unsupported macOS deployment target '{}': requires macOS >= {}",
                eff_min, kDefaultMacosMinVersion));
        }

        auto cf_parent = cf_file.parent_path().empty() ? std::filesystem::path(".") : cf_file.parent_path();
        auto uni_dir = cf_parent / ".cfire" / "gen" / "universal";
        std::error_code uec;
        std::filesystem::create_directories(uni_dir, uec);
        auto stem = test_mode ? (cf_file.stem().string() + "_test") : cf_file.stem().string();
        auto slice_arm64 = uni_dir / (stem + "_arm64");
        auto slice_x86_64 = uni_dir / (stem + "_x86_64");

        struct UniversalSliceGuard {
            bool& flag;
            bool prev;
            explicit UniversalSliceGuard(bool& f) : flag(f), prev(f) { flag = true; }
            ~UniversalSliceGuard() { flag = prev; }
        } guard(in_universal_slice);

        auto arm_res = build_native(
            cf_file, runtime_dir, slice_arm64, test_mode, sanitize,
            "arm64-apple-macos" + eff_min, sysroot, extra_link_libs, extra_includes,
            web_profile, "console", app_name, bundle_id, icon_path, extra_pkg_configs, extra_defines
        );
        if (!arm_res) return std::unexpected(arm_res.error());

        auto x86_res = build_native(
            cf_file, runtime_dir, slice_x86_64, test_mode, sanitize,
            "x86_64-apple-macos" + eff_min, sysroot, extra_link_libs, extra_includes,
            web_profile, "console", app_name, bundle_id, icon_path, extra_pkg_configs, extra_defines
        );
        if (!x86_res) return std::unexpected(x86_res.error());

        const bool uni_gui = (subsystem == "gui") || (subsystem == "auto" && arm_res->is_gui);
        std::filesystem::path lipo_out = out_bin;
        if (uni_gui && !test_mode && out_bin.extension() == ".app") {
            lipo_out = out_bin / "Contents" / "MacOS" / out_bin.stem().string();
        }
        if (lipo_out.has_parent_path()) {
            std::filesystem::create_directories(lipo_out.parent_path(), uec);
        }
        std::string lipo_cmd = std::format(
            "/usr/bin/lipo -create {} {} -output {} 2>&1",
            bridge::shell_quote(slice_arm64.string()),
            bridge::shell_quote(slice_x86_64.string()),
            bridge::shell_quote(lipo_out.string())
        );
        int lipo_rc = 0;
        std::string lipo_out_str = bridge::run_command_capture(lipo_cmd, lipo_rc);
        if (lipo_rc != 0) {
            return std::unexpected(std::format("failed to create universal binary via lipo:\n{}", lipo_out_str));
        }

        if (g_emit_dsym) {
            int dsym_rc = 0;
            (void)bridge::run_command_capture(
                std::format("dsymutil {} 2>&1", bridge::shell_quote(lipo_out.string())),
                dsym_rc
            );
        }

        BuildPlan combined = std::move(*arm_res);
        combined.target_triple = "";
        combined.universal = true;
        combined.macos_min = eff_min;
        combined.portable = combined.portable || x86_res->portable;
        combined.static_link = combined.static_link || x86_res->static_link;
        for (const auto& n : x86_res->portable_notes) {
            if (std::find(combined.portable_notes.begin(), combined.portable_notes.end(), n) == combined.portable_notes.end()) {
                combined.portable_notes.push_back(n);
            }
        }
        combined.subsystem = subsystem;
        bool gui_subsystem = (subsystem == "gui") || (subsystem == "auto" && combined.is_gui);
        if (gui_subsystem && !test_mode) {
            std::string bin_stem = out_bin.extension() == ".app" ? out_bin.stem().string() : out_bin.filename().string();
            auto app_bundle = out_bin.extension() == ".app" ? out_bin : (out_bin.parent_path() / (bin_stem + ".app"));
            combined.macos_app_bundle = app_bundle.string();
        }
        if (auto pk = package_apple_app_bundles(cf_file, out_bin, combined); !pk) {
            return std::unexpected(pk.error());
        }
        return combined;
#endif
    };

#if defined(__APPLE__)
    if (!in_universal_slice && g_universal_mode) {
        std::string quick_min = !g_macos_min_version.empty()
            ? g_macos_min_version
            : resolve_effective_macos_min("", false, false);
        if (g_macos_min_version.empty() && cf_file.filename() != "build.cf") {
            std::error_code bec;
            auto bcf_dir = cf_file.parent_path().empty() ? std::filesystem::path(".") : cf_file.parent_path();
            auto sd = std::filesystem::weakly_canonical(bcf_dir, bec);
            if (bec || sd.empty()) sd = std::filesystem::absolute(bcf_dir, bec);
            auto cf_canon = std::filesystem::weakly_canonical(cf_file, bec);
            for (int depth = 0; depth < 8 && !sd.empty(); ++depth) {
                auto cand_bcf = sd / "build.cf";
                if (std::filesystem::exists(cand_bcf, bec)) {
                    if (auto parsed = parse_build_cf(cand_bcf); parsed && !parsed->executables.empty()) {
                        const ExecutableSpec* matched = &parsed->executables[0];
                        for (const auto& ex : parsed->executables) {
                            if (!ex.entry.empty()) {
                                auto ex_c = std::filesystem::weakly_canonical(sd / ex.entry, bec);
                                if (!bec && ex_c == cf_canon) { matched = &ex; break; }
                            }
                        }
                        if (!matched->macos_min.empty()) quick_min = matched->macos_min;
                    }
                    break;
                }
                if (sd == sd.parent_path()) break;
                sd = sd.parent_path();
            }
        }
        return run_universal_build(quick_min);
    }
#endif

    frontend::ShardedEmission sharded;
    auto res = analyze_and_emit_impl(
        cf_file, runtime_dir, true, test_mode, target_triple, sysroot, web_profile, subsystem, sanitize,
        &sharded, extra_link_libs, extra_pkg_configs, extra_includes, extra_defines
    );
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);

    const bool want_universal = !in_universal_slice && (g_universal_mode || (plan.universal && target_triple.empty()));
    if (want_universal) {
        if (plan.is_ios || is_wasm_triple(plan.target_triple)) {
            return std::unexpected("--universal is only supported for macOS desktop targets, not iOS or WebAssembly");
        }
        return run_universal_build(plan.macos_min);
    }
    if (!g_ios_min_version.empty() && !plan.is_ios) {
        return std::unexpected("--ios-min is only supported for iOS targets (--target=ios or --target=ios-sim)");
    }
    if ((plan.emit_ipa || out_bin.extension() == ".ipa") && !plan.is_ios) {
        return std::unexpected("--ipa is only supported for iOS targets (--target=ios or --target=ios-sim)");
    }
    for (const auto& w : plan.warnings) {
        std::println(stderr, "{}", w);
    }
    plan.sanitized = sanitize;
    plan.extra_link_libs = extra_link_libs;
    for (const auto& inc : extra_includes) {
        if (!inc.empty() && std::find(plan.extra_includes.begin(), plan.extra_includes.end(), inc) == plan.extra_includes.end()) {
            plan.extra_includes.push_back(inc);
        }
    }
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
        std::string c_lit;
        c_lit.reserve(plan.icon_path.size() + 4);
        for (char c : plan.icon_path) {
            if (c == '\\' || c == '"') c_lit.push_back('\\');
            c_lit.push_back(c);
        }
        plan.cflags.push_back(std::format("-DCFIRE_APP_ICON_PATH=\"{}\"", c_lit));
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
            std::string bin_stem = (out_bin.extension() == ".app" || out_bin.extension() == ".ipa")
                ? out_bin.stem().string()
                : out_bin.filename().string();
            auto app_bundle = out_bin.extension() == ".app" ? out_bin : (out_bin.parent_path() / (bin_stem + ".app"));
            plan.ios_app_bundle = app_bundle.string();
        }
    } else {
        bool gui_subsystem = (plan.subsystem == "gui") || (plan.subsystem == "auto" && plan.is_gui);
        bool is_windows = is_windows_target(target_triple);
        if (is_windows && !test_mode &&
            (!plan.icon_path.empty() || !plan.version.empty() || !plan.build_number.empty() || !plan.app_name.empty())) {
            std::string rc_app_name = !plan.app_name.empty() ? plan.app_name : out_bin.stem().string();
            auto res_obj = compile_windows_icon(
                plan.icon_path, out_bin, target_triple,
                plan.version, plan.build_number, rc_app_name);
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
                    if (plan.macos_min.empty()) {
                        plan.macos_min = resolve_effective_macos_min(plan.target_triple, plan.is_ios, is_wasm, "", true);
                        sync_macos_min_flags(plan);
                    }
                }
#endif
            }
        }
    }

    std::filesystem::path compile_out_bin = out_bin;
    if (!test_mode) {
        if (out_bin.extension() == ".ipa" && !plan.ios_app_bundle.empty()) {
            std::filesystem::path ab(plan.ios_app_bundle);
            compile_out_bin = ab / ab.stem().string();
        } else if (out_bin.extension() == ".app") {
            if (!plan.ios_app_bundle.empty()) {
                std::filesystem::path ab(plan.ios_app_bundle);
                compile_out_bin = ab / ab.stem().string();
            } else if (!plan.macos_app_bundle.empty()) {
                std::filesystem::path ab(plan.macos_app_bundle);
                compile_out_bin = ab / "Contents" / "MacOS" / ab.stem().string();
            }
        }
    }
    if (compile_out_bin.has_parent_path()) {
        std::error_code out_ec;
        std::filesystem::create_directories(compile_out_bin.parent_path(), out_ec);
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
        if (is_macos_target(plan.target_triple, plan.is_ios, is_wasm)) {
            plan.ldflags.insert(plan.ldflags.end(), {
                "-framework", "CoreFoundation",
                "-framework", "Security",
                "-framework", "SystemConfiguration",
                "-lresolv"
            });
        }
#endif
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

    std::string compiler = is_wasm ? plan.emscripten_compiler : resolve_cxx_compiler(plan.target_triple);

    if (plan.use_vendored_sqlite3) {
        std::erase_if(plan.ldflags, [](const std::string& f) { return f == "-lsqlite3"; });
        auto sq_ar = ensure_vendored_sqlite3_archive(runtime_dir, plan);
        if (!sq_ar) return std::unexpected(sq_ar.error());
        plan.ldflags.push_back(sq_ar->string());
        if (!is_wasm && !plan.is_ios && !is_windows_target(plan.target_triple) && !is_apple_triple(plan.target_triple)) {
            plan.ldflags.push_back("-lpthread");
            plan.ldflags.push_back("-ldl");
            plan.ldflags.push_back("-lm");
        }
    }
    if (plan.use_vendored_simdjson) {
        std::erase_if(plan.ldflags, [](const std::string& f) { return f == "-lsimdjson"; });
        auto sj_ar = ensure_vendored_simdjson_archive(runtime_dir, plan);
        if (!sj_ar) return std::unexpected(sj_ar.error());
        plan.ldflags.push_back(sj_ar->string());
    }

    for (auto& l : plan.ldflags) {
        if (l == "-lsimdjson") {
            std::error_code simd_ec;
            for (const auto& d : plan.ldflags) {
                if (d.starts_with("-L")) {
                    auto p = std::filesystem::path(dep_strip_quotes(d.substr(2))) / "libsimdjson_static.a";
                    if (std::filesystem::exists(p, simd_ec)) {
                        l = p.string();
                        break;
                    }
                }
            }
        }
    }

    if (auto pr = apply_portability_and_static_rules(plan, compiler); !pr) {
        return std::unexpected(pr.error());
    }

    const bool dev_debug = is_dev_debug_profile();
    std::string opt_flags = sanitize
        ? sanitizer_opt_flags()
        : (dev_debug ? "-O0 -g" : "-O2");

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
        auto resolve_local_hdr = [&](const std::string& hdr_key) -> std::filesystem::path {
            std::filesystem::path hp = hdr_key;
            if (std::filesystem::is_regular_file(hp, hec) && !hec) return hp;
            hec.clear();
            hp = cf_parent / hdr_key;
            if (std::filesystem::is_regular_file(hp, hec) && !hec) return hp;
            hec.clear();
            for (const auto& inc_dir : plan.extra_includes) {
                if (inc_dir.empty()) continue;
                hp = std::filesystem::path(inc_dir) / hdr_key;
                if (std::filesystem::is_regular_file(hp, hec) && !hec) return hp;
                hec.clear();
            }
            for (const auto& cf_flag : plan.cflags) {
                if (!cf_flag.starts_with("-I") || cf_flag.size() <= 2) continue;
                std::string inc_dir = dep_strip_quotes(cf_flag.substr(2));
                if (inc_dir.empty()) continue;
                hp = std::filesystem::path(inc_dir) / hdr_key;
                if (std::filesystem::is_regular_file(hp, hec) && !hec) return hp;
                hec.clear();
            }
            return {};
        };
        for (const auto& [hdr_key, _] : plan.cpp_headers) {
            std::filesystem::path hp = resolve_local_hdr(hdr_key);
            if (!hp.empty()) {
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
                            !plan.has_impl_define &&
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
            feed("schema", "shard-obj-v1");
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
        detail::Sha256State st;
        auto feed = [&](std::string_view tag, std::string_view val) {
            st.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
            const char sep1 = ':';
            st.update(reinterpret_cast<const uint8_t*>(&sep1), 1);
            st.update(reinterpret_cast<const uint8_t*>(val.data()), val.size());
            const char sep2 = '\0';
            st.update(reinterpret_cast<const uint8_t*>(&sep2), 1);
        };
        feed("schema", "pch-flags-v1");
        feed("compiler", compiler_fp);
        feed("opt", opt_flags);
        feed("target", plan.target_triple);
        for (const auto& cf_flag : plan.cflags) {
            feed("cf", cf_flag);
        }
        feed("rt", rt_fp);
        feed("cpp_hdr", local_cpp_hdr_fp);
        feed("cpp_name", cpp_path.filename().string());
        feed("cpp_code", cpp_code);
        std::string h16 = st.final_hex().substr(0, 16);
        obj_path = obj_dir / std::format("{}_{}.o", stem, h16);
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

    const bool allow_bin_cache = !g_no_binary_cache && !is_wasm && !(plan.is_ios && plan.is_gui) && (compile_out_bin == out_bin);
    std::string bin_cache_key;
    if (allow_bin_cache) {
        std::string extra_fp = std::format(
            "objc={}|ios={}|sanitize={}|test={}|dsym={}|dev={}|sharded={}|portable={}|static={}|universal={}|mmin={}|vsq={}|vsj={}|pkg={}",
            plan.uses_objc ? 1 : 0,
            plan.is_ios ? 1 : 0,
            sanitize ? 1 : 0,
            test_mode ? 1 : 0,
            g_emit_dsym ? 1 : 0,
            dev_debug ? 1 : 0,
            will_shard ? 1 : 0,
            plan.portable ? 1 : 0,
            plan.static_link ? 1 : 0,
            plan.universal ? 1 : 0,
            plan.macos_min,
            plan.use_vendored_sqlite3 ? 1 : 0,
            plan.use_vendored_simdjson ? 1 : 0,
            compute_packaging_cache_fingerprint(plan)
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
            if (BinaryCache::try_restore(bin_cache_key, compile_out_bin, &cached_meta)) {
                plan.used_pch = cached_meta.used_pch;
                if (will_shard) {
                    plan.sharded_recompiled_units = 0;
                }
#if defined(__APPLE__)
                if (g_emit_dsym && ((will_shard && dev_debug) || (!will_shard && use_persistent_obj)) && !plan.is_ios) {
                    auto dsym_path = std::filesystem::path(compile_out_bin.string() + ".dSYM");
                    std::error_code dec;
                    if (!std::filesystem::exists(dsym_path, dec)) {
                        int dsym_rc = 0;
                        (void)bridge::run_command_capture(
                            std::format("dsymutil {} 2>&1", bridge::shell_quote(compile_out_bin.string())),
                            dsym_rc
                        );
                    }
                }
#endif
                if (auto pk = package_apple_app_bundles(cf_file, out_bin, plan); !pk) {
                    return std::unexpected(pk.error());
                }
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
        bool has_cf_parent_inc = false;
        const std::string cf_parent_inc = "-I" + cf_parent.string();
        for (const auto& f : plan.cflags) {
            if (f == cf_parent_inc) has_cf_parent_inc = true;
            if (f.starts_with("-I") && f.size() > 2 && f[2] != '"' && f[2] != ' ') {
                out_cmd += " -I" + bridge::shell_quote(f.substr(2));
            } else if (f.starts_with("-DCFIRE_APP_ICON_PATH=")) {
                out_cmd += " " + bridge::shell_quote(f);
            } else {
                out_cmd += " " + f;
            }
        }
        if (!plan.cpp_headers.empty() && !cf_parent.empty() && !has_cf_parent_inc) {
            out_cmd += " -I" + bridge::shell_quote(cf_parent.string());
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
            if (!l.empty() && l.front() != '-' && l.front() != '"' && l.front() != '\'' &&
                l.find(' ') != std::string::npos) {
                out_cmd += " \"" + l + "\"";
            } else {
                out_cmd += " " + l;
            }
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

    auto stage_compiled_obj = [](
        const std::filesystem::path& tmp_obj,
        const std::filesystem::path& target_obj,
        std::string& err_msg
    ) -> bool {
        std::error_code fec;
        std::filesystem::rename(tmp_obj, target_obj, fec);
        if (!fec) return true;
        fec.clear();
        std::filesystem::copy_file(tmp_obj, target_obj, std::filesystem::copy_options::overwrite_existing, fec);
        std::error_code rm_ec;
        std::filesystem::remove(tmp_obj, rm_ec);
        if (!fec) return true;
        err_msg = "failed to stage compiled object file: " + target_obj.string() + " (" + fec.message() + ")";
        return false;
    };

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

        auto compile_shard_indices = [&](const std::vector<std::size_t>& indices, bool allow_pch, bool allow_per_unit_fallback) {
            if (indices.empty()) return;
            const std::size_t worker_count = std::min<std::size_t>(
                indices.size(),
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
                    if (j >= indices.size()) break;
                    const std::size_t u = indices[j];
                    const auto& target_obj = unit_obj_paths[u];
                    const uint64_t tmp_id = obj_tmp_seq.fetch_add(1, std::memory_order_relaxed);
                    const auto tmp_obj = std::filesystem::path(std::format("{}.tmp.{}.{}.o", target_obj.string(), current_pid(), tmp_id));

                    std::string unit_cmd = make_unit_compile_cmd(u, tmp_obj, allow_pch);
                    int rc = 0;
                    std::string out_txt = bridge::run_command_capture(unit_cmd, rc);
                    if (rc != 0 && allow_pch && allow_per_unit_fallback && !pch_path.empty()) {
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

                    if (rc == 0) {
                        std::string stage_err;
                        if (!stage_compiled_obj(tmp_obj, target_obj, stage_err)) {
                            rc = 1;
                            out_txt = std::move(stage_err);
                        }
                    } else {
                        std::error_code rm_ec;
                        std::filesystem::remove(tmp_obj, rm_ec);
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

            if (pch_fell_back.load(std::memory_order_relaxed) || !allow_pch) {
                plan.used_pch = false;
            }
        };

        if (!dirty_indices.empty()) {
            compile_shard_indices(dirty_indices, true, true);

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
            link_cmd += std::format(" -o \"{}\"", compile_out_bin.string());
            build_link_ldflags(link_cmd);
            int link_rc = 0;
            std::string link_out = bridge::run_command_capture(link_cmd, link_rc);
            if (link_rc != 0 && !pch_path.empty()) {
                if (is_verbose_build()) {
                    std::println(stderr, "note: link failed after PCH compilation; recompiling shards without PCH");
                }
                std::vector<std::size_t> all_indices(sharded.units.size());
                for (std::size_t u = 0; u < sharded.units.size(); ++u) {
                    all_indices[u] = u;
                }
                compile_shard_indices(all_indices, false, false);
                if (exit_code == 0) {
                    int retry_rc = 0;
                    std::string retry_out = bridge::run_command_capture(link_cmd, retry_rc);
                    if (retry_rc == 0) {
                        exit_code = 0;
                        output.clear();
                        plan.used_pch = false;
                        cmd = std::move(link_cmd);
                    } else {
                        exit_code = retry_rc;
                        output = std::move(retry_out);
                        cmd = std::move(link_cmd);
                    }
                }
            } else if (link_rc != 0) {
                exit_code = link_rc;
                output = std::move(link_out);
                cmd = std::move(link_cmd);
            }
        }
    } else {
        auto compile_single_obj = [&](bool with_pch) {
            static std::atomic<uint64_t> s_single_obj_seq{0};
            std::filesystem::path single_compile_target = compile_out_bin;
            std::filesystem::path tmp_single_obj;
            if (use_persistent_obj) {
                tmp_single_obj = std::filesystem::path(std::format(
                    "{}.tmp.{}.{}.o", obj_path.string(), current_pid(),
                    s_single_obj_seq.fetch_add(1, std::memory_order_relaxed)));
                single_compile_target = tmp_single_obj;
            }
            auto make_compile_cmd = [&](bool use_pch) {
                std::string c = compiler;
                build_compile_cflags(c);
                if (use_pch && !pch_path.empty()) {
                    c += std::format(" -include-pch \"{}\"{}", pch_path, pch_static_flag());
                }
                if (use_persistent_obj) {
                    c += std::format(" {} -c \"{}\" -o \"{}\"", opt_flags, cpp_path.string(), single_compile_target.string());
                } else {
                    c += std::format(" {} \"{}\" -o \"{}\"", opt_flags, cpp_path.string(), compile_out_bin.string());
                    build_link_ldflags(c);
                }
                return c;
            };

            cmd = make_compile_cmd(with_pch);
            output = bridge::run_command_capture(cmd, exit_code);
            if (exit_code != 0 && with_pch && !pch_path.empty()) {
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
            if (use_persistent_obj) {
                if (exit_code == 0) {
                    std::string stage_err;
                    if (!stage_compiled_obj(tmp_single_obj, obj_path, stage_err)) {
                        exit_code = 1;
                        output = std::move(stage_err);
                    }
                } else {
                    std::error_code fec;
                    std::filesystem::remove(tmp_single_obj, fec);
                }
            }
        };

        std::error_code oec;
        const bool obj_cache_hit = use_persistent_obj &&
            std::filesystem::is_regular_file(obj_path, oec) && !oec &&
            std::filesystem::file_size(obj_path, oec) > 0 && !oec;
        if (obj_cache_hit) {
            exit_code = 0;
        } else {
            plan.used_pch = !pch_path.empty();
            compile_single_obj(true);
        }

        if (exit_code == 0 && use_persistent_obj) {
            std::string link_cmd = compiler;
            append_link_relevant_cflags(link_cmd);
            if (sanitize) {
                link_cmd += " " + sanitizer_opt_flags();
            }
            link_cmd += std::format(" \"{}\" -o \"{}\"", obj_path.string(), compile_out_bin.string());
            build_link_ldflags(link_cmd);
            int link_rc = 0;
            std::string link_out = bridge::run_command_capture(link_cmd, link_rc);
            if (link_rc != 0 && !pch_path.empty()) {
                if (is_verbose_build()) {
                    std::println(stderr, "note: link failed after PCH compilation; recompiling object without PCH");
                }
                compile_single_obj(false);
                if (exit_code == 0) {
                    int retry_rc = 0;
                    std::string retry_out = bridge::run_command_capture(link_cmd, retry_rc);
                    if (retry_rc == 0) {
                        exit_code = 0;
                        output.clear();
                        plan.used_pch = false;
                        cmd = std::move(link_cmd);
                    } else {
                        exit_code = retry_rc;
                        output = output.empty() ? retry_out : (output + "\n" + retry_out);
                        cmd = std::move(link_cmd);
                    }
                }
            } else if (link_rc != 0) {
                exit_code = link_rc;
                output = output.empty() ? link_out : (output + "\n" + link_out);
                cmd = std::move(link_cmd);
            }
        }
    }

    if (exit_code != 0) {
        std::string clean_out = cfire::diag::format_backend_failure(output, plan.has_cpp_interop, is_verbose_build());
        if (is_verbose_build()) {
            return std::unexpected(std::format("{} compilation failed:\n{}\nCommand: {}", compiler, clean_out, cmd));
        }
        return std::unexpected(std::format("compilation failed:\n{}", clean_out));
    }

#if defined(__APPLE__)
    if (g_emit_dsym && ((will_shard && dev_debug) || (!will_shard && use_persistent_obj)) && !plan.is_ios) {
        int dsym_rc = 0;
        (void)bridge::run_command_capture(
            std::format("dsymutil {} 2>&1", bridge::shell_quote(compile_out_bin.string())),
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
        BinaryCache::store(bin_cache_key, compile_out_bin, store_meta);
    }

    if (auto pk = package_apple_app_bundles(cf_file, out_bin, plan); !pk) {
        return std::unexpected(pk.error());
    }

    return plan;
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto generate_cmake_content(const BuildPlan& plan) -> std::string {
    std::ostringstream cmake;
    const char* langs = plan.uses_objc ? "C CXX OBJCXX" : "C CXX";
    const char* main_src = plan.uses_objc ? "main.mm" : "main.cpp";
    cmake << "cmake_minimum_required(VERSION 3.25)\n";
    cmake << std::format("project(cfire_ejected_app LANGUAGES {})\n", langs);
    cmake << "set(CMAKE_CXX_STANDARD 23)\n";
    cmake << "set(CMAKE_CXX_STANDARD_REQUIRED OFF)\n\n";
    cmake << "include_directories(\"${CMAKE_CURRENT_SOURCE_DIR}\")\n\n";
    cmake << "find_package(Threads REQUIRED)\n\n";
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
    if (plan.use_vendored_sqlite3) {
        cmake << "add_library(cfire_sqlite3 STATIC \"${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3/sqlite3.c\")\n";
        cmake << "target_include_directories(cfire_sqlite3 PUBLIC \"${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3\")\n";
        cmake << "target_compile_definitions(cfire_sqlite3 PRIVATE SQLITE_DQS=0 SQLITE_ENABLE_FTS5=1 SQLITE_ENABLE_JSON1=1 SQLITE_ENABLE_RTREE=1 SQLITE_ENABLE_MATH_FUNCTIONS=1)\n\n";
    }
    if (plan.use_vendored_simdjson) {
        cmake << "add_library(cfire_simdjson STATIC \"${CMAKE_CURRENT_SOURCE_DIR}/vendor/simdjson/simdjson.cpp\")\n";
        cmake << "target_include_directories(cfire_simdjson PUBLIC \"${CMAKE_CURRENT_SOURCE_DIR}/vendor/simdjson\")\n";
        cmake << "target_compile_definitions(cfire_simdjson PUBLIC SIMDJSON_DEVELOPMENT_CHECKS=0)\n\n";
    }
    cmake << std::format("add_executable(app {})\n", main_src);
    cmake << "if (CMAKE_CXX_COMPILER_ID MATCHES \"Clang|AppleClang\")\n";
    cmake << "    target_compile_options(app PRIVATE -std=c++2b";
    if (plan.uses_objc) cmake << " -fobjc-arc";
    cmake << ")\n";
    cmake << "endif()\n";
    cmake << "target_include_directories(app PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}\")\n";
    cmake << "target_compile_definitions(app PRIVATE SIMDJSON_DEVELOPMENT_CHECKS=0)\n";
    for (const auto& cf_flag : plan.cflags) {
        if (cf_flag == "-DSIMDJSON_DEVELOPMENT_CHECKS=0" ||
            cf_flag.starts_with("-DCFIRE_APP_ICON_PATH")) {
            continue;
        }
        if (cf_flag == "-DCFIRE_RELEASE_BUILD=1") {
            cmake << "target_compile_definitions(app PRIVATE \"$<$<NOT:$<CONFIG:Debug>>:CFIRE_RELEASE_BUILD=1>\")\n";
            continue;
        }
        if (cf_flag.starts_with("-D") && cf_flag.size() > 2) {
            std::string def = dep_strip_quotes(cf_flag.substr(2));
            if (def.find('"') == std::string::npos && def.find('\\') == std::string::npos) {
                cmake << std::format("target_compile_definitions(app PRIVATE \"{}\")\n", def);
            }
        }
    }
    cmake << "target_link_libraries(app PRIVATE Threads::Threads)\n";
    cmake << "if (UNIX AND NOT APPLE)\n";
    cmake << "    target_link_libraries(app PRIVATE ${CMAKE_DL_LIBS} m)\n";
    cmake << "endif()\n";
    for (std::size_t i = 0; i < plan.pkg_config_deps.size(); ++i) {
        cmake << std::format("target_link_libraries(app PRIVATE PkgConfig::DEP_{})\n", i);
    }
    if (plan.use_vendored_sqlite3) {
        cmake << "target_link_libraries(app PRIVATE cfire_sqlite3)\n";
    }
    if (plan.use_vendored_simdjson) {
        cmake << "target_link_libraries(app PRIVATE cfire_simdjson)\n";
    }
    for (const auto& dep : plan.external_deps) {
        cmake << std::format("target_link_libraries(app PRIVATE {})\n", dep.name);
    }
    for (std::size_t i = 0; i < plan.ldflags.size(); ++i) {
        std::string flag = dep_strip_quotes(plan.ldflags[i]);
        if (flag.find("libcfire_sqlite3.a") != std::string::npos ||
            flag.find("libcfire_simdjson.a") != std::string::npos ||
            flag == "-pthread" || flag == "-lpthread" || flag == "-ldl" || flag == "-lm") {
            continue;
        }
        if (flag == "-isysroot") {
            if (i + 1 < plan.ldflags.size()) ++i;
            continue;
        }
        if (flag == "-framework") {
            if (i + 1 < plan.ldflags.size()) {
                cmake << std::format("target_link_libraries(app PRIVATE \"-Wl,-framework,{}\")\n",
                                     dep_strip_quotes(plan.ldflags[++i]));
            }
            continue;
        }
        cmake << std::format("target_link_libraries(app PRIVATE \"{}\")\n", flag);
    }
    return cmake.str();
}

[[nodiscard]] CFIRE_BUILDSYS_INLINE auto eject_cmake(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_dir,
    const std::vector<std::string>& extra_pkg_configs,
    const std::vector<std::string>& extra_defines
) -> std::expected<void, std::string> {
    auto res = analyze_and_emit(
        cf_file, runtime_dir, true, false, "", "", "auto", "auto", false, {}, extra_pkg_configs, {}, extra_defines);
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);

    std::filesystem::create_directories(out_dir);
    {
        const char* main_src = plan.uses_objc ? "main.mm" : "main.cpp";
        std::ofstream out(out_dir / main_src, std::ios::binary | std::ios::trunc);
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

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_BUILDSYS_IMPL)
#undef CFIRE_BUILDSYS_INLINE

} // namespace cfire::build
