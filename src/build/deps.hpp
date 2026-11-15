#pragma once
// src/build/deps.hpp — External Dependency Fetching, Lockfile Pinning & Native Archiving

#include "manifest.hpp"
#include "pch.hpp"
#include "toolchain_config.hpp"

namespace cfire::build {

inline auto get_package_cache_root() -> std::filesystem::path {
    if (const char* env = std::getenv("CFIRE_CACHE_DIR")) {
        auto p = std::filesystem::path(env) / "pkg";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }
    if (const char* home = std::getenv("HOME")) {
        auto xdg_pkg = std::filesystem::path(home) / ".cache" / "cfire" / "pkg";
        if (std::filesystem::exists(xdg_pkg)) {
            return xdg_pkg;
        }
        auto home_pkg = std::filesystem::path(home) / ".cfire" / "cache" / "pkg";
        std::error_code ec;
        std::filesystem::create_directories(home_pkg, ec);
        if (!ec) {
            auto probe = home_pkg / ".write_probe";
            std::ofstream probe_file(probe);
            if (probe_file.good()) {
                probe_file.close();
                std::filesystem::remove(probe, ec);
                return home_pkg;
            }
        }
    }
    // No writable per-user location: use a private per-process cache, never a shared temp-dir name that
    // another account could pre-populate with poisoned packages.
    if (auto user = cfire::detail::user_cache_dir("pkg"); !user.empty()) return user;
    static cfire::detail::SecureTempDir fallback("cfire_pkg_cache");
    return fallback.path();
}

inline auto read_build_lock(const std::filesystem::path& lock_file) -> std::unordered_map<std::string, LockedPackage> {
    std::unordered_map<std::string, LockedPackage> out;
    auto txt = read_file_text(lock_file);
    if (!txt) return out;
    std::string content = *txt;

    std::size_t pos = content.find("\"packages\"");
    if (pos == std::string::npos) return out;

    std::size_t cur = pos;
    while ((cur = content.find('"', cur + 1)) != std::string::npos) {
        auto end_q = content.find('"', cur + 1);
        if (end_q == std::string::npos) break;
        std::string key = content.substr(cur + 1, end_q - cur - 1);
        cur = end_q + 1;
        while (cur < content.size() && (content[cur] == ' ' || content[cur] == ':' || content[cur] == '\n' || content[cur] == '\r')) ++cur;
        if (cur < content.size() && content[cur] == '{') {
            auto end_brace = content.find('}', cur);
            if (end_brace == std::string::npos) break;
            std::string block = content.substr(cur, end_brace - cur + 1);
            cur = end_brace + 1;

            auto extract_field = [&](std::string_view fld) -> std::string {
                std::string target = std::format("\"{}\"", fld);
                auto fp = block.find(target);
                if (fp == std::string::npos) return "";
                auto colon = block.find(':', fp);
                if (colon == std::string::npos) return "";
                auto q1 = block.find('"', colon);
                if (q1 == std::string::npos) return "";
                auto q2 = block.find('"', q1 + 1);
                if (q2 == std::string::npos) return "";
                return block.substr(q1 + 1, q2 - q1 - 1);
            };

            LockedPackage pkg;
            pkg.name = key;
            pkg.url = extract_field("url");
            pkg.ref = extract_field("ref");
            pkg.commit = extract_field("commit");
            pkg.sha256 = extract_field("sha256");
            pkg.driver = extract_field("driver");
            out[key] = std::move(pkg);
        }
    }
    return out;
}

inline auto write_build_lock(
    const std::filesystem::path& lock_file,
    const std::unordered_map<std::string, LockedPackage>& pkgs
) -> bool {
    std::string json = "{\n  \"version\": 1,\n  \"packages\": {\n";
    bool first = true;
    for (const auto& [name, pkg] : pkgs) {
        if (!first) json += ",\n";
        first = false;
        json += std::format("    \"{}\": {{\n", name);
        json += std::format("      \"url\": \"{}\",\n", pkg.url);
        if (!pkg.ref.empty()) json += std::format("      \"ref\": \"{}\",\n", pkg.ref);
        if (!pkg.commit.empty()) json += std::format("      \"commit\": \"{}\",\n", pkg.commit);
        if (!pkg.sha256.empty()) json += std::format("      \"sha256\": \"{}\",\n", pkg.sha256);
        json += std::format("      \"driver\": \"{}\"\n", pkg.driver);
        json += "    }";
    }
    json += "\n  }\n}\n";

    std::ofstream out(lock_file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << json;
    return true;
}

// arguments that reach `git` / `curl` as positional values must not be parseable as options
// (`--upload-pack=...`), and git's command-running transports (`ext::`, `fd::`) are never allowed.
[[nodiscard]] inline auto validate_fetch_arg(std::string_view what, std::string_view v) -> std::expected<void, std::string> {
    if (v.starts_with("-")) {
        return std::unexpected(std::format("cfire/INVALID-DEPENDENCY: {} '{}' must not start with '-'", what, v));
    }
    if (v.starts_with("ext::") || v.starts_with("fd::") || v.find("\n") != std::string_view::npos) {
        return std::unexpected(std::format("cfire/INVALID-DEPENDENCY: {} '{}' uses a forbidden transport", what, v));
    }
    return {};
}

inline constexpr std::string_view kGitSafe = "git -c protocol.ext.allow=never -c protocol.fd.allow=never";

inline auto fetch_git_repository(
    const std::string& url,
    const std::string& ref,
    const std::filesystem::path& dest_dir,
    std::string& resolved_commit,
    const std::string& expected_commit = ""
) -> std::expected<void, std::string> {
    if (auto v = validate_fetch_arg("git url", url); !v) return v;
    if (!ref.empty()) {
        if (auto v = validate_fetch_arg("git ref", ref); !v) return v;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest_dir.parent_path(), ec);
    const std::string q_dest = bridge::shell_quote(dest_dir.string());

    if (std::filesystem::exists(dest_dir / ".git", ec)) {
        // a cache hit is trusted only if it is exactly the locked commit with no modified tracked
        // files; otherwise it is discarded and refetched.
        int rc = 0;
        std::string rev = trim_whitespace(bridge::run_command_capture(std::format("git -C {} rev-parse HEAD", q_dest), rc));
        int dirty_rc = 0;
        (void)bridge::run_command_capture(std::format("git -C {} diff --quiet HEAD --", q_dest), dirty_rc);
        if (rc == 0 && dirty_rc == 0 && (expected_commit.empty() || rev == expected_commit)) {
            resolved_commit = rev;
            return {};
        }
    }

    std::filesystem::remove_all(dest_dir, ec);

    std::string clone_cmd;
    if (!ref.empty()) {
        clone_cmd = std::format("{} clone --depth 1 --branch {} -- {} {}",
            kGitSafe, bridge::shell_quote(ref), bridge::shell_quote(url), q_dest);
    } else {
        clone_cmd = std::format("{} clone --depth 1 -- {} {}", kGitSafe, bridge::shell_quote(url), q_dest);
    }

    int rc = 0;
    std::string out = bridge::run_command_capture(clone_cmd, rc);
    if (rc != 0) {
        std::string fallback_cmd = std::format("{} clone -- {} {}", kGitSafe, bridge::shell_quote(url), q_dest);
        out = bridge::run_command_capture(fallback_cmd, rc);
        if (rc != 0) {
            return std::unexpected(std::format("failed to clone git repository '{}':\n{}", url, out));
        }
        if (!ref.empty()) {
            std::string co_cmd = std::format("git -C {} checkout --detach {} --", q_dest, bridge::shell_quote(ref));
            out = bridge::run_command_capture(co_cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("failed to checkout ref '{}' in '{}':\n{}", ref, url, out));
            }
        }
    }

    int rev_rc = 0;
    std::string rev_out = bridge::run_command_capture(std::format("git -C {} rev-parse HEAD", q_dest), rev_rc);
    if (rev_rc == 0) {
        resolved_commit = trim_whitespace(rev_out);
    }
    if (!expected_commit.empty() && resolved_commit != expected_commit) {
        std::filesystem::remove_all(dest_dir, ec);
        return std::unexpected(std::format(
            "cfire/INTEGRITY-MISMATCH: '{}' resolved to commit {} but build.lock pins {}; update the lock deliberately with 'cfire vendor' if this is intended",
            url, resolved_commit, expected_commit));
    }
    return {};
}

inline auto fetch_archive(
    const std::string& url,
    const std::string& expected_sha256,
    const std::filesystem::path& dest_dir,
    std::string& actual_sha256
) -> std::expected<void, std::string> {
    std::error_code ec;
    std::filesystem::create_directories(dest_dir.parent_path(), ec);

    if (auto v = validate_fetch_arg("archive url", url); !v) return v;
    auto to_lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    // a cache hit is trusted only if its recorded archive hash matches the pin (`.cfire_sha256`
    // is written after a verified download); a missing or different record forces a refetch.
    const auto sidecar = dest_dir / ".cfire_sha256";
    if (std::filesystem::exists(dest_dir, ec) && !std::filesystem::is_empty(dest_dir, ec)) {
        std::string recorded;
        if (std::ifstream in(sidecar); in) std::getline(in, recorded);
        recorded = trim_whitespace(recorded);
        if (!recorded.empty() && (expected_sha256.empty() || to_lower(recorded) == to_lower(expected_sha256))) {
            actual_sha256 = recorded;
            return {};
        }
        std::filesystem::remove_all(dest_dir, ec);
    }

    cfire::detail::SecureTempDir dl_dir("cfire_pkg_dl");  // private 0700 dir, removed on return
    if (!dl_dir.ok()) return std::unexpected("failed to create a private temporary directory for the download");
    auto tmp_archive = dl_dir.file("download.archive");

    int rc = 0;
    std::string q_archive = bridge::shell_quote(tmp_archive.string());
    std::string q_dest = bridge::shell_quote(dest_dir.string());
    std::string dl_cmd = std::format("curl -fsSL --proto =https --proto-redir =https --tlsv1.2 -o {} -- {}", q_archive, bridge::shell_quote(url));
    std::string out = bridge::run_command_capture(dl_cmd, rc);
    if (rc != 0) {
        return std::unexpected(std::format("failed to download archive from '{}':\n{}", url, out));
    }

    auto hash_res = cfire::build::sha256_file_hex(tmp_archive);
    if (!hash_res) {
        return std::unexpected(hash_res.error());
    }
    actual_sha256 = *hash_res;

    if (!expected_sha256.empty() && !actual_sha256.empty() && to_lower(actual_sha256) != to_lower(expected_sha256)) {
        return std::unexpected(std::format("cfire/INTEGRITY-MISMATCH: checksum mismatch for '{}': expected {}, got {}",
            url, expected_sha256, actual_sha256));
    }

    std::filesystem::create_directories(dest_dir, ec);

    int ext_rc = 0;
    if (url.ends_with(".zip")) {
        std::string unz_cmd = std::format("unzip -q -o {} -d {}", q_archive, q_dest);
        out = bridge::run_command_capture(unz_cmd, ext_rc);
        if (ext_rc == 0) {
            std::vector<std::filesystem::directory_entry> entries;
            for (const auto& e : std::filesystem::directory_iterator(dest_dir, ec)) {
                entries.push_back(e);
            }
            if (entries.size() == 1 && entries[0].is_directory()) {
                auto single_dir = entries[0].path();
                for (const auto& inner : std::filesystem::directory_iterator(single_dir, ec)) {
                    std::filesystem::rename(inner.path(), dest_dir / inner.path().filename(), ec);
                }
                std::filesystem::remove(single_dir, ec);
            }
        }
    } else {
        std::string tar_cmd = std::format("tar -xf {} -C {} --strip-components=1 2>/dev/null || tar -xf {} -C {}",
            q_archive, q_dest, q_archive, q_dest);
        out = bridge::run_command_capture(tar_cmd, ext_rc);
    }
    if (ext_rc != 0) {
        return std::unexpected(std::format("failed to extract archive from '{}':\n{}", url, out));
    }
    // Record the verified archive hash so later cache hits can be checked against the pin.
    if (std::ofstream rec(sidecar, std::ios::trunc); rec) rec << actual_sha256 << "\n";
    return {};
}

inline auto resolve_external_dep_source(
    const std::filesystem::path& project_dir,
    ExternalDepSpec& dep
) -> std::expected<std::filesystem::path, std::string> {
    auto vendor_dir = project_dir / "vendor" / dep.name;
    if (std::filesystem::exists(vendor_dir) && std::filesystem::is_directory(vendor_dir)) {
        return vendor_dir;
    }

    if (!dep.is_remote && !dep.source.empty()) {
        auto local_p = std::filesystem::absolute(project_dir / dep.source);
        if (std::filesystem::exists(local_p) && std::filesystem::is_directory(local_p)) {
            return local_p;
        }
    }

    if (dep.is_remote || !dep.url.empty()) {
        std::string target_url = dep.url.empty() ? dep.source : dep.url;
        std::string key = target_url + "@" + dep.ref;
        std::size_t h = std::hash<std::string>{}(key);
        auto cache_dest = get_package_cache_root() / std::format("{}_{:08x}", dep.name, static_cast<std::uint32_t>(h & 0xFFFFFFFF));

        std::filesystem::path lock_file = project_dir / "build.lock";
        auto lock_entries = read_build_lock(lock_file);

        if (auto it = lock_entries.find(dep.name); it != lock_entries.end()) {
            if (!it->second.commit.empty() && dep.ref.empty()) {
                dep.ref = it->second.commit;
            }
            if (!it->second.sha256.empty() && dep.sha256.empty()) {
                dep.sha256 = it->second.sha256;
            }
        }

        std::string resolved_commit;
        std::string actual_sha256;

        if (dep.fetch_kind == "git" || dep.driver == "Git" || target_url.ends_with(".git") || !dep.ref.empty() || target_url.starts_with("git@")) {
            std::string locked_commit;
            // The pin applies while build.cf still asks for the locked ref; an intentional ref change re-pins.
            if (auto it = lock_entries.find(dep.name);
                it != lock_entries.end() && (it->second.ref == dep.ref || it->second.commit == dep.ref)) {
                locked_commit = it->second.commit;
            }
            auto fetch_res = fetch_git_repository(target_url, dep.ref, cache_dest, resolved_commit, locked_commit);
            if (!fetch_res) return std::unexpected(fetch_res.error());
        } else {
            auto fetch_res = fetch_archive(target_url, dep.sha256, cache_dest, actual_sha256);
            if (!fetch_res) return std::unexpected(fetch_res.error());
        }

        lock_entries[dep.name] = LockedPackage{
            .name = dep.name,
            .url = target_url,
            .ref = dep.ref,
            .commit = resolved_commit,
            .sha256 = !dep.sha256.empty() ? dep.sha256 : actual_sha256,
            .driver = dep.driver
        };
        (void)write_build_lock(lock_file, lock_entries);

        return cache_dest;
    }

    return std::filesystem::absolute(project_dir / dep.source);
}

[[nodiscard]] inline auto dep_is_shared_build(const ExternalDepSpec& dep) -> bool {
    return dep.is_shared || dep.defines.contains("shared") || dep.defines.contains("SHARED");
}

[[nodiscard]] inline auto dep_strip_quotes(std::string_view sv) -> std::string {
    if (sv.size() >= 2 &&
        ((sv.front() == '"' && sv.back() == '"') || (sv.front() == '\'' && sv.back() == '\''))) {
        return std::string(sv.substr(1, sv.size() - 2));
    }
    return std::string(sv);
}

[[nodiscard]] inline auto dep_effective_macos_min(const BuildPlan& plan) -> std::string {
    const bool is_wasm = is_wasm_triple(plan.target_triple);
    if (!is_macos_target(plan.target_triple, plan.is_ios, is_wasm)) return {};
    if (!plan.macos_min.empty()) return plan.macos_min;
    return resolve_effective_macos_min(plan.target_triple, plan.is_ios, is_wasm);
}

[[nodiscard]] inline auto compute_dep_target_cache_key(
    const ExternalDepSpec& dep,
    const BuildPlan& plan
) -> std::string {
    const bool is_wasm = is_wasm_triple(plan.target_triple);
    std::string cxx_cmd = is_wasm
        ? (plan.emscripten_compiler.empty() ? resolve_emscripten_compiler_cfg() : plan.emscripten_compiler)
        : resolve_cxx_compiler(plan.target_triple);
    std::string cc_cmd = is_wasm
        ? resolve_emscripten_c_compiler_cfg()
        : resolve_c_compiler(plan.target_triple);
    std::string ar_cmd = resolve_archiver(is_wasm ? std::string_view("wasm32-unknown-emscripten") : std::string_view(plan.target_triple));
    std::string eff_min = dep_effective_macos_min(plan);
    std::string raw = std::format(
        "dep={}|cxx={}|cc={}|ar={}|target={}|sysroot={}|min={}|shared={}",
        dep.name,
        compiler_binary_fingerprint(cxx_cmd),
        compiler_binary_fingerprint(cc_cmd),
        ar_cmd,
        plan.target_triple,
        plan.sysroot,
        eff_min,
        dep_is_shared_build(dep) ? 1 : 0
    );
    return sha256_hex(raw).substr(0, 12);
}

[[nodiscard]] inline auto build_dep_cmake_configure_cmd(
    const std::filesystem::path& src_dir,
    const std::filesystem::path& build_dir,
    const ExternalDepSpec& dep,
    const BuildPlan& plan
) -> std::string {
    const bool is_wasm = is_wasm_triple(plan.target_triple);
    const bool is_apple = plan.is_ios || is_apple_triple(plan.target_triple);
    std::string cc_cmd;
    std::string cxx_cmd;
    if (is_wasm) {
        cxx_cmd = plan.emscripten_compiler.empty() ? resolve_emscripten_compiler_cfg() : plan.emscripten_compiler;
        cc_cmd = resolve_emscripten_c_compiler_cfg();
        if (cc_cmd.empty()) cc_cmd = cxx_cmd;
    } else {
        auto cc_res = resolve_c_compiler_detailed(plan.target_triple);
        auto cxx_res = resolve_cxx_compiler_detailed(plan.target_triple);
        cc_cmd = !cc_res.executable_path.empty() ? cc_res.executable_path.string() : dep_strip_quotes(cc_res.command);
        cxx_cmd = !cxx_res.executable_path.empty() ? cxx_res.executable_path.string() : dep_strip_quotes(cxx_res.command);
    }
    auto ar_res = resolve_archiver_detailed(is_wasm ? std::string_view("wasm32-unknown-emscripten") : std::string_view(plan.target_triple));
    std::string ar_cmd = !ar_res.executable_path.empty() ? ar_res.executable_path.string() : dep_strip_quotes(ar_res.command);

    std::string cfg_cmd = std::format("cmake -S \"{}\" -B \"{}\"", src_dir.string(), build_dir.string());
    if (!cc_cmd.empty()) cfg_cmd += std::format(" \"-DCMAKE_C_COMPILER={}\"", dep_strip_quotes(cc_cmd));
    if (!cxx_cmd.empty()) cfg_cmd += std::format(" \"-DCMAKE_CXX_COMPILER={}\"", dep_strip_quotes(cxx_cmd));
    if (!ar_cmd.empty()) cfg_cmd += std::format(" \"-DCMAKE_AR={}\"", dep_strip_quotes(ar_cmd));

    std::string eff_min = dep_effective_macos_min(plan);
    if (!eff_min.empty()) {
        cfg_cmd += std::format(" \"-DCMAKE_OSX_DEPLOYMENT_TARGET={}\"", eff_min);
    }
    if (!plan.sysroot.empty()) {
        if (is_apple) {
            cfg_cmd += std::format(" \"-DCMAKE_OSX_SYSROOT={}\"", plan.sysroot);
        } else {
            cfg_cmd += std::format(" \"-DCMAKE_SYSROOT={}\"", plan.sysroot);
        }
    }
    if (!plan.target_triple.empty() && !is_wasm) {
        if (!compiler_is_gcc_driver(cxx_cmd)) {
            cfg_cmd += std::format(" \"-DCMAKE_C_COMPILER_TARGET={}\"", plan.target_triple);
            cfg_cmd += std::format(" \"-DCMAKE_CXX_COMPILER_TARGET={}\"", plan.target_triple);
        }
        if (is_apple && !plan.is_ios) {
            if (plan.target_triple.starts_with("x86_64-")) {
                cfg_cmd += " \"-DCMAKE_OSX_ARCHITECTURES=x86_64\"";
            } else if (plan.target_triple.starts_with("arm64-") || plan.target_triple.starts_with("aarch64-")) {
                cfg_cmd += " \"-DCMAKE_OSX_ARCHITECTURES=arm64\"";
            }
        }
        if (plan.is_ios) {
            cfg_cmd += " \"-DCMAKE_SYSTEM_NAME=iOS\"";
        }
    }
    for (const auto& [k, v] : dep.defines) {
        cfg_cmd += std::format(" -D{}={}", k, v);
    }
    return cfg_cmd;
}

[[nodiscard]] inline auto build_dep_source_compile_cmd(
    const std::filesystem::path& sf,
    const std::filesystem::path& src_dir,
    const std::filesystem::path& obj_path,
    const ExternalDepSpec& dep,
    const BuildPlan& plan
) -> std::string {
    const bool is_cpp = (sf.extension() != ".c");
    const bool is_wasm = is_wasm_triple(plan.target_triple);
    const bool is_win = is_windows_triple(plan.target_triple);
    const bool is_apple = plan.is_ios || is_apple_triple(plan.target_triple);
    const bool build_shared = dep_is_shared_build(dep);

    std::string cc;
    if (is_wasm) {
        if (is_cpp) {
            std::string emxx = plan.emscripten_compiler.empty() ? resolve_emscripten_compiler_cfg() : plan.emscripten_compiler;
            cc = (emxx.empty() ? std::string("em++") : emxx) + " -std=c++2b";
        } else {
            std::string emcc = resolve_emscripten_c_compiler_cfg();
            cc = (emcc.empty() ? std::string("emcc") : emcc) + " -std=c17";
        }
    } else {
        std::string tool = is_cpp ? resolve_cxx_compiler(plan.target_triple) : resolve_c_compiler(plan.target_triple);
        cc = tool + (is_cpp ? " -std=c++2b" : " -std=c17");
        if (!plan.target_triple.empty() && !compiler_is_gcc_driver(tool)) {
            cc += " --target=" + plan.target_triple;
        }
        if (!plan.sysroot.empty()) {
            if (is_apple) {
                cc += std::format(" -isysroot \"{}\"", plan.sysroot);
            } else {
                cc += std::format(" --sysroot=\"{}\"", plan.sysroot);
            }
        }
        std::string eff_min = dep_effective_macos_min(plan);
        if (!eff_min.empty()) {
            cc += " -mmacosx-version-min=" + eff_min;
        }
        if (build_shared || (!is_win && !plan.target_triple.empty())) {
            if (!is_win) cc += " -fPIC";
        }
    }

    std::string cmd = std::format("{} -O2 -c \"{}\" -o \"{}\"", cc, sf.string(), obj_path.string());
    for (const auto& inc : dep.includes) {
        auto inc_path = (inc == ".") ? src_dir : (src_dir / inc);
        cmd += std::format(" -I\"{}\"", inc_path.string());
    }
    if (dep.includes.empty()) {
        cmd += std::format(" -I\"{}\"", src_dir.string());
        if (std::filesystem::is_directory(src_dir / "include")) {
            cmd += std::format(" -I\"{}\"", (src_dir / "include").string());
        }
    }
    for (const auto& [k, v] : dep.defines) {
        if (v == "true") {
            cmd += std::format(" -D{}=1", k);
        } else if (v != "false") {
            cmd += std::format(" -D{}={}", k, v);
        }
    }
    return cmd;
}

[[nodiscard]] inline auto build_dep_archive_cmd(
    const std::vector<std::filesystem::path>& obj_files,
    const std::filesystem::path& archive_path,
    const ExternalDepSpec& dep,
    const BuildPlan& plan
) -> std::string {
    const bool build_shared = dep_is_shared_build(dep);
    const bool is_wasm = is_wasm_triple(plan.target_triple);
    const bool is_apple = plan.is_ios || is_apple_triple(plan.target_triple);
    std::string link_cmd;
    if (build_shared && !is_wasm) {
        std::string cxx = resolve_cxx_compiler(plan.target_triple);
        link_cmd = std::format("{} -shared -o \"{}\"", cxx, archive_path.string());
        if (!plan.target_triple.empty() && !compiler_is_gcc_driver(cxx)) {
            link_cmd += " --target=" + plan.target_triple;
        }
        if (!plan.sysroot.empty()) {
            if (is_apple) {
                link_cmd += std::format(" -isysroot \"{}\"", plan.sysroot);
            } else {
                link_cmd += std::format(" --sysroot=\"{}\"", plan.sysroot);
            }
        }
        std::string eff_min = dep_effective_macos_min(plan);
        if (!eff_min.empty()) {
            link_cmd += " -mmacosx-version-min=" + eff_min;
        }
    } else {
        std::string ar_cmd = resolve_archiver(
            is_wasm ? std::string_view("wasm32-unknown-emscripten") : std::string_view(plan.target_triple));
        link_cmd = std::format("{} rcs \"{}\"", ar_cmd, archive_path.string());
    }
    for (const auto& ob : obj_files) {
        link_cmd += std::format(" \"{}\"", ob.string());
    }
    return link_cmd;
}

// Compiles a local C/C++ dependency (`build.CC`, `build.CMake`, `build.Make`, or `build.Ninja`)
// into `.cfire/cache/lib/<key>/lib<name>.a` and appends its include paths and `.a` archive to `plan`.
[[nodiscard]] inline auto build_local_dep(
    const std::filesystem::path& project_dir,
    ExternalDepSpec& dep,
    BuildPlan& plan
) -> std::expected<void, std::string> {
    auto resolved_src = resolve_external_dep_source(project_dir, dep);
    if (!resolved_src) return std::unexpected(resolved_src.error());
    std::filesystem::path src_dir = *resolved_src;
    if (!std::filesystem::exists(src_dir) || !std::filesystem::is_directory(src_dir)) {
        // Remote reference without local checkout (metadata-only mode)
        return {};
    }

    // If CMakeLists.txt exists, enrich `dep` with any sources/includes/defines declared in CMakeLists.txt
    if (std::filesystem::exists(src_dir / "CMakeLists.txt")) {
        parse_cmake_manifest(src_dir / "CMakeLists.txt", dep);
    }

    // 1. Register public include directories (auto-discovering `include/` if present)
    if (dep.includes.empty()) {
        plan.cflags.push_back("-I" + src_dir.string());
        if (std::filesystem::is_directory(src_dir / "include")) {
            plan.cflags.push_back("-I" + (src_dir / "include").string());
        }
    } else {
        for (const auto& inc : dep.includes) {
            auto inc_path = (inc == ".") ? src_dir : (src_dir / inc);
            plan.cflags.push_back("-I" + inc_path.string());
        }
    }

    auto cache_root = std::filesystem::absolute(project_dir / ".cfire" / "cache");
    const std::string dep_key = compute_dep_target_cache_key(dep, plan);
    auto obj_dir = cache_root / "objs" / dep.name;
    auto lib_dir = cache_root / "lib" / dep_key;
    std::filesystem::create_directories(obj_dir);
    std::filesystem::create_directories(lib_dir);

    auto is_lib_file = [](const std::filesystem::path& p) -> bool {
        auto ext = p.extension().string();
        if (ext == ".a" || ext == ".dylib" || ext == ".so" || ext == ".dll") return true;
        std::string fn = p.filename().string();
        return fn.find(".so.") != std::string::npos;
    };
    auto is_shared_lib = [](const std::filesystem::path& p) -> bool {
        auto ext = p.extension().string();
        if (ext == ".dylib" || ext == ".so" || ext == ".dll") return true;
        std::string fn = p.filename().string();
        return fn.find(".so.") != std::string::npos;
    };
    auto register_lib = [&](const std::filesystem::path& p) {
        dep.built_archive = p.string();
        dep.is_shared = is_shared_lib(p);
        plan.ldflags.push_back(dep.built_archive);
        if (dep.is_shared) {
            plan.ldflags.push_back("-Wl,-rpath," + p.parent_path().string());
        }
    };

    // 2A. If dep.driver == "Make" and Makefile exists and `make` binary is installed, run Make build!
    if ((dep.driver == "Make" || dep.driver == "Autotools") &&
        (std::filesystem::exists(src_dir / "Makefile") || std::filesystem::exists(src_dir / "makefile"))) {
        int has_make = 0;
#if defined(_WIN32)
        (void)bridge::run_command_capture("where make", has_make);
#else
        (void)bridge::run_command_capture("which make", has_make);
#endif
        if (has_make == 0) {
            auto make_build_dir = cache_root / "make" / dep.name / dep_key;
            std::filesystem::create_directories(make_build_dir);
            const bool is_wasm_make = is_wasm_triple(plan.target_triple);
            const bool is_apple_make = plan.is_ios || is_apple_triple(plan.target_triple);
            std::string cc_bin = dep_strip_quotes(is_wasm_make
                ? resolve_emscripten_c_compiler_cfg()
                : resolve_c_compiler(plan.target_triple));
            std::string cxx_bin = dep_strip_quotes(is_wasm_make
                ? (plan.emscripten_compiler.empty() ? resolve_emscripten_compiler_cfg() : plan.emscripten_compiler)
                : resolve_cxx_compiler(plan.target_triple));
            std::string ar_bin = dep_strip_quotes(resolve_archiver(
                is_wasm_make ? std::string_view("wasm32-unknown-emscripten") : std::string_view(plan.target_triple)));

            std::string cross_cflags;
            if (!is_wasm_make) {
                if (!plan.target_triple.empty() && !compiler_is_gcc_driver(cc_bin)) {
                    cross_cflags += " --target=" + plan.target_triple;
                }
                if (is_apple_make && !plan.is_ios) {
                    if (plan.target_triple.starts_with("x86_64-")) {
                        cross_cflags += " -arch x86_64";
                    } else if (plan.target_triple.starts_with("arm64-") || plan.target_triple.starts_with("aarch64-")) {
                        cross_cflags += " -arch arm64";
                    }
                }
                if (!plan.sysroot.empty()) {
                    std::string sr = plan.sysroot;
                    cross_cflags += is_apple_make
                        ? std::format(" -isysroot {}", sr)
                        : std::format(" --sysroot={}", sr);
                }
                std::string eff_min = dep_effective_macos_min(plan);
                if (!eff_min.empty()) {
                    cross_cflags += " -mmacosx-version-min=" + eff_min;
                }
            }
            std::string make_cc = cross_cflags.empty() ? cc_bin : (cc_bin + cross_cflags);
            std::string make_cxx = cross_cflags.empty() ? cxx_bin : (cxx_bin + cross_cflags);
            std::string make_cmd = std::format(
                "make -C \"{}\" BUILD_DIR=\"{}\" CC=\"{}\" CXX=\"{}\" AR=\"{}\"",
                src_dir.string(),
                make_build_dir.string(),
                make_cc,
                make_cxx,
                ar_bin);
            std::string extra_cflags = cross_cflags;
            for (const auto& [k, v] : dep.defines) {
                std::string val = (v == "true") ? "1" : v;
                make_cmd += std::format(" {}={}", k, val);
                extra_cflags += std::format(" -D{}={}", k, val);
            }
            if (!extra_cflags.empty()) {
                make_cmd += std::format(" EXTRA_CFLAGS=\"{}\"", extra_cflags);
            }
            if (!cross_cflags.empty()) {
                make_cmd += std::format(" LDFLAGS=\"{}\"", cross_cflags);
            }
            int rc = 0;
            std::string out = bridge::run_command_capture(make_cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("Make build failed for '{}':\n{}", dep.name, out));
            }
            for (const auto& search_root : {make_build_dir, src_dir}) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(search_root)) {
                    if (entry.is_regular_file() && is_lib_file(entry.path())) {
                        register_lib(entry.path());
                        return {};
                    }
                }
            }
        }
    }

    // 2B. If dep.driver == "CMake" and CMakeLists.txt exists and `cmake` binary is installed, run CMake build!
    if (dep.driver == "CMake" && std::filesystem::exists(src_dir / "CMakeLists.txt")) {
        int has_cmake = 0;
#if defined(_WIN32)
        (void)bridge::run_command_capture("where cmake", has_cmake);
#else
        (void)bridge::run_command_capture("which cmake", has_cmake);
#endif
        if (has_cmake == 0) {
            auto cmake_build_dir = cache_root / "cmake" / dep.name / dep_key;
            std::filesystem::create_directories(cmake_build_dir);
            std::string cfg_cmd = build_dep_cmake_configure_cmd(src_dir, cmake_build_dir, dep, plan);
            int rc = 0;
            std::string out = bridge::run_command_capture(cfg_cmd + " && cmake --build \"" + cmake_build_dir.string() + "\"", rc);
            if (rc != 0) {
                return std::unexpected(std::format("CMake build failed for '{}':\n{}", dep.name, out));
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(cmake_build_dir)) {
                if (entry.is_regular_file() && is_lib_file(entry.path())) {
                    register_lib(entry.path());
                }
            }
            if (!dep.built_archive.empty()) return {};
        }
    }

    // 2C. If dep.driver == "Ninja" and build.ninja exists and `ninja` binary is installed, run Ninja build!
    if (dep.driver == "Ninja" && std::filesystem::exists(src_dir / "build.ninja")) {
        int has_ninja = 0;
#if defined(_WIN32)
        (void)bridge::run_command_capture("where ninja", has_ninja);
#else
        (void)bridge::run_command_capture("which ninja", has_ninja);
#endif
        if (has_ninja == 0) {
            int rc = 0;
            std::string out = bridge::run_command_capture(std::format("ninja -C \"{}\"", src_dir.string()), rc);
            if (rc != 0) {
                return std::unexpected(std::format("Ninja build failed for '{}':\n{}", dep.name, out));
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
                if (entry.is_regular_file() && is_lib_file(entry.path())) {
                    register_lib(entry.path());
                    return {};
                }
            }
        }
    }

    // 3. Collect source files (.c / .cpp / .cc) from `dep.sources` or `<src_dir>` / `<src_dir>/src`
    std::vector<std::filesystem::path> src_files;
    if (!dep.sources.empty()) {
        for (const auto& rel : dep.sources) {
            src_files.push_back(src_dir / rel);
        }
    } else {
        auto collect_dir = [&](const std::filesystem::path& d) {
            if (!std::filesystem::is_directory(d)) return;
            for (const auto& entry : std::filesystem::directory_iterator(d)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension();
                if (ext == ".c" || ext == ".cpp" || ext == ".cc") {
                    src_files.push_back(entry.path());
                }
            }
        };
        collect_dir(src_dir);
        collect_dir(src_dir / "src");
    }
    if (src_files.empty()) return {};

    bool has_max_hdr_mtime = false;
    std::filesystem::file_time_type max_hdr_mtime{};
    {
        std::vector<std::filesystem::path> hdr_roots;
        if (dep.includes.empty()) {
            hdr_roots.push_back(src_dir);
            if (std::filesystem::is_directory(src_dir / "include")) {
                hdr_roots.push_back(src_dir / "include");
            }
        } else {
            for (const auto& inc : dep.includes) {
                hdr_roots.push_back((inc == ".") ? src_dir : (src_dir / inc));
            }
        }
        std::error_code hec;
        for (const auto& root : hdr_roots) {
            if (!std::filesystem::is_directory(root, hec) || hec) {
                hec.clear();
                continue;
            }
            std::filesystem::recursive_directory_iterator it(
                root, std::filesystem::directory_options::skip_permission_denied, hec);
            for (; !hec && it != std::filesystem::recursive_directory_iterator(); it.increment(hec)) {
                auto fname = it->path().filename().string();
                if (it->is_directory(hec)) {
                    if (fname == ".git" || fname == ".cfire") it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(hec)) continue;
                auto ext = it->path().extension();
                if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".inc") {
                    auto mt = it->last_write_time(hec);
                    if (!hec && (!has_max_hdr_mtime || mt > max_hdr_mtime)) {
                        max_hdr_mtime = mt;
                        has_max_hdr_mtime = true;
                    }
                }
                hec.clear();
            }
        }
    }

    bool build_shared = dep_is_shared_build(dep);
    std::vector<std::filesystem::path> obj_files;
    bool any_obj_recompiled = false;
    for (const auto& sf : src_files) {
        // Disambiguate identical stems across folders and target/ABI configs via key
        std::error_code ec;
        auto rel_p = std::filesystem::relative(sf, src_dir, ec);
        std::string rel_str = ec ? sf.string() : rel_p.string();
        std::size_t h = std::hash<std::string>{}(rel_str);
        auto obj_path = obj_dir / std::format("{}_{:08x}_{}.o", sf.stem().string(), static_cast<std::uint32_t>(h & 0xFFFFFFFF), dep_key);

        // Incremental compilation check: skip compiler if object is newer than source and all headers
        bool need_compile = true;
        if (std::filesystem::exists(obj_path, ec)) {
            auto src_time = std::filesystem::last_write_time(sf, ec);
            if (!ec) {
                auto obj_time = std::filesystem::last_write_time(obj_path, ec);
                if (!ec && obj_time >= src_time && (!has_max_hdr_mtime || obj_time >= max_hdr_mtime)) {
                    need_compile = false;
                }
            }
        }

        if (need_compile) {
            any_obj_recompiled = true;
            std::string cmd = build_dep_source_compile_cmd(sf, src_dir, obj_path, dep, plan);

            int rc = 0;
            std::string out = bridge::run_command_capture(cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("failed to compile dependency '{}' source '{}':\n{}",
                    dep.name, sf.string(), out));
            }
        }
        obj_files.push_back(std::move(obj_path));
    }

    const bool is_win = is_windows_triple(plan.target_triple);
    const bool is_apple = plan.is_ios || is_apple_triple(plan.target_triple);
    std::string lib_ext = build_shared
        ? (is_win ? ".dll" : (is_apple ? ".dylib" : ".so"))
        : ".a";
    auto archive_path = lib_dir / std::format("lib{}{}", dep.name, lib_ext);
    bool need_archive = any_obj_recompiled || !std::filesystem::exists(archive_path);
    if (!need_archive) {
        std::error_code ec;
        auto ar_time = std::filesystem::last_write_time(archive_path, ec);
        if (ec) {
            need_archive = true;
        } else {
            for (const auto& ob : obj_files) {
                auto ob_time = std::filesystem::last_write_time(ob, ec);
                if (ec || ob_time > ar_time) {
                    need_archive = true;
                    break;
                }
            }
        }
    }

    if (need_archive) {
        std::filesystem::remove(archive_path);
        std::string link_cmd = build_dep_archive_cmd(obj_files, archive_path, dep, plan);
        int ar_rc = 0;
        std::string ar_out = bridge::run_command_capture(link_cmd, ar_rc);
        if (ar_rc != 0) {
            return std::unexpected(std::format("failed to produce library for dependency '{}':\n{}", dep.name, ar_out));
        }
    }

    register_lib(archive_path);
    return {};
}

