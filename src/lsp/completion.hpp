#pragma once
// src/lsp/completion.hpp — LSP `textDocument/completion` & Auto-Import Edit Synthesis

#include "server.hpp"

namespace cfire::lsp {

inline auto Server::compute_import_edit(const std::vector<std::string>& lines, std::string_view import_path) -> std::string {
    int last_import_line = -1;
    int package_line = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::string_view l = lines[i];
        while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
        if (l.starts_with("package ")) {
            package_line = i;
        } else if (l.starts_with("import ")) {
            last_import_line = i;
        }
    }

    int insert_line = 0;
    std::string new_text;
    if (last_import_line >= 0) {
        insert_line = last_import_line + 1;
        new_text = std::format("import \"{}\"\n", import_path);
    } else if (package_line >= 0) {
        if (package_line + 1 < static_cast<int>(lines.size()) && lines[package_line + 1].empty()) {
            insert_line = package_line + 2;
            new_text = std::format("import \"{}\"\n\n", import_path);
        } else {
            insert_line = package_line + 1;
            new_text = std::format("\nimport \"{}\"\n", import_path);
        }
    } else {
        insert_line = 0;
        new_text = std::format("import \"{}\"\n\n", import_path);
    }

    return std::format(
        R"([{{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":0}}}},"newText":"{}"}}])",
        insert_line, insert_line, detail::json_escape(new_text));
}

