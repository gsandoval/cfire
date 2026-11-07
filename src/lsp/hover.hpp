#pragma once
// src/lsp/hover.hpp — LSP `textDocument/hover` & `textDocument/signatureHelp` Providers

#include "server.hpp"

namespace cfire::lsp {

inline auto Server::compute_hover(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);

    // 0A. Check if cursor is on or inside a struct tag (`` `json:"..."` ``, `` `cli:"..."` ``, `` `db:"..."` ``)
    if (line >= 0 && static_cast<std::size_t>(line) < lines.size()) {
        const std::string& ln = lines[static_cast<std::size_t>(line)];
        auto b1 = ln.find('`');
        if (b1 != std::string::npos && character >= static_cast<int>(b1)) {
            auto b2 = ln.find('`', b1 + 1);
            if (b2 != std::string::npos && character <= static_cast<int>(b2) + 1) {
                std::string tag_content = ln.substr(b1, b2 - b1 + 1);
                std::string desc;
                if (tag_content.contains("json:")) {
                    desc += "### Struct Tag `` `json:\"<key>\"` ``\n"
                            "Specifies the JSON object property key for compile-time reflection in `ext:json` (`json.parse_struct[T]` / `json.stringify_struct`).\n\n"
                            "**Example:**\n```cf\nService: str `json:\"service\"`\n```\n";
                }
                if (tag_content.contains("cli:") || tag_content.contains("help:")) {
                    if (!desc.empty()) desc += "\n---\n\n";
                    desc += "### Struct Tag `` `cli:\"--flag, -f\" help:\"...\"` ``\n"
                            "Declares command-line argument mapping for `core:cli` (`cli.parse[Options]()`).\n"
                            "Binds long/short flags and auto-generates `--help` documentation.\n\n"
                            "**Example:**\n```cf\nInput: str `cli:\"--input, -i\" help:\"Path to input log file\"`\n```\n";
                }
                if (tag_content.contains("db:")) {
                    if (!desc.empty()) desc += "\n---\n\n";
                    desc += "### Struct Tag `` `db:\"<column>\"` ``\n"
                            "Maps this struct field to an SQLite database column for `ext:db/sqlite` (`sqlite.map_row[T]`).\n\n"
                            "**Example:**\n```cf\nId: i64 `db:\"id\"`\n```\n";
                }
                if (!desc.empty()) return desc;
            }
        }
    }

    // 0B. Check if cursor is on `@pkg` attribute
    if (sym.name == "@pkg" || sym.name == "pkg") {
        if (line >= 0 && static_cast<std::size_t>(line) < lines.size() && lines[static_cast<std::size_t>(line)].contains("@pkg")) {
            return "### Attribute `@pkg(\"<name>\")`\n"
                   "Declares an external C/C++ library dependency resolved automatically via `pkg-config`.\n\n"
                   "The build system automatically runs `pkg-config --cflags <name>` and `pkg-config --libs <name>` "
                   "to link the native library and inject headers into the **Clang AST Header Bridge**.\n\n"
                   "**Example:**\n```cf\nimport cpp \"zstd.h\" @pkg(\"libzstd\")\n```";
        }
    }

