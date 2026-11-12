#pragma once
// src/build/deps.hpp — External Dependency Fetching, Lockfile Pinning & Native Archiving

#include "manifest.hpp"

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
    // another account could pre-populate with poisoned packages (CF-153).
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

// CF-152: arguments that reach `git` / `curl` as positional values must not be parseable as options
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
        // CF-152: a cache hit is trusted only if it is exactly the locked commit with no modified tracked
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
    // CF-152: a cache hit is trusted only if its recorded archive hash matches the pin (`.cfire_sha256`
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

    cfire::detail::SecureTempDir dl_dir("cfire_pkg_dl");  // private 0700 dir, removed on return (CF-153)
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

    auto hash_res = cfire::sha256::hash_file_hex(tmp_archive);
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
    // Record the verified archive hash so later cache hits can be checked against the pin (CF-152).
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

// Compiles a local C/C++ dependency (`build.CC`, `build.CMake`, `build.Make`, or `build.Ninja`)
// into `.cfire/cache/lib/lib<name>.a` and appends its include paths and `.a` archive to `plan`.
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
    auto obj_dir = cache_root / "objs" / dep.name;
    auto lib_dir = cache_root / "lib";
    std::filesystem::create_directories(obj_dir);
    std::filesystem::create_directories(lib_dir);

    auto is_lib_file = [](const std::filesystem::path& p) -> bool {
        auto ext = p.extension().string();
        if (ext == ".a" || ext == ".dylib" || ext == ".so") return true;
        std::string fn = p.filename().string();
        return fn.find(".so.") != std::string::npos;
    };
    auto is_shared_lib = [](const std::filesystem::path& p) -> bool {
        auto ext = p.extension().string();
        if (ext == ".dylib" || ext == ".so") return true;
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
            auto make_build_dir = cache_root / "make" / dep.name;
            std::filesystem::create_directories(make_build_dir);
            std::string make_cmd = std::format("make -C \"{}\" BUILD_DIR=\"{}\"", src_dir.string(), make_build_dir.string());
            std::string extra_cflags;
            for (const auto& [k, v] : dep.defines) {
                std::string val = (v == "true") ? "1" : v;
                make_cmd += std::format(" {}={}", k, val);
                extra_cflags += std::format(" -D{}={}", k, val);
            }
            if (!extra_cflags.empty()) {
                make_cmd += std::format(" EXTRA_CFLAGS=\"{}\"", extra_cflags);
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
            auto cmake_build_dir = cache_root / "cmake" / dep.name;
            std::filesystem::create_directories(cmake_build_dir);
            std::string cfg_cmd = std::format("cmake -S \"{}\" -B \"{}\"", src_dir.string(), cmake_build_dir.string());
            for (const auto& [k, v] : dep.defines) {
                cfg_cmd += std::format(" -D{}={}", k, v);
            }
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

    bool build_shared = dep.is_shared || dep.defines.contains("shared") || dep.defines.contains("SHARED");
    std::vector<std::filesystem::path> obj_files;
    bool any_obj_recompiled = false;
    for (const auto& sf : src_files) {
        // Disambiguate identical stems across folders by hashing the relative path
        std::error_code ec;
        auto rel_p = std::filesystem::relative(sf, src_dir, ec);
        std::string rel_str = ec ? sf.string() : rel_p.string();
        std::size_t h = std::hash<std::string>{}(rel_str);
        auto obj_path = obj_dir / std::format("{}_{:08x}.o", sf.stem().string(), static_cast<std::uint32_t>(h & 0xFFFFFFFF));

        // Incremental compilation check: skip clang if object is up to date
        bool need_compile = true;
        if (std::filesystem::exists(obj_path, ec)) {
            auto src_time = std::filesystem::last_write_time(sf, ec);
            if (!ec) {
                auto obj_time = std::filesystem::last_write_time(obj_path, ec);
                if (!ec && obj_time >= src_time) {
                    need_compile = false;
                }
            }
        }

        if (need_compile) {
            any_obj_recompiled = true;
            bool is_cpp = (sf.extension() != ".c");
            std::string cc = is_cpp ? "clang++ -std=c++2b" : "clang -std=c17";
            if (!plan.target_triple.empty()) cc += " --target=" + plan.target_triple;
            if (!plan.sysroot.empty()) cc += " --sysroot=" + plan.sysroot;
            if (build_shared) cc += " -fPIC";
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

            int rc = 0;
            std::string out = bridge::run_command_capture(cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("failed to compile dependency '{}' source '{}':\n{}",
                    dep.name, sf.string(), out));
            }
        }
        obj_files.push_back(std::move(obj_path));
    }

    std::string lib_ext = build_shared ?
#if defined(__APPLE__)
        ".dylib"
#else
        ".so"
#endif
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
        std::string link_cmd;
        if (build_shared) {
            link_cmd = std::format("clang++ -shared -o \"{}\"", archive_path.string());
            if (!plan.target_triple.empty()) link_cmd += " --target=" + plan.target_triple;
            if (!plan.sysroot.empty()) link_cmd += " --sysroot=" + plan.sysroot;
            for (const auto& ob : obj_files) {
                link_cmd += std::format(" \"{}\"", ob.string());
            }
        } else {
            link_cmd = std::format("ar rcs \"{}\"", archive_path.string());
            for (const auto& ob : obj_files) {
                link_cmd += std::format(" \"{}\"", ob.string());
            }
        }
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
