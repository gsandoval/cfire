#include "cfire_prelude.hpp"

struct RefactorCliArgs;

struct RefactorCliArgs {
    std::string action{};
    std::string target_file{};
    std::int32_t target_line{};
    std::int32_t target_col{};
    std::string new_name{};
    bool apply_mode{};
    bool diff_mode{};
    bool dry_run_mode{};
    bool json_mode{};
    bool valid{};
    std::string error_message{};
};

template <>
struct cfire::reflect_struct<RefactorCliArgs> {
    static constexpr std::string_view name = "RefactorCliArgs";
    static constexpr std::size_t field_count = 11;
    static constexpr std::array<std::string_view, 11> field_names = {
        "action",
        "target_file",
        "target_line",
        "target_col",
        "new_name",
        "apply_mode",
        "diff_mode",
        "dry_run_mode",
        "json_mode",
        "valid",
        "error_message",
    };
    static constexpr std::array<std::string_view, 11> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] auto selfhost_get_usage() -> std::string;
[[nodiscard]] auto selfhost_get_llm_doc() -> std::string;
[[nodiscard]] auto driver_starts_with(std::string_view s, std::string_view prefix) -> bool;
[[nodiscard]] auto driver_ends_with(std::string_view s, std::string_view suffix) -> bool;
[[nodiscard]] auto driver_find_first_not_of_ws(std::string_view s) -> std::size_t;
[[nodiscard]] auto driver_find_char(std::string_view s, std::uint8_t ch, std::size_t start) -> std::size_t;
[[nodiscard]] auto driver_parse_i32(std::string_view s) -> std::int32_t;
[[nodiscard]] auto selfhost_wrap_eval_code(const std::string& raw_code, bool expr_mode) -> std::string;
[[nodiscard]] auto selfhost_parse_refactor_args(const std::vector<std::string>& args) -> RefactorCliArgs;
[[nodiscard]] auto selfhost_run_driver(const std::vector<std::string>& args, const std::string& runtime_dir) -> std::int32_t;

[[nodiscard]] auto selfhost_get_usage() -> std::string {
    std::string u = "Usage: cfire <command> [args...]\n";
    u += "Commands:\n";
    u += "  run      <file.cf>            Transpile to C++23, auto-link core/vendor/cpp deps, and run\n";
    u += "  test     <file.cf>            Compile and execute all `test \"...\" {{ ... }}` blocks in file\n";
    u += "  build    [--target <triple>] <file.cf> Compile optimized binary for host or target triple\n";
    u += "  eval     [-e <expr>|code]     Evaluate one-shot statement, expression, or stdin\n";
    u += "  refactor rename <loc> <to>    Semantic symbol rename across workspace (--diff, --apply, --json)\n";
    u += "  check    [--json] <file.cf>   Type-check, verify parameter intents, and inspect C++ headers\n";
    u += "  outline  [--json] [path]      Token-compressed outline of packages, types, and functions\n";
    u += "  fmt      [--check] <path>     Canonically format .cf file(s) or directory\n";
    u += "  lsp                           Start built-in Language Server Protocol (JSON-RPC 2.0 stdio)\n";
    u += "  ide      [editor|--all]       Configure IDE (VS Code, Cursor, Zed, Neovim, CLion)\n";
    u += "  doc      [--check|--llm|<f>]  Generate docs site, guide, or LLM agent reference sheet\n";
    u += "  emit-cpp <file.cf>            Print generated human-readable C++23 source\n";
    u += "  eject    <file.cf> [out_dir]  Export standalone C++23 source tree + CMakeLists.txt\n";
    u += "  vendor   [dest_dir]           Fetch and vendor all build.cf dependencies for offline builds\n";
    return u;
}

