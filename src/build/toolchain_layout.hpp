#pragma once
// src/build/toolchain_layout.hpp — Toolchain Layout Discovery, File Helpers & pkg-config Probe

#include "../clang_bridge.hpp"
#if !defined(CFIRE_SPLIT_TUS)
#include "../codegen.hpp"
#include "../sema.hpp"
#endif
#include "../parser.hpp"
#include "cfire_prelude.hpp"
#include "probe_cache.hpp"
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(__APPLE__)
extern "C" int _NSGetExecutablePath(char* buf, std::uint32_t* bufsize);
#endif

#ifndef CFIRE_INFRA_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_INFRA_INLINE
#else
#define CFIRE_INFRA_INLINE inline
#endif
#endif

namespace cfire::build {

enum class InstallKind {
    PortableDir,      // <dir>/cfire + <dir>/runtime/ (or <dir>/bin/cfire + <dir>/runtime/)
    SystemPrefix,     // <prefix>/bin/cfire + <prefix>/share/cfire/runtime/ (or lib/include)
    UserHome,         // ~/.cfire/runtime or ~/.local/share/cfire/runtime
    EnvOverride,      // CFIRE_RUNTIME_DIR
    WorkspaceFallback // CWD ancestor walk
};

struct ToolchainLayout {
    std::filesystem::path executable_path{};
    std::filesystem::path install_root{};
    std::filesystem::path runtime_dir{};
    std::filesystem::path docs_dir{};
    std::filesystem::path pkg_dir{};
    InstallKind kind{InstallKind::PortableDir};