    // 0C. Check built-in functions & keywords
    if (sym.qualifier.empty()) {
        if (sym.name == "println") {
            return "### Built-in Function `println(format, ...args)`\n```cf\nprintln(format: str, ...args: Any) -> void\n```\n"
                   "Formats and writes text to `stdout` with a trailing newline. Powered by ISO C++23 `std::println`.\n\n"
                   "**Example:**\n```cf\nprintln(\"Hello {}, count: {}\", name, 42)\n```";
        }
        if (sym.name == "print") {
            return "### Built-in Function `print(format, ...args)`\n```cf\nprint(format: str, ...args: Any) -> void\n```\n"
                   "Formats and writes text to `stdout` without a trailing newline. Powered by ISO C++23 `std::print`.\n\n"
                   "**Example:**\n```cf\nprint(\"Processing... \")\n```";
        }
        if (sym.name == "eprintln") {
            return "### Built-in Function `eprintln(format, ...args)`\n```cf\neprintln(format: str, ...args: Any) -> void\n```\n"
                   "Formats and writes text to `stderr` with a trailing newline. Powered by ISO C++23 `std::println(stderr, ...)`.\n\n"
                   "**Example:**\n```cf\neprintln(\"Error: failed to open {}\", path)\n```";
        }
        if (sym.name == "eprint") {
            return "### Built-in Function `eprint(format, ...args)`\n```cf\neprint(format: str, ...args: Any) -> void\n```\n"
                   "Formats and writes text to `stderr` without a trailing newline. Powered by ISO C++23 `std::print(stderr, ...)`.\n\n"
                   "**Example:**\n```cf\neprint(\"Warning: \")\n```";
        }
        if (sym.name == "readln") {
            return "### Built-in Function `readln(prompt)`\n```cf\nreadln(prompt: str = \"\") -> str?\n```\n"
                   "Optionally prints `prompt` to `stdout` (flushed) and reads a single line from `stdin` without the trailing newline (`\\n` / `\\r\\n`), returning `none` on EOF.\n\n"
                   "**Example:**\n```cf\nline := readln(\"> \") or { return }\n```";
        }
        if (sym.name == "panic") {
            return "### Built-in Function `panic(msg)`\n```cf\npanic(msg: str) -> void\n```\n"
                   "Aborts the current process immediately with a formatted error message to `stderr`.\n\n"
                   "**Example:**\n```cf\npanic(\"unreachable state reached\")\n```";
        }
        if (sym.name == "assert") {
            return "### Built-in Power-Assertion `assert(condition, message)`\n```cf\nassert(cond: bool, msg: str = \"\") -> void\n```\n"
                   "Evaluates `cond` once and aborts execution if false. For binary comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`), automatically formats `[left: <val>, right: <val>]` value diffs.\n\n"
                   "**Example:**\n```cf\nassert(capacity >= 1, \"capacity must be positive\")\n```";
        }
        if (sym.name == "assert_eq") {
            return "### Built-in Power-Assertion `assert_eq(left, right, message)`\n```cf\nassert_eq(left: T, right: T, msg: str = \"\") -> void\n```\n"
                   "Evaluates `left` and `right` once, verifies `left == right`, and formats `[left: <val>, right: <val>]` value diffs on failure.\n\n"
                   "**Example:**\n```cf\nassert_eq(actual_count, 42, \"count mismatch\")\n```";
        }
        if (sym.name == "assert_ne") {
            return "### Built-in Power-Assertion `assert_ne(left, right, message)`\n```cf\nassert_ne(left: T, right: T, msg: str = \"\") -> void\n```\n"
                   "Evaluates `left` and `right` once, verifies `left != right`, and formats `[left: <val>, right: <val>]` value diffs on failure.\n\n"
                   "**Example:**\n```cf\nassert_ne(status, 0, \"expected non-zero status\")\n```";
        }
        if (sym.name == "black_box") {
            return "### Built-in Optimization Barrier `black_box(value)`\n```cf\nblack_box[T](val: T) -> T\n```\n"
                   "Prevents the C++23 optimizer (`-O2`/`-O3`) from constant-folding or dead-code-eliminating benchmark expressions inside `bench \"...\"` blocks.\n\n"
                   "**Example:**\n```cf\nbench \"hash throughput\" |n in [64, 1024]| {\n    black_box(compute_hash(n))\n}\n```";
        }
        if (sym.name == "fuzz") {
            return "### Property-Based Fuzz Test `fuzz \"name\" |params...| { ... }`\n"
                   "Automatically generates deterministic adversarial edge-case inputs (`0`, `-1`, `MIN`, `MAX`, empty/UTF-8/NUL/SQL/JSON strings, reflected structs) and shrinks any failing counterexample to its minimal input.\n\n"
                   "**CLI Flags:** `cfire test --fuzz-runs 500 --seed 12345`\n\n"
                   "**Example:**\n```cf\nfuzz \"addition is commutative\" |a: i64, b: i64| {\n    assert_eq(a + b, b + a)\n}\n```";
        }
        if (sym.name == "bench") {
            return "### Statistical Micro-Benchmark `bench \"name\" |param in [...]| { ... }`\n"
                   "Runs warmup, dynamic batch calibration, and multi-sample statistical timing (`iters`, `min`, `p50`, `mean`, `p99`, `±stddev%`, `ops/s`) across parameter variations (`cfire bench`). Also runs 1 smoke iteration during `cfire test`.\n\n"
                   "**Example:**\n```cf\nbench \"payload encode\" |size in [64, 1024, 16384]| {\n    black_box(encode_payload(size))\n}\n```";
        }
        if (sym.name == "make") {
            return "### Built-in Allocator `make[T](...args)`\n```cf\nmake[own[T]](args...) -> own[T]\nmake[shared[T]](args...) -> shared[T]\nmake[sync.Channel[T]](capacity: usize) -> sync.Channel[T]\n```\n"
                   "Allocates a managed smart pointer on the heap (`std::make_unique` for `own[T]`, `std::make_shared` for `shared[T]`), or initializes a bounded/unbounded channel.\n\n"
                   "**Example:**\n```cf\nbox := make[own[Node]](Node{Id: 1})\n```";
        }
        if (sym.name == "defer") {
            return "### Keyword `defer`\n"
                   "Schedules a single statement (`defer expr`) or a multi-statement block (`defer { ... }`) to execute when the enclosing function or block exits (LIFO order).\n\n"
                   "Lowers 1-to-1 to `cfire::scope_exit` in C++23 (`return` inside `defer { ... }` is rejected via `cfire/RETURN-IN-DEFER`).\n\n"
                   "**Example:**\n```cf\ndefer println(\"Cleaned up session\")\ndefer {\n    file.flush()\n    file.close()\n}\n```";
        }
        if (sym.name == "cpp") {
            return "### Block `cpp { ... }`\n"
                   "Directly embeds raw ISO C++23 code into the generated output without syntax translation, providing zero-overhead interoperability.\n\n"
                   "**Example:**\n```cf\ncpp {\n    std::println(\"Direct native C++23 call\");\n}\n```";
        }
    }

