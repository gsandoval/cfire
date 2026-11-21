#pragma once
// src/cli/usage.hpp — CLI Help Registry, Exit Unwrapping, and Walk Exclusions

#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace cfire::cli {

[[nodiscard]] inline auto unwrap_system_exit_code(int rc) -> int {
    if (rc == 0) return 0;
#if !defined(_WIN32)
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    if (WIFSIGNALED(rc)) return 128 + WTERMSIG(rc);
#endif
    return rc == -1 ? 1 : rc;
}

// One row per subcommand: `cfire <cmd> --help` prints its row (and details) instead of the global usage.
struct CommandHelp {
    std::string_view name;
    std::string_view synopsis;
    std::string_view details;
};

inline constexpr CommandHelp kCommandHelp[] = {
    {"init", "[--template cli|tako|webui] [--force] [dir] Initialize a new cfire project in current/target directory", ""},
    {"new", "[--template cli|tako|webui] [--force] <dir> Create a new cfire project directory", ""},
    {"run", "[--web|--ios|--ios-device] [--device <name|udid>] [target|file.cf] Run native app, web app (--web), iOS Simulator (--ios), or physical iOS device (--ios-device)", "Usage: cfire run [--web|--ios|--ios-device] [--device <name|udid>] [--sign <id>] [--provisioning-profile <path>] [--entitlements <path>] [--ios-min <ver>] [--define <K[=V]>] [--pkg-config <lib>] [target|file.cf] [-- args...]\n\nBuilds and runs. A single file argument builds that file and the packages it imports, not every .cf file beside it.\n  --ios                 Build and launch on iOS Simulator (arm64-apple-ios*-simulator)\n  --ios-device          Build, sign, install, and launch on a connected physical iOS device via xcrun devicectl (requires --sign / SignIdentity)\n  --device <name|udid>  Select a specific iOS Simulator or connected physical iOS device"},
    {"test", "[--json] [--filter <pat>] [--fuzz-runs <N>] [--seed <S>] [path|file.cf] Compile and execute test, fuzz & doc-test suites", "Usage: cfire test [--json] [--filter <pat>] [--fuzz-runs <N>] [--seed <S>] [--define <K[=V]>] [--pkg-config <lib>] [path|file.cf]"},
    {"bench", "[--json] [--filter <pat>] [--bench-time <ms>] [path|file.cf] Run statistical micro-benchmarks (`bench \"...\"`)", ""},
    {"build", "[--target wasm|ios-sim|ios|<triple>] [--portable|--static] [--universal] [--macos-min <ver>] [--ios-min <ver>] [--ipa] [target|file.cf]", "Usage: cfire build [--target wasm|ios-sim|ios|<triple>] [--portable|--static] [--universal] [--macos-min <ver>] [--ios-min <ver>] [--sign <id>] [--provisioning-profile <path>] [--entitlements <path>] [--ipa] [--pkg-config <lib>] [--define <K[=V]>|-D<K[=V]>] [--link-lib <lib>] [--include <dir>] [--no-cache] [--no-shard] [--release] [target|file.cf]\n\nBuilds a standalone native binary (default Release -O2), WebAssembly bundle (--target wasm), or iOS/macOS app.\n  --portable                      Prefer static third-party .a archives (graceful dynamic fallback + bundle-relative rpath; default macOS >= 13.3)\n  --static                        Strictly static link (fail if any third-party .a is unavailable; unsupported on macOS/iOS)\n  --universal                     Build universal macOS fat binary (arm64 + x86_64) via lipo\n  --macos-min <ver>               macOS deployment target (minimum supported: 13.3)\n  --ios-min <ver>                 iOS deployment target (minimum supported: 16.3; default: 17.0)\n  --sign <identity>               Apple code-signing identity (e.g. \"Apple Development: ...\")\n  --provisioning-profile <path>   Path to .mobileprovision or .provisionprofile\n  --entitlements <path>           Path to custom .entitlements plist\n  --ipa                           Package signed iOS .app inside a Payload/ .ipa archive\n  --pkg-config <lib>              Resolve C/C++ library CFLAGS/LIBS via pkg-config for single-file builds\n  --define, -D                    Define a C/C++ preprocessor macro (K or K=V) for single-file builds\nMulti-package builds use progressive per-package C++23 sharding; pass --no-shard to force single-TU codegen."},
    {"clean", "[--cache] [--all] [--dry-run] [--json] [path] Remove project .cfire/ & build outputs (and OS/XDG user cache with --cache)", "Usage: cfire clean [--cache] [--all] [--dry-run] [--json] [path|build.cf]\n\nRemoves nested project .cfire/ directories and exact manifest target outputs.\nPass --cache (without path) to purge only the user cache root and owned legacy caches, or --all for both.\nSafety rules: refuses symlinks, /, $HOME, ~/.cfire, ancestors, and non-empty cache roots lacking .cfire-cache."},
    {"serve", "[--port <n>] [--no-open] [target|path] Build & serve WebAssembly / static web app with zero config", ""},
    {"eval", "[-e <expr>|code]     Evaluate one-shot statement, expression, or stdin", ""},
    {"refactor", "rename <loc> <to>    Semantic symbol rename across workspace (--diff, --apply, --json)", "Usage: cfire refactor rename <file.cf:line:col> <new_name> [--diff|--apply|--json]\n\nRenames the symbol at <file.cf:line:col> and every reference to it across the workspace.\nClosure parameters and match bindings are separate scopes, so an inner `|count|` is not renamed with an outer `count`.\n\n  --diff    print a unified diff (applies with `git apply`) without changing files (default)\n  --apply   write the changes\n  --json    print the planned edits as JSON"},
    {"check", "[--json] [--fix] [--verify-cpp] [path|file.cf] Type-check, verify parameter intents, and inspect C++ headers", "Usage: cfire check [--json] [--fix] [--verify-cpp] [--define <K[=V]>] [--pkg-config <lib>] [path|file.cf]\n\nType-checks without building. Pure .cf files short-circuit after Sema; C++ interop (`import cpp`, `cpp { ... }`) or `--verify-cpp` (`--full`) also verifies generated C++23 with Clang over stdin."},
    {"fix", "[--dry-run] [--json] [--only codes] [path|file.cf] Auto-fix diagnostics (--only cfire/RETIRED-API runs just the 0.16 migration)", "Usage: cfire fix [--dry-run] [--json] [--only <codes>] [path|file.cf]\n\nApplies the preferred fix-it of every diagnostic, repeating until nothing changes.\n\n  --dry-run       report the fixes without writing files\n  --json          machine-readable result\n  --only <codes>  comma-separated diagnostic codes to fix, e.g. `--only cfire/RETIRED-API` for the 0.16 migration"},
    {"inspect", "<target> [--json]    Stateless symbol/type hover, foreign C header, or stdlib docs", ""},
    {"outline", "[--json] [path]      Token-compressed outline of packages, types, and functions", ""},
    {"fmt", "[--check] <path>     Canonically format .cf file(s) or directory", "Usage: cfire fmt [--check] <path|file.cf>\n\nRewrites files into canonical form. With --check, lists files that would change and exits 1."},
    {"lsp", "Start built-in Language Server Protocol (JSON-RPC 2.0 stdio)", ""},
    {"mcp", "[--setup|--print-config] Start MCP server over stdio, or configure AI agent MCP clients", ""},
    {"ide", "[target|--all|--status] [--global] Configure IDE & AI agents (VS Code, Antigravity, Claude Code, Cursor, Windsurf, Zed, Neovim, CLion)", ""},
    {"doc", "[--check|--llm|<f>]  Generate docs site, guide, or LLM agent reference sheet", ""},
    {"emit-cpp", "<file.cf>            Print generated human-readable C++23 source", ""},
    {"eject", "<file.cf> [out_dir]  Export standalone C++23 source tree + CMakeLists.txt", ""},
    {"vendor", "[dest_dir]           Fetch and vendor all build.cf dependencies for offline builds", ""},
    {"install", "[--prefix <dir>|--user|--portable <dir>] Install cfire binary & runtime headers (FHS prefix or portable bundle)", ""},
    {"upgrade", "[--check] [--version <tag>] [--yes] [--force] Upgrade installed cfire binary and runtime headers in place (alias: self-update)", "Usage: cfire upgrade [--check] [--version <tag>] [--yes] [--force]\n       cfire self-update [--check] [--version <tag>] [--yes] [--force]\n\nChecks for or installs a verified GitHub Release matching your installed layout (PortableDir, SystemPrefix, or UserHome).\n  --check, -c       Check current vs latest release version without modifying disk\n  --version <tag>   Upgrade or pin to a specific release tag (e.g. v0.12.0)\n  --yes, -y         Skip interactive confirmation prompt\n  --force, -f       Reinstall even if already on target version or in a source workspace"},
    {"doctor", "[--json]             Verify C++23 compiler, runtime headers, and native libraries", ""},
    {"config", "<list|get|set|unset> [key] [val] Manage persistent user toolchain config (~/.cfire/config.json)", "Usage: cfire config <list|get|set|unset> [key] [value]\n\nSupported keys: cxx, cc, emxx, ar, ld (or toolchains.<key>).\nPrecedence: CLI flags (--cxx/--cc/--emxx/--ar/--ld) > CFIRE_* env vars > config.json > auto-discovery."},
    {"setup", "[--yes] [--wasm]     Install missing host dependencies and warm C++23 PCH cache", ""},
    {"version", "(--version, -v)      Print cfire toolchain version", ""},
};

inline void print_usage() {
    std::println("Usage: cfire <command> [args...]");
    std::println("Commands:");
    for (const auto& c : kCommandHelp) std::println("  {:<8} {}", c.name, c.synopsis);
    std::println("\nRun `cfire <command> --help` for one command's options.");
}

// `cfire <cmd> --help`. Returns false for an unknown command so the caller prints the global usage.
inline auto print_command_usage(std::string_view cmd) -> bool {
    if (cmd == "self-update") cmd = "upgrade";
    for (const auto& c : kCommandHelp) {
        if (c.name != cmd) continue;
        if (!c.details.empty()) std::println("{}", c.details);
        else std::println("Usage: cfire {} {}", c.name, c.synopsis);
        return true;
    }
    return false;
}

[[nodiscard]] inline auto is_ignored_walk_dir(const std::filesystem::path& p) -> bool {
    std::string s = p.generic_string();
    return s.contains("/.cfire") || s.contains("/.tako") || s.contains("/.git") || s.contains("/dist") ||
           s.contains("/vendor") || s.contains("/node_modules") || s.contains("/ejected_cpp");
}

} // namespace cfire::cli
