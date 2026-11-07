#pragma once
// src/lsp/ide_setup.hpp — Editor, IDE & AI Agent (MCP + LSP) Integration Bootstrapper (`cfire ide` & `cfire mcp --setup`)

#include <algorithm>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace cfire::lsp {

[[nodiscard]] inline auto resolve_cfire_binary(const std::filesystem::path& repo_root) -> std::string {
    std::error_code ec;
    auto local_bin = repo_root / "cfire";
    if (std::filesystem::exists(local_bin, ec) && !std::filesystem::is_directory(local_bin, ec)) {
        auto can = std::filesystem::weakly_canonical(local_bin, ec);
        if (!ec) return can.string();
        return local_bin.string();
    }
#if defined(__APPLE__)
    char ep[1024];
    uint32_t sz = sizeof(ep);
    if (_NSGetExecutablePath(ep, &sz) == 0) {
        auto p = std::filesystem::canonical(ep, ec);
        if (!ec && p.filename() == "cfire") {
            return p.string();
        }
    }
#elif defined(__linux__)
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec && p.filename() == "cfire") {
        return p.string();
    }
#endif
    return local_bin.string();
}

struct DetectedIde {
    std::string id;       // e.g. "vscode", "antigravity", "claude", "cursor", "windsurf", "zed", "nvim", "clion"
    std::string label;    // Human-readable name
    std::string reason;   // Why it was detected (env var or directory)
    bool configured = false;
};