inline auto Server::compute_completions(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto sym = detail::symbol_at_position(lines, line, character);

    std::string out = "[";
    bool first = true;
    auto append_item = [&](std::string_view label, int kind, std::string_view detail_str, std::string_view doc_str,
                           std::string_view insert_text = "", int insert_text_format = 1,
                           std::string_view filter_text = "", std::string_view additional_edits = "") {
        if (!first) out += ",";
        first = false;
        out += std::format(
            R"({{"label":"{}","kind":{},"detail":"{}","documentation":"{}"}})",
            detail::json_escape(label), kind, detail::json_escape(detail_str), detail::json_escape(doc_str));
        out.pop_back();

        if (!insert_text.empty() && insert_text != label) {
            out += std::format(R"(,"insertText":"{}")", detail::json_escape(insert_text));
        }
        if (insert_text_format > 1 && client_snippet_support_) {
            out += std::format(R"(,"insertTextFormat":{})", insert_text_format);
        }
        if (!filter_text.empty() && filter_text != label) {
            out += std::format(R"(,"filterText":"{}")", detail::json_escape(filter_text));
        }
        if (!additional_edits.empty()) {
            out += std::format(R"(,"additionalTextEdits":{})", additional_edits);
        }
        out += "}";
    };

    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);

    auto is_module_imported = [&](std::string_view import_path, std::string_view alias) -> bool {
        if (ast_res) {
            for (const auto& imp : ast_res->imports) {
                if (imp.path == import_path || imp.path == alias) {
                    return true;
                }
            }
        }
        for (const auto& l : lines) {
            if (l.find(import_path) != std::string::npos && l.find("import ") != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    if (!sym.qualifier.empty()) {
        // 1. Check `core:*` module functions (`fs.`, `json.`, `net.`, etc.)
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            if (mod.ns_alias == sym.qualifier) {
                bool imported = is_module_imported(mod.import_path, mod.ns_alias);
                std::string add_edit = imported ? "" : compute_import_edit(lines, mod.import_path);
                for (const auto& fn : mod.functions) {
                    if (fn.name.find('.') != std::string::npos) continue;
                    std::string fn_detail = imported ? fn.cf_sig : std::format("{} (auto-import)", fn.cf_sig);
                    std::string fn_doc = std::format("{} (C++23: {})", fn.summary, fn.cpp_lowering);
                    if (!imported) {
                        fn_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod.import_path);
                    }
                    append_item(fn.name, 3, fn_detail, fn_doc, fn.name, 1, fn.name, add_edit);
                }
            }
        }
        // 2. Check imported local `.cf` package symbols (`checksum.`)
        for (const auto& pkg : ws.local_packages) {
            if (pkg.alias == sym.qualifier) {
                auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                for (const auto& fn : pkg.ast.functions) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                    if (doc.empty()) doc = detail::lower_fn_preview(fn);
                    append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc);
                }
                for (const auto& st : pkg.ast.structs) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                    if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
                    append_item(st.name, 22, "type " + st.name + " struct", doc);
                }
                for (const auto& en : pkg.ast.enums) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, en.line);
                    if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
                    append_item(en.name, 13, "type " + en.name + " enum", doc);
                }
            }
        }
        // 3. Check `cpp::` Clang AST Bridge functions
        if (sym.qualifier == "cpp") {
            for (const auto& [cname, csym] : ws.cpp_symbols) {
                std::string params_str;
                for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                    if (i > 0) params_str += ", ";
                    params_str += csym.param_types[i];
                }
                append_item(cname, 3, std::format("{}({}) -> {}", cname, params_str, detail::cpp_return_to_cf(csym.return_type)),
                    std::format("Native C/C++ symbol from <{}>", ws.cpp_symbol_header[cname]));
            }
        }
        // 4. Check local variable / chained field type for struct fields, user methods, and stdlib handle methods!
        if (ast_res) {
            auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);
            std::string rtype = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env);
            if (rtype == "str") {
                append_item("len", 10, "usize", "Length in bytes (lowers to .size())");
                append_item("empty", 2, "fn empty() -> bool", "Returns true if empty");
                append_item("data", 2, "fn data() -> *const c_char", "Returns raw pointer to null-terminated UTF-8 C string (lowers to .data())");
                append_item("bytes", 2, "fn bytes() -> []byte", "Returns safe byte slice view over UTF-8 bytes (lowers to cfire::to_bytes())");
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    if (mod.import_path == "core:str") {
                        for (const auto& fn : mod.functions) {
                            if (!fn.name.contains('.')) {
                                append_item(fn.name, 2, fn.cf_sig, fn.summary);
                            }
                        }
                    }
                }
            }
            if (rtype.starts_with("[")) {
                append_item("len", 10, "usize", "Length in elements (lowers to .size())");
                append_item("data", 2, "fn data() -> *T", "Returns raw pointer to contiguous element buffer (lowers to .data())");
                append_item("data_ptr", 2, "fn data_ptr() -> *T", "Returns raw pointer to contiguous element buffer (lowers to .data())");
                append_item("empty", 2, "fn empty() -> bool", "Returns true if empty (lowers to .empty())");
                append_item("push", 2, "fn push(elem: T) -> void", "Appends element to vector (lowers to .push_back())");
                append_item("pop", 2, "fn pop() -> void", "Removes last element from vector (lowers to .pop_back())");
                append_item("capacity", 2, "fn capacity() -> usize", "Allocated storage capacity of the vector (lowers to .capacity())");
                append_item("clear", 2, "fn clear() -> void", "Erases all elements from vector (lowers to .clear())");
            }
            auto add_struct_fields = [&](const std::vector<cfire::frontend::StructDecl>& structs) {
                for (const auto& st : structs) {
                    if (st.name == rtype) {
                        for (const auto& f : st.fields) {
                            std::string f_sig = cfire::fmt::format_type_ref(f.type);
                            std::string f_doc = std::format("Field of {} (C++23: {}) {}",
                                st.name, detail::lower_type_preview(f.type), f.raw_tag);
                            append_item(f.name, 5, f_sig, f_doc);
                        }
                    }
                }
            };
            add_struct_fields(ast_res->structs);
            for (const auto& pkg : ws.local_packages) add_struct_fields(pkg.ast.structs);

            // User methods on `rtype`
            for (const auto& fn : ast_res->functions) {
                if (fn.receiver && fn.receiver->type.name == rtype) {
                    append_item(fn.name, 2, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn));
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.receiver && fn.receiver->type.name == rtype) {
                        append_item(fn.name, 2, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn));
                    }
                }
            }
            // Enum variants (`Shape.Circle`, `Shape.Rect`)
            for (const auto& en : ast_res->enums) {
                if (en.name == rtype) {
                    for (const auto& v : en.variants) {
                        append_item(v.name, 20, en.name + "::" + v.name, "Algebraic enum variant");
                    }
                }
            }
            // Stdlib handle methods (`TcpListener.`, `TcpConn.`, `Channel.`, `Conn.`)
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& fn : mod.functions) {
                    if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                        std::string htype = fn.name.substr(0, dot);
                        std::string mname = fn.name.substr(dot + 1);
                        if (rtype == htype || rtype == mod.ns_alias + "." + htype || rtype.starts_with("sync." + htype)) {
                            append_item(mname, 2, fn.cf_sig, fn.summary);
                        }
                    }
                }
            }
        }
        out += "]";
        return out;
    }

    // Unqualified completions: local bindings, `core:*` modules, imported packages, package functions/structs/enums
    if (ast_res) {
        auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);
        for (const auto& [vname, b] : env) {
            append_item(vname, 6, b.cf_type, std::format("C++23: {}", b.cpp_type));
        }
        for (const auto& pkg : ws.local_packages) {
            append_item(pkg.alias, 9, pkg.import_path, "Imported local .cf package");
        }
        if (!ws.cpp_symbols.empty()) {
            append_item("cpp", 9, "Clang AST Header Bridge", "Native C/C++ imported symbols");
        }
        for (const auto& fn : ast_res->functions) {
            std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
            if (doc.empty()) doc = detail::lower_fn_preview(fn);
            append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc);
        }
        for (const auto& st : ast_res->structs) {
            std::string doc = detail::extract_leading_doc_comments(lines, st.line);
            if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
            append_item(st.name, 22, "type " + st.name + " struct", doc);
        }
        for (const auto& en : ast_res->enums) {
            std::string doc = detail::extract_leading_doc_comments(lines, en.line);
            if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
            append_item(en.name, 13, "type " + en.name + " enum", doc);
        }
        for (const auto& pkg : ws.local_packages) {
            if (pkg.ast.package_name == ast_res->package_name && !ast_res->package_name.empty()) {
                auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.receiver) continue;
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                    if (doc.empty()) doc = detail::lower_fn_preview(fn);
                    append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc);
                }
                for (const auto& st : pkg.ast.structs) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                    if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
                    append_item(st.name, 22, "type " + st.name + " struct", doc);
                }
                for (const auto& en : pkg.ast.enums) {
                    std::string doc = detail::extract_leading_doc_comments(pkg_lines, en.line);
                    if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
                    append_item(en.name, 13, "type " + en.name + " enum", doc);
                }
            }
        }
    }
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        bool imported = is_module_imported(mod.import_path, mod.ns_alias);
        std::string add_edit = imported ? "" : compute_import_edit(lines, mod.import_path);
        std::string mod_detail = imported ? mod.import_path : (mod.import_path + " (auto-import)");
        append_item(mod.ns_alias, 9, mod_detail, mod.title, mod.ns_alias, 1, mod.ns_alias, add_edit);

        for (const auto& fn : mod.functions) {
            if (fn.name.find('.') != std::string::npos) continue;
            std::string call_name = mod.ns_alias + "." + fn.name;
            std::string fn_detail = imported
                ? std::format("{}: {}", mod.ns_alias, fn.cf_sig)
                : std::format("{} (auto-import): {}", mod.import_path, fn.cf_sig);
            std::string fn_doc = std::format("{}\n\nSignature: `{}`\nC++23: `{}`\nExample: `{}`",
                fn.summary, fn.cf_sig, fn.cpp_lowering, fn.example);
            if (!imported) {
                fn_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod.import_path);
            }
            std::string filter = fn.name + " " + call_name;
            append_item(fn.name, 3, fn_detail, fn_doc, call_name, 1, filter, add_edit);
        }
    }

    // Built-in functions, type constructors, and directives
    append_item("println", 3, "println(args...)", "Prints formatted arguments followed by a newline to standard output.");
    append_item("print", 3, "print(args...)", "Prints formatted arguments to standard output without trailing newline.");
    append_item("eprintln", 3, "eprintln(args...)", "Prints formatted arguments followed by a newline to standard error.");
    append_item("eprint", 3, "eprint(args...)", "Prints formatted arguments to standard error without trailing newline.");
    append_item("panic", 3, "panic(message: str)", "Terminates the program immediately with an error message.");
    append_item("assert", 3, "assert(cond: bool, msg: str = \"\")", "Aborts execution if condition evaluates to false (formats [left, right] value diffs).");
    append_item("assert_eq", 3, "assert_eq(left: T, right: T, msg: str = \"\")", "Verifies left == right with single evaluation and [left, right] value diff reporting.");
    append_item("assert_ne", 3, "assert_ne(left: T, right: T, msg: str = \"\")", "Verifies left != right with single evaluation and [left, right] value diff reporting.");
    append_item("[]byte", 3, "[]byte(val: str | []u8) -> []byte", "Constructs an owning byte slice ([]byte) from a string or slice.");
    append_item("[]u8", 3, "[]u8(val: str | []byte) -> []u8", "Constructs an owning u8 slice ([]u8) from a string or byte slice.");
    append_item("str", 3, "str(val: []byte | []u8 | scalar) -> str", "Constructs a UTF-8 string from a byte/u8 slice or scalar value.");
    append_item("byte", 3, "byte(val: integer) -> byte", "Constructs a std::byte value from an integer.");
    append_item("black_box", 3, "black_box(val: T) -> T", "Compiler optimization barrier for benchmarks inside bench \"...\" blocks.");
    append_item("make", 3, "make([]T, len, cap) / make(map[K]V)", "Allocates and initializes slices or maps.");
    append_item("@pkg", 14, "@pkg(\"libname\")", "Specifies system package / pkg-config dependency for C/C++ header import.");

    // Core language constructs & snippets (LSP-01)
    if (client_snippet_support_) {
        append_item("fn", 15, "fn name(params) -> void { ... }", "Function declaration snippet",
                    "fn ${1:name}(${2:params}) -> ${3:void} {\n\t${0}\n}", 2);
        append_item("type struct", 15, "type Name struct { ... }", "Struct declaration snippet",
                    "type ${1:Name} struct {\n\t${0}\n}", 2);
        append_item("type enum", 15, "type Name enum { ... }", "Algebraic enum declaration snippet",
                    "type ${1:Name} enum {\n\t${0}\n}", 2);
        append_item("match", 15, "match expr { ... }", "Pattern matching expression snippet",
                    "match ${1:expr} {\n\t${2:case} => ${0}\n}", 2);
        append_item("for", 15, "for item in iter { ... }", "For-in iteration loop snippet",
                    "for ${1:item} in ${2:iter} {\n\t${0}\n}", 2);
        append_item("for ..", 15, "for i in 0..n { ... }", "Numeric range loop snippet",
                    "for ${1:i} in ${2:0}..${3:n} {\n\t${0}\n}", 2);
        append_item("if", 15, "if condition { ... }", "Conditional statement snippet",
                    "if ${1:condition} {\n\t${0}\n}", 2);
        append_item("defer", 15, "defer <expr>", "Schedules statement to execute when enclosing function returns.",
                    "defer ${0}", 2);
        append_item("defer {}", 15, "defer { ... }", "Schedules a multi-statement cleanup block on scope exit.",
                    "defer {\n\t${0}\n}", 2);
        append_item("test", 15, "test \"name\" { ... }", "Unit test block snippet",
                    "test \"${1:name}\" {\n\t${0}\n}", 2);
        append_item("fuzz", 15, "fuzz \"name\" |x: i64| { ... }", "Property-based fuzz test with counterexample shrinking",
                    "fuzz \"${1:name}\" |${2:x: i64}| {\n\t${0}\n}", 2);
        append_item("bench", 15, "bench \"name\" |n in [64, 1024]| { ... }", "Statistical micro-benchmark with parameter variations",
                    "bench \"${1:name}\" |${2:n in [64, 1024]}| {\n\tblack_box(${0})\n}", 2);
        append_item("cpp", 15, "cpp { ... }", "Direct inline C++23 emission block.",
                    "cpp {\n\t${0}\n}", 2);
    } else {
        append_item("fn", 14, "fn keyword", "Function declaration");
        append_item("type struct", 14, "type ... struct", "Struct declaration");
        append_item("type enum", 14, "type ... enum", "Enum declaration");
        append_item("match", 14, "match keyword", "Pattern matching");
        append_item("for", 14, "for keyword", "Loop statement");
        append_item("if", 14, "if keyword", "Conditional statement");
        append_item("defer", 14, "defer <expr> / defer { ... }", "Schedules statement or block to execute when enclosing scope exits.");
        append_item("test", 14, "test \"name\" { ... }", "Unit test declaration");
        append_item("fuzz", 14, "fuzz \"name\" |...| { ... }", "Property-based fuzz test declaration");
        append_item("bench", 14, "bench \"name\" |...| { ... }", "Statistical micro-benchmark declaration");
        append_item("cpp", 14, "cpp { ... }", "Direct inline C++23 emission block.");
    }
    out += "]";
    return out;
}

} // namespace cfire::lsp
