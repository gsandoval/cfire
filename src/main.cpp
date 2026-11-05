// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)

#include "buildsys.hpp"
#include "diagnostics.hpp"
#include "docgen.hpp"
#include "doctor.hpp"
#include "formatter.hpp"
#include "lsp.hpp"
#include "mcp.hpp"
#include "refactor.hpp"
#include "server.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#if __has_include(<print>)
#include <print>
#endif
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

void print_usage() {
    std::println("Usage: cfire <command> [args...]");
    std::println("Commands:");
    std::println("  init     [--template cli|tako|gui|webui] [--force] [dir] Initialize a new cfire project in current/target directory");
    std::println("  new      [--template cli|tako|gui|webui] [--force] <dir> Create a new cfire project directory");
    std::println("  run      [--web|--ios] [target|file.cf] Run native app, web app (--web), or iOS Simulator app (--ios)");
    std::println("  test     [--json] [--filter <pat>] [--fuzz-runs <N>] [--seed <S>] [path|file.cf] Compile and execute test, fuzz & doc-test suites");
    std::println("  bench    [--json] [--filter <pat>] [--bench-time <ms>] [path|file.cf] Run statistical micro-benchmarks (`bench \"...\"`)");
    std::println("  build    [--target wasm|ios-sim|ios|<triple>] [--icon <path>] [--name <name>] [target|file.cf]");
    std::println("  serve    [--port <n>] [--no-open] [target|path] Build & serve WebAssembly / static web app with zero config");
    std::println("  eval     [-e <expr>|code]     Evaluate one-shot statement, expression, or stdin");
    std::println("  refactor rename <loc> <to>    Semantic symbol rename across workspace (--diff, --apply, --json)");
    std::println("  check    [--json] [--fix] [path|file.cf] Type-check, verify parameter intents, and inspect C++ headers");
    std::println("  fix      [--dry-run] [--json] [path|file.cf] Auto-fix missing imports, mut annotations, and enum matches");
    std::println("  inspect  <target> [--json]    Stateless symbol/type hover, foreign C header, or stdlib docs");
    std::println("  outline  [--json] [path]      Token-compressed outline of packages, types, and functions");
    std::println("  fmt      [--check] <path>     Canonically format .cf file(s) or directory");
    std::println("  lsp                           Start built-in Language Server Protocol (JSON-RPC 2.0 stdio)");
    std::println("  mcp      [--setup|--print-config] Start MCP server over stdio, or configure AI agent MCP clients");
    std::println("  ide      [target|--all|--status] [--global] Configure IDE & AI agents (VS Code, Antigravity, Claude Code, Cursor, Windsurf, Zed, Neovim, CLion)");
    std::println("  doc      [--check|--llm|<f>]  Generate docs site, guide, or LLM agent reference sheet");
    std::println("  emit-cpp <file.cf>            Print generated human-readable C++23 source");
    std::println("  eject    <file.cf> [out_dir]  Export standalone C++23 source tree + CMakeLists.txt");
    std::println("  vendor   [dest_dir]           Fetch and vendor all build.cf dependencies for offline builds");
    std::println("  install  [--prefix <dir>|--user|--portable <dir>] Install cfire binary & runtime headers (FHS prefix or portable bundle)");
    std::println("  doctor   [--json]             Verify C++23 compiler, runtime headers, and native libraries");
    std::println("  setup    [--yes] [--wasm]     Install missing host dependencies and warm C++23 PCH cache");
    std::println("  version  (--version, -v)      Print cfire toolchain version");
}

[[nodiscard]] auto is_ignored_walk_dir(const std::filesystem::path& p) -> bool {
    std::string s = p.string();
    return s.contains("/.cfire") || s.contains("/.tako") || s.contains("/.git") || s.contains("/dist") ||
           s.contains("/vendor") || s.contains("/node_modules") || s.contains("/ejected_cpp");
}

[[nodiscard]] auto run_scaffold_command(
    std::string_view sub,
    const std::filesystem::path& target_dir,
    std::string_view tmpl,
    bool force
) -> int {
    std::error_code ec;
    std::filesystem::path abs_dir = std::filesystem::absolute(target_dir, ec);
    if (ec || abs_dir.empty()) abs_dir = target_dir;
    std::filesystem::create_directories(abs_dir, ec);
    if (ec) {
        std::println(stderr, "error creating directory '{}': {}", abs_dir.string(), ec.message());
        return 1;
    }

    std::string proj_name = abs_dir.filename().string();
    if (proj_name.empty() || proj_name == "." || proj_name == "/") {
        proj_name = std::filesystem::current_path(ec).filename().string();
    }
    if (proj_name.empty()) proj_name = "cfire_app";

    auto build_cf_path = abs_dir / "build.cf";
    auto main_cf_path = abs_dir / "main.cf";
    auto test_cf_path = abs_dir / "main_test.cf";
    auto gitignore_path = abs_dir / ".gitignore";

    if (!force && (std::filesystem::exists(build_cf_path, ec) || std::filesystem::exists(main_cf_path, ec))) {
        std::println(stderr, "error: project files already exist in '{}' (pass `--force` to overwrite)", abs_dir.string());
        return 1;
    }

    std::string build_cf_txt;
    std::string main_cf_txt;
    std::string test_cf_txt;
    std::string ui_html_txt;

    if (tmpl == "tako") {
        build_cf_txt = std::format(R"CF(package build

app := Executable{{
    name:        "{}",
    entry:       "main.cf",
    targets:     ["native", "wasm", "ios-sim"],
    subsystem:   "gui",
    web_profile: "canvas",
}}
)CF", proj_name);
        main_cf_txt = std::format(R"CF(package main

import "pkg:tako"

fn greet_label(count: i32) -> str {{
    return "Clicks: {{count}}"
}}

fn main() {{
    mut app := tako.new_app("{}", 420, 320)
    defer app.close()

    mut count: i32 = 0
    for app.step() {{
        app.begin_frame()
        defer app.end_frame()
        if app.button(greet_label(count)) {{
            count += 1
        }}
    }}
}}
)CF", proj_name);
        test_cf_txt = R"CF(package main

test "greet_label formats click count" {
    assert(greet_label(3) == "Clicks: 3")
}
)CF";
    } else if (tmpl == "gui") {
        build_cf_txt = std::format(R"CF(package build

app := Executable{{
    name:        "{}",
    entry:       "main.cf",
    targets:     ["native", "wasm", "ios-sim"],
    subsystem:   "gui",
    web_profile: "canvas",
}}
)CF", proj_name);
        main_cf_txt = std::format(R"CF(package main

import "vendor:raylib" as rl

fn window_title(name: str) -> str {{
    return "{{name}} — cfire + raylib"
}}

fn main() {{
    rl.InitWindow(800, 450, window_title("{}"))
    defer rl.CloseWindow()
    rl.SetTargetFPS(60)

    for !rl.WindowShouldClose() {{
        rl.BeginDrawing()
        defer rl.EndDrawing()
        rl.ClearBackground(rl.RAYWHITE)
        rl.DrawText("Hello from {}!", 40, 40, 28, rl.DARKGRAY)
    }}
}}
)CF", proj_name, proj_name);
        test_cf_txt = std::format(R"CF(package main

test "window_title formats name" {{
    assert(window_title("{0}") == "{0} — cfire + raylib")
}}
)CF", proj_name);
    } else if (tmpl == "webui") {
        build_cf_txt = std::format(R"CF(package build

app := Executable{{
    name:      "{}",
    entry:     "main.cf",
    targets:   ["native", "ios-sim"],
    subsystem: "gui",
}}
)CF", proj_name);
        main_cf_txt = std::format(R"CF(package main

import "ext:webui"

type GreetReq struct {{
    name:  str `json:"name"`
    count: i32 `json:"count"`
}}

type GreetRes struct {{
    message: str `json:"message"`
    count:   i32 `json:"count"`
}}

fn format_greeting(req: GreetReq) -> GreetRes {{
    next_count := req.count + 1
    return GreetRes{{
        message: "Hello, {{req.name}}! (clicked {{next_count}} times)",
        count:   next_count,
    }}
}}

fn build_app() -> webui.App {{
    mut app := webui.new_app("{}", 960, 640)
    app.serve_dir("./ui")
    app.bind[GreetReq]("greet") |req| {{
        res := format_greeting(req)
        app.emit("greet:updated", res)
        return res
    }}
    app.bind_action("ping") || {{
        return "pong"
    }}
    return app
}}

fn main() {{
    mut app := build_app()
    defer app.close()
    app.run()
}}
)CF", proj_name);
        test_cf_txt = R"CF(package main

test "webui bound greet RPC round-trip" {
    mut app := build_app()
    defer app.close()
    raw := try app.call_rpc("greet", "{\"name\":\"Developer\",\"count\":2}")
    assert(raw.contains("\"count\":3"))
    assert(app.emitted_events_count("greet:updated") == 1)
}
)CF";
        ui_html_txt = std::format(R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{0}</title>
  <style>
    body {{
      margin: 0;
      background: #0d1117;
      color: #e6edf3;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
    }}
    .card {{
      background: #161b22;
      border: 1px solid #30363d;
      border-radius: 12px;
      padding: 28px;
      max-width: 440px;
      width: 100%;
    }}
    button {{
      background: #238636;
      color: #fff;
      border: none;
      border-radius: 8px;
      padding: 10px 18px;
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
    }}
  </style>
</head>
<body>
  <div class="card">
    <h2>{0}</h2>
    <p id="status">Ready</p>
    <button id="btn">Invoke cfire Backend</button>
  </div>
  <script>
    let count = 0;
    window.runtime.EventsOn("greet:updated", (ev) => {{
      document.getElementById("status").textContent = ev.message;
    }});
    document.getElementById("btn").addEventListener("click", async () => {{
      const res = await window.backend.greet({{ name: "cfire", count }});
      count = res.count;
    }});
  </script>
</body>
</html>
)HTML", proj_name);
    } else {
        // Default: cli
        build_cf_txt = std::format(R"CF(package build

app := Executable{{
    name:  "{}",
    entry: "main.cf",
}}
)CF", proj_name);
        main_cf_txt = std::format(R"CF(package main

fn greet(name: str) -> str {{
    return "Hello, {{name}}!"
}}

fn main() {{
    println("{{}}", greet("{}"))
}}
)CF", proj_name);
        test_cf_txt = std::format(R"CF(package main

test "greet returns expected message" {{
    assert(greet("{0}") == "Hello, {0}!")
}}
)CF", proj_name);
    }

    std::string gitignore_txt = std::format(".cfire/\ndist/\n{}\n*.app/\n", proj_name);

    auto write_f = [](const std::filesystem::path& p, std::string_view content) -> bool {
        std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs << content;
        return static_cast<bool>(ofs);
    };

    if (!write_f(build_cf_path, build_cf_txt) ||
        !write_f(main_cf_path, main_cf_txt) ||
        !write_f(test_cf_path, test_cf_txt) ||
        !write_f(gitignore_path, gitignore_txt)) {
        std::println(stderr, "error writing scaffold files in '{}'", abs_dir.string());
        return 1;
    }
    if (!ui_html_txt.empty()) {
        auto ui_dir = abs_dir / "ui";
        std::filesystem::create_directories(ui_dir, ec);
        if (!write_f(ui_dir / "index.html", ui_html_txt)) {
            std::println(stderr, "error writing ui/index.html in '{}'", abs_dir.string());
            return 1;
        }
    }

    std::println("✓ Initialized cfire {} project '{}' in {}", tmpl, proj_name, abs_dir.string());
    std::println("  + build.cf");
    std::println("  + main.cf");
    std::println("  + main_test.cf");
    if (!ui_html_txt.empty()) {
        std::println("  + ui/index.html");
    }
    std::println("  + .gitignore");
    if (sub == "new") {
        std::println("\nNext steps:\n  cd {}\n  cfire run\n  cfire test", target_dir.string());
    } else {
        std::println("\nNext steps:\n  cfire run\n  cfire test");
    }
    return 0;
}

