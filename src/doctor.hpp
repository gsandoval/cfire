#pragma once
// src/doctor.hpp — Environment Diagnostics (`cfire doctor`) & Dependency Bootstrapper (`cfire setup`)

#include "buildsys.hpp"
#include "lsp/ide_setup.hpp"
#include "../runtime/cfire_prelude.hpp"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>
#include <vector>

namespace cfire::doctor {

inline constexpr std::string_view kCfireVersion = "0.9.5";
#ifdef CFIRE_GIT_SHA
inline constexpr std::string_view kCfireGitSha = CFIRE_GIT_SHA;
#else
inline constexpr std::string_view kCfireGitSha = "dev";
#endif

struct CheckItem {
    std::string category;
    std::string name;
    std::string status; // "ok", "warn", "error"
    std::string detail;
    std::string fix_hint;
};

[[nodiscard]] inline auto trim_str(std::string s) -> std::string {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) {
        ++start;
    }
    return s.substr(start);
}

[[nodiscard]] inline auto first_line(std::string_view sv) -> std::string {
    auto pos = sv.find('\n');
    std::string s(pos == std::string_view::npos ? sv : sv.substr(0, pos));
    return trim_str(std::move(s));
}

[[nodiscard]] inline auto has_command(std::string_view cmd) -> bool {
    int rc = 0;
    auto out = bridge::run_command_capture(std::format("command -v {} 2>/dev/null", cmd), rc);
    return rc == 0 && !trim_str(out).empty();
}

[[nodiscard]] inline auto command_version(std::string_view cmd, std::string_view flag = "--version") -> std::string {
    int rc = 0;
    auto out = bridge::run_command_capture(std::format("{} {} 2>/dev/null", cmd, flag), rc);
    if (rc != 0 || out.empty()) return "";
    return first_line(out);
}

[[nodiscard]] inline auto detect_pkg_manager() -> std::string {
#if defined(__APPLE__)
    if (has_command("brew")) return "brew";
    return "macos";
#else
    if (has_command("apt-get")) return "apt";
    if (has_command("dnf")) return "dnf";
    if (has_command("pacman")) return "pacman";
    if (has_command("apk")) return "apk";
    return "linux";
#endif
}

