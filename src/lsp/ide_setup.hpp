#pragma once
// src/lsp/ide_setup.hpp — Editor, IDE & AI Agent (MCP + LSP) Integration Bootstrapper (`cfire ide` & `cfire mcp --setup`)

#include "../version.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
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
extern "C" int _NSGetExecutablePath(char* buf, std::uint32_t* bufsize);
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace cfire::lsp {

#ifndef CFIRE_LSP_IDE_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_LSP_IDE_INLINE
#else
#define CFIRE_LSP_IDE_INLINE inline
#endif
#endif

struct DetectedIde {
    std::string id;       // e.g. "vscode", "antigravity", "claude", "cursor", "windsurf", "zed", "nvim", "clion"
    std::string label;    // Human-readable name
    std::string reason;   // Why it was detected (env var or directory)
    bool configured = false;
};

[[nodiscard]] CFIRE_LSP_IDE_INLINE auto resolve_cfire_binary(const std::filesystem::path& repo_root) -> std::string;
[[nodiscard]] CFIRE_LSP_IDE_INLINE auto detect_environments(const std::filesystem::path& workspace_dir) -> std::vector<DetectedIde>;
[[nodiscard]] CFIRE_LSP_IDE_INLINE auto format_mcp_config_snippet(const std::filesystem::path& repo_root) -> std::string;
[[nodiscard]] CFIRE_LSP_IDE_INLINE auto configure_ide(
    const std::filesystem::path& repo_root,
    std::string_view editor_input = "auto",
    bool global_scope = false
) -> std::expected<std::string, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_IDE_IMPL)