[[nodiscard]] auto selfhost_get_llm_doc() -> std::string {
    std::string d = "# cfire Language Reference Sheet for LLMs & AI Agents\n";
    d += "Grammar:\n";
    d += "- Package: `package <name>` (canonical: main)\n";
    d += "- Imports: `import \"core:<mod>\"`, `import \"./path\"`, `import cpp \"header.h\" @define(\"MACRO\")`\n";
    d += "- Declarations:\n";
    d += "  - Const: `x := 42`, `name: str = \"cfire\"`\n";
    d += "  - Mutable: `mut y := 10`, `mut count: i64 = 0`\n";
    d += "- Signatures: `fn name(in a: i32, mut b: str, move c: []byte) -> Ret! { ... }`\n";
    d += "  - Fallible return: `-> T!` lowers to `std::expected<T, cfire::Error>`\n";
    d += "  - Optional return: `-> T?` lowers to `std::optional<T>`\n";
    d += "  - Tuples: `fn coords() -> (f64, f64) { return (1.0, 2.0) }`, deconstruct: `(x, y) := coords()`\n";
    d += "- Types:\n";
    d += "  - Struct: `type User struct { id: i64, name: str `json:\"name\"` }`\n";
    d += "  - Enum: `type Shape enum { Circle(f64), Rect(f64, f64) }`\n";
    d += "  - Interface: `type Writer interface { write(b: []byte) -> usize! }`\n";
    d += "- Control Flow:\n";
    d += "  - `if cond { ... } else { ... }`\n";
    d += "  - `for item in slice { ... }` / `for i, item in slice { ... }`\n";
    d += "  - `match val { Shape::Circle(r) => 3.14 * r * r, Shape::Rect(w, h) => w * h }`\n";
    d += "  - `defer resource.close()`\n";
    d += "- Error Handling:\n";
    d += "  - `val := try fallible()` (early return on error)\n";
    d += "  - `val := fallible() or fallback_expr`\n";
    d += "  - `val := fallible() or err { return -1 }` (must diverge or panic)\n";
    d += "- Foreign C/C++: `cpp { /* verbatim C++23 */ }`\n";
    d += "- Standard Library Modules:\n";
    d += "  - `core:fs`: open, File (read, read_all, write, seek, tell, flush, close), dirname, basename, extension\n";
    d += "  - `core:json`: parse_dynamic, stringify_dynamic, Value, decode[T], stringify[T]\n";
    d += "  - `core:net/http`: get, post, make_server; `core:net/url`: parse, encode, decode\n";
    d += "  - `core:unicode`: rune_count, runes, is_valid_utf8, to_lower, to_upper, encode_rune\n";
    d += "  - `core:os`: command, exec, getenv; `core:os/exec`: exec.command\n";
    d += "  - `core:time`: now, Duration, format_rfc3339, parse_rfc3339\n";
    d += "  - `core:regex`: compile, is_match, replace_all\n";
    d += "  - `core:collections`: Set[T], PriorityQueue[T], Deque[T]\n";
    d += "  - `core:sort`: slice, slice_by, binary_search\n";
    d += "  - `core:crypto/sha256`: hash_hex, hash_bytes; `core:crypto/rand`: bytes\n";
    d += "  - `core:log`: set_level, debug, info, warn, error";
    return d;
}

[[nodiscard]] auto driver_starts_with(std::string_view s, std::string_view prefix) -> bool {
    auto n = s.size();
    auto pn = prefix.size();
    if (pn > n) {
        return false;
    }
    return (cfire::slice(s, 0, pn) == prefix);
}

[[nodiscard]] auto driver_ends_with(std::string_view s, std::string_view suffix) -> bool {
    auto n = s.size();
    auto sn = suffix.size();
    if (sn > n) {
        return false;
    }
    return (cfire::slice(s, (n - sn), n) == suffix);
}

[[nodiscard]] auto driver_find_first_not_of_ws(std::string_view s) -> std::size_t {
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto c = s[i];
        if ((((c != 32) && (c != 9)) && (c != 10)) && (c != 13)) {
            return i;
        }
        i = (i + 1);
    }
    return n;
}

[[nodiscard]] auto driver_find_char(std::string_view s, std::uint8_t ch, std::size_t start) -> std::size_t {
    std::size_t i = start;
    auto n = s.size();
    while (i < n) {
        if (s[i] == ch) {
            return i;
        }
        i = (i + 1);
    }
    return n;
}

[[nodiscard]] auto driver_parse_i32(std::string_view s) -> std::int32_t {
    std::int32_t res = 0;
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto c = s[i];
        if ((c >= 48) && (c <= 57)) {
            res = ((res * 10) + (c - 48));
        }
        i = (i + 1);
    }
    return res;
}