    [[nodiscard]] auto kind_label() const -> std::string_view {
        switch (kind) {
            case InstallKind::PortableDir:       return "Portable Directory";
            case InstallKind::SystemPrefix:      return "System FHS Prefix";
            case InstallKind::UserHome:          return "User Home";
            case InstallKind::EnvOverride:       return "CFIRE_RUNTIME_DIR Override";
            case InstallKind::WorkspaceFallback: return "Workspace Directory";
        }
        return "Portable Directory";
    }
};

[[nodiscard]] CFIRE_INFRA_INLINE auto derive_install_root(const std::filesystem::path& runtime_dir) -> std::filesystem::path;

[[nodiscard]] CFIRE_INFRA_INLINE auto derive_pkg_dir(const std::filesystem::path& runtime_dir) -> std::filesystem::path;

[[nodiscard]] CFIRE_INFRA_INLINE auto locate_toolchain_layout(const char* argv0 = nullptr) -> ToolchainLayout;

[[nodiscard]] CFIRE_INFRA_INLINE auto read_file_text(const std::filesystem::path& p) -> std::expected<std::string, std::string>;

[[nodiscard]] CFIRE_INFRA_INLINE auto split_ws(std::string_view sv) -> std::vector<std::string>;

CFIRE_INFRA_INLINE void query_pkg_config(
    const std::string& pkg,
    std::vector<std::string>& cflags,
    std::vector<std::string>& libs,
    const std::filesystem::path& install_root = {},
    bool static_link = false
);

CFIRE_INFRA_INLINE auto trim_whitespace(std::string_view s) -> std::string;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

[[nodiscard]] CFIRE_INFRA_INLINE auto derive_install_root(const std::filesystem::path& runtime_dir) -> std::filesystem::path {
    std::error_code ec;
    auto can_rt = std::filesystem::weakly_canonical(runtime_dir, ec);
    if (ec) can_rt = runtime_dir;
    auto p1 = can_rt.parent_path();
    auto p2 = p1.parent_path();
    auto p3 = p2.parent_path();
    // FHS layout: <prefix>/share/cfire/runtime or <prefix>/lib/cfire/runtime or <prefix>/include/cfire/runtime
    if (p1.filename() == "cfire" && (p2.filename() == "share" || p2.filename() == "lib" || p2.filename() == "include")) {
        return p3;
    }
    // <prefix>/include/cfire where cfire_prelude.hpp is directly inside include/cfire
    if (can_rt.filename() == "cfire" && p1.filename() == "include") {
        return p2;
    }
    // Portable layout: <root>/runtime
    return p1;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto derive_pkg_dir(const std::filesystem::path& runtime_dir) -> std::filesystem::path {
    auto root = derive_install_root(runtime_dir);
    std::error_code ec;
    if (std::filesystem::is_directory(root / "pkg", ec)) return root / "pkg";
    if (std::filesystem::is_directory(root / "share" / "cfire" / "pkg", ec)) return root / "share" / "cfire" / "pkg";
    return root / "pkg";
}

[[nodiscard]] CFIRE_INFRA_INLINE auto locate_toolchain_layout(const char* argv0) -> ToolchainLayout {
    ToolchainLayout layout;
    std::error_code ec;

    auto has_prelude = [](const std::filesystem::path& rt) -> bool {
        std::error_code e;
        return !rt.empty() && std::filesystem::exists(rt / "cfire_prelude.hpp", e);
    };

    // Resolve candidate executable paths (both raw invocation path before symlink resolution and canonical target)
    std::vector<std::filesystem::path> exe_candidates;
    auto add_exe_candidate = [&](const std::filesystem::path& p) {
        if (p.empty()) return;
        std::error_code e;
        auto abs_p = std::filesystem::absolute(p, e);
        if (!e && std::filesystem::exists(abs_p, e)) {
            if (layout.executable_path.empty()) layout.executable_path = abs_p;
            exe_candidates.push_back(abs_p);
            auto can_p = std::filesystem::canonical(abs_p, e);
            if (!e && can_p != abs_p) {
                exe_candidates.push_back(can_p);
            }
        }
    };

#if defined(__APPLE__)
    char ep[1024];
    std::uint32_t sz = sizeof(ep);
    if (_NSGetExecutablePath(ep, &sz) == 0) {
        add_exe_candidate(std::filesystem::path(ep));
    }
#elif defined(__linux__)
    char ep[1024];
    ssize_t len = ::readlink("/proc/self/exe", ep, sizeof(ep) - 1);
    if (len > 0) {
        ep[len] = '\0';
        add_exe_candidate(std::filesystem::path(ep));
    }
#endif
    if (argv0 != nullptr && argv0[0] != '\0') {
        std::filesystem::path a0(argv0);
        if (a0.has_parent_path()) {
            add_exe_candidate(a0);
        } else if (const char* path_env = std::getenv("PATH")) {
            std::string_view sv(path_env);
            std::size_t start = 0;
            while (start <= sv.size()) {
                auto colon = sv.find(':', start);
                auto dir_sv = (colon == std::string_view::npos) ? sv.substr(start) : sv.substr(start, colon - start);
                if (!dir_sv.empty()) {
                    auto cand = std::filesystem::path(dir_sv) / a0;
                    if (std::filesystem::exists(cand, ec)) {
                        add_exe_candidate(cand);
                        break;
                    }
                }
                if (colon == std::string_view::npos) break;
                start = colon + 1;
            }
        }
    }

    auto finalize = [&](const std::filesystem::path& rt, InstallKind k) -> ToolchainLayout {
        std::error_code e;
        auto can_rt = std::filesystem::weakly_canonical(rt, e);
        layout.runtime_dir = e ? rt : can_rt;
        layout.install_root = derive_install_root(layout.runtime_dir);
        layout.kind = k;
        if (std::filesystem::exists(layout.install_root / "docs", e)) {
            layout.docs_dir = layout.install_root / "docs";
        } else if (std::filesystem::exists(layout.install_root / "share" / "cfire" / "docs", e)) {
            layout.docs_dir = layout.install_root / "share" / "cfire" / "docs";
        } else {
            layout.docs_dir = layout.install_root / "docs";
        }
        layout.pkg_dir = derive_pkg_dir(layout.runtime_dir);
        return layout;
    };

    // 1. Explicit CFIRE_RUNTIME_DIR environment variable override
    if (const char* env = std::getenv("CFIRE_RUNTIME_DIR")) {
        std::filesystem::path env_rt(env);
        if (has_prelude(env_rt)) {
            return finalize(env_rt, InstallKind::EnvOverride);
        }
    }

    // 2. Executable-relative discovery (Portable Directory & System FHS Prefix layouts)
    for (const auto& exe_p : exe_candidates) {
        auto exe_dir = exe_p.parent_path();
        // 2a. Portable Directory: <exe_dir>/runtime or <exe_dir>/../runtime
        if (has_prelude(exe_dir / "runtime")) {
            return finalize(exe_dir / "runtime", InstallKind::PortableDir);
        }
        auto parent_dir = exe_dir.parent_path();
        if (!parent_dir.empty() && has_prelude(parent_dir / "runtime")) {
            return finalize(parent_dir / "runtime", InstallKind::PortableDir);
        }
        // 2b. System / Prefix FHS: <prefix>/bin/cfire -> <prefix>/share/cfire/runtime, lib/cfire/runtime, include/cfire
        if (!parent_dir.empty()) {
            for (const auto& rel : {
                parent_dir / "share" / "cfire" / "runtime",
                parent_dir / "lib" / "cfire" / "runtime",
                parent_dir / "include" / "cfire" / "runtime",
                parent_dir / "include" / "cfire"
            }) {
                if (has_prelude(rel)) {
                    return finalize(rel, InstallKind::SystemPrefix);
                }
            }
        }
        // 2c. Ancestor walk from exe_dir (for binaries inside nested build directories)
        for (auto d = exe_dir; !d.empty() && d != d.parent_path(); d = d.parent_path()) {
            if (has_prelude(d / "runtime")) {
                return finalize(d / "runtime", InstallKind::PortableDir);
            }
        }
    }

    // 3. Compile-time configured prefix fallback (CFIRE_INSTALL_PREFIX)
#if defined(CFIRE_INSTALL_PREFIX)
    {
        std::filesystem::path compiled_prefix(CFIRE_INSTALL_PREFIX);
        if (has_prelude(compiled_prefix / "share" / "cfire" / "runtime")) {
            return finalize(compiled_prefix / "share" / "cfire" / "runtime", InstallKind::SystemPrefix);
        }
        if (has_prelude(compiled_prefix / "runtime")) {
            return finalize(compiled_prefix / "runtime", InstallKind::PortableDir);
        }
    }
#endif

    // 4. User-level & Standard System FHS locations
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path home_p(home);
        for (const auto& u_rt : {
            home_p / ".local" / "share" / "cfire" / "runtime",
            home_p / ".cfire" / "runtime"
        }) {
            if (has_prelude(u_rt)) {
                return finalize(u_rt, InstallKind::UserHome);
            }
        }
    }
    for (const auto* sys_rt : {
        "/opt/homebrew/share/cfire/runtime",
        "/usr/local/share/cfire/runtime",
        "/usr/share/cfire/runtime"
    }) {
        std::filesystem::path s_rt(sys_rt);
        if (has_prelude(s_rt)) {
            return finalize(s_rt, InstallKind::SystemPrefix);
        }
    }

    // 5. CWD ancestor walk (used by unit test runners compiled into /tmp or workspace subdirs)
    auto cwd = std::filesystem::current_path(ec);
    for (auto d = cwd; !d.empty() && d != d.parent_path(); d = d.parent_path()) {
        if (has_prelude(d / "runtime")) {
            return finalize(d / "runtime", InstallKind::WorkspaceFallback);
        }
    }

    return finalize(cwd / "runtime", InstallKind::WorkspaceFallback);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto read_file_text(const std::filesystem::path& p) -> std::expected<std::string, std::string> {
    std::ifstream f(p, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return std::unexpected(std::format("failed to open file: {}", p.string()));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

[[nodiscard]] CFIRE_INFRA_INLINE auto split_ws(std::string_view sv) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::istringstream iss{std::string(sv)};
    std::string tok;
    while (iss >> tok) out.push_back(std::move(tok));
    return out;
}

CFIRE_INFRA_INLINE void query_pkg_config(
    const std::string& pkg,
    std::vector<std::string>& cflags,
    std::vector<std::string>& libs,
    const std::filesystem::path& install_root,
    bool static_link) {
    std::vector<std::string> extra_pc_dirs;
    if (!install_root.empty()) {
        std::error_code ec;
        for (const auto& pc_rel : {install_root / "lib" / "pkgconfig", install_root / "share" / "pkgconfig"}) {
            if (std::filesystem::is_directory(pc_rel, ec)) {
                extra_pc_dirs.push_back(pc_rel.string());
            }
        }
    }
    std::string hdr;
    std::string lib_flag;
    std::string lib_stem;
    if (pkg == "sqlite3") { hdr = "sqlite3.h"; lib_flag = "-lsqlite3"; lib_stem = "libsqlite3"; }
    else if (pkg == "sqlcipher") { hdr = "sqlcipher/sqlite3.h"; lib_flag = "-lsqlcipher"; lib_stem = "libsqlcipher"; }
    else if (pkg == "libsodium") { hdr = "sodium.h"; lib_flag = "-lsodium"; lib_stem = "libsodium"; }
    else if (pkg == "libssh") { hdr = "libssh/libssh.h"; lib_flag = "-lssh"; lib_stem = "libssh"; }
    else if (pkg == "libzstd") { hdr = "zstd.h"; lib_flag = "-lzstd"; lib_stem = "libzstd"; }
    else if (pkg == "simdjson") { hdr = "simdjson.h"; lib_flag = "-lsimdjson"; lib_stem = "libsimdjson"; }

    // 1. Prioritize user-local override in ~/.local/include + ~/.local/lib (CF-075)
    std::error_code ec;
    if (const char* home = std::getenv("HOME"); home && !hdr.empty()) {
        std::filesystem::path local_root = std::filesystem::path(home) / ".local";
        auto local_inc = local_root / "include" / hdr;
        auto local_inc_alt = (pkg == "sqlcipher") ? (local_root / "include" / "sqlite3.h") : local_inc;
        bool has_local_lib =
            std::filesystem::exists(local_root / "lib" / (lib_stem + ".a"), ec) ||
            std::filesystem::exists(local_root / "lib" / (lib_stem + ".so"), ec) ||
            std::filesystem::exists(local_root / "lib" / (lib_stem + ".dylib"), ec);
        if ((std::filesystem::exists(local_inc, ec) || std::filesystem::exists(local_inc_alt, ec)) && has_local_lib) {
            if (pkg == "sqlcipher" && std::filesystem::exists(local_inc, ec)) {
                cflags.push_back("-I" + (local_root / "include" / "sqlcipher").string());
            }
            cflags.push_back("-I" + (local_root / "include").string());
            libs.push_back("-L" + (local_root / "lib").string());
            libs.push_back(lib_flag);
            return;
        }
        for (const auto& pc_rel : {local_root / "lib" / "pkgconfig", local_root / "share" / "pkgconfig"}) {
            if (std::filesystem::is_directory(pc_rel, ec)) {
                extra_pc_dirs.push_back(pc_rel.string());
            }
        }
    }
    if (pkg == "sqlcipher") {
        for (const char* keg : {"/opt/homebrew/opt/sqlcipher/lib/pkgconfig", "/usr/local/opt/sqlcipher/lib/pkgconfig"}) {
            if (std::filesystem::is_directory(keg, ec)) {
                extra_pc_dirs.push_back(keg);
            }
        }
    }

    auto pc_res = ProbeCache::instance().probe_pkg_config(pkg, static_link, install_root, extra_pc_dirs);
    if (pc_res.found) {
        for (auto& f : split_ws(pc_res.cflags)) cflags.push_back(std::move(f));
        if (!pc_res.libs.empty()) {
            for (auto& l : split_ws(pc_res.libs)) libs.push_back(std::move(l));
            return;
        }
    }
    // Fallback probe when pkg-config is unavailable or package has no .pc file
    if (!hdr.empty()) {
        std::vector<std::string> prefixes;
        if (const char* home = std::getenv("HOME")) {
            prefixes.push_back((std::filesystem::path(home) / ".local").string());
        }
        if (const char* mp = std::getenv("MINGW_PREFIX"); mp && *mp) {
            prefixes.push_back(mp);
        }
        if (!install_root.empty()) prefixes.push_back(install_root.string());
        if (pkg == "sqlcipher") {
            prefixes.push_back("/opt/homebrew/opt/sqlcipher");
            prefixes.push_back("/usr/local/opt/sqlcipher");
        }
        prefixes.push_back("/opt/homebrew");
        prefixes.push_back("/usr/local");
        prefixes.push_back("/usr");
        prefixes.push_back("/clang64");
        prefixes.push_back("C:/msys64/clang64");
        for (const auto& prefix : prefixes) {
            std::filesystem::path inc_p = std::filesystem::path(prefix) / "include" / hdr;
            if (std::filesystem::exists(inc_p, ec)) {
                if (pkg == "sqlcipher") {
                    cflags.push_back(std::string("-I") + prefix + "/include/sqlcipher");
                }
                if (prefix != "/usr") {
                    cflags.push_back(std::string("-I") + prefix + "/include");
                    libs.push_back(std::string("-L") + prefix + "/lib");
                }
                libs.push_back(lib_flag);
                return;
            }
        }
#if defined(__APPLE__)
        if (pkg == "sqlite3") {
            libs.push_back("-lsqlite3");
        }
#endif
    }
}

CFIRE_INFRA_INLINE auto trim_whitespace(std::string_view s) -> std::string {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return std::string(s);
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