[[nodiscard]] CFIRE_LSP_IDE_INLINE auto resolve_cfire_binary(const std::filesystem::path& repo_root) -> std::string {
    std::error_code ec;
    auto local_bin = repo_root / "cfire";
    if (std::filesystem::exists(local_bin, ec) && !std::filesystem::is_directory(local_bin, ec)) {
        auto can = std::filesystem::weakly_canonical(local_bin, ec);
        if (!ec) return can.string();
        return local_bin.string();
    }
#if defined(__APPLE__)
    char ep[1024];
    std::uint32_t sz = sizeof(ep);
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

[[nodiscard]] CFIRE_LSP_IDE_INLINE auto detect_environments(const std::filesystem::path& workspace_dir) -> std::vector<DetectedIde> {
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

[[nodiscard]] CFIRE_LSP_IDE_INLINE auto format_mcp_config_snippet(const std::filesystem::path& repo_root) -> std::string {
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
  - **Standard Library, Cloud & Hybrid Native WebUI Packages**: Use `import "ext:tako"` (`cfire new <dir> --template tako` / `cfire init --template tako`) for local/cloud S3 & SQS primitives, or `import "ext:webui"` (`cfire new <dir> --template webui`) for hybrid native WebView + Two-Way Typed RPC & Event applications across macOS, iOS, Linux, and Windows.
  - **C/C++ Interop & Declarative `build.cf` Linkage**: Use `import cpp "<header.h>"` (with optional `as <alias>`) for foreign C/C++ headers, and declare preprocessor defines and native linkage (`Defines: {{...}}`, `PkgConfig: [...]`, `Libraries: [...]`, `Frameworks: [...]`, `Includes: [...]`, or `build.Define(...)`) in `build.cf` (never `@pkg` or `@define` attributes on imports).
- **Core Ergonomics**:
  - **String Interpolation**: Double-quoted strings interpolate arbitrary expressions and format specifiers inline: `"sum={{a + b}} hex={{mask:04x}}"`.
  - **Loops & Defer**: Use `for i in 0..n` (half-open) or `for i in 1..=n` (inclusive) for numeric ranges, `for item in slice` / `for idx, item in slice` for collections, and `defer cleanup()` or `defer {{ ... }}` for RAII scope cleanup (`return` inside `defer {{ ... }}` is forbidden via `cfire/RETURN-IN-DEFER`).
  - **Testing, Fuzzing & Benchmarking**:
    - Unit tests: `test "name" {{ ... }}` using `assert(a == b)` or `assert_eq(a, b)` (power-assertions format `[left: ..., right: ...]` diffs on failure).
    - Property-based fuzzing: `fuzz "name" |x: i64, s: str| {{ ... }}` (or parameterized `test "name" |x: i64| {{ ... }}`) runs 16 adversarial edge-case dictionary seeds + deterministic PRNG iterations (`--fuzz-runs <N>`, `--seed <S>`) with automatic counterexample shrinking.
    - Multi-iteration statistical benchmarks: `bench "name" |size in [64, 1024, 16384]| {{ black_box(work(size)) }}` (run via `cfire bench` or `cfire test --bench`).
    - Zero-code automatic invariants: fenced ```` ```cf ```` blocks in `///` doc-comments with `assert` run as doc-tests; JSON-tagged structs auto-run `[auto] json round-trip: <Struct>` during `cfire test`; every test checks for open file-descriptor leaks.
- **Parameter Intents**:
  - Default parameters are read-only views (including zero-copy `str` views with implicit owned copies when stored or returned).
  - `mut` parameters require `mut` at both declaration and call site (`update(mut val)`).
  - `move` parameters transfer ownership and require `move` at the call site (`consume(move item)`).
- **Compile-Time Memory Safety**:
  - **Move Discipline (`cfire/USE-AFTER-MOVE`, `cfire/MOVE-REQUIRED`, `cfire/MOVE-CALLSITE`)**: `own[T]` and structs containing `own[T]` are move-only; passing or moving a non-Copy value without `move` or reading a moved variable without reassigning on all paths is rejected.
  - **Loan & View Tracking (`cfire/USE-AFTER-FREE`, `cfire/DANGLING-RETURN`, `cfire/DANGLING-TEMPORARY`, `cfire/ITERATOR-INVALIDATION`)**: Do not mutate, move, or reallocate a container (`.push()`, `.clear()`, `.erase()`) while a view borrows from it or while iterating over it, and never return a view into a local or temporary.
  - **Flow-Sensitive Null Safety (`cfire/NULL-DEREF`)**: Guard smart pointers (`own[T]`, `shared[T]`) and FFI raw pointers (`*T`, `*mut T`) with `if p != none {{ ... }}` or `assert(p != none)` before dereference (raw pointers are restricted to `import cpp` boundaries via `cfire/RAW-POINTER`).
  - **Bounds, Arithmetic & Struct Tags (`cfire/INDEX-OUT-OF-BOUNDS`, `cfire/DIVIDE-BY-ZERO`, `cfire/INVALID-STRUCT-TAG`)**: Constant out-of-bounds indexing, division by zero, duplicate `json`/`sql`/`cli` struct tags, and unparsable `default:"..."` tags are rejected at compile time.
  - **Injection Safety (`cfire/SQL-INJECTION-RISK`, `cfire/SHELL-INJECTION-RISK`)**: Always use parameterized `?1` queries in `db.exec`/`db.query` instead of string concatenation.
- **Built-In LSP, CLI & MCP Tools**:
  - **LSP Code Intelligence**: `cfire lsp` provides full LSP 3.17 navigation for `.cf` files (`goToDefinition`, `findReferences`, `hover`, `documentSymbol`, `workspaceSymbol`, `goToImplementation`, `prepareCallHierarchy`, `incomingCalls`, `outgoingCalls`, and real-time `publishDiagnostics`). Prefer LSP semantic lookups over raw text `grep`.
  - **MCP Tools**: Prefer MCP tools `cfire_check` (supports both `path` and inline `code`), `cfire_test` (unit tests, fuzzing, and benchmarks), `cfire_stdlib_lookup`, `cfire_outline`, `cfire_inspect`, `cfire_eval`, and `cfire_doc_llm` (powered by `{} mcp`).
  - **Zero-arg Workspace CLI Commands**: `cfire check`, `cfire test`, `cfire bench`, `cfire fix`, `cfire build`, `cfire run`, `cfire init`, `cfire new <dir>`, `cfire inspect <target|cfire/CODE>`, `cfire outline --json`, and `cfire doc --llm`.
)", cfire_bin);
}

// Configures Editor LSP & AI Agent MCP integrations (VS Code, Cursor, Windsurf, Antigravity, Claude Code, Zed, Neovim, CLion)
inline void write_cfire_vscode_extension_bundle(const std::filesystem::path& ext_dir) {
    std::error_code ec;
    std::filesystem::create_directories(ext_dir / "syntaxes", ec);

    {
        std::ofstream p_ofs(ext_dir / "package.json", std::ios::trunc);
        p_ofs << std::format(R"cfjson({{
  "name": "cfire-lang",
  "publisher": "cfire",
  "version": "{}",
  "displayName": "cfire Language Support",
  "description": "Syntax highlighting, bracket matching, auto-indentation, and LSP language intelligence for cfire (.cf)",
  "engines": {{
    "vscode": "^1.75.0"
  }},
  "categories": ["Programming Languages"],
  "activationEvents": ["onLanguage:cfire"],
  "main": "./extension.js",
  "contributes": {{
    "languages": [
      {{
        "id": "cfire",
        "aliases": ["cfire", "cf"],
        "extensions": [".cf"],
        "configuration": "./language-configuration.json"
      }}
    ],
    "grammars": [
      {{
        "language": "cfire",
        "scopeName": "source.cfire",
        "path": "./syntaxes/cfire.tmLanguage.json"
      }}
    ],
    "commands": [
      {{
        "command": "cfire.restartServer",
        "title": "cfire: Restart Language Server"
      }}
    ],
    "configuration": {{
      "title": "cfire",
      "properties": {{
        "cfire.lsp.path": {{
          "type": "string",
          "default": "cfire",
          "description": "Path to the cfire executable used for LSP code intelligence."
        }},
        "cfire.lsp.arguments": {{
          "type": "array",
          "items": {{
            "type": "string"
          }},
          "default": ["lsp"],
          "description": "Arguments passed to the cfire binary to launch the language server."
        }},
        "cfire.inlayHints.enable": {{
          "type": "boolean",
          "default": true,
          "description": "Enable inferred type and parameter inlay hints for cfire files."
        }}
      }}
    }}
  }}
}}
)cfjson", cfire::version::kVersion);
    }

    {
        std::ofstream lc_ofs(ext_dir / "language-configuration.json", std::ios::trunc);
        lc_ofs << R"cfjson({
  "comments": {
    "lineComment": "//",
    "blockComment": ["/*", "*/"]
  },
  "brackets": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"]
  ],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"", "notIn": ["string"] },
    { "open": "'", "close": "'", "notIn": ["string", "comment"] },
    { "open": "`", "close": "`", "notIn": ["string", "comment"] }
  ],
  "surroundingPairs": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"],
    ["\"", "\""],
    ["'", "'"],
    ["`", "`"]
  ],
  "indentationRules": {
    "increaseIndentPattern": "^((?!\\/\\/).)*(\\{[^}\"'`]*|\\([^)\"'`]*|\\[[^\\]\"'`]*)$",
    "decreaseIndentPattern": "^((?!.*?\\/\\*).*\\*/)?\\s*[}\\])]"
  },
  "onEnterRules": [
    {
      "beforeText": "^.*\\{\\s*$",
      "afterText": "^\\s*\\}",
      "action": { "indent": "indentOutdent" }
    },
    {
      "beforeText": "^.*\\(\\s*$",
      "afterText": "^\\s*\\)",
      "action": { "indent": "indentOutdent" }
    },
    {
      "beforeText": "^.*\\[\\s*$",
      "afterText": "^\\s*\\]",
      "action": { "indent": "indentOutdent" }
    },
    {
      "beforeText": "^\\s*/\\*\\*(?!/)([^\\*]|\\*(?!/))*$",
      "afterText": "^\\s*\\*/$",
      "action": { "indent": "indentOutdent", "appendText": " * " }
    }
  ]
}
)cfjson";
    }

    {
        std::ofstream tm_ofs(ext_dir / "syntaxes" / "cfire.tmLanguage.json", std::ios::trunc);
        tm_ofs << R"cfjson({
  "scopeName": "source.cfire",
  "name": "cfire",
  "patterns": [
    { "include": "#comments" },
    { "include": "#strings" },
    { "include": "#keywords" },
    { "include": "#types" },
    { "include": "#numbers" }
  ],
  "repository": {
    "comments": {
      "patterns": [
        { "name": "comment.line.double-slash.cfire", "match": "//.*$" },
        { "name": "comment.block.cfire", "begin": "/\\*", "end": "\\*/" }
      ]
    },
    "strings": {
      "patterns": [
        {
          "name": "string.quoted.double.cfire",
          "begin": "\"",
          "end": "\"",
          "patterns": [
            { "name": "constant.character.escape.cfire", "match": "\\\\." }
          ]
        },
        { "name": "string.quoted.other.raw.cfire", "begin": "`", "end": "`" }
      ]
    },
    "keywords": {
      "patterns": [
        {
          "name": "keyword.control.cfire",
          "match": "\\b(package|import|fn|return|if|else|for|in|match|try|or|defer|break|continue|async|test|fuzz|bench|type|struct|enum|interface|mut|move|out)\\b"
        },
        {
          "name": "constant.language.cfire",
          "match": "\\b(true|false|none|some|ok|err)\\b"
        }
      ]
    },
    "types": {
      "patterns": [
        {
          "name": "storage.type.cfire",
          "match": "\\b(i8|i16|i32|i64|isize|int|u8|u16|u32|u64|usize|uint|byte|f32|f64|bool|str|char|rune|void|any|own|shared|map)\\b"
        }
      ]
    },
    "numbers": {
      "patterns": [
        { "name": "constant.numeric.cfire", "match": "\\b0x[0-9a-fA-F_]+\\b|\\b[0-9][0-9_]*(\\.[0-9_]+)?\\b" }
      ]
    }
  }
}
)cfjson";
    }

    {
        std::ofstream js_ofs(ext_dir / "extension.js", std::ios::trunc);
        js_ofs << R"cfjs('use strict';

const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const TOKEN_TYPES = [
  'type', 'struct', 'enum', 'interface', 'parameter', 'variable', 'property',
  'enumMember', 'function', 'method', 'keyword', 'modifier', 'string', 'number',
  'operator', 'namespace'
];
const TOKEN_MODIFIERS = [
  'declaration', 'definition', 'readonly', 'modification', 'defaultLibrary', 'async'
];
const SEMANTIC_LEGEND = new vscode.SemanticTokensLegend(TOKEN_TYPES, TOKEN_MODIFIERS);
const SELECTOR = [
  { language: 'cfire', scheme: 'file' },
  { language: 'cfire', scheme: 'untitled' }
];

function resolveCfireBinary() {
  const cfg = vscode.workspace.getConfiguration('cfire');
  const configured = cfg.get('lsp.path');
  if (typeof configured === 'string' && configured.trim()) {
    const p = configured.trim();
    if (path.isAbsolute(p) && fs.existsSync(p)) {
      return p;
    }
  }

  const exeName = process.platform === 'win32' ? 'cfire.exe' : 'cfire';
  const folders = vscode.workspace.workspaceFolders || [];
  for (const folder of folders) {
    const wsBin = path.join(folder.uri.fsPath, exeName);
    if (fs.existsSync(wsBin)) return wsBin;
  }

  const candidates = process.platform === 'win32'
    ? [path.join(os.homedir(), '.local', 'bin', exeName)]
    : [
        path.join(os.homedir(), '.local', 'bin', 'cfire'),
        '/opt/homebrew/bin/cfire',
        '/usr/local/bin/cfire'
      ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return (typeof configured === 'string' && configured.trim()) ? configured.trim() : exeName;
}

function getRootUri() {
  const folders = vscode.workspace.workspaceFolders || [];
  if (folders.length > 0) return folders[0].uri.toString();
  const active = vscode.window.activeTextEditor;
  if (active && active.document.uri.scheme === 'file') {
    return vscode.Uri.file(path.dirname(active.document.uri.fsPath)).toString();
  }
  return null;
}

function toVsPosition(p) {
  return new vscode.Position(Math.max(0, p?.line || 0), Math.max(0, p?.character || 0));
}

function toVsRange(r) {
  return new vscode.Range(toVsPosition(r?.start), toVsPosition(r?.end));
}

function toLspPosition(p) {
  return { line: p.line, character: p.character };
}

function toLspRange(r) {
  return { start: toLspPosition(r.start), end: toLspPosition(r.end) };
}

function toMdDocumentation(doc) {
  if (!doc) return undefined;
  if (typeof doc === 'string') return doc ? new vscode.MarkdownString(doc) : undefined;
  if (typeof doc.value === 'string') return doc.value ? new vscode.MarkdownString(doc.value) : undefined;
  return undefined;
}

function toWorkspaceEdit(lspEdit) {
  const edit = new vscode.WorkspaceEdit();
  if (!lspEdit || !lspEdit.changes) return edit;
  for (const [uriStr, textEdits] of Object.entries(lspEdit.changes)) {
    if (!Array.isArray(textEdits)) continue;
    const uri = vscode.Uri.parse(uriStr);
    for (const te of textEdits) {
      edit.replace(uri, toVsRange(te.range), String(te.newText ?? ''));
    }
  }
  return edit;
}

class CfireLspClient {
  constructor(output, diagnostics) {
    this.output = output;
    this.diagnostics = diagnostics;
    this.proc = null;
    this.recvBuf = Buffer.alloc(0);
    this.nextId = 1;
    this.pending = new Map();
    this.openDocs = new Set();
    this.initPromise = null;
    this.stopping = false;
    this.crashCount = 0;
  }

  async ensureStarted() {
    if (this.proc && !this.proc.killed && this.initPromise) {
      return this.initPromise;
    }
    this.startProcess();
    return this.initPromise;
  }

  startProcess() {
    this.stopping = false;
    const bin = resolveCfireBinary();
    const cfg = vscode.workspace.getConfiguration('cfire');
    const argsCfg = cfg.get('lsp.arguments');
    const args = Array.isArray(argsCfg) && argsCfg.length > 0 ? argsCfg.map(String) : ['lsp'];

    this.output.appendLine(`Starting cfire LSP: ${bin} ${args.join(' ')}`);
    const proc = cp.spawn(bin, args, { stdio: ['pipe', 'pipe', 'pipe'] });
    this.proc = proc;
    this.recvBuf = Buffer.alloc(0);
    this.openDocs.clear();

    proc.stdout.on('data', (chunk) => this.onData(chunk));
    proc.stderr.on('data', (chunk) => {
      const msg = chunk.toString('utf8').trimEnd();
      if (msg) this.output.appendLine(`[stderr] ${msg}`);
    });
    proc.on('error', (err) => {
      this.output.appendLine(`cfire LSP spawn error (${bin}): ${err.message}`);
      this.failAllPending(err);
      this.proc = null;
      this.initPromise = null;
    });
    proc.on('exit', (code, signal) => {
      this.output.appendLine(`cfire LSP exited (code=${code}, signal=${signal})`);
      this.failAllPending(new Error(`cfire LSP exited (${code})`));
      if (this.proc === proc) {
        this.proc = null;
        this.initPromise = null;
        this.openDocs.clear();
      }
      if (!this.stopping && this.crashCount < 3) {
        this.crashCount += 1;
        setTimeout(() => {
          if (!this.stopping) this.ensureStarted().catch(() => {});
        }, 500);
      }
    });

    this.initPromise = (async () => {
      const initResult = await this.sendRequestRaw('initialize', {
        processId: process.pid,
        rootUri: getRootUri(),
        capabilities: {
          textDocument: {
            synchronization: {
              didSave: true,
              willSave: false
            },
            completion: {
              completionItem: {
                snippetSupport: true,
                labelDetailsSupport: true,
                documentationFormat: ['markdown', 'plaintext']
              }
            },
            hover: {
              contentFormat: ['markdown', 'plaintext']
            },
            signatureHelp: {
              signatureInformation: {
                parameterInformation: { labelOffsetSupport: false }
              }
            },
            semanticTokens: {
              requests: { full: true },
              tokenTypes: TOKEN_TYPES,
              tokenModifiers: TOKEN_MODIFIERS,
              formats: ['relative']
            }
          }
        }
      }, 15000);

      this.crashCount = 0;
      this.sendNotificationRaw('initialized', {});
      for (const doc of vscode.workspace.textDocuments) {
        if (doc.languageId === 'cfire') {
          this.didOpenDoc(doc);
        }
      }
      return initResult;
    })();
  }

  failAllPending(err) {
    for (const [, entry] of this.pending) {
      clearTimeout(entry.timer);
      entry.reject(err);
    }
    this.pending.clear();
  }

  onData(chunk) {
    this.recvBuf = Buffer.concat([this.recvBuf, chunk]);
    while (true) {
      const hdrEnd = this.recvBuf.indexOf('\r\n\r\n');
      if (hdrEnd < 0) break;
      const hdrStr = this.recvBuf.slice(0, hdrEnd).toString('ascii');
      const m = /Content-Length:\s*(\d+)/i.exec(hdrStr);
      if (!m) {
        this.recvBuf = this.recvBuf.slice(hdrEnd + 4);
        continue;
      }
      const len = Number(m[1]);
      const bodyStart = hdrEnd + 4;
      if (this.recvBuf.length < bodyStart + len) break;
      const bodyBuf = this.recvBuf.slice(bodyStart, bodyStart + len);
      this.recvBuf = this.recvBuf.slice(bodyStart + len);

      let msg;
      try {
        msg = JSON.parse(bodyBuf.toString('utf8'));
      } catch (e) {
        this.output.appendLine(`Invalid JSON from cfire LSP: ${e.message}`);
        continue;
      }
      this.handleIncoming(msg);
    }
  }

  handleIncoming(msg) {
    if (msg && Object.prototype.hasOwnProperty.call(msg, 'id')) {
      const key = String(msg.id);
      const pending = this.pending.get(key);
      if (pending) {
        clearTimeout(pending.timer);
        this.pending.delete(key);
        if (msg.error) {
          pending.reject(new Error(msg.error.message || JSON.stringify(msg.error)));
        } else {
          pending.resolve(msg.result);
        }
      }
      return;
    }

    if (msg && msg.method === 'textDocument/publishDiagnostics' && msg.params) {
      const uriStr = msg.params.uri;
      if (!uriStr) return;
      const uri = vscode.Uri.parse(uriStr);
      const rawDiags = Array.isArray(msg.params.diagnostics) ? msg.params.diagnostics : [];
      const vsDiags = rawDiags.map((d) => {
        const sevMap = {
          1: vscode.DiagnosticSeverity.Error,
          2: vscode.DiagnosticSeverity.Warning,
          3: vscode.DiagnosticSeverity.Information,
          4: vscode.DiagnosticSeverity.Hint
        };
        const sev = sevMap[d.severity] ?? vscode.DiagnosticSeverity.Error;
        const diag = new vscode.Diagnostic(toVsRange(d.range), String(d.message || ''), sev);
        diag.source = d.source || 'cfire';
        if (d.code !== undefined) diag.code = d.code;
        return diag;
      });
      this.diagnostics.set(uri, vsDiags);
    }
  }

  writeFrame(obj) {
    if (!this.proc || !this.proc.stdin || !this.proc.stdin.writable) return false;
    const json = JSON.stringify(obj);
    const body = Buffer.from(json, 'utf8');
    const hdr = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii');
    this.proc.stdin.write(Buffer.concat([hdr, body]));
    return true;
  }

  sendNotificationRaw(method, params) {
    this.writeFrame({ jsonrpc: '2.0', method, params });
  }

  sendRequestRaw(method, params, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
      const id = this.nextId++;
      const key = String(id);
      const timer = setTimeout(() => {
        this.pending.delete(key);
        reject(new Error(`cfire LSP timeout for ${method}`));
      }, timeoutMs);
      this.pending.set(key, { resolve, reject, timer });
      if (!this.writeFrame({ jsonrpc: '2.0', id, method, params })) {
        clearTimeout(timer);
        this.pending.delete(key);
        reject(new Error(`cfire LSP stdin not writable for ${method}`));
      }
    });
  }

  async request(method, params, doc = null) {
    try {
      await this.ensureStarted();
      if (doc) this.ensureDocOpen(doc);
      return await this.sendRequestRaw(method, params);
    } catch (err) {
      this.output.appendLine(`${method} failed: ${err.message}`);
      return null;
    }
  }

  didOpenDoc(doc) {
    if (!doc || doc.languageId !== 'cfire') return;
    const uri = doc.uri.toString();
    if (this.openDocs.has(uri)) return;
    this.openDocs.add(uri);
    this.sendNotificationRaw('textDocument/didOpen', {
      textDocument: {
        uri,
        languageId: 'cfire',
        version: doc.version,
        text: doc.getText()
      }
    });
  }

  ensureDocOpen(doc) {
    if (!doc || doc.languageId !== 'cfire') return;
    const uri = doc.uri.toString();
    if (!this.openDocs.has(uri)) {
      this.didOpenDoc(doc);
    }
  }

  didChangeDoc(event) {
    const doc = event.document;
    if (!doc || doc.languageId !== 'cfire') return;
    if (!this.proc || !this.initPromise) return;
    const uri = doc.uri.toString();
    if (!this.openDocs.has(uri)) {
      this.didOpenDoc(doc);
      return;
    }
    const contentChanges = event.contentChanges.map((c) => ({
      range: toLspRange(c.range),
      rangeLength: c.rangeLength,
      text: c.text
    }));
    this.sendNotificationRaw('textDocument/didChange', {
      textDocument: { uri, version: doc.version },
      contentChanges
    });
  }

  didSaveDoc(doc) {
    if (!doc || doc.languageId !== 'cfire' || !this.proc) return;
    const uri = doc.uri.toString();
    if (!this.openDocs.has(uri)) {
      this.didOpenDoc(doc);
      return;
    }
    this.sendNotificationRaw('textDocument/didSave', {
      textDocument: { uri },
      text: doc.getText()
    });
  }

  didCloseDoc(doc) {
    if (!doc || doc.languageId !== 'cfire') return;
    const uri = doc.uri.toString();
    this.openDocs.delete(uri);
    this.diagnostics.delete(doc.uri);
    if (this.proc) {
      this.sendNotificationRaw('textDocument/didClose', {
        textDocument: { uri }
      });
    }
  }

  async stop() {
    this.stopping = true;
    const proc = this.proc;
    if (!proc) return;
    try {
      await this.sendRequestRaw('shutdown', null, 1500);
      this.sendNotificationRaw('exit', null);
    } catch (_) {
      // Ignore shutdown error on exit
    }
    setTimeout(() => {
      try {
        if (!proc.killed) proc.kill();
      } catch (_) {}
    }, 300);
    this.proc = null;
    this.initPromise = null;
    this.openDocs.clear();
  }
}

