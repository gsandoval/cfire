#pragma once
// src/cli/scaffold.hpp — Project Scaffolding (`init`/`new`), `eval`, `refactor`, and `vendor` Subcommands

#include "../buildsys.hpp"
#include "../build/package_loader.hpp"
#include "../diagnostics.hpp"
#include "../docgen/catalog_types.hpp"
#include "../refactor.hpp"
#include "usage.hpp"
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
#if !defined(_WIN32)
#include <sys/wait.h>
#endif


namespace cfire::cli {

[[nodiscard]] inline auto run_scaffold_command(
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

    if (tmpl == "gui") tmpl = "webui";  // CF-185: the raylib `gui` template became the webui one
    std::string build_cf_txt;
    std::string main_cf_txt;
    std::string test_cf_txt;
    std::string ui_html_txt;

    if (tmpl == "tako") {
        build_cf_txt = std::format(R"CF(package main

import "core:build"

app := build.Executable{{
    Name:       "{}",
    Entry:      "main.cf",
    Targets:    ["native", "wasm", "ios-sim"],
    Subsystem:  "gui",
    WebProfile: "canvas",
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
    } else if (tmpl == "webui") {
        build_cf_txt = std::format(R"CF(package main

import "core:build"

app := build.Executable{{
    Name:      "{}",
    Entry:     "main.cf",
    Targets:   ["native", "ios-sim"],
    Subsystem: "gui",
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
    mut app := make(webui.App, "{}", 960, 640)
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
        build_cf_txt = std::format(R"CF(package main

import "core:build"

app := build.Executable{{
    Name:  "{}",
    Entry: "main.cf",
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

inline auto wrap_eval_code(std::string_view raw_code, bool expr_mode) -> std::string {
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
    auto_imports.insert("core:math");
    auto_imports.insert("core:time");
    auto_imports.insert("core:unicode");
    auto_imports.insert("core:json");

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


[[nodiscard]] inline auto run_eval_subcommand(int argc, char** argv, const std::filesystem::path& runtime_dir) -> int {
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

        cfire::detail::SecureTempDir eval_dir("cfire_eval");  // private 0700 dir (CF-153)
        auto tmp_dir = eval_dir.path();
        std::error_code ec;

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

#if defined(_WIN32)
        auto eval_bin = tmp_dir / "eval_bin.exe";
#else
        auto eval_bin = tmp_dir / "eval_bin";
#endif
        auto plan = cfire::build::build_native(cf_file, runtime_dir, eval_bin, false, false);
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }

        std::string eval_cmd = cfire::bridge::win32_quote_cmd(cfire::bridge::shell_quote(eval_bin.string()));
        int rc = std::system(eval_cmd.c_str());
        return unwrap_system_exit_code(rc);
}

[[nodiscard]] inline auto run_refactor_subcommand(int argc, char** argv) -> int {
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
                // `<file>:<line>[:<col>]`, read from the right: a Windows path has its own colon (`C:\a\main.cf:4:5`).
                auto is_num = [](std::string_view t) {
                    return !t.empty() && std::ranges::all_of(t, [](char c) { return c >= '0' && c <= '9'; });
                };
                std::string_view rest = arg;
                std::vector<int> nums;
                while (nums.size() < 2) {
                    auto c = rest.rfind(':');
                    if (c == std::string_view::npos || !is_num(rest.substr(c + 1))) break;
                    nums.insert(nums.begin(), std::atoi(std::string(rest.substr(c + 1)).c_str()));
                    rest = rest.substr(0, c);
                }
                target_file = rest;
                target_line = nums.empty() ? 0 : nums[0];
                target_col = nums.size() > 1 ? nums[1] : 1;
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

[[nodiscard]] inline auto run_vendor_subcommand(int argc, char** argv) -> int {
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

} // namespace cfire::cli