[[nodiscard]] inline auto detect_environments(const std::filesystem::path& workspace_dir) -> std::vector<DetectedIde> {
    std::vector<DetectedIde> out;
    std::error_code ec;

    const char* term_prog = std::getenv("TERM_PROGRAM");
    std::string_view term_sv = term_prog ? std::string_view(term_prog) : std::string_view{};

    const char* home_env = std::getenv("HOME");
    std::filesystem::path home_dir = home_env ? std::filesystem::path(home_env) : std::filesystem::path{};

    // 1. VS Code
    {
        std::string reason;
        if (std::getenv("VSCODE_PID") != nullptr) reason = "VSCODE_PID env";
        else if (term_sv.contains("vscode")) reason = "TERM_PROGRAM=vscode";
        else if (std::filesystem::exists(workspace_dir / ".vscode", ec)) reason = ".vscode/ directory";
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".vscode" / "settings.json", ec) &&
                       std::filesystem::exists(workspace_dir / ".vscode" / "mcp.json", ec);
            out.push_back({"vscode", "VS Code", reason, cfg});
        }
    }

    // 2. Cursor
    {
        std::string reason;
        if (std::getenv("CURSOR_TRACE_ID") != nullptr || std::getenv("CURSOR_AGENT") != nullptr) reason = "CURSOR env";
        else if (term_sv.contains("cursor") || term_sv.contains("Cursor")) reason = "TERM_PROGRAM=cursor";
        else if (std::filesystem::exists(workspace_dir / ".cursor", ec)) reason = ".cursor/ directory";
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".cursor" / "mcp.json", ec);
            out.push_back({"cursor", "Cursor", reason, cfg});
        }
    }

    // 3. Windsurf
    {
        std::string reason;
        if (std::getenv("WINDSURF_PID") != nullptr || std::getenv("CODEIUM_EDITOR_APP_ROOT") != nullptr) reason = "WINDSURF env";
        else if (term_sv.contains("windsurf") || term_sv.contains("Windsurf")) reason = "TERM_PROGRAM=windsurf";
        else if (std::filesystem::exists(workspace_dir / ".windsurf", ec)) reason = ".windsurf/ directory";
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".windsurf" / "mcp_config.json", ec);
            out.push_back({"windsurf", "Windsurf", reason, cfg});
        }
    }

    // 4. Antigravity (Google Antigravity / agy)
    {
        std::string reason;
        if (std::getenv("ANTIGRAVITY_AGENT") != nullptr || std::getenv("ANTIGRAVITY_CLI_ALIAS") != nullptr) {
            reason = "ANTIGRAVITY env";
        } else if (std::filesystem::exists(workspace_dir / ".agents", ec) || std::filesystem::exists(workspace_dir / ".agent", ec)) {
            reason = ".agents/ directory";
        } else if (std::filesystem::exists(workspace_dir / "GEMINI.md", ec)) {
            reason = "GEMINI.md file";
        } else if (!home_dir.empty() && std::filesystem::exists(home_dir / ".gemini" / "antigravity", ec)) {
            reason = "~/.gemini/antigravity installation";
        }
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".agents" / "plugins" / "cfire" / "mcp_config.json", ec) ||
                       (!home_dir.empty() && std::filesystem::exists(home_dir / ".gemini" / "config" / "mcp_config.json", ec));
            out.push_back({"antigravity", "Google Antigravity (agy)", reason, cfg});
        }
    }

    // 5. Claude Code
    {
        std::string reason;
        if (std::getenv("CLAUDECODE") != nullptr || std::getenv("CLAUDE_CODE_ENTRYPOINT") != nullptr) {
            reason = "CLAUDECODE env";
        } else if (std::filesystem::exists(workspace_dir / ".claude", ec)) {
            reason = ".claude/ directory";
        } else if (std::filesystem::exists(workspace_dir / ".mcp.json", ec)) {
            reason = ".mcp.json file";
        } else if (std::filesystem::exists(workspace_dir / "CLAUDE.md", ec)) {
            reason = "CLAUDE.md file";
        } else if (!home_dir.empty() && std::filesystem::exists(home_dir / ".claude", ec)) {
            reason = "~/.claude installation";
        }
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".mcp.json", ec) &&
                       (std::filesystem::exists(workspace_dir / ".lsp.json", ec) ||
                        std::filesystem::exists(workspace_dir / ".claude-plugin" / "plugin.json", ec));
            out.push_back({"claude", "Claude Code", reason, cfg});
        }
    }

    // 6. OpenCode
    {
        std::string reason;
        if (std::getenv("OPENCODE") != nullptr) {
            reason = "OPENCODE env";
        } else if (std::filesystem::exists(workspace_dir / "opencode.json", ec) ||
                   std::filesystem::exists(workspace_dir / "opencode.jsonc", ec)) {
            reason = "opencode.json file";
        } else if (!home_dir.empty() && std::filesystem::exists(home_dir / ".config" / "opencode", ec)) {
            reason = "~/.config/opencode installation";
        }
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / "opencode.json", ec);
            out.push_back({"opencode", "OpenCode", reason, cfg});
        }
    }

    // 7. Codex CLI
    {
        std::string reason;
        if (std::getenv("CODEX_HOME") != nullptr) {
            reason = "CODEX_HOME env";
        } else if (std::filesystem::exists(workspace_dir / ".codex", ec)) {
            reason = ".codex/ directory";
        } else if (!home_dir.empty() && std::filesystem::exists(home_dir / ".codex", ec)) {
            reason = "~/.codex installation";
        }
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".codex" / "config.toml", ec);
            out.push_back({"codex", "Codex CLI", reason, cfg});
        }
    }

    // 6. Zed
    {
        std::string reason;
        if (std::getenv("ZED_TERM") != nullptr) reason = "ZED_TERM env";
        else if (term_sv.contains("zed")) reason = "TERM_PROGRAM=zed";
        else if (std::filesystem::exists(workspace_dir / ".zed", ec)) reason = ".zed/ directory";
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".zed" / "settings.json", ec);
            out.push_back({"zed", "Zed", reason, cfg});
        }
    }

    // 7. Neovim
    {
        std::string reason;
        if (std::getenv("NVIM") != nullptr || std::getenv("VIMRUNTIME") != nullptr) reason = "NVIM env";
        else if (std::filesystem::exists(workspace_dir / ".nvim", ec) || std::filesystem::exists(workspace_dir / ".nvim.lua", ec)) {
            reason = ".nvim.lua config";
        }
        if (!reason.empty()) {
            bool cfg = std::filesystem::exists(workspace_dir / ".nvim.lua", ec);
            out.push_back({"nvim", "Neovim", reason, cfg});
        }
    }

    // 8. JetBrains CLion / Fleet
    {
        if (std::filesystem::exists(workspace_dir / ".idea", ec)) {
            bool cfg = std::filesystem::exists(workspace_dir / ".idea" / "lsp.xml", ec);
            out.push_back({"clion", "JetBrains CLion / Fleet", ".idea/ directory", cfg});
        }
    }

    return out;
}