    // 1. Check `core:*` stdlib catalog (either `mod.fn` or `mod`)
    if (!sym.name.empty() || !sym.qualifier.empty()) {
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            if (sym.qualifier == mod.ns_alias) {
                for (const auto& fn : mod.functions) {
                    if (fn.name == sym.name) {
                        return std::format("### `{}.{}` (`{}`)\n```cf\n{}\n```\n**Lowered C++23:** `{}`\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                            mod.ns_alias, fn.name, mod.import_path, fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
                    }
                }
            }
            if (sym.qualifier.empty() && sym.name == mod.ns_alias) {
                std::string fns_list;
                for (const auto& fn : mod.functions) {
                    fns_list += std::format("- `{}` — {}\n", fn.cf_sig, fn.summary);
                }
                return std::format("### `import \"{}\"` (`{}`)\n{}\n\n**Auto-link flags:** `{}`\n\n**Functions:**\n{}",
                    mod.import_path, mod.ns_alias, mod.overview, mod.link_flags, fns_list);
            }
        }
    }

    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (!ast_res) return "";
    const auto& ast = *ast_res;
    auto env = detail::build_scope_bindings(ast, ws, lines, line);

    // 2. Check imported local `.cf` packages (`checksum` or `checksum.compute_tag`) and `cpp::` Clang AST Bridge!
    for (const auto& pkg : ws.local_packages) {
        auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
        if (sym.qualifier.empty() && sym.name == pkg.alias) {
            std::string exports;
            for (const auto& fn : pkg.ast.functions) {
                exports += std::format("- `{}` (`{}`)\n", cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn));
            }
            for (const auto& st : pkg.ast.structs) {
                exports += std::format("- `type {} struct` ({} fields)\n", st.name, st.fields.size());
            }
            return std::format("### Package `{}` (`import \"{}\"`)\n**Source:** `{}`\n\n**Exported Symbols:**\n{}",
                pkg.alias, pkg.import_path, pkg.file_path, exports);
        }
        if (sym.qualifier == pkg.alias) {
            for (const auto& fn : pkg.ast.functions) {
                if (fn.name == sym.name) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                    std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                    return std::format("### `{}.{}` (from `{}`)\n```cf\n{}\n```\n**Lowered ISO C++23:**\n```cpp\n{}\n```\nDefined at `{}:{}`{}",
                        pkg.alias, fn.name, pkg.import_path, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), pkg.file_path, fn.line, doc_sec);
                }
            }
            for (const auto& st : pkg.ast.structs) {
                if (st.name == sym.name) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                    std::unordered_set<std::string> vis;
                    auto layout = detail::compute_struct_layout(st, pkg.ast, ws, vis);
                    std::string layout_hdr = std::format("\n\n**Memory Layout (64-bit ABI):** `Size: {} bytes | Align: {} bytes`\n", layout.total_size, layout.align);
                    std::string fields_table = "\n| Field | Offset | `.cf` Type | Lowered C++23 | Tags |\n|---|---|---|---|---|\n";
                    for (std::size_t i = 0; i < st.fields.size(); ++i) {
                        const auto& f = st.fields[i];
                        int off = (i < layout.fields.size()) ? layout.fields[i].offset : 0;
                        int fsize = (i < layout.fields.size()) ? layout.fields[i].size : 0;
                        fields_table += std::format("| `{}` | `+{} ({}B)` | `{}` | `{}` | `{}` |\n",
                            f.name, off, fsize, cfire::fmt::format_type_ref(f.type), detail::lower_type_preview(f.type),
                            f.raw_tag.empty() ? "-" : f.raw_tag);
                    }
                    std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                    return std::format("### Struct `{}.{}` (from `{}`)\n```cf\n{}\n```{}{}{}",
                        pkg.alias, st.name, pkg.import_path, cfire::fmt::format_struct_signature(st), layout_hdr, doc_sec, fields_table);
                }
            }
        }
    }

    if (sym.qualifier == "cpp" || ws.cpp_symbols.contains(sym.name)) {
        if (auto cit = ws.cpp_symbols.find(sym.name); cit != ws.cpp_symbols.end()) {
            const auto& csym = cit->second;
            std::string params_str;
            for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                if (i > 0) params_str += ", ";
                params_str += csym.param_types[i];
            }
            std::string hdr = ws.cpp_symbol_header[sym.name];
            return std::format("### Native C/C++ Function `cpp::{}` (`<{}>`)\n```cpp\n{} {}({})\n```\n**cfire Return Type:** `{}`\n\nIngested via the **Clang AST Header Bridge** (`clang++ -Xclang -ast-dump=json`) with automatic `cfire::c_str` boundary coercion.",
                csym.name, hdr, csym.return_type, csym.name, params_str, detail::cpp_return_to_cf(csym.return_type));
        }
    }

    // 3. If `qualifier.member` where `qualifier` is a local variable, chained field, or struct/enum:
    if (!sym.qualifier.empty()) {
        if (sym.name == "len") {
            return "### Property `.len`\n```cf\nlen: usize\n```\n**Lowered C++23:** `.size()` (`std::size_t`)";
        }
        if (sym.name == "data" || sym.name == "data_ptr") {
            return "### Method `." + sym.name + "()`\n```cf\nfn (s: []T | str) " + sym.name + "() -> *T\n```\n**Lowered C++23:** `({}).data()`\n\nReturns a raw pointer to the underlying contiguous element buffer (`*const c_char` for `str`, `*const T` for immutable slices) for C FFI interop.";
        }
        if (sym.name == "bytes") {
            return "### Method `.bytes()`\n```cf\nfn (s: str) bytes() -> []byte\n```\n**Lowered C++23:** `cfire::to_bytes({})`\n\nReturns a safe byte slice view (`std::span<const std::byte>`) over the string's UTF-8 bytes.";
        }
        std::string recv_type = detail::resolve_qualifier_type(sym.qualifier, ast, ws, env);
        // Check stdlib handle methods (`TcpListener.accept`, `TcpConn.read_str`, `Conn.query`, `Channel.send`)
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            for (const auto& fn : mod.functions) {
                if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                    std::string htype = fn.name.substr(0, dot);
                    std::string mname = fn.name.substr(dot + 1);
                    if ((recv_type == htype || recv_type == mod.ns_alias + "." + htype || recv_type.starts_with("sync." + htype)) &&
                        mname == sym.name) {
                        return std::format("### `{}` (`{}`)\n```cf\n{}\n```\n**Lowered C++23:** `{}`\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                            fn.name, mod.import_path, fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
                    }
                } else if (recv_type == "str" && mod.import_path == "core:str" && fn.name == sym.name) {
                    return std::format("### `{}.{}` (`{}`)\n```cf\n{}\n```\n**Lowered C++23:** `{}`\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                        mod.ns_alias, fn.name, mod.import_path, fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
                }
            }
        }
        // Check user struct fields & methods on `recv_type` (both local and imported packages)
        auto check_structs = [&](const std::vector<cfire::frontend::StructDecl>& structs, const std::vector<std::string>& doc_lines) -> std::string {
            for (const auto& st : structs) {
                if (st.name == recv_type) {
                    for (const auto& f : st.fields) {
                        if (f.name == sym.name) {
                            std::string f_doc = detail::extract_leading_doc_comments(doc_lines, f.line);
                            if (f.line >= 1 && static_cast<std::size_t>(f.line) <= doc_lines.size()) {
                                const std::string& fln = doc_lines[static_cast<std::size_t>(f.line - 1)];
                                if (auto cpos = fln.find("//"); cpos != std::string::npos) {
                                    std::string ic = fln.substr(cpos + 2);
                                    if (!ic.empty() && ic.front() == ' ') ic = ic.substr(1);
                                    if (!ic.empty()) {
                                        if (!f_doc.empty()) f_doc += "\n";
                                        f_doc += ic;
                                    }
                                }
                            }
                            std::string f_cf = cfire::fmt::format_type_ref(f.type);
                            std::string f_cpp = detail::lower_type_preview(f.type);
                            std::unordered_set<std::string> vis;
                            auto layout = detail::compute_struct_layout(st, ast, ws, vis);
                            std::string layout_info;
                            for (const auto& fl : layout.fields) {
                                if (fl.name == f.name) {
                                    layout_info = std::format("\n**Field Layout:** `Offset: +{} bytes | Size: {} bytes`", fl.offset, fl.size);
                                    break;
                                }
                            }
                            std::string tag_info = f.raw_tag.empty() ? "" : std::format("\n\n**Struct Tag:** `` `{}` ``", f.raw_tag);
                            std::string doc_sec = f_doc.empty() ? "" : ("\n\n**Documentation:**\n" + f_doc);
                            return std::format("### Field `{}.{}`\n```cf\n{}: {}\n```\n**Lowered C++23:** `{} {}{{}};`{}{}{}",
                                st.name, f.name, f.name, f_cf, f_cpp, f.name, layout_info, tag_info, doc_sec);
                        }
                    }
                }
            }
            return "";
        };
        if (std::string s = check_structs(ast.structs, lines); !s.empty()) return s;
        for (const auto& pkg : ws.local_packages) {
            auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
            if (std::string s = check_structs(pkg.ast.structs, pkg_lines); !s.empty()) return s;
        }

        for (const auto& fn : ast.functions) {
            if (fn.receiver && fn.receiver->type.name == recv_type && fn.name == sym.name) {
                std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
                std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                return std::format("### Method `{}.{}`\n```cf\n{}\n```\n**Lowered C++23:**\n```cpp\n{}\n```{}",
                    recv_type, fn.name, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), doc_sec);
            }
        }
        for (const auto& pkg : ws.local_packages) {
            for (const auto& fn : pkg.ast.functions) {
                if (fn.receiver && fn.receiver->type.name == recv_type && fn.name == sym.name) {
                    auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                    std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                    return std::format("### Method `{}.{}`\n```cf\n{}\n```\n**Lowered C++23:**\n```cpp\n{}\n```{}",
                        recv_type, fn.name, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), doc_sec);
                }
            }
        }
        // Check enum variants (`Shape.Circle`)
        for (const auto& en : ast.enums) {
            if (en.name == recv_type) {
                for (const auto& v : en.variants) {
                    if (v.name == sym.name) {
                        return std::format("### Enum Variant `{}.{}`\n```cf\n{}\n```\nLowers to `{}::{}` inside `std::variant` (line {}).",
                            en.name, v.name, cfire::fmt::format_enum_signature(en), en.name, v.name, v.line);
                    }
                }
            }
        }
    }

    // 4. Check if hovering over a local variable or function parameter!
    if (sym.qualifier.empty()) {
        if (auto it = env.find(sym.name); it != env.end()) {
            const auto& b = it->second;
            std::string kind_label = b.is_param
                ? std::format("Parameter (`{}` intent)", b.intent)
                : (b.is_mut ? "Local Variable (`mut`)" : "Local Binding (`const` by default)");
            std::string extra;
            std::string unwrapped = detail::strip_wrappers(b.cf_type);
            for (const auto& st : ast.structs) {
                if (st.name == unwrapped) {
                    extra = "\n\n**Struct Fields & Properties:**\n```cf\n" + cfire::fmt::format_struct_signature(st) + "\n```";
                    break;
                }
            }
            return std::format("### {} `{}`\n```cf\n{}{}: {}\n```\n**Lowered C++23 Type:** `{}`\n\nDeclared at line {}{}",
                kind_label, b.name, b.is_mut ? "mut " : "", b.name, b.cf_type, b.cpp_type, b.decl_line, extra);
        }
    }

    // 5. Check top-level functions, structs, enums, and enum variants
    for (const auto& fn : ast.functions) {
        if (fn.name == sym.name) {
            std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
            std::string cf_sig = cfire::fmt::format_fn_signature(fn);
            std::string cpp_sig = detail::lower_fn_preview(fn);
            std::string param_docs;
            if (!fn.params.empty()) {
                param_docs = "\n\n**Parameters:**\n";
                for (const auto& p : fn.params) {
                    param_docs += std::format("- `{}` (`{}` intent): `{}` $\\rightarrow$ `{}`\n",
                        p.name, p.intent, cfire::fmt::format_type_ref(p.type), detail::lower_type_preview(p.type, true, p.intent));
                }
            }
            std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
            return std::format("### Function `{}`\n```cf\n{}\n```\n**Lowered ISO C++23:**\n```cpp\n{}\n```{}{}",
                fn.name, cf_sig, cpp_sig, doc_sec, param_docs);
        }
    }
    for (const auto& st : ast.structs) {
        if (st.name == sym.name) {
            std::string doc = detail::extract_leading_doc_comments(lines, st.line);
            std::unordered_set<std::string> vis;
            auto layout = detail::compute_struct_layout(st, ast, ws, vis);
            std::string layout_hdr = std::format("\n\n**Memory Layout (64-bit ABI):** `Size: {} bytes | Align: {} bytes`\n", layout.total_size, layout.align);
            std::string fields_table = "\n| Field | Offset | `.cf` Type | Lowered C++23 | Tags |\n|---|---|---|---|---|\n";
            for (std::size_t i = 0; i < st.fields.size(); ++i) {
                const auto& f = st.fields[i];
                int off = (i < layout.fields.size()) ? layout.fields[i].offset : 0;
                int fsize = (i < layout.fields.size()) ? layout.fields[i].size : 0;
                fields_table += std::format("| `{}` | `+{} ({}B)` | `{}` | `{}` | `{}` |\n",
                    f.name, off, fsize, cfire::fmt::format_type_ref(f.type), detail::lower_type_preview(f.type),
                    f.raw_tag.empty() ? "-" : f.raw_tag);
            }
            std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
            return std::format("### Struct `{}`\n```cf\n{}\n```\nLowers 1-to-1 to C++23 aggregate `struct {}` (line {}).{}{}{}",
                st.name, cfire::fmt::format_struct_signature(st), st.name, st.line, layout_hdr, doc_sec, fields_table);
        }
    }
    // Check unqualified functions/structs/enums from sibling/local packages sharing the package
    for (const auto& pkg : ws.local_packages) {
        auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
        for (const auto& fn : pkg.ast.functions) {
            if (fn.name == sym.name) {
                std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                return std::format("### Function `{}` (from `{}`)\n```cf\n{}\n```\n**Lowered ISO C++23:**\n```cpp\n{}\n```\nDefined at `{}:{}`{}",
                    fn.name, pkg.file_path, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), pkg.file_path, fn.line, doc_sec);
            }
        }
        for (const auto& st : pkg.ast.structs) {
            if (st.name == sym.name) {
                std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                return std::format("### Struct `{}` (from `{}`)\n```cf\n{}\n```\nDefined at `{}:{}`{}",
                    st.name, pkg.file_path, cfire::fmt::format_struct_signature(st), pkg.file_path, st.line, doc_sec);
            }
        }
    }
    for (const auto& en : ast.enums) {
        if (en.name == sym.name) {
            std::string doc = detail::extract_leading_doc_comments(lines, en.line);
            std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
            return std::format("### Algebraic Enum `{}`\n```cf\n{}\n```\nLowers to `std::variant` with exhaustive `match` verification (line {}).{}",
                en.name, cfire::fmt::format_enum_signature(en), en.line, doc_sec);
        }
        for (const auto& v : en.variants) {
            if (v.name == sym.name) {
                return std::format("### Enum Variant `{}.{}`\n```cf\n{}\n```\nLowers to `{}::{}` inside `std::variant` (line {}).",
                    en.name, v.name, cfire::fmt::format_enum_signature(en), en.name, v.name, v.line);
            }
        }
    }

    // 6. Sub-expression type hover (operators, binary expressions, call expressions, literals)
    int target_line_1 = line + 1;
    int target_col_1 = character + 1;

    std::vector<cfire::frontend::ExprPtr> all_exprs;
    for (const auto& fn : ast.functions) detail::collect_stmt_exprs(fn.body, all_exprs);
    for (const auto& t : ast.tests) detail::collect_stmt_exprs(t.body, all_exprs);
    for (const auto& gv : ast.global_vars) if (gv.value) detail::collect_all_exprs(gv.value, all_exprs);

    cfire::frontend::ExprPtr best_expr = nullptr;
    int best_span = 999999;

    for (const auto& expr : all_exprs) {
        if (!expr || expr->line != target_line_1) continue;
        std::string text_rep = detail::expr_to_string(expr);
        int est_len = static_cast<int>(text_rep.size());
        if (est_len <= 0) est_len = 1;
        int col_start = expr->col;
        if (const auto* bin = std::get_if<cfire::frontend::BinaryExpr>(&expr->node)) {
            if (bin->left) col_start = std::min(col_start, bin->left->col);
        }
        int col_end = col_start + est_len - 1;
        if (target_col_1 >= col_start && target_col_1 <= col_end) {
            if (est_len < best_span) {
                best_span = est_len;
                best_expr = expr;
            }
        }
    }

    if (best_expr) {
        std::string exp_str = detail::expr_to_string(best_expr);
        std::string cf_type = detail::infer_expr_cf_type(best_expr, ast, ws, env);
        if (!exp_str.empty() && cf_type != "auto") {
            std::string cpp_type = detail::cf_type_string_to_cpp(cf_type);
            return std::format("### Expression `{}`\n```cf\n{}: {}\n```\n**Lowered ISO C++23:** `{}`",
                exp_str, exp_str, cf_type, cpp_type);
        }
    }

    // 7. Diagnostic hover: if a diagnostic exists on `line`, explain the diagnostic rule from `diagnostics_catalog()`
    {
        std::vector<detail::DiagnosticEntry> diags;
        {
            std::lock_guard<std::mutex> lock(diag_store_mutex_);
            if (auto it = last_diagnostics_.find(uri); it != last_diagnostics_.end()) {
                diags = it->second;
            }
        }
        for (const auto& d : diags) {
            if (d.line == line) {
                for (const auto& doc : cfire::doc::diagnostics_catalog()) {
                    if (doc.code == d.code) {
                        return std::format(
                            "### Diagnostic `{}` — {}\n{}\n\n**Rejected Example:**\n```cf\n{}\n```\n**Accepted Fix:**\n```cf\n{}\n```",
                            doc.code, doc.title, doc.rule, doc.bad_example, doc.good_example);
                    }
                }
            }
        }
    }

    return "";
}