[[nodiscard]] inline auto execute_pre_build_command(
    const PreBuildCommandSpec& cmd,
    const std::filesystem::path& default_wd
) -> std::expected<void, std::string> {
    std::filesystem::path wd = cmd.work_dir.empty() ? default_wd : std::filesystem::path(cmd.work_dir);
    bool needs_run = false;
    if (cmd.outputs.empty()) {
        needs_run = true;
    } else {
        std::filesystem::file_time_type min_output_time = std::filesystem::file_time_type::max();
        for (const auto& out : cmd.outputs) {
            std::filesystem::path outp = wd / out;
            if (!std::filesystem::exists(outp)) {
                needs_run = true;
                break;
            }
            std::error_code ec;
            auto t = std::filesystem::last_write_time(outp, ec);
            if (!ec && t < min_output_time) min_output_time = t;
        }
        if (!needs_run) {
            for (const auto& in : cmd.inputs) {
                std::filesystem::path inp = wd / in;
                if (std::filesystem::exists(inp)) {
                    std::error_code ec;
                    auto t = std::filesystem::last_write_time(inp, ec);
                    if (!ec && t > min_output_time) {
                        needs_run = true;
                        break;
                    }
                }
            }
        }
    }

    if (needs_run) {
        std::string cmd_str = "cd \"" + wd.string() + "\" && " + cmd.command;
        for (const auto& arg : cmd.args) {
            if (arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos || arg.find('\n') != std::string::npos) {
                std::string esc;
                for (char c : arg) {
                    if (c == '"' || c == '\\') esc.push_back('\\');
                    esc.push_back(c);
                }
                cmd_str += " \"" + esc + "\"";
            } else {
                cmd_str += " " + arg;
            }
        }
        int code = 0;
        std::string output = bridge::run_command_capture(cmd_str, code);
        if (code != 0) {
            return std::unexpected(std::format("pre-build command failed (exit code {}): {}\n{}", code, cmd_str, output));
        }
    }
    return {};
}


} // namespace cfire::build