auto wrap_eval_code(std::string_view raw_code, bool expr_mode) -> std::string {
    auto tokens_res = cfire::frontend::tokenize("<eval>", raw_code);
    if (!tokens_res) {
        return std::string(raw_code);
    }
    const auto& tokens = *tokens_res;
    if (!tokens.empty() && tokens[0].kind == cfire::frontend::TokenKind::KwPackage) {
        return std::string(raw_code);
    }

    const auto& catalog = cfire::doc::stdlib_catalog();
    std::set<std::string> auto_imports;

    // Always include essential prelude modules
    auto_imports.insert("core:fs");
    auto_imports.insert("core:str");
    auto_imports.insert("core:math");
    auto_imports.insert("core:time");
    auto_imports.insert("core:unicode");
    auto_imports.insert("ext:json");

    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].kind == cfire::frontend::TokenKind::Ident &&
            tokens[i + 1].kind == cfire::frontend::TokenKind::Dot) {
            std::string_view alias = tokens[i].lexeme;
            for (const auto& mod : catalog) {
                if (mod.ns_alias == alias) {
                    auto_imports.insert(mod.import_path);
                    break;
                }
            }
        }
    }

    auto lines = cfire::diag::split_lines(raw_code);
    int token_idx = 0;
    int brace_depth = 0;
    bool in_decl_block = false;
    std::string current_block;

    std::vector<std::string> user_imports;
    std::vector<std::string> decls;
    std::vector<std::string> stmts;

    for (int ln = 1; ln <= static_cast<int>(lines.size()); ++ln) {
        const auto& line = lines[static_cast<std::size_t>(ln - 1)];

        bool line_has_top_level_decl = false;
        bool line_has_import = false;

        while (token_idx < static_cast<int>(tokens.size()) && tokens[static_cast<std::size_t>(token_idx)].line == ln) {
            const auto& tok = tokens[static_cast<std::size_t>(token_idx)];
            if (brace_depth == 0) {
                if (tok.kind == cfire::frontend::TokenKind::KwImport) {
                    line_has_import = true;
                } else if (tok.kind == cfire::frontend::TokenKind::KwFn || tok.kind == cfire::frontend::TokenKind::KwType) {
                    line_has_top_level_decl = true;
                }
            }
            if (tok.kind == cfire::frontend::TokenKind::LBrace) {
                brace_depth++;
            } else if (tok.kind == cfire::frontend::TokenKind::RBrace) {
                brace_depth--;
            }
            token_idx++;
        }

        if (!in_decl_block && line_has_import) {
            user_imports.push_back(line);
            continue;
        }

        if (!in_decl_block && line_has_top_level_decl) {
            in_decl_block = true;
            current_block = line + "\n";
            if (brace_depth <= 0) {
                in_decl_block = false;
                decls.push_back(std::move(current_block));
                current_block.clear();
                brace_depth = 0;
            }
            continue;
        }

        if (in_decl_block) {
            current_block += line + "\n";
            if (brace_depth <= 0) {
                in_decl_block = false;
                decls.push_back(std::move(current_block));
                current_block.clear();
                brace_depth = 0;
            }
            continue;
        }

        stmts.push_back(line);
    }
    if (!current_block.empty()) {
        decls.push_back(std::move(current_block));
    }

    std::string out = "package main\n\n";
    for (const auto& imp : auto_imports) {
        out += std::format("import \"{}\"\n", imp);
    }
    for (const auto& imp : user_imports) {
        out += imp + "\n";
    }
    out += "\n";

    for (const auto& decl : decls) {
        out += decl + "\n";
    }
    out += "\n";

    out += "fn main() {\n";
    if (expr_mode) {
        std::string joined_stmts;
        for (const auto& s : stmts) {
            if (!joined_stmts.empty()) joined_stmts += " ";
            joined_stmts += s;
        }
        out += "    println(\"{}\", " + joined_stmts + ")\n";
    } else {
        for (const auto& s : stmts) {
            out += "    " + s + "\n";
        }
    }
    out += "}\n";

    return out;
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string sub = argv[1];
    auto toolchain_layout = cfire::build::locate_toolchain_layout(argv[0]);
    auto runtime_dir = toolchain_layout.runtime_dir;

    if (sub == "--version" || sub == "-v" || sub == "version") {
        std::println("cfire {} (ISO C++23 systems toolchain)", cfire::doctor::kCfireVersion);
        return 0;
    }

    if (sub == "--help" || sub == "-h" || sub == "help") {
        print_usage();
        return 0;
    }

    if (sub == "init" || sub == "new") {
        std::string tmpl = "cli";
        bool force = false;
        std::filesystem::path target_dir;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if ((arg == "--template" || arg == "-t") && i + 1 < argc) {
                tmpl = argv[++i];
            } else if (arg.starts_with("--template=")) {
                tmpl = std::string(arg.substr(11));
            } else if (arg == "--tako") {
                tmpl = "tako";
            } else if (arg == "--gui" || arg == "--raylib") {
                tmpl = "gui";
            } else if (arg == "--webui") {
                tmpl = "webui";
            } else if (arg == "--cli") {
                tmpl = "cli";
            } else if (arg == "--force" || arg == "-f") {
                force = true;
            } else if (!arg.starts_with("-") && target_dir.empty()) {
                target_dir = arg;
            }
        }
        if (sub == "new" && target_dir.empty()) {
            std::println(stderr, "error: `cfire new` requires a target directory (e.g., `cfire new my_app`)");
            return 1;
        }
        if (target_dir.empty()) target_dir = ".";
        return run_scaffold_command(sub, target_dir, tmpl, force);
    }

    if (sub == "install") {
        bool portable_mode = false;
        bool user_mode = false;
        std::string target_dir;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--portable" || arg == "--dir" || arg == "--bundle") {
                portable_mode = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    target_dir = argv[++i];
                }
            } else if (arg == "--prefix" && i + 1 < argc) {
                target_dir = argv[++i];
            } else if (arg.starts_with("--prefix=")) {
                target_dir = std::string(arg.substr(9));
            } else if (arg == "--user") {
                user_mode = true;
            } else if (!arg.starts_with("-") && target_dir.empty()) {
                target_dir = std::string(arg);
            }
        }
        return cfire::doctor::run_install(toolchain_layout, target_dir, portable_mode, user_mode);
    }

    if (sub == "doctor") {
        bool json_mode = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--json") json_mode = true;
        }
        return cfire::doctor::run_doctor(runtime_dir, json_mode);
    }

    if (sub == "setup") {
        bool auto_yes = false;
        bool with_wasm = false;
        bool with_raylib = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--yes" || arg == "-y") auto_yes = true;
            else if (arg == "--wasm" || arg == "--web") with_wasm = true;
            else if (arg == "--raylib" || arg == "--gui") with_raylib = true;
        }
        return cfire::doctor::run_setup(runtime_dir, auto_yes, with_wasm, with_raylib);
    }

    if (sub == "serve" || sub == "server") {
        int port = 8080;
        bool open_browser = true;
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--no-open") {
                open_browser = false;
                continue;
            }
            if (arg == "--open") {
                open_browser = true;
                continue;
            }
            if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
                try { port = std::stoi(argv[++i]); } catch (...) {}
                continue;
            }
            if (arg.starts_with("--port=")) {
                try { port = std::stoi(std::string(arg.substr(7))); } catch (...) {}
                continue;
            }
            if (arg.starts_with("-p=")) {
                try { port = std::stoi(std::string(arg.substr(3))); } catch (...) {}
                continue;
            }
            target_path = arg;
        }

        std::string explicit_entry;
        auto cur_dir = std::filesystem::current_path();
        std::string target_name;
        if (target_path != "." && !std::filesystem::exists(target_path)) {
            target_name = target_path.string();
            target_path = ".";
        }

        auto bcf_res = cfire::build::resolve_build_target(cur_dir, target_name, true);
        if (bcf_res && bcf_res->has_wasm_target) {
            if (!std::filesystem::exists(bcf_res->wasm_html)) {
                std::println("==> Building {} (WebAssembly target from build.cf)...", bcf_res->exe.name);
                std::error_code ec;
                std::filesystem::create_directories(bcf_res->wasm_html.parent_path(), ec);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    bcf_res->wasm_html,
                    false,
                    false,
                    "wasm32-unknown-emscripten",
                    "",
                    {},
                    {},
                    bcf_res->exe.web_profile.empty() ? (bcf_res->exe.subsystem == "gui" ? "canvas" : "auto") : bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem.empty() ? "auto" : bcf_res->exe.subsystem,
                    bcf_res->exe.app_name,
                    bcf_res->exe.bundle_id,
                    bcf_res->exe.icon
                );
                if (!plan) {
                    std::println(stderr, "error auto-building wasm target: {}", plan.error());
                    return 1;
                }
                std::println("✓ Built WebAssembly: {} (profile={})", bcf_res->wasm_html.string(), plan->web_profile);
            }
            target_path = bcf_res->wasm_html.parent_path();
            explicit_entry = bcf_res->wasm_html.filename().string();
        }

        return cfire::serve::run_server(target_path, port, open_browser, explicit_entry);
    }

    if (sub == "lsp") {
        cfire::lsp::Server srv;
        srv.handle_stream(std::cin, std::cout);
        return 0;
    }

    if (sub == "mcp") {
        bool setup_mode = false;
        bool print_cfg = false;
        bool global_scope = false;
        std::string target = "auto";
        std::filesystem::path ws_dir = std::filesystem::current_path();
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--setup" || arg == "setup" || arg == "--config" || arg == "config") {
                setup_mode = true;
            } else if (arg == "--print-config" || arg == "print-config") {
                print_cfg = true;
            } else if (arg == "--global" || arg == "-g") {
                global_scope = true;
            } else if (arg == "--dir" && i + 1 < argc) {
                ws_dir = argv[++i];
            } else if (arg == "--all" || arg == "all" || !arg.starts_with("-")) {
                target = std::string(arg);
            }
        }
        if (print_cfg) {
            std::print("{}", cfire::lsp::format_mcp_config_snippet(runtime_dir.parent_path()));
            return 0;
        }
        if (setup_mode) {
            auto res = cfire::lsp::configure_ide(ws_dir, target, global_scope);
            if (!res) {
                std::println(stderr, "{}", res.error());
                return 1;
            }
            std::print("{}", *res);
            return 0;
        }
        return cfire::mcp::serve_stdio(runtime_dir);
    }

    if (sub == "ide") {
        std::string editor = "auto";
        bool global_scope = false;
        std::filesystem::path ws_dir = std::filesystem::current_path();
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--global" || arg == "-g") {
                global_scope = true;
            } else if (arg == "--dir" && i + 1 < argc) {
                ws_dir = argv[++i];
            } else {
                editor = std::string(arg);
            }
        }
        auto res = cfire::lsp::configure_ide(ws_dir, editor, global_scope);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::print("{}", *res);
        return 0;
    }

    if (sub == "eval") {
        bool expr_mode = false;
        std::string raw_code;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "-e") {
                expr_mode = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    raw_code = argv[++i];
                }
            } else if (raw_code.empty()) {
                raw_code = arg;
            } else {
                raw_code += " ";
                raw_code += arg;
            }
        }

        if (raw_code.empty() || raw_code == "-") {
            std::string line;
            raw_code.clear();
            while (std::getline(std::cin, line)) {
                raw_code += line;
                raw_code += "\n";
            }
        }

        if (raw_code.empty()) {
            std::println(stderr, "error: no code provided to eval");
            return 1;
        }

        std::string wrapped_cf = wrap_eval_code(raw_code, expr_mode);

        auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        auto tmp_dir = std::filesystem::temp_directory_path() / std::format("cfire_eval_{}_{}", ::getpid(), now_ns);
        std::error_code ec;
        std::filesystem::create_directories(tmp_dir, ec);

        struct DirCleanup {
            std::filesystem::path p;
            ~DirCleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{tmp_dir};

        auto cf_file = tmp_dir / "eval.cf";
        {
            std::ofstream out(cf_file, std::ios::binary | std::ios::trunc);
            out << wrapped_cf;
        }

        auto eval_bin = tmp_dir / "eval_bin";
        auto plan = cfire::build::build_native(cf_file, runtime_dir, eval_bin, false, false);
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }

        std::string eval_cmd = cfire::bridge::shell_quote(eval_bin.string());
        int rc = std::system(eval_cmd.c_str());
        return rc == 0 ? 0 : (rc >> 8 != 0 ? rc >> 8 : 1);
    }

    if (sub == "refactor") {
        if (argc < 3) {
            std::println(stderr, "Usage: cfire refactor rename <file>:<line>:<col> <new_name> [--diff|--apply|--dry-run|--json]");
            std::println(stderr, "   or: cfire refactor rename --file <file> --line <l> --col <c> --to <new_name> [--diff|--apply|--dry-run|--json]");
            return 1;
        }
        std::string action = argv[2];
        if (action != "rename") {
            std::println(stderr, "unknown refactor action '{}' (supported: rename)", action);
            return 1;
        }

        std::filesystem::path target_file;
        int target_line = 0;
        int target_col = 0;
        std::string new_name;
        bool apply_mode = false;
        bool diff_mode = false;
        bool dry_run_mode = false;
        bool json_mode = false;

        for (int i = 3; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--apply") {
                apply_mode = true;
            } else if (arg == "--diff") {
                diff_mode = true;
            } else if (arg == "--dry-run") {
                dry_run_mode = true;
            } else if (arg == "--json") {
                json_mode = true;
            } else if (arg == "--file" && i + 1 < argc) {
                target_file = argv[++i];
            } else if (arg == "--line" && i + 1 < argc) {
                target_line = std::atoi(argv[++i]);
            } else if (arg == "--col" && i + 1 < argc) {
                target_col = std::atoi(argv[++i]);
            } else if (arg == "--to" && i + 1 < argc) {
                new_name = argv[++i];
            } else if (target_file.empty() && arg.find(':') != std::string_view::npos) {
                auto first_colon = arg.find(':');
                auto second_colon = arg.find(':', first_colon + 1);
                target_file = arg.substr(0, first_colon);
                if (second_colon != std::string_view::npos) {
                    target_line = std::atoi(std::string(arg.substr(first_colon + 1, second_colon - first_colon - 1)).c_str());
                    target_col = std::atoi(std::string(arg.substr(second_colon + 1)).c_str());
                } else {
                    target_line = std::atoi(std::string(arg.substr(first_colon + 1)).c_str());
                    target_col = 1;
                }
            } else if (new_name.empty()) {
                new_name = arg;
            }
        }

        if (target_file.empty() || target_line <= 0 || new_name.empty()) {
            std::println(stderr, "error: missing required arguments for refactor rename");
            std::println(stderr, "Usage: cfire refactor rename <file>:<line>:<col> <new_name> [--diff|--apply|--dry-run|--json]");
            return 1;
        }

        (void)diff_mode;
        (void)dry_run_mode;

        if (target_col <= 0) target_col = 1;

        auto plan = cfire::refactor::plan_rename(target_file, target_line, target_col, new_name);
        if (!plan.success) {
            if (json_mode) {
                std::print("{}", cfire::refactor::to_json(plan));
            } else {
                std::println(stderr, "error: {}", plan.error);
            }
            return 1;
        }

        if (json_mode) {
            if (apply_mode) {
                auto app = cfire::refactor::apply_rename(plan);
                if (!app) {
                    std::println(stderr, "error: {}", app.error());
                    return 1;
                }
            }
            std::print("{}", cfire::refactor::to_json(plan));
            return 0;
        }

        if (apply_mode) {
            auto app = cfire::refactor::apply_rename(plan);
            if (!app) {
                std::println(stderr, "error: {}", app.error());
                return 1;
            }
            std::println("✓ Successfully renamed '{}' to '{}' across {} file(s) ({} occurrence(s))",
                plan.old_name, plan.new_name, plan.modifications.size(), plan.total_occurrences);
            return 0;
        }

        std::string diff = cfire::refactor::generate_unified_diff(plan);
        std::print("{}", diff);
        std::println("Renaming '{}' -> '{}': {} occurrence(s) across {} file(s) (run with `--apply` to commit)",
            plan.old_name, plan.new_name, plan.total_occurrences, plan.modifications.size());
        return 0;
    }

    if (sub == "vendor") {
        std::filesystem::path target_dir = (argc >= 3) ? argv[2] : "vendor";
        auto build_cf = std::filesystem::current_path() / "build.cf";
        if (!std::filesystem::exists(build_cf)) {
            std::println(stderr, "error: no build.cf found in current directory");
            return 1;
        }
        auto parsed = cfire::build::parse_build_cf(build_cf);
        if (!parsed) {
            std::println(stderr, "error parsing build.cf: {}", parsed.error());
            return 1;
        }

        std::filesystem::create_directories(target_dir);
        std::size_t vendored_count = 0;

        for (auto& dep : parsed->external_deps) {
            auto src_res = cfire::build::resolve_external_dep_source(std::filesystem::current_path(), dep);
            if (!src_res) {
                std::println(stderr, "error vendoring '{}': {}", dep.name, src_res.error());
                return 1;
            }
            auto dest_pkg = target_dir / dep.name;
            std::error_code ec;
            std::filesystem::create_directories(dest_pkg, ec);
            std::filesystem::copy(*src_res, dest_pkg,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                std::println(stderr, "error copying '{}' to '{}': {}", src_res->string(), dest_pkg.string(), ec.message());
                return 1;
            }
            vendored_count++;
            std::println("  + Vendored {} -> {}", dep.name, dest_pkg.string());
        }

        std::println("✓ Successfully vendored {} package(s) into {}", vendored_count, target_dir.string());
        return 0;
    }

    if (sub == "doc") {
        bool check_only = false;
        bool llm_mode = false;
        std::filesystem::path target_cf;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--check") {
                check_only = true;
            } else if (arg == "--llm") {
                llm_mode = true;
            } else {
                target_cf = arg;
            }
        }
        if (llm_mode) {
            std::println("{}", cfire::doc::generate_llm_reference());
            return 0;
        }
        if (!target_cf.empty() && target_cf.extension() == ".cf") {
            auto md = cfire::doc::extract_file_markdown_doc(target_cf);
            if (!md) {
                std::println(stderr, "{}", md.error());
                return 1;
            }
            std::print("{}", *md);
            return 0;
        }
        auto docs_dir = toolchain_layout.docs_dir;
        auto res = cfire::doc::sync_docs_directory(docs_dir, check_only, runtime_dir);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        if (check_only) {
            std::println("✓ Documentation in {} is 100% up-to-date with the compiler", docs_dir.string());
        } else {
            std::println("✓ Generated landing page (index.html), guide (guide.html, GUIDE.md), and stdlib reference (reference.html, STDLIB_REFERENCE.md) in {}", docs_dir.string());
        }
        return 0;
    }

    if (sub == "fmt") {
        bool check_only = false;
        bool to_stdout = false;
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--check") {
                check_only = true;
            } else if (arg == "--stdout") {
                to_stdout = true;
            } else {
                target_path = arg;
            }
        }
        auto res = cfire::fmt::format_path(target_path, check_only, to_stdout);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        if (check_only && *res > 0) {
            std::println(stderr, "✗ {} file(s) need formatting (run `cfire fmt {}`)", *res, target_path.string());
            return 1;
        }
        if (!to_stdout) {
            std::println("✓ cfire fmt: {} ({} file(s) updated)", target_path.string(), *res);
        }
        return 0;
    }

    if (sub == "outline") {
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg != "--json") {
                target_path = arg;
            }
        }
        std::vector<std::filesystem::path> cf_files;
        std::error_code ec;
        if (std::filesystem::is_directory(target_path, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target_path, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".cf") {
                    cf_files.push_back(entry.path());
                }
            }
        } else if (std::filesystem::exists(target_path, ec)) {
            cf_files.push_back(target_path);
        } else {
            std::println(stderr, "target '{}' not found", target_path.string());
            return 1;
        }
        std::sort(cf_files.begin(), cf_files.end());

        std::string json = "{\n  \"files\": {\n";
        bool first_file = true;
        for (const auto& f : cf_files) {
            auto content = cfire::build::read_file_text(f);
            if (!content) continue;
            auto ast = cfire::frontend::parse_source(f.string(), *content);
            if (!ast) continue;

            if (!first_file) json += ",\n";
            first_file = false;

            json += std::format("    \"{}\": {{\n", f.string());
            json += std::format("      \"package\": \"{}\",\n", ast->package_name);

            json += "      \"imports\": [";
            for (std::size_t i = 0; i < ast->imports.size(); ++i) {
                if (i > 0) json += ", ";
                json += std::format("\"{}\"", ast->imports[i].path);
            }
            json += "],\n";

            json += "      \"types\": [\n";
            bool first_type = true;
            for (const auto& st : ast->structs) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"struct\", \"fields\": [", st.name);
                for (std::size_t i = 0; i < st.fields.size(); ++i) {
                    if (i > 0) json += ", ";
                    json += std::format("{{\"name\": \"{}\", \"type\": \"{}\", \"tag\": \"{}\"}}",
                        st.fields[i].name,
                        cfire::lsp::detail::json_escape(cfire::fmt::format_type_ref(st.fields[i].type)),
                        cfire::lsp::detail::json_escape(st.fields[i].raw_tag));
                }
                json += "]}";
            }
            for (const auto& en : ast->enums) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"enum\", \"variants\": [", en.name);
                for (std::size_t i = 0; i < en.variants.size(); ++i) {
                    if (i > 0) json += ", ";
                    std::string var_str = en.variants[i].name;
                    if (!en.variants[i].payload_types.empty()) {
                        var_str += "(";
                        for (std::size_t pt = 0; pt < en.variants[i].payload_types.size(); ++pt) {
                            if (pt > 0) var_str += ", ";
                            var_str += cfire::fmt::format_type_ref(en.variants[i].payload_types[pt]);
                        }
                        var_str += ")";
                    }
                    json += std::format("\"{}\"", cfire::lsp::detail::json_escape(var_str));
                }
                json += "]}";
            }
            for (const auto& ifc : ast->interfaces) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"interface\", \"methods\": [", ifc.name);
                for (std::size_t i = 0; i < ifc.methods.size(); ++i) {
                    if (i > 0) json += ", ";
                    std::string sig = std::format("fn {}(", ifc.methods[i].name);
                    for (std::size_t p = 0; p < ifc.methods[i].param_types.size(); ++p) {
                        if (p > 0) sig += ", ";
                        sig += cfire::fmt::format_type_ref(ifc.methods[i].param_types[p]);
                    }
                    sig += std::format(") -> {}", cfire::fmt::format_type_ref(ifc.methods[i].return_type));
                    json += std::format("\"{}\"", cfire::lsp::detail::json_escape(sig));
                }
                json += "]}";
            }
            json += "\n      ],\n";
            json += "      \"global_vars\": [\n";
            for (std::size_t i = 0; i < ast->global_vars.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& gv = ast->global_vars[i];
                std::string ctor_call;
                std::vector<std::string> args;
                std::string type_arg;
                if (gv.value) {
                    if (const auto* c = std::get_if<cfire::frontend::CallExpr>(&gv.value->node)) {
                        if (const auto* m = std::get_if<cfire::frontend::MemberExpr>(&c->callee->node)) {
                            if (const auto* id = std::get_if<cfire::frontend::IdentExpr>(&m->object->node)) {
                                ctor_call = id->name + "." + m->field;
                            }
                        }
                        if (!c->type_args.empty()) {
                            type_arg = c->type_args[0].name;
                        }
                        for (const auto& a : c->args) {
                            if (const auto* lit = std::get_if<cfire::frontend::LiteralExpr>(&a.value->node)) {
                                std::string s = lit->value;
                                if (s.size() >= 2 && (s.front() == '"' || s.front() == '`')) s = s.substr(1, s.size() - 2);
                                args.push_back(s);
                            } else if (const auto* id = std::get_if<cfire::frontend::IdentExpr>(&a.value->node)) {
                                args.push_back(id->name);
                            }
                        }
                    }
                }
                json += std::format("        {{\"name\": \"{}\", \"line\": {}, \"call\": \"{}\", \"type_arg\": \"{}\", \"args\": [",
                    gv.name, gv.line, ctor_call, type_arg);
                for (std::size_t a = 0; a < args.size(); ++a) {
                    if (a > 0) json += ", ";
                    json += std::format("\"{}\"", cfire::lsp::detail::json_escape(args[a]));
                }
                json += "]}";
            }
            json += "\n      ],\n";
            json += "      \"functions\": [\n";
            for (std::size_t i = 0; i < ast->functions.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& fn = ast->functions[i];
                std::string rec_str;
                if (fn.receiver) {
                    std::string rec_intent = (fn.receiver->intent == "mut") ? "mut " : "";
                    rec_str = std::format("({}{}: {})", rec_intent, fn.receiver->name, cfire::fmt::format_type_ref(fn.receiver->type));
                }
                std::string tparams_str = "[";
                for (std::size_t tp = 0; tp < fn.type_params.size(); ++tp) {
                    if (tp > 0) tparams_str += ", ";
                    const auto& t = fn.type_params[tp];
                    tparams_str += std::format("\"{}\"", t.constraint.empty() ? t.name : std::format("{}: {}", t.name, t.constraint));
                }
                tparams_str += "]";

                json += std::format("        {{\"name\": \"{}\", \"receiver\": \"{}\", \"type_params\": {}, \"params\": [",
                    fn.name, cfire::lsp::detail::json_escape(rec_str), tparams_str);
                for (std::size_t p = 0; p < fn.params.size(); ++p) {
                    if (p > 0) json += ", ";
                    std::string param_type = cfire::fmt::format_type_ref(fn.params[p].type);
                    std::string intent = fn.params[p].intent.empty() ? "in" : fn.params[p].intent;
                    std::string intent_prefix = (intent != "in") ? (intent + " ") : "";
                    json += std::format("{{\"name\": \"{}\", \"type\": \"{}\", \"intent\": \"{}\", \"signature\": \"{}{}: {}\"}}",
                        fn.params[p].name,
                        cfire::lsp::detail::json_escape(param_type),
                        cfire::lsp::detail::json_escape(intent),
                        intent_prefix, fn.params[p].name, cfire::lsp::detail::json_escape(param_type));
                }
                json += std::format("], \"returns\": \"{}\", \"async\": {}}}",
                    cfire::lsp::detail::json_escape(cfire::fmt::format_type_ref(fn.return_type)),
                    fn.is_async ? "true" : "false");
            }
            json += "\n      ],\n";
            json += "      \"tests\": [\n";
            for (std::size_t i = 0; i < ast->tests.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& td = ast->tests[i];
                std::string kind = td.is_fuzz ? "fuzz" : (td.is_bench ? "bench" : "test");
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"{}\", \"line\": {}}}",
                    cfire::lsp::detail::json_escape(td.name), kind, td.line);
            }
            json += "\n      ]\n";
            json += "    }";
        }
        json += "\n  }\n}\n";
        std::print("{}", json);
        return 0;
    }

    if (argc < 3) {
        if (sub != "build" && sub != "run" && sub != "test" && sub != "bench" && sub != "check" && sub != "fix") {
            print_usage();
            return 1;
        }
    }
    bool sanitize = false;
    bool json_mode = false;
    bool fix_mode = false;
    bool dry_run = false;
    bool bench_mode = (sub == "bench");
    std::string filter_pattern;
    std::string fuzz_runs_arg;
    std::string fuzz_seed_arg;
    std::string bench_time_arg;
    std::string custom_output_path;
    std::string target_triple;
    std::string sysroot;
    std::string web_profile = "auto";
    std::string subsystem = "auto";
    std::string app_name_override;
    std::string bundle_id_override;
    std::string icon_path_override;
    std::string ios_device_override;
    std::string build_target_filter;
    int web_port = 8080;
    bool open_browser = true;
    std::vector<std::string> extra_link_libs;
    std::vector<std::string> extra_includes;
    std::filesystem::path target;
    std::vector<std::string> run_args;
    bool after_dashdash = false;
    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (after_dashdash) {
            run_args.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            after_dashdash = true;
            continue;
        }
        if (arg == "--sanitize") {
            sanitize = true;
            continue;
        }
        if (arg == "--json") {
            json_mode = true;
            continue;
        }
        if (arg == "--fix") {
            fix_mode = true;
            continue;
        }
        if (arg == "--dry-run") {
            dry_run = true;
            continue;
        }
        if (arg == "--bench" || arg == "-bench") {
            bench_mode = true;
            continue;
        }
        if ((arg == "--fuzz-runs" || arg == "-fuzz-runs") && i + 1 < argc) {
            fuzz_runs_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--fuzz-runs=")) {
            fuzz_runs_arg = std::string(arg.substr(12));
            continue;
        }
        if ((arg == "--seed" || arg == "-seed") && i + 1 < argc) {
            fuzz_seed_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--seed=")) {
            fuzz_seed_arg = std::string(arg.substr(7));
            continue;
        }
        if ((arg == "--bench-time" || arg == "-bench-time") && i + 1 < argc) {
            bench_time_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--bench-time=")) {
            bench_time_arg = std::string(arg.substr(13));
            continue;
        }
        if ((arg == "--filter" || arg == "-filter") && i + 1 < argc) {
            filter_pattern = argv[++i];
            continue;
        }
        if (arg.starts_with("--filter=")) {
            filter_pattern = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-filter=")) {
            filter_pattern = arg.substr(8);
            continue;
        }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            custom_output_path = argv[++i];
            continue;
        }
        if (arg.starts_with("--output=")) {
            custom_output_path = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-o=")) {
            custom_output_path = arg.substr(3);
            continue;
        }
        if ((arg == "--icon" || arg == "-icon") && i + 1 < argc) {
            icon_path_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--icon=")) {
            icon_path_override = arg.substr(7);
            continue;
        }
        if ((arg == "--name" || arg == "--app-name") && i + 1 < argc) {
            app_name_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--name=")) {
            app_name_override = arg.substr(7);
            continue;
        }
        if (arg.starts_with("--app-name=")) {
            app_name_override = arg.substr(11);
            continue;
        }
        if ((arg == "--bundle-id" || arg == "-bundle-id") && i + 1 < argc) {
            bundle_id_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--bundle-id=")) {
            bundle_id_override = arg.substr(12);
            continue;
        }
        if (arg == "--target=wasm" || arg == "-target=wasm" || arg == "--target=web" || arg == "-target=web" || arg == "--web" || arg == "-web") {
            target_triple = "wasm32-unknown-emscripten";
            continue;
        }
        if (arg == "--ios" || arg == "-ios" || arg == "--ios-sim" || arg == "-ios-sim" ||
            arg == "--target=ios-sim" || arg == "-target=ios-sim") {
            target_triple = "ios-sim";
            continue;
        }
        if (arg == "--target=ios" || arg == "-target=ios") {
            target_triple = "ios";
            continue;
        }
        if ((arg == "--device" || arg == "-device") && i + 1 < argc) {
            ios_device_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--device=")) {
            ios_device_override = arg.substr(9);
            continue;
        }
        if (arg.starts_with("--web-profile=")) {
            web_profile = arg.substr(14);
            continue;
        }
        if (arg.starts_with("-web-profile=")) {
            web_profile = arg.substr(13);
            continue;
        }
        if (arg.starts_with("--subsystem=")) {
            subsystem = arg.substr(12);
            continue;
        }
        if (arg.starts_with("-subsystem=")) {
            subsystem = arg.substr(11);
            continue;
        }
        if (arg == "--no-open") {
            open_browser = false;
            continue;
        }
        if (arg == "--open") {
            open_browser = true;
            continue;
        }
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try { web_port = std::stoi(argv[++i]); } catch (...) {}
            continue;
        }
        if (arg.starts_with("--port=")) {
            try { web_port = std::stoi(std::string(arg.substr(7))); } catch (...) {}
            continue;
        }
        if (arg.starts_with("-p=")) {
            try { web_port = std::stoi(std::string(arg.substr(3))); } catch (...) {}
            continue;
        }
        if ((arg == "--target" || arg == "-target") && i + 1 < argc) {
            target_triple = argv[++i];
            if (target_triple == "wasm" || target_triple == "web") {
                target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg.starts_with("--target=")) {
            target_triple = arg.substr(9);
            if (target_triple == "wasm" || target_triple == "web") {
                target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg.starts_with("-target=")) {
            target_triple = arg.substr(8);
            if (target_triple == "wasm" || target_triple == "web") {
                target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg == "--sysroot" && i + 1 < argc) {
            sysroot = argv[++i];
            continue;
        }
        if (arg.starts_with("--sysroot=")) {
            sysroot = arg.substr(10);
            continue;
        }
        if (arg == "--link-lib" && i + 1 < argc) {
            extra_link_libs.emplace_back(argv[++i]);
            continue;
        }
        if (arg.starts_with("--link-lib=")) {
            extra_link_libs.emplace_back(arg.substr(11));
            continue;
        }
        if (arg == "--include" && i + 1 < argc) {
            extra_includes.emplace_back(argv[++i]);
            continue;
        }
        if (arg.starts_with("--include=")) {
            extra_includes.emplace_back(arg.substr(10));
            continue;
        }
        if (target.empty()) {
            target = arg;
        } else {
            run_args.emplace_back(arg);
        }
    }

    if (sub == "build" || sub == "run") {
        auto build_cf_cur = std::filesystem::current_path() / "build.cf";
        if (target.empty() || target == "." || target == "all") {
            if (std::filesystem::exists(build_cf_cur)) {
                target = build_cf_cur;
            }
        } else if (target != "build.cf" && target.extension() != ".cf") {
            if (std::filesystem::exists(build_cf_cur)) {
                build_target_filter = target.string();
                target = build_cf_cur;
            }
        }
    } else if (sub == "test" || sub == "bench" || sub == "check" || sub == "fix") {
        if (target.empty()) {
            target = ".";
        }
    }

    if (target.empty()) {
        print_usage();
        return 1;
    }

    if (sub == "fix" || (sub == "check" && fix_mode)) {
        std::vector<std::filesystem::path> fix_files;
        std::error_code fix_ec;
        if (std::filesystem::is_directory(target, fix_ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target, fix_ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                if (entry.path().filename() == "build.cf") continue;
                if (is_ignored_walk_dir(entry.path())) continue;
                fix_files.push_back(entry.path());
            }
            std::sort(fix_files.begin(), fix_files.end());
        } else {
            fix_files.push_back(target);
        }

        std::size_t grand_total_fixes = 0;
        std::vector<std::string> file_jsons;
        bool any_read_error = false;

        for (const auto& fpath : fix_files) {
            auto raw_txt = cfire::build::read_file_text(fpath);
            if (!raw_txt) {
                any_read_error = true;
                if (json_mode && fix_files.size() == 1) {
                    std::println("{{\"success\": false, \"file\": \"{}\", \"applied_fixes\": 0, \"edits\": []}}",
                        cfire::diag::json_escape(fpath.string()));
                    return 1;
                } else if (!json_mode) {
                    std::println(stderr, "error reading '{}': {}", fpath.string(), raw_txt.error());
                }
                continue;
            }

            std::string current_text = *raw_txt;
            std::vector<cfire::diag::FixItEdit> total_applied;

            for (int pass = 0; pass < 3; ++pass) {
                auto pres = cfire::frontend::parse_source_resilient(fpath.string(), current_text);
                auto lines = cfire::diag::split_lines(current_text);
                std::vector<cfire::diag::FixItEdit> pass_edits;

                // 1. Missing stdlib imports
                auto import_fixes = cfire::diag::detect_missing_stdlib_imports(pres.ast, lines);
                for (auto& f : import_fixes) {
                    pass_edits.push_back(std::move(f));
                }

                // 2. Sema diagnostics & quick-fixes
                auto chk = cfire::frontend::analyze_resilient(std::move(pres.ast));
                for (const auto& w : chk.warnings) {
                    auto d = cfire::diag::parse_diagnostic_string(w, fpath.string(), "warning", "cfire/WARNING");
                    cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                    for (auto& fix : d.fixits) {
                        pass_edits.push_back(std::move(fix));
                    }
                }
                for (const auto& err : chk.errors) {
                    auto d = cfire::diag::parse_diagnostic_string(err, fpath.string(), "error", "cfire/SEMA-ERROR");
                    cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                    for (auto& fix : d.fixits) {
                        pass_edits.push_back(std::move(fix));
                    }
                }

                if (pass_edits.empty()) {
                    break;
                }

                auto [new_text, applied] = cfire::diag::apply_fixits(current_text, pass_edits);
                if (applied.empty() || new_text == current_text) {
                    break;
                }

                for (auto& a : applied) {
                    total_applied.push_back(std::move(a));
                }
                current_text = std::move(new_text);
            }

            if (!dry_run && !total_applied.empty()) {
                std::ofstream out(fpath, std::ios::binary | std::ios::trunc);
                out << current_text;
            }

            grand_total_fixes += total_applied.size();
            if (sub == "fix") {
                if (json_mode) {
                    file_jsons.push_back(cfire::diag::serialize_fix_result_json(fpath.string(), true, total_applied.size(), total_applied));
                } else if (fix_files.size() == 1 || !total_applied.empty()) {
                    if (total_applied.empty()) {
                        std::println("✓ No fixes needed for {}", fpath.string());
                    } else {
                        std::println("✓ Applied {} fix(es) to {}{}", total_applied.size(), fpath.string(), dry_run ? " (dry run)" : "");
                        for (const auto& edit : total_applied) {
                            std::println("  - {}", edit.title);
                        }
                    }
                }
            }
        }

        if (sub == "fix") {
            if (json_mode) {
                if (fix_files.size() == 1 && !file_jsons.empty()) {
                    std::println("{}", file_jsons[0]);
                } else {
                    std::string joined;
                    for (std::size_t i = 0; i < file_jsons.size(); ++i) {
                        if (i > 0) joined += ", ";
                        joined += file_jsons[i];
                    }
                    std::println("{{\"success\": {}, \"applied_fixes\": {}, \"files\": [{}]}}",
                        !any_read_error ? "true" : "false", grand_total_fixes, joined);
                }
            } else if (fix_files.size() > 1 && grand_total_fixes == 0) {
                std::println("✓ No fixes needed across {} file(s) in {}", fix_files.size(), target.string());
            }
            return any_read_error ? 1 : 0;
        }
    }

    if (sub == "inspect") {
        std::string target_str = target.string();

        // Form 2: Foreign C/C++ Header: `cpp <header.h> [@pkg("<lib>")]`
        if (target_str == "cpp" || target_str.starts_with("cpp ")) {
            std::string header;
            if (target_str == "cpp") {
                if (!run_args.empty()) {
                    header = run_args[0];
                }
            } else {
                header = target_str.substr(4);
            }
            while (!header.empty() && std::isspace(static_cast<unsigned char>(header.front()))) header.erase(header.begin());
            while (!header.empty() && std::isspace(static_cast<unsigned char>(header.back()))) header.pop_back();

            if (!header.empty()) {
                std::vector<std::string> cflags;
                for (const auto& inc : extra_includes) {
                    cflags.push_back("-I" + inc);
                }
                auto cache_dir = runtime_dir.parent_path() / ".cfire" / "cache";
                auto idx = cfire::bridge::inspect_header(header, cflags, cache_dir);
                if (!idx) {
                    if (json_mode) {
                        std::println("{{\"success\": false, \"header\": \"{}\", \"error\": \"{}\"}}",
                            cfire::diag::json_escape(header), cfire::diag::json_escape(idx.error()));
                    } else {
                        std::println(stderr, "error inspecting header '{}': {}", header, idx.error());
                    }
                    return 1;
                }

                std::vector<std::pair<std::string, cfire::bridge::CppSymbol>> sorted_syms(idx->symbols.begin(), idx->symbols.end());
                std::sort(sorted_syms.begin(), sorted_syms.end(), [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });

                if (json_mode) {
                    std::string json = std::format("{{\"success\": true, \"header\": \"{}\", \"symbols\": [", cfire::diag::json_escape(header));
                    for (std::size_t i = 0; i < sorted_syms.size(); ++i) {
                        if (i > 0) json += ", ";
                        const auto& sym = sorted_syms[i].second;
                        json += std::format("{{\"name\": \"{}\", \"kind\": \"{}\", \"return_type\": \"{}\", \"params\": [",
                            cfire::diag::json_escape(sym.name),
                            cfire::diag::json_escape(sym.kind),
                            cfire::diag::json_escape(sym.return_type));
                        for (std::size_t p = 0; p < sym.param_types.size(); ++p) {
                            if (p > 0) json += ", ";
                            json += std::format("\"{}\"", cfire::diag::json_escape(sym.param_types[p]));
                        }
                        json += "]}";
                    }
                    json += "]}";
                    std::println("{}", json);
                } else {
                    std::println("Header: <{}> ({} symbols)", header, sorted_syms.size());
                    for (const auto& [sname, sym] : sorted_syms) {
                        std::string params_str;
                        for (std::size_t p = 0; p < sym.param_types.size(); ++p) {
                            if (p > 0) params_str += ", ";
                            params_str += sym.param_types[p];
                        }
                        std::println("  cpp::{}({}) -> {}", sym.name, params_str, sym.return_type.empty() ? "void" : sym.return_type);
                    }
                }
                return 0;
            }
        }

        // Form 1: Source coordinate `<file.cf>:<line>:<col>`
        auto c1 = target_str.rfind(':');
        auto c2 = (c1 != std::string::npos && c1 > 0) ? target_str.rfind(':', c1 - 1) : std::string::npos;
        if (c1 != std::string::npos && c2 != std::string::npos && c2 < c1) {
            auto pline = cfire::detail::parse_int<int>(target_str.substr(c2 + 1, c1 - c2 - 1));
            auto pcol = cfire::detail::parse_int<int>(target_str.substr(c1 + 1));
            if (pline && pcol && *pline > 0 && *pcol > 0) {
                std::string req_file = target_str.substr(0, c2);
                cfire::lsp::Server srv;
                std::filesystem::path p(req_file);
                std::error_code ec;
                auto abs_p = std::filesystem::absolute(p, ec);
                std::string uri = cfire::lsp::detail::path_to_uri(abs_p);
                int lsp_line = std::max(0, *pline - 1);
                int lsp_col = std::max(0, *pcol - 1);
                std::string hover_md = srv.compute_hover(uri, lsp_line, lsp_col);
                std::string def_json = srv.compute_definition(uri, lsp_line, lsp_col);

                if (json_mode) {
                    std::println("{{\"target\": \"{}\", \"file\": \"{}\", \"line\": {}, \"col\": {}, \"hover\": \"{}\", \"definition\": {}}}",
                        cfire::diag::json_escape(target_str),
                        cfire::diag::json_escape(req_file),
                        *pline,
                        *pcol,
                        cfire::diag::json_escape(hover_md),
                        def_json.empty() ? "null" : def_json);
                } else {
                    std::println("Target: {}:{}:{}", req_file, *pline, *pcol);
                    if (!hover_md.empty()) {
                        std::println("\n{}", hover_md);
                    }
                    if (def_json != "null" && !def_json.empty()) {
                        std::println("\nDefinition:\n{}", def_json);
                    }
                }
                return 0;
            }
        }

        // Form 3: `core:<mod>` / `ext:<mod>` / `vendor:<mod>` or `<mod>.<symbol>`
        const auto& catalog = cfire::doc::stdlib_catalog();
        auto matches_mod_path = [](const cfire::doc::StdModuleDoc& m, std::string_view q) -> bool {
            if (m.ns_alias == q || m.import_path == q ||
                ("core:" + m.ns_alias) == q || ("ext:" + m.ns_alias) == q || ("vendor:" + m.ns_alias) == q) {
                return true;
            }
            if (q.starts_with("core:") && m.import_path == ("ext:" + std::string(q.substr(5)))) return true;
            if (q.starts_with("ext:") && m.import_path == ("core:" + std::string(q.substr(4)))) return true;
            return false;
        };
        auto dot_pos = target_str.find('.');
        if (dot_pos != std::string::npos && !target_str.starts_with("core:") && !target_str.starts_with("ext:") && !target_str.starts_with("vendor:")) {
            std::string mod_part = target_str.substr(0, dot_pos);
            std::string sym_part = target_str.substr(dot_pos + 1);

            const cfire::doc::StdModuleDoc* found_mod = nullptr;
            for (const auto& m : catalog) {
                if (matches_mod_path(m, mod_part)) {
                    found_mod = &m;
                    break;
                }
            }

            if (found_mod) {
                for (const auto& fn : found_mod->functions) {
                    if (fn.name == sym_part) {
                        if (json_mode) {
                            std::println("{{\"module\": \"{}\", \"import_path\": \"{}\", \"name\": \"{}\", \"signature\": \"{}\", \"doc\": \"{}\", \"example\": \"{}\"}}",
                                cfire::diag::json_escape(found_mod->ns_alias),
                                cfire::diag::json_escape(found_mod->import_path),
                                cfire::diag::json_escape(fn.name),
                                cfire::diag::json_escape(fn.cf_sig),
                                cfire::diag::json_escape(fn.summary),
                                cfire::diag::json_escape(fn.example));
                        } else {
                            std::println("### {}\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                                fn.cf_sig, fn.summary, fn.example);
                        }
                        return 0;
                    }
                }
            }
        }

        const cfire::doc::StdModuleDoc* matched_mod = nullptr;
        for (const auto& m : catalog) {
            if (matches_mod_path(m, target_str)) {
                matched_mod = &m;
                break;
            }
        }

        if (matched_mod) {
            if (json_mode) {
                std::string json = std::format(
                    "{{\"name\": \"{}\", \"import_path\": \"{}\", \"title\": \"{}\", \"overview\": \"{}\", \"functions\": [",
                    cfire::diag::json_escape(matched_mod->ns_alias),
                    cfire::diag::json_escape(matched_mod->import_path),
                    cfire::diag::json_escape(matched_mod->title),
                    cfire::diag::json_escape(matched_mod->overview));
                for (std::size_t i = 0; i < matched_mod->functions.size(); ++i) {
                    if (i > 0) json += ", ";
                    const auto& fn = matched_mod->functions[i];
                    json += std::format("{{\"name\": \"{}\", \"signature\": \"{}\", \"doc\": \"{}\", \"example\": \"{}\"}}",
                        cfire::diag::json_escape(fn.name),
                        cfire::diag::json_escape(fn.cf_sig),
                        cfire::diag::json_escape(fn.summary),
                        cfire::diag::json_escape(fn.example));
                }
                json += "]}";
                std::println("{}", json);
            } else {
                std::println("# {} (import \"{}\")\n\n{}\n\n{}", matched_mod->title, matched_mod->import_path, matched_mod->overview, "## Functions:");
                for (const auto& fn : matched_mod->functions) {
                    std::println("  - {}", fn.cf_sig);
                }
            }
            return 0;
        }

        // Form 4: Diagnostic code lookup (`cfire inspect cfire/USE-AFTER-FREE` or `cfire inspect USE-AFTER-FREE`)
        for (const auto& d : cfire::doc::diagnostics_catalog()) {
            if (d.code == target_str || d.code == ("cfire/" + target_str)) {
                if (json_mode) {
                    std::println(
                        "{{\"code\": \"{}\", \"title\": \"{}\", \"rule\": \"{}\", \"bad_example\": \"{}\", \"good_example\": \"{}\"}}",
                        cfire::diag::json_escape(d.code),
                        cfire::diag::json_escape(d.title),
                        cfire::diag::json_escape(d.rule),
                        cfire::diag::json_escape(d.bad_example),
                        cfire::diag::json_escape(d.good_example));
                } else {
                    std::println("# {} — {}\n\n{}\n\n**Rejected Example:**\n```cf\n{}\n```\n\n**Accepted Fix:**\n```cf\n{}\n```",
                        d.code, d.title, d.rule, d.bad_example, d.good_example);
                }
                return 0;
            }
        }

        std::println(stderr, "error: unknown inspect target '{}'", target_str);
        return 1;
    }

    if (sub == "emit-cpp") {
        auto res = cfire::build::analyze_and_emit(target, runtime_dir, false, false, target_triple, sysroot);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::print("{}", res->first);
        return 0;
    }

    if (sub == "check") {
        std::vector<std::filesystem::path> check_targets;
        std::error_code chk_ec;
        if (std::filesystem::is_directory(target, chk_ec)) {
            auto bcf = target / "build.cf";
            if (std::filesystem::exists(bcf, chk_ec)) {
                if (auto parsed = cfire::build::parse_build_cf(bcf)) {
                    for (const auto& exe : parsed->executables) {
                        std::filesystem::path ep = exe.entry;
                        if (ep.is_relative()) ep = target / ep;
                        if (std::filesystem::exists(ep, chk_ec)) {
                            check_targets.push_back(ep);
                        }
                    }
                }
                if (!check_targets.empty()) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(target, chk_ec)) {
                        if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                        if (is_ignored_walk_dir(entry.path())) continue;
                        if (entry.path().filename().string().ends_with("_test.cf")) {
                            check_targets.push_back(entry.path());
                        }
                    }
                }
            }
            if (check_targets.empty()) {
                // Group directory `.cf` files by parent directory + package_name so multi-file packages are checked once via their primary file
                std::unordered_set<std::string> seen_pkg_dirs;
                std::vector<std::filesystem::path> all_cf;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(target, chk_ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                    if (entry.path().filename() == "build.cf") continue;
                    if (is_ignored_walk_dir(entry.path())) continue;
                    all_cf.push_back(entry.path());
                }
                std::sort(all_cf.begin(), all_cf.end());
                for (const auto& fpath : all_cf) {
                    auto raw = cfire::build::read_file_text(fpath);
                    if (!raw) {
                        check_targets.push_back(fpath);
                        continue;
                    }
                    auto ast = cfire::frontend::parse_source(fpath.string(), *raw);
                    if (!ast) {
                        check_targets.push_back(fpath);
                        continue;
                    }
                    bool has_main = cfire::build::ast_has_top_level_main(*ast);
                    bool is_test_f = fpath.filename().string().ends_with("_test.cf");
                    std::string key = std::filesystem::weakly_canonical(fpath.parent_path(), chk_ec).string() + "::" + ast->package_name;
                    if (has_main || is_test_f || !seen_pkg_dirs.contains(key)) {
                        seen_pkg_dirs.insert(key);
                        check_targets.push_back(fpath);
                    }
                }
            }
        } else {
            check_targets.push_back(target);
        }

        if (check_targets.empty()) {
            if (json_mode) {
                std::println("{{\"success\": true, \"file\": \"{}\", \"diagnostics\": []}}",
                    cfire::diag::json_escape(target.string()));
            } else {
                std::println("✓ No .cf targets found in {}", target.string());
            }
            return 0;
        }

        if (json_mode) {
            std::vector<cfire::diag::StructuredDiagnostic> all_diags;
            for (const auto& ctarget : check_targets) {
                auto raw_txt = cfire::build::read_file_text(ctarget);
                if (!raw_txt) {
                    cfire::diag::StructuredDiagnostic d;
                    d.file = ctarget.string();
                    d.line = 1;
                    d.col = 1;
                    d.severity = "error";
                    d.code = "cfire/FILE-NOT-FOUND";
                    d.message = raw_txt.error();
                    all_diags.push_back(std::move(d));
                    continue;
                }

                auto pres = cfire::frontend::parse_source_resilient(ctarget.string(), *raw_txt);
                if (!pres.ok()) {
                    for (const auto& err : pres.errors) {
                        all_diags.push_back(cfire::diag::parse_diagnostic_string(err, ctarget.string(), "error", "cfire/SYNTAX-ERROR"));
                    }
                } else {
                    auto lines = cfire::diag::split_lines(*raw_txt);
                    cfire::build::BuildPlan local_plan;
                    local_plan.entry_file = ctarget.string();
                    auto dir = ctarget.parent_path();
                    if (dir.empty()) dir = ".";
                    bool is_test_f = ctarget.filename().string().ends_with("_test.cf");
                    (void)cfire::build::resolve_local_cf_imports(dir, pres.ast, local_plan, runtime_dir, is_test_f);

                    auto chk = cfire::frontend::analyze_resilient(std::move(pres.ast));
                    for (const auto& w : chk.warnings) {
                        auto d = cfire::diag::parse_diagnostic_string(w, ctarget.string(), "warning", "cfire/WARNING");
                        cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                        all_diags.push_back(std::move(d));
                    }
                    for (const auto& err : chk.errors) {
                        auto d = cfire::diag::parse_diagnostic_string(err, ctarget.string(), "error", "cfire/SEMA-ERROR");
                        cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                        all_diags.push_back(std::move(d));
                    }

                    if (chk.errors.empty()) {
                        auto emit_res = cfire::build::analyze_and_emit(ctarget, runtime_dir, true, is_test_f, target_triple, sysroot);
                        if (!emit_res) {
                            auto d = cfire::diag::parse_diagnostic_string(emit_res.error(), ctarget.string(), "error", "cfire/SEMA-ERROR");
                            all_diags.push_back(std::move(d));
                        } else {
                            auto syntax_check = cfire::build::verify_cpp_syntax_only(ctarget, emit_res->first, emit_res->second, runtime_dir, extra_includes);
                            if (!syntax_check) {
                                auto clang_lines = cfire::diag::split_lines(syntax_check.error());
                                bool found_diag = false;
                                for (const auto& cl : clang_lines) {
                                    if (cl.contains(": error:") || cl.contains(": warning:")) {
                                        auto d = cfire::diag::parse_diagnostic_string(cl, ctarget.string(), cl.contains(": error:") ? "error" : "warning", "cfire/TYPE-ERROR");
                                        all_diags.push_back(std::move(d));
                                        found_diag = true;
                                    }
                                }
                                if (!found_diag && !syntax_check.error().empty()) {
                                    auto d = cfire::diag::parse_diagnostic_string(syntax_check.error(), ctarget.string(), "error", "cfire/TYPE-ERROR");
                                    all_diags.push_back(std::move(d));
                                }
                            }
                        }
                    }
                }
            }

            bool has_errors = false;
            for (const auto& d : all_diags) {
                if (d.severity == "error") {
                    has_errors = true;
                    break;
                }
            }
            std::println("{}", cfire::diag::serialize_diagnostics_json(target.string(), !has_errors, all_diags));
            return has_errors ? 1 : 0;
        }

        int failed_checks = 0;
        for (const auto& ctarget : check_targets) {
            bool is_test_f = ctarget.filename().string().ends_with("_test.cf");
            auto res = cfire::build::analyze_and_emit(ctarget, runtime_dir, true, is_test_f, target_triple, sysroot, web_profile, subsystem);
            if (!res) {
                std::println(stderr, "{}", res.error());
                ++failed_checks;
                continue;
            }
            auto syntax_check = cfire::build::verify_cpp_syntax_only(ctarget, res->first, res->second, runtime_dir, extra_includes);
            if (!syntax_check) {
                std::println(stderr, "{}", syntax_check.error());
                ++failed_checks;
                continue;
            }
            std::println("✓ {} checked cleanly ({} auto-linked packages, {} C++ headers ingested)",
                ctarget.string(), res->second.auto_linked.size(), res->second.cpp_headers.size());
        }
        return failed_checks > 0 ? 1 : 0;
    }

    if (sub == "build") {
        if (target.filename() == "build.cf") {
            auto parsed = cfire::build::parse_build_cf(target);
            if (!parsed) {
                std::println(stderr, "error parsing build.cf: {}", parsed.error());
                return 1;
            }
            if (parsed->executables.empty()) {
                std::println(stderr, "error: no executables declared in {}", target.string());
                return 1;
            }

            bool build_all = build_target_filter.empty() || build_target_filter == "all";
            bool matched_any = false;

            for (const auto& exe : parsed->executables) {
                if (!build_all && exe.name != build_target_filter && exe.var_name != build_target_filter) {
                    continue;
                }
                matched_any = true;

                std::vector<std::string> active_targets = exe.targets;
                if (!target_triple.empty()) {
                    active_targets = { target_triple };
                }

                std::filesystem::path entry_file = exe.entry;
                if (entry_file.is_relative()) {
                    entry_file = target.parent_path() / entry_file;
                }

                for (const auto& tgt : active_targets) {
                    bool is_wasm_target = (tgt == "wasm" || tgt == "web" || tgt.starts_with("wasm32"));
                    std::string effective_triple = is_wasm_target ? "wasm32-unknown-emscripten" : (tgt == "native" ? "" : tgt);
                    std::string effective_profile = is_wasm_target ? (!web_profile.empty() && web_profile != "auto" ? web_profile : (exe.web_profile.empty() ? (exe.subsystem == "gui" ? "canvas" : "auto") : exe.web_profile)) : web_profile;
                    std::string effective_subsystem = !subsystem.empty() && subsystem != "auto" ? subsystem : (exe.subsystem.empty() ? "auto" : exe.subsystem);
                    std::string effective_app_name = !app_name_override.empty() ? app_name_override : exe.app_name;
                    std::string effective_bundle_id = !bundle_id_override.empty() ? bundle_id_override : exe.bundle_id;
                    std::string effective_icon = !icon_path_override.empty() ? icon_path_override : exe.icon;

                    std::filesystem::path out_bin_path;
                    if (!custom_output_path.empty()) {
                        out_bin_path = custom_output_path;
                    } else {
                        std::filesystem::path base_out_dir = exe.out_dir.empty() ? target.parent_path() : (target.parent_path() / exe.out_dir);
                        if (is_wasm_target) {
                            out_bin_path = base_out_dir / (exe.name + ".html");
                        } else {
                            std::error_code ec;
                            auto candidate = base_out_dir / exe.name;
                            if (std::filesystem::is_directory(candidate, ec)) {
                                out_bin_path = candidate / exe.name;
                            } else {
                                out_bin_path = candidate;
                            }
                        }
                    }

                    std::error_code ec;
                    std::filesystem::create_directories(out_bin_path.parent_path(), ec);

                    auto plan = cfire::build::build_native(
                        entry_file,
                        runtime_dir,
                        out_bin_path,
                        false,
                        sanitize,
                        effective_triple,
                        sysroot,
                        extra_link_libs,
                        extra_includes,
                        effective_profile,
                        effective_subsystem,
                        effective_app_name,
                        effective_bundle_id,
                        effective_icon
                    );
                    if (!plan) {
                        std::println(stderr, "error building target '{}' for executable '{}':\n{}",
                            tgt, exe.name, plan.error());
                        return 1;
                    }

                    if (is_wasm_target) {
                        std::println("✓ Built WebAssembly: {} (profile={})", out_bin_path.string(), plan->web_profile);
                    } else if (plan->is_ios) {
                        std::println("✓ Built iOS App Bundle: {} (target={})", plan->ios_app_bundle, plan->target_triple);
                    } else {
                        std::string bundle_info = plan->macos_app_bundle.empty() ? "" : std::format(" -> {}", plan->macos_app_bundle);
                        std::println("✓ Built Native {}: {}{}",
                            plan->subsystem == "gui" ? "App" : "Binary",
                            out_bin_path.string(),
                            bundle_info);
                    }
                }
            }

            if (!matched_any) {
                std::println(stderr, "error: executable '{}' not found in {}", build_target_filter, target.string());
                return 1;
            }
            return 0;
        }

        std::string out_bin;
        if (!custom_output_path.empty()) {
            out_bin = custom_output_path;
        } else {
            out_bin = target.parent_path().filename().string();
            if (out_bin.empty() || out_bin == ".") out_bin = "cfire_app";
            std::error_code ec;
            if (std::filesystem::is_directory(out_bin, ec)) {
                out_bin = (std::filesystem::path(out_bin) / target.stem().string()).string();
            }
        }
        if (!target_triple.empty() && (target_triple.find("windows") != std::string::npos || target_triple.find("win32") != std::string::npos || target_triple.find("msvc") != std::string::npos)) {
            if (!out_bin.ends_with(".exe")) out_bin += ".exe";
        }
        if (target_triple.starts_with("wasm")) {
            if (!out_bin.ends_with(".html") && !out_bin.ends_with(".js")) {
                out_bin += ".html";
            }
        }
        auto plan = cfire::build::build_native(
            target,
            runtime_dir,
            out_bin,
            false,
            sanitize,
            target_triple,
            sysroot,
            extra_link_libs,
            extra_includes,
            web_profile,
            subsystem,
            app_name_override,
            bundle_id_override,
            icon_path_override
        );
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string target_info = plan->target_triple.empty() ? "" : std::format(", target={}", plan->target_triple);
        std::println("✓ Built {} (generated {}, pch={}, sanitize={}{})",
            out_bin, plan->generated_cpp, plan->used_pch, plan->sanitized, target_info);
        if (!plan->macos_app_bundle.empty()) {
            std::println("✓ Created macOS App Bundle: {}", plan->macos_app_bundle);
        }
        if (!plan->ios_app_bundle.empty()) {
            std::println("✓ Created iOS App Bundle: {}", plan->ios_app_bundle);
        }
        return 0;
    }

    if (sub == "run" || sub == "test" || sub == "bench") {
        bool is_test = (sub == "test" || sub == "bench");
        if (is_test) {
            if (json_mode) {
                run_args.emplace_back("--cfire-test-json");
            }
            if (bench_mode) {
                run_args.emplace_back("--cfire-bench");
            }
            if (!filter_pattern.empty()) {
                run_args.emplace_back(std::format("--cfire-test-filter={}", filter_pattern));
            }
            if (!fuzz_runs_arg.empty()) {
                run_args.emplace_back(std::format("--cfire-fuzz-runs={}", fuzz_runs_arg));
            }
            if (!fuzz_seed_arg.empty()) {
                run_args.emplace_back(std::format("--cfire-seed={}", fuzz_seed_arg));
            }
            if (!bench_time_arg.empty()) {
                run_args.emplace_back(std::format("--cfire-bench-time={}", bench_time_arg));
            }

            std::error_code test_ec;
            if (std::filesystem::is_directory(target, test_ec)) {
                // Discover all test suites in directory (ERG-01a), grouped by (directory, package_name)
                std::vector<std::filesystem::path> all_cf;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(target, test_ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                    if (entry.path().filename() == "build.cf") continue;
                    if (is_ignored_walk_dir(entry.path())) continue;
                    all_cf.push_back(entry.path());
                }
                std::sort(all_cf.begin(), all_cf.end());

                // Prefer `*_test.cf` files first within each directory so they act as the primary suite entry
                std::stable_sort(all_cf.begin(), all_cf.end(), [](const auto& a, const auto& b) {
                    bool a_test = a.filename().string().ends_with("_test.cf");
                    bool b_test = b.filename().string().ends_with("_test.cf");
                    if (a.parent_path() == b.parent_path() && a_test != b_test) {
                        return a_test > b_test;
                    }
                    return false;
                });

                std::unordered_set<std::string> seen_pkg_suites;
                std::vector<std::filesystem::path> suite_entries;
                for (const auto& fpath : all_cf) {
                    auto raw = cfire::build::read_file_text(fpath);
                    if (!raw) continue;
                    auto ast = cfire::frontend::parse_source(fpath.string(), *raw);
                    if (!ast) continue;
                    cfire::frontend::append_doctests(fpath.string(), *raw, *ast);
                    bool is_test_name = fpath.filename().string().ends_with("_test.cf");
                    if (!is_test_name && ast->tests.empty()) continue;

                    std::string pkg_key = std::filesystem::weakly_canonical(fpath.parent_path(), test_ec).string() + "::" + ast->package_name;
                    if (!ast->package_name.empty()) {
                        if (seen_pkg_suites.contains(pkg_key)) continue;
                        seen_pkg_suites.insert(pkg_key);
                    }
                    suite_entries.push_back(fpath);
                }

                if (suite_entries.empty()) {
                    if (json_mode) {
                        std::println("{{\"success\": true, \"passed\": 0, \"failed\": 0, \"tests\": []}}");
                    } else {
                        std::println("✓ No test suites found in {}", target.string());
                    }
                    return 0;
                }

                int total_passed = 0;
                int total_failed = 0;
                int suite_failures = 0;
                std::string combined_tests_json;

                for (std::size_t idx = 0; idx < suite_entries.size(); ++idx) {
                    const auto& suite_file = suite_entries[idx];
                    if (!json_mode && suite_entries.size() > 1) {
                        std::println("==> Suite {}/{}: {}", idx + 1, suite_entries.size(), suite_file.string());
                    }
                    auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-test-{}-{}", ::getpid(), idx);
                    auto plan = cfire::build::build_native(suite_file, runtime_dir, tmp_bin, true, sanitize, target_triple, sysroot, extra_link_libs, extra_includes, web_profile, subsystem);
                    if (!plan) {
                        ++suite_failures;
                        if (!json_mode) {
                            std::println(stderr, "{}", plan.error());
                        }
                        continue;
                    }
                    std::string cmd = cfire::bridge::shell_quote(tmp_bin.string());
                    for (const auto& arg : run_args) {
                        cmd += " " + cfire::bridge::shell_quote(arg);
                    }
                    if (json_mode) {
                        int rc = 0;
                        std::string out = cfire::bridge::run_command_capture(cmd, rc);
                        std::filesystem::remove(tmp_bin, test_ec);
                        if (rc != 0) ++suite_failures;
                        // Extract passed, failed, and tests array from suite JSON
                        if (auto p_pos = out.find("\"passed\": "); p_pos != std::string::npos) {
                            p_pos += 10;
                            auto p_end = out.find_first_of(",}", p_pos);
                            if (auto v = cfire::detail::parse_int<int>(std::string_view(out).substr(p_pos, p_end - p_pos))) {
                                total_passed += *v;
                            }
                        }
                        if (auto f_pos = out.find("\"failed\": "); f_pos != std::string::npos) {
                            f_pos += 10;
                            auto f_end = out.find_first_of(",}", f_pos);
                            if (auto v = cfire::detail::parse_int<int>(std::string_view(out).substr(f_pos, f_end - f_pos))) {
                                total_failed += *v;
                            }
                        }
                        if (auto t_pos = out.find("\"tests\": ["); t_pos != std::string::npos) {
                            t_pos += 10;
                            auto t_end = out.rfind("]}");
                            if (t_end != std::string::npos && t_end > t_pos) {
                                std::string inner = out.substr(t_pos, t_end - t_pos);
                                if (!inner.empty()) {
                                    if (!combined_tests_json.empty()) combined_tests_json += ", ";
                                    combined_tests_json += inner;
                                }
                            }
                        }
                    } else {
                        int rc = std::system(cmd.c_str());
                        std::filesystem::remove(tmp_bin, test_ec);
                        if (rc != 0) ++suite_failures;
                    }
                }

                if (json_mode) {
                    bool ok = (suite_failures == 0 && total_failed == 0);
                    std::println("{{\"success\": {}, \"passed\": {}, \"failed\": {}, \"tests\": [{}]}}",
                        ok ? "true" : "false", total_passed, total_failed, combined_tests_json);
                } else if (suite_entries.size() > 1) {
                    if (suite_failures == 0) {
                        std::println("✓ All {} test suite(s) passed", suite_entries.size());
                    } else {
                        std::println(stderr, "✗ {} of {} test suite(s) failed", suite_failures, suite_entries.size());
                    }
                }
                return suite_failures > 0 ? 1 : 0;
            }
        }

        if (!is_test && target.filename() == "build.cf") {
            bool is_wasm_run = target_triple.starts_with("wasm");
            bool is_ios_run = cfire::build::is_ios_target(target_triple);
            auto bcf_res = cfire::build::resolve_build_target(target, build_target_filter, is_wasm_run);
            if (!bcf_res) {
                std::println(stderr, "error: no matching executable found in {}", target.string());
                return 1;
            }

            if (is_ios_run) {
                std::string run_triple = (target_triple == "ios" || target_triple == "ios-sim") ? "ios-sim" : target_triple;
                auto ios_bin = bcf_res->base_out_dir / "ios-sim" / bcf_res->exe.name;
                std::error_code ec;
                std::filesystem::create_directories(ios_bin.parent_path(), ec);
                std::println("==> Building {} (iOS Simulator target from build.cf)...", bcf_res->exe.name);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    ios_bin,
                    false,
                    false,
                    run_triple,
                    sysroot,
                    extra_link_libs,
                    extra_includes,
                    bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem,
                    !app_name_override.empty() ? app_name_override : bcf_res->exe.app_name,
                    !bundle_id_override.empty() ? bundle_id_override : bcf_res->exe.bundle_id,
                    !icon_path_override.empty() ? icon_path_override : bcf_res->exe.icon
                );
                if (!plan) {
                    std::println(stderr, "error building iOS target: {}", plan.error());
                    return 1;
                }
                std::string bid = !bundle_id_override.empty() ? bundle_id_override
                    : (!bcf_res->exe.bundle_id.empty() ? bcf_res->exe.bundle_id : ("com.cfire." + bcf_res->exe.name));
                auto sim_res = cfire::build::launch_ios_simulator(plan->ios_app_bundle, bid, ios_device_override, run_args);
                if (!sim_res) {
                    std::println(stderr, "{}", sim_res.error());
                    return 1;
                }
                return *sim_res;
            }

            if (is_wasm_run) {
                if (!std::filesystem::exists(bcf_res->wasm_html)) {
                    std::println("==> Building {} (WebAssembly target from build.cf)...", bcf_res->exe.name);
                    std::error_code ec;
                    std::filesystem::create_directories(bcf_res->wasm_html.parent_path(), ec);
                    auto plan = cfire::build::build_native(
                        bcf_res->entry_file,
                        runtime_dir,
                        bcf_res->wasm_html,
                        false,
                        false,
                        "wasm32-unknown-emscripten",
                        "",
                        {},
                        {},
                        bcf_res->exe.web_profile.empty() ? (bcf_res->exe.subsystem == "gui" ? "canvas" : "auto") : bcf_res->exe.web_profile,
                        bcf_res->exe.subsystem.empty() ? "auto" : bcf_res->exe.subsystem,
                        bcf_res->exe.app_name,
                        bcf_res->exe.bundle_id,
                        bcf_res->exe.icon
                    );
                    if (!plan) {
                        std::println(stderr, "error auto-building wasm target: {}", plan.error());
                        return 1;
                    }
                    std::println("✓ Built WebAssembly: {} (profile={})", bcf_res->wasm_html.string(), plan->web_profile);
                }
                return cfire::serve::run_server(bcf_res->wasm_html.parent_path(), web_port, open_browser, bcf_res->wasm_html.filename().string());
            }

            // Native run
            [[maybe_unused]] bool is_gui = (bcf_res->exe.subsystem == "gui");
            bool needs_build = false;
#if defined(__APPLE__)
            if (is_gui) {
                if (!std::filesystem::exists(bcf_res->macos_app_bundle)) {
                    needs_build = true;
                }
            } else {
                if (!std::filesystem::exists(bcf_res->native_bin)) {
                    needs_build = true;
                }
            }
#else
            if (!std::filesystem::exists(bcf_res->native_bin)) {
                needs_build = true;
            }
#endif

            if (needs_build) {
                std::println("==> Building {} (Native target from build.cf)...", bcf_res->exe.name);
                std::error_code ec;
                std::filesystem::create_directories(bcf_res->native_bin.parent_path(), ec);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    bcf_res->native_bin,
                    false,
                    sanitize,
                    target_triple,
                    sysroot,
                    extra_link_libs,
                    extra_includes,
                    bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem,
                    bcf_res->exe.app_name,
                    bcf_res->exe.bundle_id,
                    bcf_res->exe.icon
                );
                if (!plan) {
                    std::println(stderr, "error building native target: {}", plan.error());
                    return 1;
                }
                std::string bundle_info = plan->macos_app_bundle.empty() ? "" : std::format(" -> {}", plan->macos_app_bundle);
                std::println("✓ Built Native {}: {}{}",
                    plan->subsystem == "gui" ? "App" : "Binary",
                    bcf_res->native_bin.string(),
                    bundle_info);
            }

#if defined(__APPLE__)
            if (is_gui && !bcf_res->macos_app_bundle.empty() && std::filesystem::exists(bcf_res->macos_app_bundle)) {
                std::println("==> Launching macOS App: {}", bcf_res->macos_app_bundle.string());
                std::string open_cmd = "open " + cfire::bridge::shell_quote(bcf_res->macos_app_bundle.string());
                int rc = std::system(open_cmd.c_str());
                return rc == 0 ? 0 : 1;
            }
#endif
            std::string cmd = cfire::bridge::shell_quote(bcf_res->native_bin.string());
            for (const auto& arg : run_args) {
                cmd += " " + cfire::bridge::shell_quote(arg);
            }
            int rc = std::system(cmd.c_str());
            return rc == 0 ? 0 : 1;
        }

        if (target_triple.starts_with("wasm")) {
            auto tmp_html = std::filesystem::temp_directory_path() / std::format("cfire-wasm-{}.html", ::getpid());
            auto plan = cfire::build::build_native(target, runtime_dir, tmp_html, is_test, sanitize, target_triple, sysroot, extra_link_libs, extra_includes, web_profile, subsystem);
            if (!plan) {
                std::println(stderr, "{}", plan.error());
                return 1;
            }
            if (plan->web_profile == "cli") {
                auto js_file = tmp_html;
                js_file.replace_extension(".js");
                int has_node = 0;
                (void)cfire::bridge::run_command_capture("which node 2>/dev/null", has_node);
                if (has_node == 0) {
                    std::string cmd = "node " + cfire::bridge::shell_quote(js_file.string());
                    for (const auto& arg : run_args) {
                        cmd += " " + cfire::bridge::shell_quote(arg);
                    }
                    int rc = std::system(cmd.c_str());
                    return rc == 0 ? 0 : 1;
                }
            }
            return cfire::serve::run_server(tmp_html, web_port, open_browser);
        }

        if (cfire::build::is_ios_target(target_triple)) {
            std::string run_triple = (target_triple == "ios" || target_triple == "ios-sim") ? "ios-sim" : target_triple;
            std::string app_stem = target.parent_path().filename().string();
            if (app_stem.empty() || app_stem == ".") app_stem = target.stem().string();
            if (app_stem == "main") app_stem = "cfire_ios_app";
            auto tmp_ios_dir = std::filesystem::temp_directory_path() / std::format("cfire-ios-{}", ::getpid());
            std::error_code ec;
            std::filesystem::create_directories(tmp_ios_dir, ec);
            auto tmp_ios_bin = tmp_ios_dir / app_stem;
            auto plan = cfire::build::build_native(
                target,
                runtime_dir,
                tmp_ios_bin,
                is_test,
                false,
                run_triple,
                sysroot,
                extra_link_libs,
                extra_includes,
                web_profile,
                subsystem,
                app_name_override,
                bundle_id_override,
                icon_path_override
            );
            if (!plan) {
                std::println(stderr, "{}", plan.error());
                return 1;
            }
            std::string bid = !bundle_id_override.empty() ? bundle_id_override : ("com.cfire." + app_stem);
            auto sim_res = cfire::build::launch_ios_simulator(plan->ios_app_bundle, bid, ios_device_override, run_args);
            std::filesystem::remove_all(tmp_ios_dir, ec);
            if (!sim_res) {
                std::println(stderr, "{}", sim_res.error());
                return 1;
            }
            return *sim_res;
        }

        auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-{}-{}", sub, ::getpid());
        auto plan = cfire::build::build_native(target, runtime_dir, tmp_bin, is_test, sanitize, target_triple, sysroot, extra_link_libs, extra_includes, web_profile, subsystem);
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string cmd = cfire::bridge::shell_quote(tmp_bin.string());
        for (const auto& arg : run_args) {
            cmd += " " + cfire::bridge::shell_quote(arg);
        }
        int rc = std::system(cmd.c_str());
        std::filesystem::remove(tmp_bin);
        return rc == 0 ? 0 : 1;
    }

    if (sub == "eject") {
        std::filesystem::path out_dir = (argc >= 4) ? argv[3] : "ejected_cpp";
        auto res = cfire::build::eject_cmake(target, runtime_dir, out_dir);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::println("✓ Ejected standalone C++23 + CMakeLists.txt to {}", out_dir.string());
        return 0;
    }

    print_usage();
    return 1;
}