inline auto Server::compute_signature_help(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto ctx_opt = detail::find_call_context(lines, line, character);
    if (!ctx_opt) return "null";
    const auto& ctx = *ctx_opt;

    auto format_sig_resp = [&](std::string_view label, std::string_view doc_str, const std::vector<std::string>& param_labels) -> std::string {
        std::string params_json = "[";
        for (std::size_t i = 0; i < param_labels.size(); ++i) {
            if (i > 0) params_json += ",";
            params_json += std::format(R"({{"label":"{}"}})", detail::json_escape(param_labels[i]));
        }
        params_json += "]";
        int active_p = param_labels.empty() ? 0 : std::clamp(ctx.active_param, 0, static_cast<int>(param_labels.size()) - 1);
        return std::format(
            R"({{"signatures":[{{"label":"{}","documentation":{{"kind":"markdown","value":"{}"}},"parameters":{}}}],"activeSignature":0,"activeParameter":{}}})",
            detail::json_escape(label), detail::json_escape(doc_str), params_json, active_p);
    };

    auto extract_params_from_sig = [](std::string_view sig) -> std::vector<std::string> {
        std::vector<std::string> plabels;
        auto lp = sig.find('(');
        auto rp = sig.rfind(')');
        if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp + 1) {
            std::string plist(sig.substr(lp + 1, rp - lp - 1));
            std::istringstream iss(plist);
            std::string item;
            while (std::getline(iss, item, ',')) {
                plabels.push_back(cfire::fmt::detail::trim_ws(item));
            }
        }
        return plabels;
    };

    // 1. Check `core:*` stdlib catalog
    if (!ctx.qualifier.empty()) {
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            if (mod.ns_alias == ctx.qualifier) {
                for (const auto& fn : mod.functions) {
                    if (fn.name == ctx.callee) {
                        return format_sig_resp(
                            fn.cf_sig,
                            std::format("{} (C++23: `{}`)", fn.summary, fn.cpp_lowering),
                            extract_params_from_sig(fn.cf_sig));
                    }
                }
            }
        }
    }

    // 2. Check user functions, methods, imported `.cf` packages, stdlib handles, and `cpp::` functions
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (ast_res) {
        auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);

        auto format_fn_decl = [&](const cfire::frontend::FnDecl& fn) -> std::string {
            std::vector<std::string> plabels;
            for (const auto& p : fn.params) {
                std::string pl = (p.intent != "in" && !p.intent.empty()) ? (p.intent + " ") : "";
                pl += std::format("{}: {}", p.name, cfire::fmt::format_type_ref(p.type));
                plabels.push_back(std::move(pl));
            }
            return format_sig_resp(
                cfire::fmt::format_fn_signature(fn),
                std::format("Lowered C++23: `{}`", detail::lower_fn_preview(fn)),
                plabels);
        };

        for (const auto& fn : ast_res->functions) {
            if (fn.name == ctx.callee) return format_fn_decl(fn);
        }
        for (const auto& pkg : ws.local_packages) {
            if (ctx.qualifier.empty() || ctx.qualifier == pkg.alias) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.name == ctx.callee) return format_fn_decl(fn);
                }
            }
        }
        if (ctx.qualifier == "cpp" || ws.cpp_symbols.contains(ctx.callee)) {
            if (auto cit = ws.cpp_symbols.find(ctx.callee); cit != ws.cpp_symbols.end()) {
                const auto& csym = cit->second;
                std::string params_str;
                for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                    if (i > 0) params_str += ", ";
                    params_str += csym.param_types[i];
                }
                std::string sig = std::format("cpp::{}({}) -> {}", csym.name, params_str, detail::cpp_return_to_cf(csym.return_type));
                return format_sig_resp(sig, std::format("C/C++ Header `<{}>`", ws.cpp_symbol_header[csym.name]), csym.param_types);
            }
        }
        if (!ctx.qualifier.empty()) {
            std::string recv_type = detail::resolve_qualifier_type(ctx.qualifier, *ast_res, ws, env);
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& fn : mod.functions) {
                    if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                        std::string htype = fn.name.substr(0, dot);
                        std::string mname = fn.name.substr(dot + 1);
                        if ((recv_type == htype || recv_type == mod.ns_alias + "." + htype || recv_type.starts_with("sync." + htype)) &&
                            mname == ctx.callee) {
                            return format_sig_resp(fn.cf_sig, fn.summary, extract_params_from_sig(fn.cf_sig));
                        }
                    }
                }
            }
        }
    }
    return "null";
}

} // namespace cfire::lsp