let client = null;

function activate(context) {
  const output = vscode.window.createOutputChannel('cfire LSP');
  const diagnostics = vscode.languages.createDiagnosticCollection('cfire');
  client = new CfireLspClient(output, diagnostics);
  context.subscriptions.push(output, diagnostics);

  client.ensureStarted().catch((err) => {
    output.appendLine(`Initial start failed: ${err.message}`);
  });

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => {
      if (doc.languageId === 'cfire') {
        client.ensureStarted().then(() => client.didOpenDoc(doc)).catch(() => {});
      }
    }),
    vscode.workspace.onDidChangeTextDocument((e) => client.didChangeDoc(e)),
    vscode.workspace.onDidSaveTextDocument((doc) => client.didSaveDoc(doc)),
    vscode.workspace.onDidCloseTextDocument((doc) => client.didCloseDoc(doc)),
    vscode.commands.registerCommand('cfire.restartServer', async () => {
      await client.stop();
      client.crashCount = 0;
      await client.ensureStarted();
    })
  );

  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      SELECTOR,
      {
        async provideCompletionItems(doc, pos, _token, compCtx) {
          const res = await client.request('textDocument/completion', {
            textDocument: { uri: doc.uri.toString() },
            position: toLspPosition(pos),
            context: compCtx ? { triggerKind: compCtx.triggerKind, triggerCharacter: compCtx.triggerCharacter } : undefined
          }, doc);
          if (!res) return undefined;
          const isIncomplete = Array.isArray(res) ? false : Boolean(res.isIncomplete);
          const rawItems = Array.isArray(res) ? res : (Array.isArray(res.items) ? res.items : []);
          const items = rawItems.map((it) => {
            const kind = typeof it.kind === 'number' ? Math.max(0, it.kind - 1) : vscode.CompletionItemKind.Text;
            const label = it.labelDetails
              ? {
                  label: String(it.label),
                  detail: it.labelDetails.detail ? String(it.labelDetails.detail) : undefined,
                  description: it.labelDetails.description ? String(it.labelDetails.description) : undefined
                }
              : String(it.label);
            const ci = new vscode.CompletionItem(label, kind);
            if (it.detail) ci.detail = String(it.detail);
            const docMd = toMdDocumentation(it.documentation);
            if (docMd) ci.documentation = docMd;
            if (it.sortText) ci.sortText = String(it.sortText);
            if (it.filterText) ci.filterText = String(it.filterText);
            if (it.textEdit && it.textEdit.range) {
              ci.range = toVsRange(it.textEdit.range);
              const newText = String(it.textEdit.newText ?? it.label);
              ci.insertText = it.insertTextFormat === 2 ? new vscode.SnippetString(newText) : newText;
            } else if (it.insertText !== undefined) {
              const ins = String(it.insertText);
              ci.insertText = it.insertTextFormat === 2 ? new vscode.SnippetString(ins) : ins;
            }
            if (Array.isArray(it.additionalTextEdits)) {
              ci.additionalTextEdits = it.additionalTextEdits.map((ae) =>
                vscode.TextEdit.replace(toVsRange(ae.range), String(ae.newText ?? ''))
              );
            }
            return ci;
          });
          return new vscode.CompletionList(items, isIncomplete);
        }
      },
      '.', ':'
    ),

    vscode.languages.registerHoverProvider(SELECTOR, {
      async provideHover(doc, pos) {
        const res = await client.request('textDocument/hover', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos)
        }, doc);
        if (!res || !res.contents) return null;
        const md = toMdDocumentation(res.contents) || new vscode.MarkdownString(String(res.contents));
        return new vscode.Hover(md, res.range ? toVsRange(res.range) : undefined);
      }
    }),

    vscode.languages.registerSignatureHelpProvider(
      SELECTOR,
      {
        async provideSignatureHelp(doc, pos) {
          const res = await client.request('textDocument/signatureHelp', {
            textDocument: { uri: doc.uri.toString() },
            position: toLspPosition(pos)
          }, doc);
          if (!res || !Array.isArray(res.signatures) || res.signatures.length === 0) return null;
          const sh = new vscode.SignatureHelp();
          sh.activeSignature = res.activeSignature || 0;
          sh.activeParameter = res.activeParameter || 0;
          sh.signatures = res.signatures.map((s) => {
            const si = new vscode.SignatureInformation(String(s.label), toMdDocumentation(s.documentation));
            if (Array.isArray(s.parameters)) {
              si.parameters = s.parameters.map((p) => new vscode.ParameterInformation(p.label, toMdDocumentation(p.documentation)));
            }
            return si;
          });
          return sh;
        }
      },
      '(', ','
    ),

    vscode.languages.registerDefinitionProvider(SELECTOR, {
      async provideDefinition(doc, pos) {
        const res = await client.request('textDocument/definition', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos)
        }, doc);
        if (!res) return null;
        const arr = Array.isArray(res) ? res : [res];
        return arr.filter((l) => l && l.uri && l.range).map((l) => new vscode.Location(vscode.Uri.parse(l.uri), toVsRange(l.range)));
      }
    }),

    vscode.languages.registerTypeDefinitionProvider(SELECTOR, {
      async provideTypeDefinition(doc, pos) {
        const res = await client.request('textDocument/typeDefinition', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos)
        }, doc);
        if (!res) return null;
        const arr = Array.isArray(res) ? res : [res];
        return arr.filter((l) => l && l.uri && l.range).map((l) => new vscode.Location(vscode.Uri.parse(l.uri), toVsRange(l.range)));
      }
    }),

    vscode.languages.registerImplementationProvider(SELECTOR, {
      async provideImplementation(doc, pos) {
        const res = await client.request('textDocument/implementation', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos)
        }, doc);
        if (!res) return null;
        const arr = Array.isArray(res) ? res : [res];
        return arr.filter((l) => l && l.uri && l.range).map((l) => new vscode.Location(vscode.Uri.parse(l.uri), toVsRange(l.range)));
      }
    }),

    vscode.languages.registerReferenceProvider(SELECTOR, {
      async provideReferences(doc, pos, ctx) {
        const res = await client.request('textDocument/references', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos),
          context: { includeDeclaration: ctx ? Boolean(ctx.includeDeclaration) : true }
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.filter((l) => l && l.uri && l.range).map((l) => new vscode.Location(vscode.Uri.parse(l.uri), toVsRange(l.range)));
      }
    }),

    vscode.languages.registerDocumentHighlightProvider(SELECTOR, {
      async provideDocumentHighlights(doc, pos) {
        const res = await client.request('textDocument/documentHighlight', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos)
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.filter((h) => h && h.range).map((h) =>
          new vscode.DocumentHighlight(toVsRange(h.range), typeof h.kind === 'number' ? Math.max(0, h.kind - 1) : vscode.DocumentHighlightKind.Text)
        );
      }
    }),

    vscode.languages.registerDocumentSymbolProvider(SELECTOR, {
      async provideDocumentSymbols(doc) {
        const res = await client.request('textDocument/documentSymbol', {
          textDocument: { uri: doc.uri.toString() }
        }, doc);
        if (!Array.isArray(res)) return [];
        const conv = (s) => {
          const kind = typeof s.kind === 'number' ? Math.max(0, s.kind - 1) : vscode.SymbolKind.Function;
          if (s.range && s.selectionRange) {
            const ds = new vscode.DocumentSymbol(String(s.name), String(s.detail || ''), kind, toVsRange(s.range), toVsRange(s.selectionRange));
            if (Array.isArray(s.children)) ds.children = s.children.map(conv);
            return ds;
          }
          if (s.location && s.location.uri && s.location.range) {
            return new vscode.SymbolInformation(String(s.name), kind, String(s.containerName || ''), new vscode.Location(vscode.Uri.parse(s.location.uri), toVsRange(s.location.range)));
          }
          return null;
        };
        return res.map(conv).filter(Boolean);
      }
    }),

    vscode.languages.registerWorkspaceSymbolProvider({
      async provideWorkspaceSymbols(query) {
        const res = await client.request('workspace/symbol', { query: String(query || '') });
        if (!Array.isArray(res)) return [];
        return res.filter((s) => s && s.location && s.location.uri).map((s) =>
          new vscode.SymbolInformation(
            String(s.name),
            typeof s.kind === 'number' ? Math.max(0, s.kind - 1) : vscode.SymbolKind.Function,
            String(s.containerName || ''),
            new vscode.Location(vscode.Uri.parse(s.location.uri), toVsRange(s.location.range))
          )
        );
      }
    }),

    vscode.languages.registerFoldingRangeProvider(SELECTOR, {
      async provideFoldingRanges(doc) {
        const res = await client.request('textDocument/foldingRange', {
          textDocument: { uri: doc.uri.toString() }
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.map((f) => new vscode.FoldingRange(f.startLine, f.endLine));
      }
    }),

    vscode.languages.registerCodeActionsProvider(SELECTOR, {
      async provideCodeActions(doc, range, ctx) {
        const res = await client.request('textDocument/codeAction', {
          textDocument: { uri: doc.uri.toString() },
          range: toLspRange(range),
          context: {
            diagnostics: (ctx?.diagnostics || []).map((d) => ({ code: d.code, message: d.message }))
          }
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.map((a) => {
          const ca = new vscode.CodeAction(String(a.title || 'Quick Fix'), vscode.CodeActionKind.QuickFix);
          ca.isPreferred = Boolean(a.isPreferred);
          if (a.edit) ca.edit = toWorkspaceEdit(a.edit);
          return ca;
        });
      }
    }),

    vscode.languages.registerRenameProvider(SELECTOR, {
      async prepareRename(doc, pos) {
        const res = await client.request('textDocument/prepareRename', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos)
        }, doc);
        if (!res) return null;
        return toVsRange(res.range || res);
      },
      async provideRenameEdits(doc, pos, newName) {
        const res = await client.request('textDocument/rename', {
          textDocument: { uri: doc.uri.toString() },
          position: toLspPosition(pos),
          newName: String(newName)
        }, doc);
        return res ? toWorkspaceEdit(res) : null;
      }
    }),

    vscode.languages.registerDocumentFormattingEditProvider(SELECTOR, {
      async provideDocumentFormattingEdits(doc) {
        const res = await client.request('textDocument/formatting', {
          textDocument: { uri: doc.uri.toString() },
          options: { tabSize: 4, insertSpaces: true }
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.map((te) => vscode.TextEdit.replace(toVsRange(te.range), String(te.newText ?? '')));
      }
    }),

    vscode.languages.registerDocumentRangeFormattingEditProvider(SELECTOR, {
      async provideDocumentRangeFormattingEdits(doc, range) {
        const res = await client.request('textDocument/rangeFormatting', {
          textDocument: { uri: doc.uri.toString() },
          range: toLspRange(range),
          options: { tabSize: 4, insertSpaces: true }
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.map((te) => vscode.TextEdit.replace(toVsRange(te.range), String(te.newText ?? '')));
      }
    }),

    vscode.languages.registerInlayHintsProvider(SELECTOR, {
      async provideInlayHints(doc) {
        if (!vscode.workspace.getConfiguration('cfire').get('inlayHints.enable', true)) return [];
        const res = await client.request('textDocument/inlayHint', {
          textDocument: { uri: doc.uri.toString() }
        }, doc);
        if (!Array.isArray(res)) return [];
        return res.map((h) => {
          const hint = new vscode.InlayHint(toVsPosition(h.position), String(h.label || ''), h.kind || 1);
          hint.paddingLeft = Boolean(h.paddingLeft);
          hint.paddingRight = Boolean(h.paddingRight);
          return hint;
        });
      }
    }),

    vscode.languages.registerDocumentSemanticTokensProvider(SELECTOR, {
      async provideDocumentSemanticTokens(doc) {
        const res = await client.request('textDocument/semanticTokens/full', {
          textDocument: { uri: doc.uri.toString() }
        }, doc);
        if (!res || !Array.isArray(res.data)) return null;
        return new vscode.SemanticTokens(new Uint32Array(res.data));
      }
    }, SEMANTIC_LEGEND)
  );
}

async function deactivate() {
  if (client) {
    await client.stop();
    client = null;
  }
}

module.exports = { activate, deactivate };
)cfjs";
    }
}

[[nodiscard]] inline auto ide_json_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += std::format("\\u{:04x}", c);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

inline void register_vscode_extension_manifest(
    const std::filesystem::path& ext_root,
    std::string_view folder_name,
    std::string_view version
) {
    std::error_code ec;
    std::filesystem::create_directories(ext_root, ec);

    std::filesystem::path full_dir = ext_root / folder_name;
    std::string fs_path = full_dir.string();
    std::string uri_path = fs_path;
    std::replace(uri_path.begin(), uri_path.end(), '\\', '/');
    if (uri_path.size() >= 2 && std::isalpha(static_cast<unsigned char>(uri_path[0])) && uri_path[1] == ':') {
        uri_path.insert(uri_path.begin(), '/');
    }
    std::string external = "file://" + uri_path;
    auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();

    std::string new_entry = std::format(
        R"({{"identifier":{{"id":"cfire.cfire-lang"}},"version":"{}","location":{{"$mid":1,"fsPath":"{}","external":"{}","path":"{}","scheme":"file"}},"relativeLocation":"{}","metadata":{{"installedTimestamp":{},"pinned":false,"source":"vsix"}}}})",
        ide_json_escape(version),
        ide_json_escape(fs_path),
        ide_json_escape(external),
        ide_json_escape(uri_path),
        ide_json_escape(folder_name),
        ts_ms);

    std::vector<std::string> entries;
    std::filesystem::path manifest_path = ext_root / "extensions.json";
    if (std::filesystem::exists(manifest_path, ec)) {
        std::ifstream ifs(manifest_path, std::ios::binary);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string existing = ss.str();

        std::size_t i = existing.find('[');
        if (i != std::string::npos) {
            ++i;
            while (i < existing.size()) {
                while (i < existing.size() &&
                       (existing[i] == ' ' || existing[i] == '\t' || existing[i] == '\n' ||
                        existing[i] == '\r' || existing[i] == ',')) {
                    ++i;
                }
                if (i >= existing.size() || existing[i] == ']') break;
                if (existing[i] != '{') {
                    ++i;
                    continue;
                }
                std::size_t start = i;
                int depth = 0;
                bool in_str = false;
                while (i < existing.size()) {
                    char c = existing[i];
                    if (in_str) {
                        if (c == '\\' && i + 1 < existing.size()) {
                            i += 2;
                            continue;
                        }
                        if (c == '"') in_str = false;
                    } else {
                        if (c == '"') in_str = true;
                        else if (c == '{') ++depth;
                        else if (c == '}') {
                            --depth;
                            if (depth == 0) {
                                ++i;
                                break;
                            }
                        }
                    }
                    ++i;
                }
                std::string obj = existing.substr(start, i - start);
                if (!obj.contains(R"("id":"cfire.cfire-lang")") &&
                    !obj.contains(R"("id": "cfire.cfire-lang")")) {
                    entries.push_back(std::move(obj));
                }
            }
        }
    }

    entries.push_back(std::move(new_entry));
    std::string out = "[";
    for (std::size_t idx = 0; idx < entries.size(); ++idx) {
        if (idx > 0) out += ",";
        out += entries[idx];
    }
    out += "]";

    std::filesystem::path tmp_path = ext_root / "extensions.json.tmp";
    {
        std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
        ofs << out;
    }
    std::filesystem::rename(tmp_path, manifest_path, ec);
}

[[nodiscard]] CFIRE_LSP_IDE_INLINE auto configure_ide(
    const std::filesystem::path& repo_root,
    std::string_view editor_input,
    bool global_scope
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
    if (has_target("vscode", targets) || has_target("cursor", targets) || has_target("windsurf", targets) || has_target("antigravity", targets)) {
        auto vscode_dir = repo_root / ".vscode";
        std::filesystem::create_directories(vscode_dir, ec);
        std::string vs_settings = std::format(R"({{
  "files.associations": {{
    "*.cf": "cfire"
  }},
  "[cfire]": {{
    "editor.defaultFormatter": "cfire.cfire-lang",
    "editor.formatOnSave": true,
    "editor.autoIndent": "full"
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
            e_ofs << "{\n  \"recommendations\": [\"cfire.cfire-lang\"]\n}\n";
        }
        write_cfire_vscode_extension_bundle(vscode_dir / "extensions" / "cfire.cfire-lang");
        std::vector<std::string> global_ext_paths;
        if (global_scope) {
            if (const char* home = std::getenv("HOME")) {
                std::filesystem::path h(home);
                for (const char* ext_root : {".vscode/extensions", ".antigravity/extensions", ".cursor/extensions"}) {
                    auto root_dir = h / ext_root;
                    if (std::filesystem::exists(root_dir, ec)) {
                        for (const auto& entry : std::filesystem::directory_iterator(root_dir, ec)) {
                            std::string fname = entry.path().filename().string();
                            if (fname == "cfire.cfire-lang" || fname.starts_with("cfire.cfire-lang-")) {
                                std::filesystem::remove_all(entry.path(), ec);
                            }
                        }
                        std::string folder_name = std::format("cfire.cfire-lang-{}", cfire::version::kVersion);
                        auto dst = root_dir / folder_name;
                        write_cfire_vscode_extension_bundle(dst);
                        register_vscode_extension_manifest(root_dir, folder_name, cfire::version::kVersion);
                        global_ext_paths.push_back(dst.string());
                    }
                }
            }
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
            std::string desc = "VS Code (.vscode/settings.json + .vscode/mcp.json + .vscode/extensions/cfire.cfire-lang";
            if (!global_ext_paths.empty()) {
                desc += "; global extension installed in " + global_ext_paths.front();
                for (std::size_t gi = 1; gi < global_ext_paths.size(); ++gi) desc += ", " + global_ext_paths[gi];
                desc += ")";
            } else {
                desc += "; run `cfire ide vscode --global` to install into ~/.vscode/extensions/)";
            }
            configured.push_back(std::move(desc));
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
        std::string claude_plugin = std::format(R"({{
  "name": "cfire",
  "version": "{}",
  "description": "Official cfire (.cf) Language Server (LSP) plugin for Claude Code.",
  "lspServers": {{
    "cfire": {{
      "command": "cfire",
      "args": ["lsp"],
      "transport": "stdio",
      "extensionToLanguage": {{
        ".cf": "cfire"
      }}
    }}
  }}
}}
)", cfire::version::kVersion);
        {
            std::ofstream p_ofs(claude_plugin_dir / "plugin.json", std::ios::trunc);
            p_ofs << claude_plugin;
        }
        std::string claude_marketplace = std::format(R"({{
  "name": "cfire",
  "owner": {{
    "name": "Memo Sandoval"
  }},
  "plugins": [
    {{
      "name": "cfire",
      "source": "./",
      "description": "Official cfire (.cf) Language Server (LSP) plugin for Claude Code.",
      "version": "{}"
    }}
  ]
}}
)", cfire::version::kVersion);
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
    "Rust": ["cf"]
  }},
  "inlay_hints": {{
    "enabled": true
  }},
  "languages": {{
    "Rust": {{
      "tab_size": 4,
      "formatter": "language_server",
      "format_on_save": "on",
      "language_servers": ["rust-analyzer"]
    }}
  }},
  "lsp": {{
    "rust-analyzer": {{
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

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_IDE_IMPL)

} // namespace cfire::lsp

#undef CFIRE_LSP_IDE_INLINE
