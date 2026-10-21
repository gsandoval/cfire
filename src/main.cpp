// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)

#include "buildsys.hpp"
#include "diagnostics.hpp"
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
#include <set>
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
    std::println("  test     [--json] [--filter <pat>] <file.cf> Compile and execute all `test \"...\" {{ ... }}` blocks in file");
    std::println("  build    [--target <triple>] [-o <path>] <file.cf> Compile optimized binary for host or target triple");
    std::println("  eval     [-e <expr>|code]     Evaluate one-shot statement, expression, or stdin");
    std::println("  refactor rename <loc> <to>    Semantic symbol rename across workspace (--diff, --apply, --json)");
    std::println("  check    [--json] [--fix] <file.cf> Type-check, verify parameter intents, and inspect C++ headers");
    std::println("  fix      [--dry-run] [--json] <file.cf> Auto-fix missing imports, mut annotations, and enum matches");
    std::println("  inspect  <target> [--json]    Stateless symbol/type hover, foreign C header, or stdlib docs");
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
    bool fix_mode = false;
    bool dry_run = false;
    std::string filter_pattern;
    std::string custom_output_path;
    std::string target_triple;
    std::string sysroot;
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
    if (target.empty()) {
        print_usage();
        return 1;
    }

    if (sub == "fix" || (sub == "check" && fix_mode)) {
        auto raw_txt = cfire::build::read_file_text(target);
        if (!raw_txt) {
            if (json_mode) {
                std::println("{{\"success\": false, \"file\": \"{}\", \"applied_fixes\": 0, \"edits\": []}}",
                    cfire::diag::json_escape(target.string()));
            } else {
                std::println(stderr, "error reading '{}': {}", target.string(), raw_txt.error());
            }
            return 1;
        }

        std::string current_text = *raw_txt;
        std::vector<cfire::diag::FixItEdit> total_applied;

        for (int pass = 0; pass < 3; ++pass) {
            auto pres = cfire::frontend::parse_source_resilient(target.string(), current_text);
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
                auto d = cfire::diag::parse_diagnostic_string(w, target.string(), "warning", "cfire/WARNING");
                cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                for (auto& fix : d.fixits) {
                    pass_edits.push_back(std::move(fix));
                }
            }
            for (const auto& err : chk.errors) {
                auto d = cfire::diag::parse_diagnostic_string(err, target.string(), "error", "cfire/SEMA-ERROR");
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
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out << current_text;
        }

        if (sub == "fix") {
            if (json_mode) {
                std::println("{}", cfire::diag::serialize_fix_result_json(target.string(), true, total_applied.size(), total_applied));
            } else {
                if (total_applied.empty()) {
                    std::println("✓ No fixes needed for {}", target.string());
                } else {
                    std::println("✓ Applied {} fix(es) to {}{}", total_applied.size(), target.string(), dry_run ? " (dry run)" : "");
                    for (const auto& edit : total_applied) {
                        std::println("  - {}", edit.title);
                    }
                }
            }
            return 0;
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

        // Form 3: `core:<mod>` or `<mod>.<symbol>`
        const auto& catalog = cfire::doc::stdlib_catalog();
        auto dot_pos = target_str.find('.');
        if (dot_pos != std::string::npos && !target_str.starts_with("core:")) {
            std::string mod_part = target_str.substr(0, dot_pos);
            std::string sym_part = target_str.substr(dot_pos + 1);

            const cfire::doc::StdModuleDoc* found_mod = nullptr;
            for (const auto& m : catalog) {
                if (m.ns_alias == mod_part || m.import_path == mod_part || ("core:" + m.ns_alias) == mod_part) {
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
            if (m.import_path == target_str || m.ns_alias == target_str ||
                ("core:" + m.ns_alias) == target_str) {
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
        if (json_mode) {
            auto raw_txt = cfire::build::read_file_text(target);
            if (!raw_txt) {
                std::println("{{\"success\": false, \"file\": \"{}\", \"diagnostics\": [{{\"file\": \"{}\", \"line\": 1, \"col\": 1, \"severity\": \"error\", \"message\": \"{}\", \"code\": \"cfire/FILE-NOT-FOUND\", \"fixits\": []}}]}}",
                    cfire::diag::json_escape(target.string()), cfire::diag::json_escape(target.string()), cfire::diag::json_escape(raw_txt.error()));
                return 1;
            }

            std::vector<cfire::diag::StructuredDiagnostic> diags;
            auto pres = cfire::frontend::parse_source_resilient(target.string(), *raw_txt);
            if (!pres.ok()) {
                for (const auto& err : pres.errors) {
                    diags.push_back(cfire::diag::parse_diagnostic_string(err, target.string(), "error", "cfire/SYNTAX-ERROR"));
                }
            } else {
                auto lines = cfire::diag::split_lines(*raw_txt);
                cfire::build::BuildPlan local_plan;
                local_plan.entry_file = target.string();
                auto dir = target.parent_path();
                if (dir.empty()) dir = ".";
                (void)cfire::build::resolve_local_cf_imports(dir, pres.ast, local_plan);

                auto chk = cfire::frontend::analyze_resilient(std::move(pres.ast));
                for (const auto& w : chk.warnings) {
                    auto d = cfire::diag::parse_diagnostic_string(w, target.string(), "warning", "cfire/WARNING");
                    cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                    diags.push_back(std::move(d));
                }
                for (const auto& err : chk.errors) {
                    auto d = cfire::diag::parse_diagnostic_string(err, target.string(), "error", "cfire/SEMA-ERROR");
                    cfire::diag::synthesize_fixits_for_diagnostic(d, &chk.ast, lines);
                    diags.push_back(std::move(d));
                }

                if (chk.errors.empty()) {
                    auto emit_res = cfire::build::analyze_and_emit(target, runtime_dir, true, false, target_triple, sysroot);
                    if (!emit_res) {
                        auto d = cfire::diag::parse_diagnostic_string(emit_res.error(), target.string(), "error", "cfire/SEMA-ERROR");
                        diags.push_back(std::move(d));
                    } else {
                        auto syntax_check = cfire::build::verify_cpp_syntax_only(target, emit_res->first, emit_res->second, runtime_dir);
                        if (!syntax_check) {
                            auto clang_lines = cfire::diag::split_lines(syntax_check.error());
                            bool found_diag = false;
                            for (const auto& cl : clang_lines) {
                                if (cl.contains(": error:") || cl.contains(": warning:")) {
                                    auto d = cfire::diag::parse_diagnostic_string(cl, target.string(), cl.contains(": error:") ? "error" : "warning", "cfire/TYPE-ERROR");
                                    diags.push_back(std::move(d));
                                    found_diag = true;
                                }
                            }
                            if (!found_diag && !syntax_check.error().empty()) {
                                auto d = cfire::diag::parse_diagnostic_string(syntax_check.error(), target.string(), "error", "cfire/TYPE-ERROR");
                                diags.push_back(std::move(d));
                            }
                        }
                    }
                }
            }

            bool has_errors = false;
            for (const auto& d : diags) {
                if (d.severity == "error") {
                    has_errors = true;
                    break;
                }
            }
            std::println("{}", cfire::diag::serialize_diagnostics_json(target.string(), !has_errors, diags));
            return has_errors ? 1 : 0;
        }

        auto res = cfire::build::analyze_and_emit(target, runtime_dir, true, false, target_triple, sysroot);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        auto syntax_check = cfire::build::verify_cpp_syntax_only(target, res->first, res->second, runtime_dir);
        if (!syntax_check) {
            std::println(stderr, "{}", syntax_check.error());
            return 1;
        }
        std::println("✓ {} checked cleanly ({} auto-linked packages, {} C++ headers ingested)",
            target.string(), res->second.auto_linked.size(), res->second.cpp_headers.size());
        return 0;
    }

    if (sub == "build") {
        std::string out_bin;
        if (!custom_output_path.empty()) {
            out_bin = custom_output_path;
        } else {
            out_bin = target.parent_path().filename().string();
            if (out_bin.empty() || out_bin == ".") out_bin = "cfire_app";
        }
        if (!target_triple.empty() && (target_triple.find("windows") != std::string::npos || target_triple.find("win32") != std::string::npos || target_triple.find("msvc") != std::string::npos)) {
            if (!out_bin.ends_with(".exe")) out_bin += ".exe";
        }
        auto plan = cfire::build::build_native(target, runtime_dir, out_bin, false, sanitize, target_triple, sysroot, extra_link_libs, extra_includes);
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
        if (is_test) {
            if (json_mode) {
                run_args.emplace_back("--cfire-test-json");
            }
            if (!filter_pattern.empty()) {
                run_args.emplace_back(std::format("--cfire-test-filter={}", filter_pattern));
            }
        }
        auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-{}-{}", sub, ::getpid());
        auto plan = cfire::build::build_native(target, runtime_dir, tmp_bin, is_test, sanitize, target_triple, sysroot, extra_link_libs, extra_includes);
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