[[nodiscard]] inline auto format_mcp_config_snippet(const std::filesystem::path& repo_root) -> std::string {
    auto cfire_bin = resolve_cfire_binary(repo_root);
    return std::format(R"({{
  "mcpServers": {{
    "cfire": {{
      "command": "{}",
      "args": ["mcp"]
    }}
  }}
}}
)", cfire_bin);
}

[[nodiscard]] inline auto cfire_agent_rules_markdown(std::string_view cfire_bin) -> std::string {
    return std::format(R"(# cfire (.cf) Language & Toolchain Guidelines

- **Language Overview**: `cfire` (`.cf`) is a memory-safe, zero-overhead systems programming language that transpiles 1-to-1 to human-readable ISO C++23.
- **Packages & Imports**:
  - **Same-Directory Package Merging**: All `.cf` files in the same directory declaring the same `package <name>` automatically share types, functions, methods, and constants. Never write `import "./sibling.cf"` for same-directory files.
  - **Toolchain & Hybrid Native WebUI Packages**: Use `import "pkg:tako"` (or `import "core:tako"`) along with `build.Tako()` in `build.cf` for zero-hardcoded-path Tako GUI integration, or `import "ext:webui"` (`cfire init --webui` / `cfire new <dir> --webui`) for Wails/Tauri-style hybrid native WebView + Two-Way Typed RPC & Event applications across macOS, iOS, and Linux.
- **Core Ergonomics**:
  - **String Interpolation**: Double-quoted strings interpolate arbitrary expressions and format specifiers inline: `"sum={{a + b}} hex={{mask:04x}}"`.
  - **Loops & Defer**: Use `for i in 0..n` (half-open) or `for i in 1..=n` (inclusive) for numeric ranges, `for item in slice` / `for idx, item in slice` for collections, and `defer cleanup()` or `defer {{ ... }}` for RAII scope cleanup (`return` inside `defer {{ ... }}` is forbidden via `cfire/RETURN-IN-DEFER`).
  - **Testing, Fuzzing & Benchmarking**:
    - Unit tests: `test "name" {{ ... }}` using `assert(a == b)` or `assert_eq(a, b)` (power-assertions format `[left: ..., right: ...]` diffs on failure).
    - Property-based fuzzing: `fuzz "name" |x: i64, s: str| {{ ... }}` (or parameterized `test "name" |x: i64| {{ ... }}`) runs 16 adversarial edge-case dictionary seeds + deterministic PRNG iterations (`--fuzz-runs <N>`, `--seed <S>`) with automatic counterexample shrinking.
    - Multi-iteration statistical benchmarks: `bench "name" |size in [64, 1024, 16384]| {{ black_box(work(size)) }}` (run via `cfire bench` or `cfire test --bench`).
    - Zero-code automatic invariants: fenced ```` ```cf ```` blocks in `///` doc-comments with `assert` run as doc-tests; JSON-tagged structs auto-run `[auto] json round-trip: <Struct>` during `cfire test`; every test checks for open file-descriptor leaks.
- **Parameter Intents**:
  - Default parameters are read-only borrows.
  - `mut` parameters require `mut` at both declaration and call site (`update(mut val)`).
  - `move` parameters transfer ownership and require `move` at the call site (`consume(move item)`).
  - `out` parameters require `out` at the call site (`write_into(out buf)`).
- **Compile-Time Memory Safety**:
  - **Loan & Origin Tracking (`cfire/USE-AFTER-FREE`, `cfire/DANGLING-POINTER`, `cfire/DANGLING-RETURN`, `cfire/ITERATOR-INVALIDATION`)**: Do not mutate, move, or reallocate a container (`.push()`, `.clear()`, `.erase()`) while a pointer (`&x`, `.data()`) or slice view (`s[a..b]`) borrows from it, or while iterating over it.
  - **Flow-Sensitive Null Safety (`cfire/NULL-DEREF`)**: Guard nullable raw pointers (`*T`, `*mut T`) and smart pointers (`own[T]`, `shared[T]`) with `if p != none {{ ... }}` or `assert(p != none)` before dereferencing (`*p` or `p.field`).
  - **Bounds, Arithmetic & Struct Tags (`cfire/INDEX-OUT-OF-BOUNDS`, `cfire/DIVIDE-BY-ZERO`, `cfire/INVALID-STRUCT-TAG`)**: Constant out-of-bounds indexing, division by zero, duplicate `json`/`sql`/`cli` struct tags, and unparsable `default:"..."` tags are rejected at compile time.
  - **Injection Safety (`cfire/SQL-INJECTION-RISK`, `cfire/SHELL-INJECTION-RISK`)**: Always use parameterized `?1` queries in `db.exec`/`db.query` instead of string concatenation.
- **Built-In LSP, CLI & MCP Tools**:
  - **LSP Code Intelligence**: `cfire lsp` provides full LSP 3.17 navigation for `.cf` files (`goToDefinition`, `findReferences`, `hover`, `documentSymbol`, `workspaceSymbol`, `goToImplementation`, `prepareCallHierarchy`, `incomingCalls`, `outgoingCalls`, and real-time `publishDiagnostics`). Prefer LSP semantic lookups over raw text `grep`.
  - **MCP Tools**: Prefer MCP tools `cfire_check` (supports both `path` and inline `code`), `cfire_test` (unit tests, fuzzing, and benchmarks), `cfire_stdlib_lookup`, `cfire_outline`, `cfire_inspect`, `cfire_eval`, and `cfire_doc_llm` (powered by `{} mcp`).
  - **Zero-arg Workspace CLI Commands**: `cfire check`, `cfire test`, `cfire bench`, `cfire fix`, `cfire build`, `cfire run`, `cfire init`, `cfire new <dir>`, `cfire inspect <target|cfire/CODE>`, `cfire outline --json`, and `cfire doc --llm`.
)", cfire_bin);
}

