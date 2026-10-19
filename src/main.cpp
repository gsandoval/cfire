// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)

#include "buildsys.hpp"
#include "docgen.hpp"
#include "formatter.hpp"
#include "lsp.hpp"
#include "refactor.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

auto locate_runtime_dir() -> std::filesystem::path {
    if (const char* env = std::getenv("CFIRE_RUNTIME_DIR")) {
        return std::filesystem::path(env);
    }
    auto cwd = std::filesystem::current_path();
    for (auto d = cwd; !d.empty() && d != d.parent_path(); d = d.parent_path()) {
        if (std::filesystem::exists(d / "runtime" / "cfire_prelude.hpp")) {
            return d / "runtime";
        }
    }
#if defined(__APPLE__)
    char ep[1024];
    uint32_t sz = sizeof(ep);
    if (_NSGetExecutablePath(ep, &sz) == 0) {
        std::error_code ec;
        auto p = std::filesystem::canonical(ep, ec);
        if (!ec) {
            for (auto d = p.parent_path(); !d.empty() && d != d.parent_path(); d = d.parent_path()) {
                if (std::filesystem::exists(d / "runtime" / "cfire_prelude.hpp")) {
                    return d / "runtime";
                }
            }
        }
    }
#elif defined(__linux__)
    std::error_code ec;
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec) {
        for (auto d = p.parent_path(); !d.empty() && d != d.parent_path(); d = d.parent_path()) {
            if (std::filesystem::exists(d / "runtime" / "cfire_prelude.hpp")) {
                return d / "runtime";
            }
        }
    }
#endif
    if (const char* home = std::getenv("HOME")) {
        auto user_rt = std::filesystem::path(home) / ".cfire" / "runtime";
        if (std::filesystem::exists(user_rt / "cfire_prelude.hpp")) {
            return user_rt;
        }
    }
    for (const auto* share : {"/usr/local/share/cfire/runtime", "/usr/share/cfire/runtime"}) {
        auto share_rt = std::filesystem::path(share);
        if (std::filesystem::exists(share_rt / "cfire_prelude.hpp")) {
            return share_rt;
        }
    }
    return cwd / "runtime";
}

void print_usage() {
    std::println("Usage: cfire <command> [args...]");
    std::println("Commands:");
    std::println("  run      <file.cf>            Transpile to C++23, auto-link core/vendor/cpp deps, and run");
    std::println("  test     <file.cf>            Compile and execute all `test \"...\" {{ ... }}` blocks in file");
    std::println("  build    [--target <triple>] <file.cf> Compile optimized binary for host or target triple");
    std::println("  eval     [-e <expr>|code]     Evaluate one-shot statement, expression, or stdin");
    std::println("  refactor rename <loc> <to>    Semantic symbol rename across workspace (--diff, --apply, --json)");
    std::println("  check    [--json] <file.cf>   Type-check, verify parameter intents, and inspect C++ headers");
    std::println("  outline  [--json] [path]      Token-compressed outline of packages, types, and functions");
    std::println("  fmt      [--check] <path>     Canonically format .cf file(s) or directory");
    std::println("  lsp                           Start built-in Language Server Protocol (JSON-RPC 2.0 stdio)");
    std::println("  ide      [editor|--all]       Configure IDE (VS Code, Cursor, Zed, Neovim, CLion)");
    std::println("  doc      [--check|--llm|<f>]  Generate docs site, guide, or LLM agent reference sheet");
    std::println("  emit-cpp <file.cf>            Print generated human-readable C++23 source");
    std::println("  eject    <file.cf> [out_dir]  Export standalone C++23 source tree + CMakeLists.txt");
    std::println("  vendor   [dest_dir]           Fetch and vendor all build.cf dependencies for offline builds");
}