[[nodiscard]] inline auto check_pkg_or_header(
    std::string_view pc_name,
    std::string_view header_name,
    std::string& detail_out
) -> bool {
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path loc_p = std::filesystem::path(home) / ".local" / "include" / header_name;
        std::error_code ec;
        if (std::filesystem::exists(loc_p, ec)) {
            detail_out = loc_p.string() + " (~/.local override)";
            return true;
        }
    }
    if (has_command("pkg-config")) {
        int rc = 0;
        auto ver = bridge::run_command_capture(std::format("pkg-config --modversion {} 2>/dev/null", pc_name), rc);
        if (rc == 0) {
            detail_out = std::format("pkg-config {} ({})", pc_name, trim_str(ver));
            return true;
        }
    }
    for (const auto* prefix : {"/opt/homebrew/include", "/usr/local/include", "/usr/include"}) {
        std::filesystem::path p = std::filesystem::path(prefix) / header_name;
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            detail_out = p.string();
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline auto collect_checks(const std::filesystem::path& runtime_dir) -> std::vector<CheckItem> {
    std::vector<CheckItem> items;
    std::string pm = detect_pkg_manager();

    // --- 1. Core Compiler & Runtime ---
    {
        if (has_command("clang++")) {
            std::string ver = command_version("clang++");
            int rc = 0;
            (void)bridge::run_command_capture(
                "printf '#include <expected>\\nint main(){return 0;}\\n' | clang++ -std=c++2b -Wno-builtin-macro-redefined -D__cpp_concepts=202002L -x c++ - -fsyntax-only 2>/dev/null",
                rc
            );
            if (rc == 0) {
                items.push_back({
                    "Core Toolchain",
                    "clang++ (ISO C++23)",
                    "ok",
                    ver.empty() ? "clang++ (-std=c++2b verified)" : ver,
                    ""
                });
            } else {
                items.push_back({
                    "Core Toolchain",
                    "clang++ (ISO C++23)",
                    "error",
                    "clang++ found (" + ver + "), but -std=c++2b probe failed",
                    pm == "apt" ? "sudo apt-get install -y clang lld libc++-dev" : "Upgrade Clang to 17+"
                });
            }
        } else if (has_command("g++")) {
            std::string ver = command_version("g++");
            items.push_back({
                "Core Toolchain",
                "clang++ (ISO C++23)",
                "warn",
                "g++ found (" + ver + "), but clang++ is required for `cfire build` and C/C++ AST interop",
                pm == "apt" ? "sudo apt-get install -y clang" :
                (pm == "dnf" ? "sudo dnf install -y clang" :
                (pm == "pacman" ? "sudo pacman -S --noconfirm clang" : "xcode-select --install"))
            });
        } else {
            items.push_back({
                "Core Toolchain",
                "clang++ (ISO C++23)",
                "error",
                "No C++23 compiler (clang++) found in PATH",
                pm == "apt" ? "sudo apt-get install -y clang make" :
                (pm == "brew" || pm == "macos" ? "xcode-select --install" : "Install clang++ 17+")
            });
        }
    }

    // Runtime headers check
    {
        std::error_code ec;
        auto prelude = runtime_dir / "cfire_prelude.hpp";
        auto stdlib_hdr = runtime_dir / "cfire_core_stdlib.hpp";
        auto can_rt = std::filesystem::weakly_canonical(runtime_dir, ec);
        if (ec) can_rt = runtime_dir;
        auto p1 = can_rt.parent_path();
        auto p2 = p1.parent_path();
        std::string mode_label = "Portable Directory";
        if (p1.filename() == "cfire" && (p2.filename() == "share" || p2.filename() == "lib" || p2.filename() == "include")) {
            mode_label = "System FHS Prefix";
        } else if (p1.filename() == ".cfire") {
            mode_label = "User Home";
        }
        if (std::filesystem::exists(prelude, ec) && std::filesystem::exists(stdlib_hdr, ec)) {
            items.push_back({
                "Core Toolchain",
                "cfire Runtime Headers",
                "ok",
                std::format("{} [{}]", runtime_dir.string(), mode_label),
                ""
            });
        } else {
            items.push_back({
                "Core Toolchain",
                "cfire Runtime Headers",
                "error",
                "Missing cfire_prelude.hpp in " + runtime_dir.string(),
                "Run `cfire install` or `cfire setup`"
            });
        }
    }

    // Precompiled header (PCH) cache check
    {
        std::error_code ec;
        auto pch_dir = std::filesystem::temp_directory_path(ec) / "cfire_pch_cache";
        auto can_rt = std::filesystem::weakly_canonical(runtime_dir, ec);
        std::size_t rt_hash = std::hash<std::string>{}((ec ? runtime_dir : can_rt).string());
        auto pch_file = pch_dir / std::format("cfire_prelude_opt_{:08x}.pch", static_cast<std::uint32_t>(rt_hash & 0xFFFFFFFFu));
        if (std::filesystem::exists(pch_file, ec)) {
            items.push_back({
                "Core Toolchain",
                "C++23 Prelude PCH Cache",
                "ok",
                pch_file.string() + " (warm)",
                ""
            });
        } else {
            items.push_back({
                "Core Toolchain",
                "C++23 Prelude PCH Cache",
                "ok",
                pch_dir.string() + " (builds automatically on first compile or `cfire setup`)",
                ""
            });
        }
    }

    // --- 2. Build & Fetch Utilities ---
    {
        if (has_command("pkg-config")) {
            items.push_back({"Build & Fetch Tools", "pkg-config", "ok", command_version("pkg-config"), ""});
        } else {
            items.push_back({
                "Build & Fetch Tools",
                "pkg-config",
                "warn",
                "Not found in PATH (used for auto-linking native libraries)",
                pm == "brew" ? "brew install pkg-config" :
                (pm == "apt" ? "sudo apt-get install -y pkg-config" : "Run `cfire setup`")
            });
        }

        if (has_command("cmake")) {
            items.push_back({"Build & Fetch Tools", "cmake", "ok", command_version("cmake"), ""});
        } else {
            items.push_back({
                "Build & Fetch Tools",
                "cmake",
                "warn",
                "Not found (needed for `build.CMake` dependencies and building `cfire eject` projects)",
                pm == "brew" ? "brew install cmake" :
                (pm == "apt" ? "sudo apt-get install -y cmake" : "Run `cfire setup`")
            });
        }

        if (has_command("git") && has_command("curl")) {
            items.push_back({
                "Build & Fetch Tools",
                "git + curl",
                "ok",
                command_version("git") + " / " + command_version("curl"),
                ""
            });
        } else {
            items.push_back({
                "Build & Fetch Tools",
                "git + curl",
                "warn",
                "git or curl missing (needed for `build.Git`, `build.Archive`, and `cfire vendor`)",
                pm == "apt" ? "sudo apt-get install -y git curl" : "Install git and curl"
            });
        }
    }

    // --- 3. Native `ext:*` & `vendor:*` Engine Libraries ---
    {
        std::string det;
        if (check_pkg_or_header("simdjson", "simdjson.h", det)) {
            bool sj_compat_err = false;
            if (det.find("~/.local") == std::string::npos && has_command("pkg-config") && has_command("clang++")) {
                int sj_rc = 0;
                auto sj_ver = trim_str(bridge::run_command_capture("pkg-config --modversion simdjson 2>/dev/null", sj_rc));
                int cl_rc = 0;
                auto cl_ver = trim_str(bridge::run_command_capture("clang++ -dumpversion 2>/dev/null", cl_rc));
                int cl_maj = 0;
                if (auto dot = cl_ver.find('.'); dot != std::string::npos) {
                    if (auto pmaj = cfire::detail::parse_int<int>(cl_ver.substr(0, dot))) cl_maj = *pmaj;
                }
                if (sj_rc == 0 && cl_maj >= 19 &&
                    (sj_ver.starts_with("1.") || sj_ver.starts_with("2.") || sj_ver.starts_with("3."))) {
                    sj_compat_err = true;
                    items.push_back({
                        "Native Engine Libraries",
                        "simdjson (`ext:json`, `ext:ai`)",
                        "warn",
                        std::format("{} is incompatible with Clang {} (requires simdjson >= 4.0)", det, cl_maj),
                        "Install simdjson >= 4.0 into ~/.local/{include,lib} or upgrade libsimdjson-dev"
                    });
                }
            }
            if (!sj_compat_err) {
                items.push_back({"Native Engine Libraries", "simdjson (`ext:json`, `ext:ai`)", "ok", det, ""});
            }
        } else {
            items.push_back({
                "Native Engine Libraries",
                "simdjson (`ext:json`, `ext:ai`)",
                "warn",
                "Not installed (using built-in zero-dependency JSON parser fallback; install simdjson for SIMD acceleration)",
                pm == "brew" ? "brew install simdjson" :
                (pm == "apt" ? "sudo apt-get install -y libsimdjson-dev" : "Run `cfire setup`")
            });
        }

        det.clear();
        if (check_pkg_or_header("libsodium", "sodium.h", det)) {
            items.push_back({"Native Engine Libraries", "libsodium (`ext:crypto/sodium`)", "ok", det, ""});
        } else {
            items.push_back({
                "Native Engine Libraries",
                "libsodium (`ext:crypto/sodium`)",
                "warn",
                "Not installed (required when importing `ext:crypto/sodium`)",
                pm == "brew" ? "brew install libsodium" :
                (pm == "apt" ? "sudo apt-get install -y libsodium-dev" : "Install libsodium")
            });
        }

        det.clear();
        if (check_pkg_or_header("sqlcipher", "sqlcipher/sqlite3.h", det) ||
            std::filesystem::exists("/opt/homebrew/opt/sqlcipher/include/sqlcipher/sqlite3.h")) {
            if (det.empty()) det = "/opt/homebrew/opt/sqlcipher";
            items.push_back({"Native Engine Libraries", "sqlcipher (`sqlite.open_encrypted`)", "ok", det, ""});
        } else {
            items.push_back({
                "Native Engine Libraries",
                "sqlcipher (`sqlite.open_encrypted`)",
                "warn",
                "Optional: SQLCipher not installed (needed for encrypted SQLite databases via `sqlite.open_encrypted`)",
                pm == "brew" ? "brew install sqlcipher" :
                (pm == "apt" ? "sudo apt-get install -y libsqlcipher-dev" : "Install sqlcipher")
            });
        }

        det.clear();
        if (check_pkg_or_header("libzstd", "zstd.h", det)) {
            items.push_back({"Native Engine Libraries", "libzstd (`ext:compress/zstd`)", "ok", det, ""});
        } else {
            items.push_back({
                "Native Engine Libraries",
                "libzstd (`ext:compress/zstd`)",
                "warn",
                "Not installed (needed when importing `ext:compress/zstd`)",
                pm == "brew" ? "brew install zstd" :
                (pm == "apt" ? "sudo apt-get install -y libzstd-dev" : "Run `cfire setup`")
            });
        }

        det.clear();
        if (check_pkg_or_header("sqlite3", "sqlite3.h", det)) {
            items.push_back({"Native Engine Libraries", "sqlite3 (`ext:db/sqlite`)", "ok", det, ""});
        } else {
#if defined(__APPLE__)
            items.push_back({"Native Engine Libraries", "sqlite3 (`ext:db/sqlite`)", "ok", "macOS SDK libsqlite3", ""});
#else
            items.push_back({
                "Native Engine Libraries",
                "sqlite3 (`ext:db/sqlite`)",
                "warn",
                "Not installed (needed when importing `ext:db/sqlite`)",
                pm == "apt" ? "sudo apt-get install -y libsqlite3-dev" : "Run `cfire setup`"
            });
#endif
        }

        det.clear();
        if (check_pkg_or_header("openssl", "openssl/ssl.h", det)) {
            items.push_back({"Native Engine Libraries", "OpenSSL (`ext:net/tls`, HTTPS)", "ok", det, ""});
        } else {
#if defined(__APPLE__)
            if (std::filesystem::exists("/opt/homebrew/opt/openssl/include/openssl/ssl.h") ||
                std::filesystem::exists("/usr/lib/libssl.48.dylib") ||
                std::filesystem::exists("/opt/homebrew/lib/libssl.dylib")) {
                items.push_back({"Native Engine Libraries", "OpenSSL (`ext:net/tls`, HTTPS)", "ok", "Dynamic libssl/libcrypto detected", ""});
            } else {
                items.push_back({
                    "Native Engine Libraries",
                    "OpenSSL (`ext:net/tls`, HTTPS)",
                    "warn",
                    "OpenSSL not found (needed for `ext:net/tls` and HTTPS sockets)",
                    "brew install openssl"
                });
            }
#else
            if (std::filesystem::exists("/usr/lib/x86_64-linux-gnu/libssl.so.3") ||
                std::filesystem::exists("/usr/lib/aarch64-linux-gnu/libssl.so.3") ||
                std::filesystem::exists("/lib/x86_64-linux-gnu/libssl.so.3") ||
                std::filesystem::exists("/lib/aarch64-linux-gnu/libssl.so.3")) {
                items.push_back({"Native Engine Libraries", "OpenSSL (`ext:net/tls`, HTTPS)", "ok", "Dynamic libssl.so.3 detected", ""});
            } else {
                items.push_back({
                    "Native Engine Libraries",
                    "OpenSSL (`ext:net/tls`, HTTPS)",
                    "warn",
                    "libssl not found (needed for `ext:net/tls` and HTTPS)",
                    pm == "apt" ? "sudo apt-get install -y libssl-dev" : "Run `cfire setup`"
                });
            }
#endif
        }

        det.clear();
        auto cache_rl = build::get_package_cache_root() / "vendor_raylib" / "include" / "raylib.h";
        if (check_pkg_or_header("raylib", "raylib.h", det)) {
            items.push_back({"Native Engine Libraries", "raylib (`vendor:raylib`)", "ok", det, ""});
        } else if (std::filesystem::exists(cache_rl)) {
            items.push_back({"Native Engine Libraries", "raylib (`vendor:raylib`)", "ok", cache_rl.parent_path().parent_path().string() + " (cached)", ""});
        } else {
            items.push_back({
                "Native Engine Libraries",
                "raylib (`vendor:raylib`)",
                "ok",
                "Auto-downloaded on first `vendor:raylib` build (or pre-fetch via `cfire setup --raylib`)",
                ""
            });
        }
    }

    // --- 4. Cross-Platform & Target SDKs ---
    {
        auto em = build::resolve_emscripten_compiler();
        if (em) {
            items.push_back({"Cross-Platform Targets", "WebAssembly (`em++`)", "ok", *em, ""});
        } else {
            items.push_back({
                "Cross-Platform Targets",
                "WebAssembly (`em++`)",
                "warn",
                "Optional: Emscripten (`em++`) not installed (needed only for `--target=wasm` / `--web`)",
                pm == "brew" ? "brew install emscripten  (or `cfire setup --wasm`)" : "cfire setup --wasm"
            });
        }

#if defined(__APPLE__)
        int sdk_rc = 0;
        auto sdk_out = bridge::run_command_capture("xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null", sdk_rc);
        if (sdk_rc == 0 && !trim_str(sdk_out).empty()) {
            items.push_back({"Cross-Platform Targets", "iOS Simulator SDK (`--ios`)", "ok", trim_str(sdk_out), ""});
        } else {
            items.push_back({
                "Cross-Platform Targets",
                "iOS Simulator SDK (`--ios`)",
                "warn",
                "Optional: Xcode iOS Simulator SDK not installed (needed only for `cfire run --ios`)",
                "xcodebuild -downloadPlatform iOS"
            });
        }
#elif defined(__linux__)
        std::string alsa_det;
        if (check_pkg_or_header("alsa", "alsa/asoundlib.h", alsa_det) ||
            std::filesystem::exists("/usr/lib/x86_64-linux-gnu/libasound.so.2") ||
            std::filesystem::exists("/usr/lib/aarch64-linux-gnu/libasound.so.2")) {
            items.push_back({"Cross-Platform Targets", "Linux ALSA Audio (`ext:audio`)", "ok", "libasound.so.2 ready", ""});
        } else {
            items.push_back({
                "Cross-Platform Targets",
                "Linux ALSA Audio (`ext:audio`)",
                "warn",
                "Optional: libasound2 not installed (needed only for live speaker playback; WAV/DSP synthesis works offline)",
                pm == "apt" ? "sudo apt-get install -y libasound2-dev" : "Install alsa-lib"
            });
        }
#endif
    }

    // --- 5. IDE & AI Agent Integrations (LSP & MCP) ---
    {
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (ec) cwd = runtime_dir.parent_path();
        auto detected = lsp::detect_environments(cwd);
        if (detected.empty()) {
            items.push_back({
                "IDE & AI Agent Integrations",
                "LSP & MCP Auto-Config",
                "ok",
                "Run `cfire ide --all` or `cfire mcp --setup` to configure VS Code, Antigravity, Claude Code, Cursor, or Zed",
                ""
            });
        } else {
            for (const auto& d : detected) {
                items.push_back({
                    "IDE & AI Agent Integrations",
                    d.label,
                    "ok",
                    std::format("{} ({})", d.configured ? "LSP/MCP configured" : "detected — run `cfire ide` to configure", d.reason),
                    d.configured ? "" : "cfire ide"
                });
            }
        }
    }

    return items;
}

inline auto run_doctor(const std::filesystem::path& runtime_dir, bool json_mode) -> int {
    auto checks = collect_checks(runtime_dir);
    int ok_count = 0;
    int warn_count = 0;
    int err_count = 0;
    for (const auto& c : checks) {
        if (c.status == "ok") ++ok_count;
        else if (c.status == "warn") ++warn_count;
        else ++err_count;
    }

    if (json_mode) {
        std::string out = "{\n";
        out += std::format("  \"version\": \"{}\",\n", kCfireVersion);
        out += std::format("  \"commit\": \"{}\",\n", kCfireGitSha);
        out += std::format("  \"runtime_dir\": \"{}\",\n", json::escape_string(runtime_dir.string()));
        out += std::format("  \"ok_count\": {},\n", ok_count);
        out += std::format("  \"warn_count\": {},\n", warn_count);
        out += std::format("  \"error_count\": {},\n", err_count);
        out += std::format("  \"healthy\": {},\n", err_count == 0 ? "true" : "false");
        out += "  \"checks\": [\n";
        for (std::size_t i = 0; i < checks.size(); ++i) {
            const auto& c = checks[i];
            out += std::format(
                "    {{\"category\": \"{}\", \"name\": \"{}\", \"status\": \"{}\", \"detail\": \"{}\", \"fix_hint\": \"{}\"}}{}\n",
                json::escape_string(c.category),
                json::escape_string(c.name),
                json::escape_string(c.status),
                json::escape_string(c.detail),
                json::escape_string(c.fix_hint),
                (i + 1 < checks.size()) ? "," : ""
            );
        }
        out += "  ]\n}";
        std::println("{}", out);
        return err_count == 0 ? 0 : 1;
    }

    std::println("cfire doctor — v{} ({}) Environment & Dependency Health Report", kCfireVersion, kCfireGitSha);
    std::println("====================================================================");

    std::string current_cat;
    for (const auto& c : checks) {
        if (c.category != current_cat) {
            current_cat = c.category;
            std::println("\n[{}]", current_cat);
        }
        std::string_view badge = (c.status == "ok") ? "  [✓]" : (c.status == "warn" ? "  [!]" : "  [✗]");
        std::println("{} {:<34} {}", badge, c.name, c.detail);
        if (!c.fix_hint.empty() && c.status != "ok") {
            std::println("      └─ Fix: {}", c.fix_hint);
        }
    }

    std::println("\n--------------------------------------------------------------------");
    if (err_count == 0 && warn_count == 0) {
        std::println("✓ All {} environment checks passed! Ready to build and run `.cf` programs.", ok_count);
    } else if (err_count == 0) {
        std::println("✓ Core compiler & runtime ready ({} passed, {} optional warning(s)).", ok_count, warn_count);
        std::println("  Tip: Run `cfire setup --yes` to automatically install optional native libraries.");
    } else {
        std::println("✗ {} required check(s) failed ({} passed, {} warning(s)).", err_count, ok_count, warn_count);
        std::println("  Run `cfire setup --yes` to install missing dependencies.");
    }
    return err_count == 0 ? 0 : 1;
}

inline auto run_setup(
    const std::filesystem::path& runtime_dir,
    bool auto_yes,
    bool with_wasm,
    bool with_raylib
) -> int {
    std::println("==> cfire setup (v{}) — Configuring Toolchain & Dependencies", kCfireVersion);
    std::string pm = detect_pkg_manager();
    bool is_root = (::geteuid() == 0);
    std::string sudo_prefix = is_root ? "" : (has_command("sudo") ? "sudo " : "");

    // 1. Install missing system libraries via platform package manager
    std::string install_cmd;
    if (pm == "brew") {
        std::vector<std::string> pkgs;
        std::string dummy;
        if (!has_command("pkg-config")) pkgs.push_back("pkg-config");
        if (!has_command("cmake")) pkgs.push_back("cmake");
        if (!check_pkg_or_header("simdjson", "simdjson.h", dummy)) pkgs.push_back("simdjson");
        if (!check_pkg_or_header("libzstd", "zstd.h", dummy)) pkgs.push_back("zstd");
        if (!check_pkg_or_header("sqlite3", "sqlite3.h", dummy)) pkgs.push_back("sqlite");
        if (!check_pkg_or_header("openssl", "openssl/ssl.h", dummy) &&
            !std::filesystem::exists("/opt/homebrew/opt/openssl/include/openssl/ssl.h")) {
            pkgs.push_back("openssl");
        }
        if (with_raylib && !check_pkg_or_header("raylib", "raylib.h", dummy)) {
            pkgs.push_back("raylib");
        }
        if (with_wasm && !build::resolve_emscripten_compiler()) {
            pkgs.push_back("emscripten");
        }
        if (!pkgs.empty()) {
            install_cmd = "brew install";
            for (const auto& p : pkgs) install_cmd += " " + p;
        }
    } else if (pm == "apt") {
        std::vector<std::string> pkgs;
        std::string dummy;
        if (!has_command("clang++")) pkgs.push_back("clang");
        if (!has_command("make")) pkgs.push_back("make");
        if (!has_command("cmake")) pkgs.push_back("cmake");
        if (!has_command("pkg-config")) pkgs.push_back("pkg-config");
        if (!has_command("git")) pkgs.push_back("git");
        if (!has_command("curl")) pkgs.push_back("curl");
        if (!check_pkg_or_header("simdjson", "simdjson.h", dummy)) pkgs.push_back("libsimdjson-dev");
        if (!check_pkg_or_header("libzstd", "zstd.h", dummy)) pkgs.push_back("libzstd-dev");
        if (!check_pkg_or_header("sqlite3", "sqlite3.h", dummy)) pkgs.push_back("libsqlite3-dev");
        if (!check_pkg_or_header("openssl", "openssl/ssl.h", dummy)) pkgs.push_back("libssl-dev");
        if (with_wasm && !build::resolve_emscripten_compiler()) pkgs.push_back("emscripten");
        if (!pkgs.empty()) {
            install_cmd = sudo_prefix + "apt-get update && " + sudo_prefix + "DEBIAN_FRONTEND=noninteractive apt-get install -y";
            for (const auto& p : pkgs) install_cmd += " " + p;
        }
    } else if (pm == "dnf") {
        install_cmd = sudo_prefix + "dnf install -y clang make cmake pkgconf-pkg-config git curl simdjson-devel libzstd-devel sqlite-devel openssl-devel";
    } else if (pm == "pacman") {
        install_cmd = sudo_prefix + "pacman -Sy --needed --noconfirm clang make cmake pkgconf git curl simdjson zstd sqlite openssl";
    } else if (pm == "apk") {
        install_cmd = sudo_prefix + "apk add --no-cache clang make cmake pkgconf git curl simdjson-dev zstd-dev sqlite-dev openssl-dev";
    }

    if (!install_cmd.empty()) {
        std::println("==> Missing native packages detected. Package manager command:");
        std::println("    {}", install_cmd);
        bool proceed = auto_yes;
        if (!proceed) {
            std::print("    Run this command now? [Y/n]: ");
            std::cout.flush();
            std::string ans;
            if (std::getline(std::cin, ans)) {
                proceed = (ans.empty() || ans[0] == 'y' || ans[0] == 'Y');
            }
        }
        if (proceed) {
            int rc = std::system(install_cmd.c_str());
            if (rc != 0) {
                std::println(stderr, "  [!] Package installation exited with code {} (continuing setup)...", rc);
            }
        }
    } else {
        std::println("==> Native system libraries are already installed.");
    }

    // 2. Verify runtime headers (already resolved relative to executable in PortableDir or SystemPrefix mode)
    std::error_code ec;
    if (std::filesystem::exists(runtime_dir / "cfire_prelude.hpp", ec)) {
        std::println("==> Runtime headers auto-detected at: {}", runtime_dir.string());
    } else if (const char* home = std::getenv("HOME")) {
        auto dest_rt = std::filesystem::path(home) / ".cfire" / "runtime";
        std::println("==> Warning: runtime headers not found at {}", runtime_dir.string());
        std::println("    Run `cfire install` from a cfire distribution to install runtime headers.");
        (void)dest_rt;
    }

    // 3. Pre-warm C++23 Prelude Precompiled Header (.pch)
    if (has_command("clang++") && std::filesystem::exists(runtime_dir / "cfire_prelude.hpp", ec)) {
        std::println("==> Pre-compiling C++23 runtime prelude header (PCH)...");
        auto pch_flags = build::ensure_prelude_pch(runtime_dir, false);
        if (!pch_flags.empty()) {
            std::println("    ✓ Prelude PCH ready ({})", pch_flags);
        }
    }

    // 4. Optional Raylib pre-fetch
    if (with_raylib) {
        std::string dummy;
        auto cache_rl = build::get_package_cache_root() / "vendor_raylib";
        if (!check_pkg_or_header("raylib", "raylib.h", dummy) && !std::filesystem::exists(cache_rl / "include" / "raylib.h")) {
            std::string url;
#if defined(__APPLE__)
            url = "https://github.com/raysan5/raylib/releases/download/5.0/raylib-5.0_macos.tar.gz";
#elif defined(__linux__)
            url = "https://github.com/raysan5/raylib/releases/download/5.0/raylib-5.0_linux_amd64.tar.gz";
#endif
            if (!url.empty()) {
                std::println("==> Pre-fetching vendor:raylib from {}...", url);
                std::string sha;
                auto res = build::fetch_archive(url, "", cache_rl, sha);
                if (res) {
                    std::println("    ✓ Cached vendor:raylib in {}", cache_rl.string());
                }
            }
        }
    }

    std::println("");
    return run_doctor(runtime_dir, false);
}

inline auto run_install(
    const build::ToolchainLayout& layout,
    std::string_view target_dir_arg,
    bool portable_mode,
    bool user_mode
) -> int {
    std::error_code ec;
    if (layout.executable_path.empty() || !std::filesystem::exists(layout.executable_path, ec)) {
        std::println(stderr, "error: could not determine running cfire executable path");
        return 1;
    }
    if (layout.runtime_dir.empty() || !std::filesystem::exists(layout.runtime_dir / "cfire_prelude.hpp", ec)) {
        std::println(stderr, "error: could not locate source runtime headers at {}", layout.runtime_dir.string());
        return 1;
    }

    std::filesystem::path target_root;
    if (!target_dir_arg.empty()) {
        target_root = std::filesystem::path(target_dir_arg);
    } else if (user_mode) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::println(stderr, "error: HOME environment variable is not set for --user install");
            return 1;
        }
        target_root = std::filesystem::path(home) / ".local";
    } else if (portable_mode) {
        std::println(stderr, "error: --portable requires a destination directory (e.g. `cfire install --portable ./dist/cfire`)");
        return 1;
    } else {
        if (::geteuid() == 0) {
            target_root = "/usr/local";
        } else if (const char* home = std::getenv("HOME")) {
            target_root = std::filesystem::path(home) / ".local";
        } else {
            target_root = "/usr/local";
        }
    }

    std::filesystem::path dest_bin;
    std::filesystem::path dest_rt;
    if (portable_mode) {
        // Self-contained portable directory layout: <dest>/cfire + <dest>/runtime/
        dest_bin = target_root / "cfire";
        dest_rt = target_root / "runtime";
    } else {
        // Standard FHS prefix layout: <prefix>/bin/cfire + <prefix>/share/cfire/runtime/
        dest_bin = target_root / "bin" / "cfire";
        dest_rt = target_root / "share" / "cfire" / "runtime";
    }

    std::filesystem::create_directories(dest_bin.parent_path(), ec);
    if (ec) {
        std::println(stderr, "error creating directory '{}': {}", dest_bin.parent_path().string(), ec.message());
        return 1;
    }
    std::filesystem::create_directories(dest_rt, ec);
    if (ec) {
        std::println(stderr, "error creating directory '{}': {}", dest_rt.string(), ec.message());
        return 1;
    }

    auto src_bin_can = std::filesystem::weakly_canonical(layout.executable_path, ec);
    auto dst_bin_can = std::filesystem::weakly_canonical(dest_bin, ec);
    if (src_bin_can != dst_bin_can) {
        std::filesystem::copy_file(layout.executable_path, dest_bin, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::println(stderr, "error copying binary to '{}': {}", dest_bin.string(), ec.message());
            return 1;
        }
        std::filesystem::permissions(
            dest_bin,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
            ec
        );
    }

    auto src_rt_can = std::filesystem::weakly_canonical(layout.runtime_dir, ec);
    auto dst_rt_can = std::filesystem::weakly_canonical(dest_rt, ec);
    if (src_rt_can != dst_rt_can) {
        std::filesystem::copy(
            layout.runtime_dir,
            dest_rt,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
            ec
        );
        if (ec) {
            std::println(stderr, "error copying runtime headers to '{}': {}", dest_rt.string(), ec.message());
            return 1;
        }
    }

    if (!layout.pkg_dir.empty() && std::filesystem::is_directory(layout.pkg_dir, ec)) {
        std::filesystem::path dest_pkg = portable_mode ? (target_root / "pkg") : (target_root / "share" / "cfire" / "pkg");
        auto src_pkg_can = std::filesystem::weakly_canonical(layout.pkg_dir, ec);
        auto dst_pkg_can = std::filesystem::weakly_canonical(dest_pkg, ec);
        if (src_pkg_can != dst_pkg_can) {
            std::filesystem::create_directories(dest_pkg, ec);
            std::filesystem::copy(
                layout.pkg_dir,
                dest_pkg,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                ec
            );
        }
    }

    if (portable_mode) {
        std::println("✓ Installed portable cfire bundle to {}:", target_root.string());
        std::println("  - Binary:  {}", dest_bin.string());
        std::println("  - Runtime: {} (auto-detected relative to binary)", dest_rt.string());
    } else {
        std::println("✓ Installed cfire (System FHS Prefix) to {}:", target_root.string());
        std::println("  - Binary:  {}", dest_bin.string());
        std::println("  - Runtime: {} (auto-detected via ../share/cfire/runtime)", dest_rt.string());
    }
    return 0;
}

} // namespace cfire::doctor