// Configures Editor LSP & AI Agent MCP integrations (VS Code, Cursor, Windsurf, Antigravity, Claude Code, Zed, Neovim, CLion)
[[nodiscard]] inline auto configure_ide(
    const std::filesystem::path& repo_root,
    std::string_view editor_input = "auto",
    bool global_scope = false
) -> std::expected<std::string, std::string> {
    auto cfire_bin = resolve_cfire_binary(repo_root);
    std::string target = std::string(editor_input);

    if (target == "--status" || target == "status" || target == "--detect" || target == "detect") {
        auto detected = detect_environments(repo_root);
        std::string out = std::format("Detected IDE & AI Agent environments for {}:\n", repo_root.string());
        if (detected.empty()) {
            out += "  (none detected via environment or workspace markers; run `cfire ide --all` to generate all configs)\n";
        } else {
            for (const auto& d : detected) {
                out += std::format("  - {:<26} [{}] (detected via {})\n",
                    d.label, d.configured ? "configured" : "not configured", d.reason);
            }
        }
        return out;
    }

    auto has_target = [&](std::string_view name, const std::vector<std::string>& targets) -> bool {
        for (const auto& t : targets) {
            if (t == "all" || t == "--all" || t == name) return true;
        }
        return false;
    };

    std::vector<std::string> targets;
    if (target.empty() || target == "auto") {
        auto detected = detect_environments(repo_root);
        for (const auto& d : detected) {
            targets.push_back(d.id);
        }
        if (targets.empty()) {
            targets.push_back("all");
        }
    } else if (target == "agy") {
        targets.push_back("antigravity");
    } else if (target == "claude-code") {
        targets.push_back("claude");
    } else if (target == "neovim") {
        targets.push_back("nvim");
    } else if (target == "idea" || target == "fleet") {
        targets.push_back("clion");
    } else {
        targets.push_back(target);
    }

    std::vector<std::string> configured;
    std::error_code ec;

    // 1. VS Code (and shared .vscode/settings.json for Cursor / Windsurf)
    if (has_target("vscode", targets) || has_target("cursor", targets) || has_target("windsurf", targets)) {
        auto vscode_dir = repo_root / ".vscode";
        std::filesystem::create_directories(vscode_dir, ec);
        std::string vs_settings = std::format(R"({{
  "files.associations": {{
    "*.cf": "cfire"
  }},
  "[cfire]": {{
    "editor.defaultFormatter": "cfire",
    "editor.formatOnSave": true
  }},
  "cfire.lsp.path": "{}",
  "cfire.lsp.arguments": ["lsp"],
  "cfire.inlayHints.enable": true
}}
)", cfire_bin);
        {
            std::ofstream s_ofs(vscode_dir / "settings.json", std::ios::trunc);
            s_ofs << vs_settings;
        }
        {
            std::ofstream e_ofs(vscode_dir / "extensions.json", std::ios::trunc);
            e_ofs << "{\n  \"recommendations\": []\n}\n";
        }
        std::string vs_mcp = std::format(R"({{
  "servers": {{
    "cfire": {{
      "type": "stdio",
      "command": "{}",
      "args": ["mcp"]
    }}
  }}
}}
)", cfire_bin);
        {
            std::ofstream m_ofs(vscode_dir / "mcp.json", std::ios::trunc);
            m_ofs << vs_mcp;
        }
        if (has_target("vscode", targets)) {
            configured.push_back("VS Code (.vscode/settings.json + .vscode/mcp.json)");
        }
    }

    // 2. Cursor
    if (has_target("cursor", targets)) {
        auto cursor_dir = repo_root / ".cursor";
        auto cursor_rules_dir = cursor_dir / "rules";
        std::filesystem::create_directories(cursor_rules_dir, ec);
        {
            std::ofstream m_ofs(cursor_dir / "mcp.json", std::ios::trunc);
            m_ofs << format_mcp_config_snippet(repo_root);
        }
        {
            std::ofstream r_ofs(cursor_rules_dir / "cfire.mdc", std::ios::trunc);
            r_ofs << "---\n"
                  << "description: cfire (.cf) language rules, memory safety diagnostics, and MCP tooling\n"
                  << "globs: [\"**/*.cf\"]\n"
                  << "alwaysApply: false\n"
                  << "---\n\n"
                  << cfire_agent_rules_markdown(cfire_bin);
        }
        configured.push_back("Cursor (.cursor/mcp.json + .cursor/rules/cfire.mdc + .vscode/settings.json)");
    }

    // 3. Windsurf
    if (has_target("windsurf", targets)) {
        auto ws_dir = repo_root / ".windsurf";
        std::filesystem::create_directories(ws_dir, ec);
        {
            std::ofstream m_ofs(ws_dir / "mcp_config.json", std::ios::trunc);
            m_ofs << format_mcp_config_snippet(repo_root);
        }
        configured.push_back("Windsurf (.windsurf/mcp_config.json + .vscode/settings.json)");
    }

    // 4. Google Antigravity (agy)
    if (has_target("antigravity", targets)) {
        auto agy_plugin_dir = repo_root / ".agents" / "plugins" / "cfire";
        auto agy_rules_dir = repo_root / ".agents" / "rules";
        std::filesystem::create_directories(agy_plugin_dir, ec);
        std::filesystem::create_directories(agy_rules_dir, ec);

        std::string plugin_json = R"({
  "name": "cfire",
  "version": "1.0.0",
  "description": "Official cfire (.cf) compiler, memory-safety diagnostics, and stdlib MCP integration for Antigravity.",
  "mcpServers": "./mcp_config.json"
}
)";
        {
            std::ofstream p_ofs(agy_plugin_dir / "plugin.json", std::ios::trunc);
            p_ofs << plugin_json;
        }
        {
            std::ofstream m_ofs(agy_plugin_dir / "mcp_config.json", std::ios::trunc);
            m_ofs << format_mcp_config_snippet(repo_root);
        }
        {
            std::ofstream r_ofs(agy_rules_dir / "cfire.md", std::ios::trunc);
            r_ofs << "---\n"
                  << "trigger: model_decision\n"
                  << "description: Activate when reading, writing, building, or debugging cfire (.cf) files or build.cf configurations.\n"
                  << "---\n\n"
                  << cfire_agent_rules_markdown(cfire_bin);
        }

        std::string global_note;
        if (global_scope) {
            if (const char* home = std::getenv("HOME")) {
                auto global_cfg_dir = std::filesystem::path(home) / ".gemini" / "config";
                std::filesystem::create_directories(global_cfg_dir, ec);
                auto global_mcp = global_cfg_dir / "mcp_config.json";
                bool wrote_global = false;
                if (!std::filesystem::exists(global_mcp, ec)) {
                    std::ofstream g_ofs(global_mcp, std::ios::trunc);
                    g_ofs << format_mcp_config_snippet(repo_root);
                    wrote_global = true;
                } else {
                    std::ifstream g_ifs(global_mcp);
                    std::ostringstream ss;
                    ss << g_ifs.rdbuf();
                    std::string existing = ss.str();
                    if (!existing.contains("\"cfire\"")) {
                        auto pos = existing.find("\"mcpServers\"");
                        if (pos != std::string::npos) {
                            auto brace = existing.find('{', pos);
                            if (brace != std::string::npos) {
                                std::string entry = std::format(
                                    "\n    \"cfire\": {{\n      \"command\": \"{}\",\n      \"args\": [\"mcp\"]\n    }},",
                                    cfire_bin);
                                existing.insert(brace + 1, entry);
                                std::ofstream g_ofs(global_mcp, std::ios::trunc);
                                g_ofs << existing;
                                wrote_global = true;
                            }
                        }
                    } else {
                        wrote_global = true;
                    }
                }
                if (wrote_global) {
                    global_note = " + ~/.gemini/config/mcp_config.json";
                }
            }
        }
        configured.push_back("Google Antigravity (.agents/plugins/cfire/mcp_config.json + .agents/rules/cfire.md" + global_note + ")");
    }

    // 5. Claude Code (claude / claude-code)
    if (has_target("claude", targets)) {
        std::string claude_mcp = R"({
  "mcpServers": {
    "cfire": {
      "type": "stdio",
      "command": "cfire",
      "args": ["mcp"]
    }
  }
}
)";
        {
            std::ofstream m_ofs(repo_root / ".mcp.json", std::ios::trunc);
            m_ofs << claude_mcp;
        }
        std::string claude_lsp = R"({
  "cfire": {
    "command": "cfire",
    "args": ["lsp"],
    "transport": "stdio",
    "extensionToLanguage": {
      ".cf": "cfire"
    }
  }
}
)";
        {
            std::ofstream l_ofs(repo_root / ".lsp.json", std::ios::trunc);
            l_ofs << claude_lsp;
        }
        auto claude_plugin_dir = repo_root / ".claude-plugin";
        std::filesystem::create_directories(claude_plugin_dir, ec);
        std::string claude_plugin = R"({
  "name": "cfire",
  "version": "1.3.0",
  "description": "Official cfire (.cf) Language Server (LSP) plugin for Claude Code.",
  "lspServers": {
    "cfire": {
      "command": "cfire",
      "args": ["lsp"],
      "transport": "stdio",
      "extensionToLanguage": {
        ".cf": "cfire"
      }
    }
  }
}
)";
        {
            std::ofstream p_ofs(claude_plugin_dir / "plugin.json", std::ios::trunc);
            p_ofs << claude_plugin;
        }
        std::string claude_marketplace = R"({
  "name": "cfire",
  "owner": {
    "name": "Memo Sandoval"
  },
  "plugins": [
    {
      "name": "cfire",
      "source": "./",
      "description": "Official cfire (.cf) Language Server (LSP) plugin for Claude Code.",
      "version": "1.3.0"
    }
  ]
}
)";
        {
            std::ofstream mp_ofs(claude_plugin_dir / "marketplace.json", std::ios::trunc);
            mp_ofs << claude_marketplace;
        }
        auto claude_rules_dir = repo_root / ".claude" / "rules";
        std::filesystem::create_directories(claude_rules_dir, ec);
        {
            std::ofstream r_ofs(claude_rules_dir / "cfire.md", std::ios::trunc);
            r_ofs << cfire_agent_rules_markdown("cfire");
        }
        configured.push_back("Claude Code (.mcp.json + .lsp.json + .claude-plugin/plugin.json + .claude-plugin/marketplace.json + .claude/rules/cfire.md)");
    }

    // 6. OpenCode (opencode)
    if (has_target("opencode", targets)) {
        std::string opencode_cfg = std::format(R"({{
  "$schema": "https://opencode.ai/config.json",
  "lsp": {{
    "cfire": {{
      "command": ["{}", "lsp"],
      "extensions": [".cf"]
    }}
  }},
  "mcp": {{
    "cfire": {{
      "type": "local",
      "command": ["{}", "mcp"],
      "enabled": true
    }}
  }}
}}
)", cfire_bin, cfire_bin);
        {
            std::ofstream o_ofs(repo_root / "opencode.json", std::ios::trunc);
            o_ofs << opencode_cfg;
        }
        configured.push_back("OpenCode (opencode.json with LSP + MCP)");
    }

    // 7. Codex CLI (codex)
    if (has_target("codex", targets)) {
        auto codex_dir = repo_root / ".codex";
        std::filesystem::create_directories(codex_dir, ec);
        std::string codex_toml = std::format(R"(# Auto-generated by cfire ide
[mcp_servers.cfire]
command = "{}"
args = ["mcp"]
)", cfire_bin);
        {
            std::ofstream c_ofs(codex_dir / "config.toml", std::ios::trunc);
            c_ofs << codex_toml;
        }
        configured.push_back("Codex CLI (.codex/config.toml)");
    }

    // 6. Zed
    if (has_target("zed", targets)) {
        std::string zed_settings = std::format(R"({{
  "file_types": {{
    "Go": ["cf"]
  }},
  "inlay_hints": {{
    "enabled": true
  }},
  "languages": {{
    "Go": {{
      "tab_size": 4,
      "formatter": "language_server",
      "format_on_save": "on",
      "language_servers": ["gopls"]
    }}
  }},
  "lsp": {{
    "gopls": {{
      "binary": {{
        "path": "{}",
        "arguments": ["lsp"]
      }}
    }}
  }},
  "context_servers": {{
    "cfire-mcp": {{
      "command": {{
        "path": "{}",
        "args": ["mcp"]
      }}
    }}
  }}
}}
)", cfire_bin, cfire_bin);
        auto zed_dir = repo_root / ".zed";
        std::filesystem::create_directories(zed_dir, ec);
        {
            std::ofstream ofs(zed_dir / "settings.json", std::ios::trunc);
            ofs << zed_settings;
        }
        if (repo_root.filename() == "cfire" && std::filesystem::exists(repo_root.parent_path(), ec)) {
            auto parent_zed = repo_root.parent_path() / ".zed";
            std::filesystem::create_directories(parent_zed, ec);
            std::ofstream pofs(parent_zed / "settings.json", std::ios::trunc);
            pofs << zed_settings;
        }
        configured.push_back("Zed (.zed/settings.json with LSP + context_servers MCP)");
    }

    // 7. Neovim
    if (has_target("nvim", targets)) {
        std::string nvim_config = std::format(R"(-- Auto-generated by cfire ide
vim.filetype.add({{ extension = {{ cf = "cfire" }} }})
vim.lsp.start({{
  name = "cfire-lsp",
  cmd = {{ "{}", "lsp" }},
  root_dir = vim.fs.dirname(vim.fs.find({{"build.cf", ".git"}}, {{ upward = true }})[1]),
  settings = {{}},
}})
vim.api.nvim_create_autocmd("BufWritePre", {{
  pattern = "*.cf",
  callback = function() vim.lsp.buf.format({{ async = false }}) end,
}})
)", cfire_bin);
        std::ofstream n_ofs(repo_root / ".nvim.lua", std::ios::trunc);
        n_ofs << nvim_config;
        configured.push_back("Neovim (.nvim.lua)");
    }

    // 8. JetBrains CLion / Fleet
    if (has_target("clion", targets)) {
        auto idea_dir = repo_root / ".idea";
        std::filesystem::create_directories(idea_dir, ec);
        std::string lsp_xml = std::format(R"(<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="LspServerRegistry">
    <server id="cfire-lsp" name="cfire LSP" command="{}" extension="cf" />
  </component>
</project>
)", cfire_bin + " lsp");
        {
            std::ofstream x_ofs(idea_dir / "lsp.xml", std::ios::trunc);
            x_ofs << lsp_xml;
        }
        {
            std::ofstream m_ofs(idea_dir / "mcp.json", std::ios::trunc);
            m_ofs << format_mcp_config_snippet(repo_root);
        }
        configured.push_back("JetBrains CLion / Fleet (.idea/lsp.xml + .idea/mcp.json)");
    }

    if (configured.empty()) {
        return std::unexpected(std::format(
            "unsupported editor '{}' (supported: 'vscode', 'cursor', 'windsurf', 'antigravity'/'agy', 'claude'/'claude-code', 'opencode', 'codex', 'zed', 'nvim', 'clion', 'all', '--status')",
            editor_input));
    }

    std::string summary = "✓ Configured IDE & MCP integrations for " + repo_root.string() + ":\n";
    for (const auto& c : configured) {
        summary += "  - " + c + "\n";
    }
    return summary;
}

} // namespace cfire::lsp