[[nodiscard]] auto selfhost_wrap_eval_code(const std::string& raw_code, bool expr_mode) -> std::string {
    auto start = driver_find_first_not_of_ws(raw_code);
    auto n = raw_code.size();
    if (start < n) {
        auto trimmed = cfire::slice(raw_code, start, n);
        if (driver_starts_with(trimmed, "package ")) {
            return raw_code;
        }
    }
    std::vector<std::string> imports = {};
    std::vector<std::string> decls = {};
    std::vector<std::string> stmts = {};
    bool in_decl_block = false;
    std::int32_t brace_depth = 0;
    std::string current_block = "";
    std::size_t line_start = 0;
    std::size_t i = 0;
    while (i <= n) {
        if ((i == n) || (raw_code[i] == 10)) {
            auto line_end = i;
            if ((line_end > line_start) && (raw_code[(line_end - 1)] == 13)) {
                line_end = (line_end - 1);
            }
            auto line = cfire::slice(raw_code, line_start, line_end);
            line_start = (i + 1);
            auto ls = driver_find_first_not_of_ws(line);
            auto ln = line.size();
            std::string ltrim = "";
            if (ls < ln) {
                ltrim = cfire::slice(line, ls, ln);
            }
            std::string line_str = "";
            line_str += line;
            if (!in_decl_block && driver_starts_with(ltrim, "import ")) {
                (void)(imports.push_back(line_str));
            } else {
                if (!in_decl_block && (driver_starts_with(ltrim, "fn ") || driver_starts_with(ltrim, "type "))) {
                    in_decl_block = true;
                    current_block = (line_str + "\n");
                    std::size_t j = 0;
                    while (j < ln) {
                        auto c = line[j];
                        if (c == 123) {
                            brace_depth = (brace_depth + 1);
                        } else {
                            if (c == 125) {
                                brace_depth = (brace_depth - 1);
                            }
                        }
                        j = (j + 1);
                    }
                    if (brace_depth <= 0) {
                        in_decl_block = false;
                        (void)(decls.push_back(current_block));
                        current_block = "";
                        brace_depth = 0;
                    }
                } else {
                    if (in_decl_block) {
                        current_block += (line_str + "\n");
                        std::size_t j = 0;
                        while (j < ln) {
                            auto c = line[j];
                            if (c == 123) {
                                brace_depth = (brace_depth + 1);
                            } else {
                                if (c == 125) {
                                    brace_depth = (brace_depth - 1);
                                }
                            }
                            j = (j + 1);
                        }
                        if (brace_depth <= 0) {
                            in_decl_block = false;
                            (void)(decls.push_back(current_block));
                            current_block = "";
                            brace_depth = 0;
                        }
                    } else {
                        (void)(stmts.push_back(line_str));
                    }
                }
            }
        }
        i = (i + 1);
    }
    std::string result_code = "package main\n\n";
    result_code += "import \"core:fs\"\n";
    result_code += "import \"core:str\"\n";
    result_code += "import \"core:math\"\n";
    result_code += "import \"core:time\"\n";
    result_code += "import \"core:unicode\"\n";
    std::size_t imp_i = 0;
    auto n_imp = imports.size();
    while (imp_i < n_imp) {
        result_code += (imports[imp_i] + "\n");
        imp_i = (imp_i + 1);
    }
    result_code += "\n";
    std::size_t dec_i = 0;
    auto n_dec = decls.size();
    while (dec_i < n_dec) {
        result_code += (decls[dec_i] + "\n");
        dec_i = (dec_i + 1);
    }
    result_code += "\n";
    result_code += "fn main() {\n";
    if (expr_mode) {
        std::string joined_stmts = "";
        std::size_t stmt_i = 0;
        auto n_stmts = stmts.size();
        while (stmt_i < n_stmts) {
            if (joined_stmts.size() > 0) {
                joined_stmts += " ";
            }
            joined_stmts += stmts[stmt_i];
            stmt_i = (stmt_i + 1);
        }
        result_code += (("    println(\"{}\", " + joined_stmts) + ")\n");
    } else {
        std::size_t stmt_i = 0;
        auto n_stmts = stmts.size();
        while (stmt_i < n_stmts) {
            result_code += (("    " + stmts[stmt_i]) + "\n");
            stmt_i = (stmt_i + 1);
        }
    }
    result_code += "}\n";
    return result_code;
}