auto wrap_eval_code(std::string_view raw_code, bool expr_mode) -> std::string {
    std::string_view trimmed = raw_code;
    auto start = trimmed.find_first_not_of(" \t\r\n");
    if (start != std::string_view::npos) {
        trimmed.remove_prefix(start);
    }
    if (trimmed.starts_with("package ")) {
        return std::string(raw_code);
    }

    std::vector<std::string> imports;
    std::vector<std::string> decls;
    std::vector<std::string> stmts;

    std::istringstream stream{std::string(raw_code)};
    std::string line;
    bool in_decl_block = false;
    int brace_depth = 0;
    std::string current_block;

    while (std::getline(stream, line)) {
        std::string_view ltrim = line;
        auto ls = ltrim.find_first_not_of(" \t\r\n");
        if (ls != std::string_view::npos) ltrim.remove_prefix(ls);

        if (!in_decl_block && ltrim.starts_with("import ")) {
            imports.push_back(line);
            continue;
        }

        if (!in_decl_block && (ltrim.starts_with("fn ") || ltrim.starts_with("type "))) {
            in_decl_block = true;
            current_block = line + "\n";
            for (char c : line) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }
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
            for (char c : line) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }
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

    std::string out = "package main\n\n";
    out += "import \"core:fs\"\n";
    out += "import \"core:str\"\n";
    out += "import \"core:math\"\n";
    out += "import \"core:time\"\n";
    out += "import \"core:unicode\"\n";

    for (const auto& imp : imports) {
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
    auto runtime_dir = locate_runtime_dir();

    if (sub == "lsp") {
        cfire::lsp::Server srv;
        srv.handle_stream(std::cin, std::cout);
        return 0;
    }

    if (sub == "ide") {
        std::string editor = (argc >= 3) ? argv[2] : "auto";
        auto res = cfire::lsp::configure_ide(runtime_dir.parent_path(), editor);
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

        int rc = std::system(eval_bin.string().c_str());
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
            std::println("{}", R"(# cfire Language Reference Sheet for LLMs & AI Agents
Grammar:
- Package: `package <name>` (canonical: main)
- Imports: `import "core:<mod>"`, `import "./path"`, `import cpp "header.h" @define("MACRO")`
- Declarations:
  - Const: `x := 42`, `name: str = "cfire"`
  - Mutable: `mut y := 10`, `mut count: i64 = 0` (warning: `var` is deprecated)
- Signatures: `fn name(in a: i32, mut b: str, move c: []byte) -> Ret! { ... }`
  - Fallible return: `-> T!` lowers to `std::expected<T, cfire::Error>`
  - Optional return: `-> T?` lowers to `std::optional<T>`
  - Tuples: `fn coords() -> (f64, f64) { return (1.0, 2.0) }`, deconstruct: `(x, y) := coords()`
- Types:
  - Struct: `type User struct { id: i64, name: str `json:"name"` }`
  - Enum: `type Shape enum { Circle(f64), Rect(f64, f64) }`
  - Interface: `type Writer interface { write(b: []byte) -> usize! }`
- Control Flow:
  - `if cond { ... } else { ... }`
  - `for item in slice { ... }` / `for i, item in slice { ... }`
  - `match val { Shape::Circle(r) => 3.14 * r * r, Shape::Rect(w, h) => w * h }`
  - `defer resource.close()`
- Error Handling:
  - `val := try fallible()` (early return on error)
  - `val := fallible() or fallback_expr`
  - `val := fallible() or err { return -1 }` (must diverge or panic)
- Foreign C/C++: `cpp { /* verbatim C++23 */ }`
- Standard Library Modules:
  - `core:fs`: open, File (read, read_all, write, seek, tell, flush, close), dirname, basename, extension
  - `core:json`: parse_dynamic, stringify_dynamic, Value, decode[T], stringify[T]
  - `core:net/http`: get, post, make_server; `core:net/url`: parse, encode, decode
  - `core:unicode`: rune_count, runes, is_valid_utf8, to_lower, to_upper, encode_rune
  - `core:os`: command, exec, getenv; `core:os/exec`: exec.command
  - `core:time`: now, Duration, format_rfc3339, parse_rfc3339
  - `core:regex`: compile, is_match, replace_all
  - `core:collections`: Set[T], PriorityQueue[T], Deque[T]
  - `core:sort`: slice, slice_by, binary_search
  - `core:crypto/sha256`: hash_hex, hash_bytes; `core:crypto/rand`: bytes
  - `core:log`: set_level, debug, info, warn, error)");
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
        auto docs_dir = runtime_dir.parent_path() / "docs";
        auto res = cfire::doc::sync_docs_directory(docs_dir, check_only);
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
                    json += std::format("\"{}: {}\"", st.fields[i].name, st.fields[i].type.name);
                }
                json += "]}";
            }
            for (const auto& en : ast->enums) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"enum\", \"variants\": [", en.name);
                for (std::size_t i = 0; i < en.variants.size(); ++i) {
                    if (i > 0) json += ", ";
                    json += std::format("\"{}\"", en.variants[i].name);
                }
                json += "]}";
            }
            json += "\n      ],\n";

            json += "      \"functions\": [\n";
            for (std::size_t i = 0; i < ast->functions.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& fn = ast->functions[i];
                json += std::format("        {{\"name\": \"{}\", \"params\": [", fn.name);
                for (std::size_t p = 0; p < fn.params.size(); ++p) {
                    if (p > 0) json += ", ";
                    json += std::format("\"{}: {}\"", fn.params[p].name, fn.params[p].type.name);
                }
                json += std::format("], \"returns\": \"{}\", \"async\": {}}}", fn.return_type.name, fn.is_async ? "true" : "false");
            }
            json += "\n      ]\n";
            json += "    }";
        }
        json += "\n  }\n}\n";
        std::print("{}", json);
        return 0;
    }

    if (argc < 3) {
        print_usage();
        return 1;
    }
    bool sanitize = false;
    bool json_mode = false;
    std::string target_triple;
    std::string sysroot;
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
        if ((arg == "--target" || arg == "-target") && i + 1 < argc) {
            target_triple = argv[++i];
            continue;
        }
        if (arg.starts_with("--target=")) {
            target_triple = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-target=")) {
            target_triple = arg.substr(8);
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
        if (target.empty()) {
            target = arg;
        } else {
            run_args.emplace_back(arg);
        }
    }
    if (target.empty()) {
        print_usage();
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
        if (json_mode) {
            auto raw_txt = cfire::build::read_file_text(target);
            if (!raw_txt) {
                std::println("{{\"success\": false, \"file\": \"{}\", \"diagnostics\": [{{\"file\": \"{}\", \"line\": 1, \"col\": 1, \"severity\": \"error\", \"message\": \"{}\", \"code\": \"cfire/FILE-NOT-FOUND\"}}]}}",
                    target.string(), target.string(), raw_txt.error());
                return 1;
            }
            auto ast = cfire::frontend::parse_source(target.string(), *raw_txt);
            if (!ast) {
                std::println("{{\"success\": false, \"file\": \"{}\", \"diagnostics\": [{{\"file\": \"{}\", \"line\": 1, \"col\": 1, \"severity\": \"error\", \"message\": \"{}\", \"code\": \"cfire/SYNTAX-ERROR\"}}]}}",
                    target.string(), target.string(), cfire::lsp::detail::json_escape(ast.error()));
                return 1;
            }
            auto chk = cfire::frontend::analyze(std::move(*ast));
            if (!chk) {
                std::println("{{\"success\": false, \"file\": \"{}\", \"diagnostics\": [{{\"file\": \"{}\", \"line\": 1, \"col\": 1, \"severity\": \"error\", \"message\": \"{}\", \"code\": \"cfire/SEMA-ERROR\"}}]}}",
                    target.string(), target.string(), cfire::lsp::detail::json_escape(chk.error()));
                return 1;
            }
            std::string diag_json = "[";
            for (std::size_t i = 0; i < chk->warnings.size(); ++i) {
                if (i > 0) diag_json += ", ";
                diag_json += std::format("{{\"file\": \"{}\", \"line\": 1, \"col\": 1, \"severity\": \"warning\", \"message\": \"{}\", \"code\": \"cfire/WARNING\"}}",
                    target.string(), cfire::lsp::detail::json_escape(chk->warnings[i]));
            }
            diag_json += "]";
            std::println("{{\"success\": true, \"file\": \"{}\", \"diagnostics\": {}}}", target.string(), diag_json);
            return 0;
        }

        auto res = cfire::build::analyze_and_emit(target, runtime_dir, true, false, target_triple, sysroot);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::println("✓ {} checked cleanly ({} auto-linked packages, {} C++ headers ingested)",
            target.string(), res->second.auto_linked.size(), res->second.cpp_headers.size());
        return 0;
    }

    if (sub == "build") {
        std::string out_bin = target.parent_path().filename().string();
        if (out_bin.empty() || out_bin == ".") out_bin = "cfire_app";
        if (!target_triple.empty() && (target_triple.find("windows") != std::string::npos || target_triple.find("win32") != std::string::npos || target_triple.find("msvc") != std::string::npos)) {
            if (!out_bin.ends_with(".exe")) out_bin += ".exe";
        }
        auto plan = cfire::build::build_native(target, runtime_dir, out_bin, false, sanitize, target_triple, sysroot);
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string target_info = plan->target_triple.empty() ? "" : std::format(", target={}", plan->target_triple);
        std::println("✓ Built {} (generated {}, pch={}, sanitize={}{})",
            out_bin, plan->generated_cpp, plan->used_pch, plan->sanitized, target_info);
        return 0;
    }

    if (sub == "run" || sub == "test") {
        bool is_test = (sub == "test");
        auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-{}-{}", sub, ::getpid());
        auto plan = cfire::build::build_native(target, runtime_dir, tmp_bin, is_test, sanitize, target_triple, sysroot);
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string cmd = "\"" + tmp_bin.string() + "\"";
        for (const auto& arg : run_args) {
            cmd += std::format(" \"{}\"", arg);
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
