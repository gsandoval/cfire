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

inline constexpr std::string_view kCfireVersion = "0.9.4";

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
        if (std::filesystem::exists(prelude, ec) && std::filesystem::exists(stdlib_hdr, ec)) {
            items.push_back({
                "Core Toolchain",
                "cfire Runtime Headers",
                "ok",
                runtime_dir.string(),
                ""
            });
        } else {
            items.push_back({
                "Core Toolchain",
                "cfire Runtime Headers",
                "error",
                "Missing cfire_prelude.hpp in " + runtime_dir.string(),
                "Run `cfire setup` or set CFIRE_RUNTIME_DIR=/path/to/cfire/runtime"
            });
        }
    }

    // Precompiled header (PCH) cache check
    {
        std::error_code ec;
        auto pch_dir = std::filesystem::temp_directory_path(ec) / "cfire_pch_cache";
        auto pch_file = pch_dir / "cfire_prelude_opt.pch";
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
            items.push_back({"Native Engine Libraries", "simdjson (`ext:json`, `ext:ai`)", "ok", det, ""});
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

    std::println("cfire doctor — v{} Environment & Dependency Health Report", kCfireVersion);
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

    // 2. Sync runtime headers to ~/.cfire/runtime (or /usr/local/share/cfire/runtime if root)
    std::error_code ec;
    if (std::filesystem::exists(runtime_dir / "cfire_prelude.hpp", ec)) {
        std::filesystem::path dest_rt;
        if (is_root) {
            dest_rt = "/usr/local/share/cfire/runtime";
        } else if (const char* home = std::getenv("HOME")) {
            dest_rt = std::filesystem::path(home) / ".cfire" / "runtime";
        }
        if (!dest_rt.empty() && std::filesystem::weakly_canonical(runtime_dir, ec) != std::filesystem::weakly_canonical(dest_rt, ec)) {
            std::filesystem::create_directories(dest_rt, ec);
            std::filesystem::copy(
                runtime_dir,
                dest_rt,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                ec
            );
            if (!ec) {
                std::println("==> Synced cfire runtime headers to {}", dest_rt.string());
            }
        }
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

} // namespace cfire::doctor