[[nodiscard]] auto selfhost_parse_refactor_args(const std::vector<std::string>& args) -> RefactorCliArgs {
    auto res = RefactorCliArgs{.action = "", .target_file = "", .target_line = 0, .target_col = 1, .new_name = "", .apply_mode = false, .diff_mode = false, .dry_run_mode = false, .json_mode = false, .valid = false, .error_message = ""};
    auto n = args.size();
    if (n < 3) {
        res.error_message = "missing required arguments for refactor";
        return res;
    }
    res.action = args[2];
    if (res.action != "rename") {
        res.error_message = (("unknown refactor action '" + res.action) + "' (supported: rename)");
        return res;
    }
    std::size_t i = 3;
    while (i < n) {
        auto arg = args[i];
        if (arg == "--apply") {
            res.apply_mode = true;
        } else {
            if (arg == "--diff") {
                res.diff_mode = true;
            } else {
                if (arg == "--dry-run") {
                    res.dry_run_mode = true;
                } else {
                    if (arg == "--json") {
                        res.json_mode = true;
                    } else {
                        if ((arg == "--file") && ((i + 1) < n)) {
                            i = (i + 1);
                            res.target_file = args[i];
                        } else {
                            if ((arg == "--line") && ((i + 1) < n)) {
                                i = (i + 1);
                                res.target_line = driver_parse_i32(args[i]);
                            } else {
                                if ((arg == "--col") && ((i + 1) < n)) {
                                    i = (i + 1);
                                    res.target_col = driver_parse_i32(args[i]);
                                } else {
                                    if ((arg == "--to") && ((i + 1) < n)) {
                                        i = (i + 1);
                                        res.new_name = args[i];
                                    } else {
                                        if ((res.target_file.size() == 0) && (driver_find_char(arg, 58, 0) < arg.size())) {
                                            auto first_colon = driver_find_char(arg, 58, 0);
                                            auto second_colon = driver_find_char(arg, 58, (first_colon + 1));
                                            res.target_file = cfire::slice(arg, 0, first_colon);
                                            if (second_colon < arg.size()) {
                                                res.target_line = driver_parse_i32(cfire::slice(arg, (first_colon + 1), second_colon));
                                                res.target_col = driver_parse_i32(cfire::slice(arg, (second_colon + 1), arg.size()));
                                            } else {
                                                res.target_line = driver_parse_i32(cfire::slice(arg, (first_colon + 1), arg.size()));
                                                res.target_col = 1;
                                            }
                                        } else {
                                            if (res.new_name.size() == 0) {
                                                res.new_name = arg;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        i = (i + 1);
    }
    if (((res.target_file.size() == 0) || (res.target_line <= 0)) || (res.new_name.size() == 0)) {
        res.error_message = "error: missing required arguments for refactor rename";
        return res;
    }
    if (res.target_col <= 0) {
        res.target_col = 1;
    }
    res.valid = true;
    return res;
}

[[nodiscard]] auto selfhost_run_driver(const std::vector<std::string>& args, const std::string& runtime_dir) -> std::int32_t {
    if (args.size() < 2) {
        return cfire::main_driver::run_usage();
    }
    auto sub = args[1];
    if (sub == "lsp") {
        return cfire::main_driver::run_lsp();
    }
    if (sub == "ide") {
        std::string editor = "auto";
        if (args.size() >= 3) {
            editor = args[2];
        }
        return cfire::main_driver::run_ide(editor, runtime_dir);
    }
    if (sub == "eval") {
        bool expr_mode = false;
        std::string raw_code = "";
        std::size_t i = 2;
        auto n = args.size();
        while (i < n) {
            auto arg = args[i];
            if (arg == "-e") {
                expr_mode = true;
                if ((i + 1) < n) {
                    auto next_arg = args[(i + 1)];
                    if ((next_arg.size() == 0) || (next_arg[0] != 45)) {
                        i = (i + 1);
                        raw_code = args[i];
                    }
                }
            } else {
                if (raw_code.size() == 0) {
                    raw_code = arg;
                } else {
                    raw_code += " ";
                    raw_code += arg;
                }
            }
            i = (i + 1);
        }
        return cfire::main_driver::run_eval(raw_code, expr_mode, runtime_dir);
    }
    if (sub == "refactor") {
        auto rf = selfhost_parse_refactor_args(args);
        return cfire::main_driver::run_refactor(rf.target_file, rf.target_line, rf.target_col, rf.new_name, rf.apply_mode, rf.diff_mode, rf.dry_run_mode, rf.json_mode, rf.valid, rf.error_message);
    }
    if (sub == "vendor") {
        std::string target_dir = "vendor";
        if (args.size() >= 3) {
            target_dir = args[2];
        }
        return cfire::main_driver::run_vendor(target_dir);
    }
    if (sub == "doc") {
        bool check_only = false;
        bool llm_mode = false;
        std::string target_cf = "";
        std::size_t i = 2;
        auto n = args.size();
        while (i < n) {
            auto arg = args[i];
            if (arg == "--check") {
                check_only = true;
            } else {
                if (arg == "--llm") {
                    llm_mode = true;
                } else {
                    target_cf = arg;
                }
            }
            i = (i + 1);
        }
        auto llm_doc = selfhost_get_llm_doc();
        return cfire::main_driver::run_doc(check_only, llm_mode, target_cf, runtime_dir, llm_doc);
    }
    if (sub == "fmt") {
        bool check_only = false;
        bool to_stdout = false;
        std::string target_path = ".";
        std::size_t i = 2;
        auto n = args.size();
        while (i < n) {
            auto arg = args[i];
            if (arg == "--check") {
                check_only = true;
            } else {
                if (arg == "--stdout") {
                    to_stdout = true;
                } else {
                    target_path = arg;
                }
            }
            i = (i + 1);
        }
        return cfire::main_driver::run_fmt(target_path, check_only, to_stdout);
    }
    if (sub == "outline") {
        std::string target_path = ".";
        std::size_t i = 2;
        auto n = args.size();
        while (i < n) {
            auto arg = args[i];
            if (arg != "--json") {
                target_path = arg;
            }
            i = (i + 1);
        }
        return cfire::main_driver::run_outline(target_path);
    }
    if (args.size() < 3) {
        return cfire::main_driver::run_usage();
    }
    bool sanitize = false;
    bool json_mode = false;
    std::string target_triple = "";
    std::string sysroot = "";
    std::string target = "";
    std::vector<std::string> run_args = {};
    bool after_dashdash = false;
    std::size_t opt_i = 2;
    auto n_args = args.size();
    while (opt_i < n_args) {
        auto arg = args[opt_i];
        if (after_dashdash) {
            (void)(run_args.push_back(arg));
        } else {
            if (arg == "--") {
                after_dashdash = true;
            } else {
                if (arg == "--sanitize") {
                    sanitize = true;
                } else {
                    if (arg == "--json") {
                        json_mode = true;
                    } else {
                        if (((arg == "--target") || (arg == "-target")) && ((opt_i + 1) < n_args)) {
                            opt_i = (opt_i + 1);
                            target_triple = args[opt_i];
                        } else {
                            if (driver_starts_with(arg, "--target=")) {
                                target_triple = cfire::slice(arg, 9, arg.size());
                            } else {
                                if (driver_starts_with(arg, "-target=")) {
                                    target_triple = cfire::slice(arg, 8, arg.size());
                                } else {
                                    if ((arg == "--sysroot") && ((opt_i + 1) < n_args)) {
                                        opt_i = (opt_i + 1);
                                        sysroot = args[opt_i];
                                    } else {
                                        if (driver_starts_with(arg, "--sysroot=")) {
                                            sysroot = cfire::slice(arg, 10, arg.size());
                                        } else {
                                            if (target.size() == 0) {
                                                target = arg;
                                            } else {
                                                (void)(run_args.push_back(arg));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        opt_i = (opt_i + 1);
    }
    if (target.size() == 0) {
        return cfire::main_driver::run_usage();
    }
    if (sub == "emit-cpp") {
        return cfire::main_driver::run_emit_cpp(target, runtime_dir, target_triple, sysroot);
    }
    if (sub == "check") {
        return cfire::main_driver::run_check(target, runtime_dir, json_mode, target_triple, sysroot);
    }
    if (sub == "build") {
        return cfire::main_driver::run_build(target, runtime_dir, sanitize, target_triple, sysroot);
    }
    if ((sub == "run") || (sub == "test")) {
        auto is_test = (sub == "test");
        return cfire::main_driver::run_test(target, runtime_dir, is_test, sanitize, target_triple, sysroot, run_args);
    }
    if (sub == "eject") {
        std::string out_dir = "ejected_cpp";
        if (args.size() >= 4) {
            out_dir = args[3];
        }
        return cfire::main_driver::run_eject(target, runtime_dir, out_dir);
    }
    return cfire::main_driver::run_usage();
}
