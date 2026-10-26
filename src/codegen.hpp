#pragma once
// src/codegen.hpp — C++23 Emitter for cfire (.cf)

#include "sema.hpp"
#include <algorithm>
#include <format>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::frontend {

class Emitter {
public:
    explicit Emitter(const CheckedFile& cf, bool test_mode = false)
        : cf_(cf), test_mode_(test_mode), source_path_(cf.ast.path) {
        init_imports();
    }

    [[nodiscard]] auto emit_cpp() -> std::string {
        emit_file();
        return buf_.str();
    }

public:
    const CheckedFile& cf_;
    bool test_mode_ = false;
    bool in_main_ = false;
    bool in_test_ = false;
    bool in_fallible_void_ = false;
    std::string source_path_{};
    int last_source_line_ = -1;
    std::ostringstream buf_;
    int indent_ = 0;
    std::string cur_fn_{};
    int defer_count_ = 0;
    int try_count_ = 0;
    std::unordered_set<std::string> used_core_{};
    std::unordered_map<std::string, std::string> used_vendor_{};
    std::vector<std::string> active_async_scopes_{};
    std::unordered_set<std::string> local_vars_{};
    std::unordered_set<std::string> ptr_vars_{};

    [[nodiscard]] static auto escape_line_path(std::string_view p) -> std::string {
        std::string out;
        for (char c : p) {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else out += c;
        }
        return out;
    }

    void emit_line_directive(int line) {
        if (line > 0 && line != last_source_line_ && !source_path_.empty()) {
            last_source_line_ = line;
            writeln(std::format("#line {} \"{}\"", line, escape_line_path(source_path_)));
        }
    }

    void init_imports() {
        for (const auto& imp : cf_.ast.imports) {
            if (imp.is_cpp) continue;
            if (imp.path == "core:fs") {
                used_core_.insert("fs");
            } else if (imp.path == "core:cli") {
                used_core_.insert("cli");
            } else if (imp.path.starts_with("core:compress/") || imp.path == "core:compress" || imp.path == "core:zstd") {
                used_core_.insert("compress");
            } else if (imp.path == "core:json") {
                used_core_.insert("json");
            } else if (imp.path == "core:db/sqlite" || imp.path == "core:sqlite") {
                used_core_.insert("sqlite");
            } else if (imp.path.starts_with("core:crypto/") || imp.path == "core:crypto") {
                used_core_.insert("crypto");
            } else if (imp.path.starts_with("core:net/") || imp.path == "core:net" || imp.path == "core:http" || imp.path == "core:websocket" || imp.path == "core:tls") {
                used_core_.insert("net");
            } else if (imp.path == "core:time") {
                used_core_.insert("time");
            } else if (imp.path == "core:os") {
                used_core_.insert("os");
            } else if (imp.path.starts_with("core:sync/") || imp.path == "core:sync" || imp.path == "core:context") {
                used_core_.insert("sync");
            } else if (imp.path == "core:str") {
                used_core_.insert("str");
            } else if (imp.path == "core:math" || imp.path == "core:rand") {
                used_core_.insert("math");
            } else if (imp.path.starts_with("core:encoding/") || imp.path == "core:encoding" || imp.path == "core:base64" || imp.path == "core:hex" || imp.path == "core:csv") {
                used_core_.insert("encoding");
            } else if (imp.path == "core:regex") {
                used_core_.insert("regex");
            } else if (imp.path == "core:collections" || imp.path == "core:sort") {
                used_core_.insert("collections");
            } else if (imp.path == "core:log") {
                used_core_.insert("log");
            } else if (imp.path == "core:unicode") {
                used_core_.insert("unicode");
            } else if (imp.path.starts_with("core:")) {
                used_core_.insert(imp.path.substr(5));
            } else if (imp.path == "vendor:raylib") {
                std::string alias = imp.alias.empty() ? "raylib" : imp.alias;
                used_vendor_["raylib"] = alias;
            } else if (imp.path == "vendor:raygui") {
                std::string alias = imp.alias.empty() ? "raygui" : imp.alias;
                used_vendor_["raygui"] = alias;
                if (!used_vendor_.contains("raylib")) {
                    used_vendor_["raylib"] = "raylib";
                }
            }
        }
    }

    void writeln(std::string_view line = "") {
        if (line.empty()) {
            buf_ << "\n";
            return;
        }
        for (int i = 0; i < indent_; ++i) buf_ << "    ";
        buf_ << line << "\n";
    }

    void emit_file() {
        const auto& ast = cf_.ast;
        std::vector<std::string> link_tags;
        std::vector<std::string> defines;
        struct CppHeader {
            std::string path;
            std::vector<std::string> defines;
        };
        std::vector<CppHeader> cpp_headers;

        for (const auto& imp : ast.imports) {
            if (imp.is_cpp) {
                CppHeader header;
                header.path = imp.path;
                for (const auto& attr : imp.attrs) {
                    if (attr.name == "pkg" && !attr.args.empty()) {
                        link_tags.push_back("@pkg:" + attr.args[0]);
                    } else if (attr.name == "define" && !attr.args.empty()) {
                        if (attr.args.size() == 1) {
                            header.defines.push_back(std::format("#define {}", attr.args[0]));
                        } else if (attr.args.size() >= 2) {
                            header.defines.push_back(std::format("#define {} {}", attr.args[0], attr.args[1]));
                        }
                    }
                }
                cpp_headers.push_back(std::move(header));
                continue;
            }
            if (imp.path == "core:fs") {
                used_core_.insert("fs");
            } else if (imp.path == "core:cli") {
                used_core_.insert("cli");
            } else if (imp.path.starts_with("core:compress/") || imp.path == "core:compress" || imp.path == "core:zstd") {
                used_core_.insert("compress");
                defines.push_back("#define CFIRE_USE_ZSTD 1");
                link_tags.push_back("-lzstd");
            } else if (imp.path == "core:json") {
                used_core_.insert("json");
                defines.push_back("#define CFIRE_USE_SIMDJSON 1");
                link_tags.push_back("-lsimdjson");
            } else if (imp.path == "core:db/sqlite" || imp.path == "core:sqlite") {
                used_core_.insert("sqlite");
                defines.push_back("#define CFIRE_USE_SQLITE 1");
                link_tags.push_back("-lsqlite3");
            } else if (imp.path.starts_with("core:crypto/") || imp.path == "core:crypto") {
                used_core_.insert("crypto");
            } else if (imp.path.starts_with("core:net/") || imp.path == "core:net" || imp.path == "core:http" || imp.path == "core:websocket" || imp.path == "core:tls") {
                used_core_.insert("net");
            } else if (imp.path == "core:time") {
                used_core_.insert("time");
            } else if (imp.path == "core:os") {
                used_core_.insert("os");
            } else if (imp.path.starts_with("core:sync/") || imp.path == "core:sync" || imp.path == "core:context") {
                used_core_.insert("sync");
            } else if (imp.path == "core:str") {
                used_core_.insert("str");
            } else if (imp.path == "core:math" || imp.path == "core:rand") {
                used_core_.insert("math");
            } else if (imp.path.starts_with("core:encoding/") || imp.path == "core:encoding" || imp.path == "core:base64" || imp.path == "core:hex" || imp.path == "core:csv") {
                used_core_.insert("encoding");
            } else if (imp.path == "core:regex") {
                used_core_.insert("regex");
            } else if (imp.path == "core:collections" || imp.path == "core:sort") {
                used_core_.insert("collections");
            } else if (imp.path == "core:log") {
                used_core_.insert("log");
            } else if (imp.path == "core:unicode") {
                used_core_.insert("unicode");
            } else if (imp.path.starts_with("core:")) {
                used_core_.insert(imp.path.substr(5));
            } else if (imp.path == "vendor:raylib") {
                std::string alias = imp.alias.empty() ? "raylib" : imp.alias;
                used_vendor_["raylib"] = alias;
                link_tags.push_back("-lraylib");
            } else if (imp.path == "vendor:raygui") {
                std::string alias = imp.alias.empty() ? "raygui" : imp.alias;
                used_vendor_["raygui"] = alias;
                if (!used_vendor_.contains("raylib")) {
                    used_vendor_["raylib"] = "raylib";
                }
                if (std::find(link_tags.begin(), link_tags.end(), "-lraylib") == link_tags.end()) {
                    link_tags.push_back("-lraylib");
                }
            }
        }

        bool any_json_tag = false;
        for (const auto& [sname, tags] : cf_.struct_tags) {
            for (const auto& t : tags) {
                if (!t.json_key.empty()) { any_json_tag = true; break; }
            }
            if (any_json_tag) break;
        }
        if (any_json_tag && !used_core_.contains("json")) {
            used_core_.insert("json");
            defines.push_back("#define CFIRE_USE_SIMDJSON 1");
            link_tags.push_back("-lsimdjson");
        }

        if (!link_tags.empty()) {
            std::string joined;
            for (std::size_t i = 0; i < link_tags.size(); ++i) {
                if (i > 0) joined += " ";
                joined += link_tags[i];
            }
            writeln(std::format("// @cfire-link: {}", joined));
        }
        for (const auto& d : defines) writeln(d);
        writeln("#include \"cfire_prelude.hpp\"");
        if (!used_core_.empty()) {
            std::set<std::string> core_headers;
            for (const auto& mod : used_core_) {
                if (mod == "cli") core_headers.insert("core/cli.hpp");
                else if (mod == "collections" || mod == "sort") core_headers.insert("core/collections.hpp");
                else if (mod == "zstd" || mod == "compress") core_headers.insert("core/compress.hpp");
                else if (mod == "crypto") core_headers.insert("core/crypto.hpp");
                else if (mod == "encoding" || mod == "base64" || mod == "hex" || mod == "csv") core_headers.insert("core/encoding.hpp");
                else if (mod == "fs") core_headers.insert("core/fs.hpp");
                else if (mod == "json") core_headers.insert("core/json.hpp");
                else if (mod == "log") core_headers.insert("core/log.hpp");
                else if (mod == "math" || mod == "rand") core_headers.insert("core/math.hpp");
                else if (mod == "net" || mod == "http" || mod == "websocket" || mod == "url" || mod == "tls") core_headers.insert("core/net.hpp");
                else if (mod == "os") core_headers.insert("core/os.hpp");
                else if (mod == "regex") core_headers.insert("core/regex.hpp");
                else if (mod == "sqlite") core_headers.insert("core/sqlite.hpp");
                else if (mod == "str") core_headers.insert("core/str.hpp");
                else if (mod == "sync" || mod == "context") core_headers.insert("core/sync.hpp");
                else if (mod == "time") core_headers.insert("core/time.hpp");
                else if (mod == "unicode") core_headers.insert("core/unicode.hpp");
                else core_headers.insert(std::format("core/{}.hpp", mod));
            }
            for (const auto& hdr : core_headers) {
                writeln(std::format("#include \"{}\"", hdr));
            }
        }
        for (const auto& h : cpp_headers) {
            for (const auto& def : h.defines) {
                writeln(def);
            }
            std::string clean = h.path;
            while (!clean.empty() && (clean.front() == '<' || clean.front() == '"')) clean.erase(clean.begin());
            while (!clean.empty() && (clean.back() == '>' || clean.back() == '"')) clean.pop_back();
            writeln(std::format("#include <{}>", clean));
        }
        if (used_vendor_.contains("raylib") || used_vendor_.contains("raygui")) {
            writeln("#if defined(PLATFORM_IOS) || defined(CFIRE_RAYLIB_FALLBACK)");
            writeln("#pragma clang diagnostic push");
            writeln("#pragma clang diagnostic ignored \"-Wdeprecated-declarations\"");
            writeln("#include \"core/ios_raylib.hpp\"");
            writeln("#pragma clang diagnostic pop");
            writeln("#else");
            writeln("#include <raylib.h>");
            writeln("#if __has_include(<raymath.h>)");
            writeln("#include <raymath.h>");
            writeln("#endif");
            writeln("#if __has_include(<rlgl.h>)");
            writeln("#include <rlgl.h>");
            writeln("#endif");
            writeln("#endif");
        }
        if (used_vendor_.contains("raygui")) {
            writeln("#pragma clang diagnostic push");
            writeln("#pragma clang diagnostic ignored \"-Weverything\"");
            writeln("#define RAYGUI_IMPLEMENTATION");
            writeln("#if __has_include(<raygui.h>)");
            writeln("#include <raygui.h>");
            writeln("#else");
            writeln("#include \"vendor/raygui.h\"");
            writeln("#endif");
            writeln("#pragma clang diagnostic pop");
        }
        writeln();

        // 1. Emit Interfaces as C++20 `concept`s
        for (const auto& iface : ast.interfaces) {
            emit_interface(iface);
            writeln();
        }

        // Forward declare all structs so recursive shared_ptr/pointers work
        for (const auto& st : ast.structs) {
            writeln(std::format("struct {};", st.name));
        }
        if (!ast.structs.empty()) writeln();

        // Emit Structs & Enums in source declaration order
        struct TypeItem {
            int line;
            bool is_enum;
            std::size_t idx;
        };
        std::vector<TypeItem> items;
        for (std::size_t i = 0; i < ast.enums.size(); ++i) {
            items.push_back(TypeItem{ast.enums[i].line, true, i});
        }
        for (std::size_t i = 0; i < ast.structs.size(); ++i) {
            items.push_back(TypeItem{ast.structs[i].line, false, i});
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            return a.line < b.line;
        });

        for (const auto& it : items) {
            if (it.is_enum) {
                emit_enum(ast.enums[it.idx]);
            } else {
                emit_struct(ast.structs[it.idx]);
            }
            writeln();
        }

        // 4. Forward Declarations for Functions & Receiver Methods (before global vars so globals can reference functions)
        for (const auto& fn : ast.functions) {
            emit_function_forward_decl(fn);
        }
        if (!ast.functions.empty()) writeln();

        // 5a. Emit Global / Package-level Variables
        for (const auto& gv : ast.global_vars) {
            std::string type_str = gv.type ? lower_owning_type(*gv.type) : "auto";
            std::string mut_str = gv.is_mut ? "" : "const ";
            if (gv.value) {
                writeln(std::format("inline {}{} {} = {};", mut_str, type_str, gv.name, emit_expr(gv.value)));
            } else {
                writeln(std::format("inline {}{} {}{{}};", mut_str, type_str, gv.name));
            }
        }
        if (!ast.global_vars.empty()) writeln();

        // 5b. Emit Functions & Receiver Methods
        bool has_user_main = false;
        for (std::size_t i = 0; i < ast.functions.size(); ++i) {
            if (ast.functions[i].name == "main" && !ast.functions[i].receiver) {
                has_user_main = true;
                if (test_mode_) continue; // Omit user main when compiling in `cfire test` mode
            }
            emit_function(ast.functions[i]);
            if (i + 1 < ast.functions.size() || !ast.tests.empty()) writeln();
        }

        // 5. Emit `test "..." { ... }` Blocks & Test Harness Runner
        for (std::size_t i = 0; i < ast.tests.size(); ++i) {
            emit_test_decl(ast.tests[i], i);
            writeln();
        }
        if ((test_mode_ || !has_user_main) && !ast.tests.empty()) {
            writeln("auto main(int argc, char** argv) -> int {");
            ++indent_;
            writeln("bool json_mode = false;");
            writeln("std::string filter;");
            writeln("for (int i = 1; i < argc; ++i) {");
            writeln("    std::string_view a = argv[i];");
            writeln("    if (a == \"--cfire-test-json\") json_mode = true;");
            writeln("    else if (a.starts_with(\"--cfire-test-filter=\")) filter = a.substr(20);");
            writeln("}");
            writeln("cfire::testing_detail::in_test_harness = true;");
            writeln("int passed_count = 0;");
            writeln("int failed_count = 0;");
            writeln("struct TestResultRecord { std::string name; bool passed; double duration_ms; std::string file; int line; std::string error; };");
            writeln("std::vector<TestResultRecord> records;");
            for (std::size_t i = 0; i < ast.tests.size(); ++i) {
                std::string t_name = ast.tests[i].name;
                writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
                ++indent_;
                writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", t_name));
                writeln("cfire::testing_detail::current_test_failed = false;");
                writeln("cfire::testing_detail::failure_file.clear();");
                writeln("cfire::testing_detail::failure_line = 0;");
                writeln("cfire::testing_detail::failure_expr.clear();");
                writeln("cfire::testing_detail::failure_msg.clear();");
                writeln("auto start_t = std::chrono::steady_clock::now();");
                writeln("if (setjmp(cfire::testing_detail::test_env) == 0) {");
                writeln(std::format("    __cfire_test_{}();", i));
                writeln("}");
                writeln("auto end_t = std::chrono::steady_clock::now();");
                writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
                writeln("bool test_pass = !cfire::testing_detail::current_test_failed;");
                writeln("if (test_pass) {");
                writeln("    passed_count++;");
                writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", t_name));
                writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
                writeln("} else {");
                writeln("    failed_count++;");
                    writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, (cfire::testing_detail::failure_expr.empty() ? cfire::testing_detail::failure_msg : cfire::testing_detail::failure_expr));");
                writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, (cfire::testing_detail::failure_expr.empty() ? cfire::testing_detail::failure_msg : cfire::testing_detail::failure_expr)}});", t_name));
                writeln("}");
                --indent_;
                writeln("}");
            }
            writeln("if (json_mode) {");
            writeln("    auto escape_json = [](std::string_view s) -> std::string {");
            writeln("        std::string out;");
            writeln("        for (char c : s) {");
            writeln("            if (c == '\"') out += \"\\\\\\\"\";");
            writeln("            else if (c == '\\\\') out += \"\\\\\\\\\";");
            writeln("            else if (c == '\\n') out += \"\\\\n\";");
            writeln("            else out += c;");
            writeln("        }");
            writeln("        return out;");
            writeln("    };");
            writeln("    std::print(\"{{\\\"success\\\": {}, \\\"passed\\\": {}, \\\"failed\\\": {}, \\\"tests\\\": [\", (failed_count == 0 ? \"true\" : \"false\"), passed_count, failed_count);");
            writeln("    for (std::size_t r = 0; r < records.size(); ++r) {");
            writeln("        if (r > 0) std::print(\", \");");
            writeln("        const auto& rec = records[r];");
            writeln("        std::print(\"{{\\\"name\\\": \\\"{}\\\", \\\"status\\\": \\\"{}\\\", \\\"duration_ms\\\": {:.2f}, \\\"file\\\": \\\"{}\\\", \\\"line\\\": {}, \\\"error\\\": \\\"{}\\\"}}\", escape_json(rec.name), (rec.passed ? \"pass\" : \"fail\"), rec.duration_ms, escape_json(rec.file), rec.line, escape_json(rec.error));");
            writeln("    }");
            writeln("    std::println(\"]}}\");");
            writeln("} else {");
            writeln("    if (failed_count == 0) {");
            writeln("        std::println(\"PASS ({} cfire tests succeeded)\", passed_count);");
            writeln("    } else {");
            writeln("        std::println(\"FAIL ({} of {} cfire tests failed)\", failed_count, (passed_count + failed_count));");
            writeln("    }");
            writeln("}");
            writeln("return failed_count > 0 ? 1 : 0;");
            --indent_;
            writeln("}");
        }
    }

    void emit_test_decl(const TestDecl& td, std::size_t idx) {
        if (td.line > 0) emit_line_directive(td.line);
        cur_fn_ = std::format("__cfire_test_{}", idx);
        defer_count_ = 0;
        in_test_ = true;
        in_fallible_void_ = false;
        local_vars_.clear();
        writeln(std::format("inline void {}() {{", cur_fn_));
        ++indent_;
        for (const auto& st : td.body) emit_stmt(st, false);
        --indent_;
        writeln("}");
        in_test_ = false;
        local_vars_.clear();
    }

    void emit_interface(const InterfaceDecl& iface) {
        writeln(std::format("// Interface '{}' -> C++20 concept", iface.name));
        writeln(std::format("template <typename T>"));
        writeln(std::format("concept {} = true;", iface.name));
    }

    void emit_enum(const EnumDecl& en) {
        writeln(std::format("struct {} {{", en.name));
        ++indent_;
        std::vector<std::string> var_types;
        for (const auto& v : en.variants) {
            writeln(std::format("struct _{} {{", v.name));
            ++indent_;
            for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                writeln(std::format("{} _{}{{}};", lower_owning_type(v.payload_types[i]), i));
            }
            --indent_;
            writeln("};");
            var_types.push_back("_" + v.name);
        }

        std::string vlist;
        for (std::size_t i = 0; i < var_types.size(); ++i) {
            if (i > 0) vlist += ", ";
            vlist += var_types[i];
        }
        writeln(std::format("std::variant<{}> _v;", vlist));

        // Emit static variant constructors (`Shape::Circle(r)`, `Shape::Empty()`)
        for (const auto& v : en.variants) {
            std::string params, args;
            for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                if (i > 0) {
                    params += ", ";
                    args += ", ";
                }
                params += std::format("{} a{}", lower_owning_type(v.payload_types[i]), i);
                args += std::format("std::move(a{})", i);
            }
            writeln(std::format("[[nodiscard]] static auto {}({}) -> {} {{ return {}{{_{}{{{}}}}}; }}",
                v.name, params, en.name, en.name, v.name, args));
        }
        --indent_;
        writeln("};");
    }

    void emit_struct(const StructDecl& st) {
        writeln(std::format("struct {} {{", st.name));
        ++indent_;
        for (const auto& fld : st.fields) {
            std::string cpp_type = lower_owning_type(fld.type);
            if (fld.default_val) {
                writeln(std::format("{} {} = {};", cpp_type, fld.name, emit_expr(fld.default_val)));
            } else {
                writeln(std::format("{} {}{{}};", cpp_type, fld.name));
            }
        }
        --indent_;
        writeln("};");

        writeln();
        writeln("template <>");
        writeln(std::format("struct cfire::reflect_struct<{}> {{", st.name));
        ++indent_;
        writeln(std::format("static constexpr std::string_view name = \"{}\";", st.name));
        writeln(std::format("static constexpr std::size_t field_count = {};", st.fields.size()));
        writeln("static constexpr std::array<std::string_view, " + std::to_string(st.fields.size()) + "> field_names = {");
        ++indent_;
        for (const auto& fld : st.fields) {
            writeln(std::format("\"{}\",", fld.name));
        }
        --indent_;
        writeln("};");
        writeln("static constexpr std::array<std::string_view, " + std::to_string(st.fields.size()) + "> field_tags = {");
        ++indent_;
        for (const auto& fld : st.fields) {
            writeln(std::format("R\"tag({})tag\",", fld.raw_tag));
        }
        --indent_;
        writeln("};");
        --indent_;
        writeln("};");

        auto it = cf_.struct_tags.find(st.name);
        if (it == cf_.struct_tags.end()) return;
        const auto& tags = it->second;

        bool has_cli = false, has_json = false, has_db = false;
        for (const auto& t : tags) {
            if (!t.cli_long.empty()) has_cli = true;
            if (!t.json_key.empty()) has_json = true;
            if (!t.db_column.empty()) has_db = true;
        }

        if (has_cli && used_core_.contains("cli")) {
            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::cli::parse<{}>(int argc, char** argv) -> {} {{", st.name, st.name));
            ++indent_;
            writeln(std::format("{} out{{}};", st.name));
            writeln("cfire::cli::FlagReader reader(argc, argv);");
            for (const auto& t : tags) {
                if (!t.cli_long.empty()) {
                    writeln(std::format("reader.bind(\"{}\", \"{}\", out.{});", t.cli_long, t.cli_short, t.field_name));
                }
            }
            writeln("return out;");
            --indent_;
            writeln("}");
        }

        if (has_json && used_core_.contains("json")) {
            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::json::parse_struct<{}>(simdjson::ondemand::object obj) -> std::expected<{}, cfire::Error> {{", st.name, st.name));
            ++indent_;
            writeln(std::format("{} out{{}};", st.name));
            for (const auto& t : tags) {
                if (!t.json_key.empty()) {
                    std::string getter = "get_str";
                    if (t.field_type.name == "u64" || t.field_type.name == "usize") getter = "get_u64";
                    else if (t.field_type.name == "i64" || t.field_type.name == "isize") getter = "get_i64";
                    else if (t.field_type.name == "i32") getter = "get_i32";
                    else if (t.field_type.name == "f64" || t.field_type.name == "f32") getter = "get_f64";
                    else if (t.field_type.name == "bool") getter = "get_bool";
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::{}(obj, \"{}\"));", t.field_name, getter, t.json_key));
                }
            }
            writeln("return out;");
            --indent_;
            writeln("}");

            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::json::stringify_struct<{}>(const {}& val) -> std::string {{", st.name, st.name));
            ++indent_;
            writeln("std::string out = \"{\";");
            bool first_j = true;
            for (const auto& t : tags) {
                if (!t.json_key.empty()) {
                    std::string prefix = first_j ? "" : ",";
                    first_j = false;
                    if (t.field_type.name == "str") {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":\\\"{{}}\\\"\", cfire::json::escape_string(val.{}));", prefix, t.json_key, t.field_name));
                    } else if (t.field_type.name == "bool") {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{} ? \"true\" : \"false\");", prefix, t.json_key, t.field_name));
                    } else {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{});", prefix, t.json_key, t.field_name));
                    }
                }
            }
            writeln("out += \"}\";");
            writeln("return out;");
            --indent_;
            writeln("}");
        }

        if (has_db && used_core_.contains("sqlite")) {
            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::sqlite::map_row<{}>(sqlite3_stmt* stmt) -> {} {{", st.name, st.name));
            ++indent_;
            writeln(std::format("return {}{{", st.name));
            ++indent_;
            int col_idx = 0;
            for (const auto& t : tags) {
                if (!t.db_column.empty()) {
                    std::string col_fn = "col_str";
                    if (t.field_type.name == "u64" || t.field_type.name == "usize") col_fn = "col_u64";
                    else if (t.field_type.name == "i64" || t.field_type.name == "isize") col_fn = "col_i64";
                    else if (t.field_type.name == "i32") col_fn = "col_i32";
                    else if (t.field_type.name == "f64" || t.field_type.name == "f32") col_fn = "col_f64";
                    else if (t.field_type.name == "bool") col_fn = "col_bool";
                    writeln(std::format(".{} = cfire::sqlite::{}(stmt, {}),", t.field_name, col_fn, col_idx++));
                }
            }
            --indent_;
            writeln("};");
            --indent_;
            writeln("}");
        }
    }

    void emit_function_forward_decl(const FnDecl& fn) {
        if (fn.name == "main" && !fn.receiver) return;
        std::string cur_fn = fn.receiver ? (fn.receiver->type.name + "__" + fn.name) : fn.name;

        if (!fn.type_params.empty()) {
            std::string tparams;
            for (std::size_t i = 0; i < fn.type_params.size(); ++i) {
                if (i > 0) tparams += ", ";
                const auto& tp = fn.type_params[i];
                if (!tp.constraint.empty()) {
                    tparams += std::format("{} {}", tp.constraint, tp.name);
                } else {
                    tparams += std::format("typename {}", tp.name);
                }
            }
            writeln(std::format("template <{}>", tparams));
        }

        std::string param_list;
        if (fn.receiver) {
            param_list += std::format("{} {}", lower_param_type(fn.receiver->intent, fn.receiver->type), fn.receiver->name);
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (!param_list.empty()) param_list += ", ";
            param_list += std::format("{} {}", lower_param_type(fn.params[i].intent, fn.params[i].type), fn.params[i].name);
        }
        std::string ret_str = lower_return_type(fn.return_type);
        std::string prefix = (ret_str == "void") ? "" : "[[nodiscard]] ";
        if (fn.receiver) {
            writeln(std::format("{}inline auto cfire_method_{}({}) -> {};", prefix, fn.name, param_list, ret_str));
            writeln(std::format("{}inline auto {}({}) -> {};", prefix, cur_fn, param_list, ret_str));
        } else {
            writeln(std::format("{}inline auto {}({}) -> {};", prefix, cur_fn, param_list, ret_str));
        }
    }

    void emit_function(const FnDecl& fn) {
        if (fn.line > 0) emit_line_directive(fn.line);
        cur_fn_ = fn.receiver ? (fn.receiver->type.name + "__" + fn.name) : fn.name;
        defer_count_ = 0;
        local_vars_.clear();
        if (fn.receiver) local_vars_.insert(fn.receiver->name);
        for (const auto& p : fn.params) local_vars_.insert(p.name);

        if (fn.name == "main" && !fn.receiver) {
            in_main_ = true;
            in_fallible_void_ = false;
            local_vars_.insert("argc");
            local_vars_.insert("argv");
            bool is_gui = used_vendor_.contains("raylib") || used_vendor_.contains("raygui");
            if (is_gui) {
                writeln("#if defined(PLATFORM_IOS)");
                writeln("auto __cfire_ios_user_main(int argc, char** argv) -> int {");
                writeln("#else");
                writeln("auto main(int argc, char** argv) -> int {");
                writeln("#endif");
                ++indent_;
                writeln("(void)argc; (void)argv;");
                for (const auto& st : fn.body) emit_stmt(st, true);
                writeln("return 0;");
                --indent_;
                writeln("}");
                writeln("#if defined(PLATFORM_IOS)");
                writeln("auto main(int argc, char** argv) -> int { return cfire::ios_rl::run_ios_app(argc, argv, &__cfire_ios_user_main); }");
                writeln("#endif");
            } else {
                writeln("auto main(int argc, char** argv) -> int {");
                ++indent_;
                for (const auto& st : fn.body) emit_stmt(st, true);
                writeln("return 0;");
                --indent_;
                writeln("}");
            }
            in_main_ = false;
            local_vars_.clear();
            return;
        }
        in_main_ = false;
        in_fallible_void_ = (fn.return_type.name == "void" && fn.return_type.is_fallible);

        if (!fn.type_params.empty()) {
            std::string tparams;
            for (std::size_t i = 0; i < fn.type_params.size(); ++i) {
                if (i > 0) tparams += ", ";
                const auto& tp = fn.type_params[i];
                if (!tp.constraint.empty()) {
                    tparams += std::format("{} {}", tp.constraint, tp.name);
                } else {
                    tparams += std::format("typename {}", tp.name);
                }
            }
            writeln(std::format("template <{}>", tparams));
        }

        std::string param_list;
        std::string pass_args;
        if (fn.receiver) {
            param_list += std::format("{} {}", lower_param_type(fn.receiver->intent, fn.receiver->type), fn.receiver->name);
            pass_args += fn.receiver->name;
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (!param_list.empty()) param_list += ", ";
            param_list += std::format("{} {}", lower_param_type(fn.params[i].intent, fn.params[i].type), fn.params[i].name);
            if (!pass_args.empty()) pass_args += ", ";
            pass_args += (fn.params[i].intent == "move") ? std::format("std::move({})", fn.params[i].name) : fn.params[i].name;
        }
        std::string ret_str = lower_return_type(fn.return_type);
        std::string prefix = (ret_str == "void") ? "" : "[[nodiscard]] ";
        std::string method_fn = fn.receiver ? std::format("cfire_method_{}", fn.name) : cur_fn_;
        writeln(std::format("{}inline auto {}({}) -> {} {{", prefix, method_fn, param_list, ret_str));
        ++indent_;
        for (const auto& st : fn.body) emit_stmt(st, false);
        if (fn.return_type.name == "void" && fn.return_type.is_fallible) {
            bool has_ret = !fn.body.empty() && std::holds_alternative<ReturnStmt>(fn.body.back()->node);
            if (!has_ret) writeln("return {};");
        }
        --indent_;
        writeln("}");
        in_fallible_void_ = false;
        local_vars_.clear();

        if (fn.receiver) {
            if (!fn.type_params.empty()) {
                std::string tparams;
                std::string targs;
                for (std::size_t i = 0; i < fn.type_params.size(); ++i) {
                    if (i > 0) {
                        tparams += ", ";
                        targs += ", ";
                    }
                    const auto& tp = fn.type_params[i];
                    if (!tp.constraint.empty()) {
                        tparams += std::format("{} {}", tp.constraint, tp.name);
                    } else {
                        tparams += std::format("typename {}", tp.name);
                    }
                    targs += tp.name;
                }
                writeln(std::format("template <{}>", tparams));
                writeln(std::format("{}inline auto {}({}) -> {} {{", prefix, cur_fn_, param_list, ret_str));
                ++indent_;
                if (ret_str == "void") {
                    writeln(std::format("cfire_method_{}<{}>({});", fn.name, targs, pass_args));
                } else {
                    writeln(std::format("return cfire_method_{}<{}>({});", fn.name, targs, pass_args));
                }
                --indent_;
                writeln("}");
            } else {
                writeln(std::format("{}inline auto {}({}) -> {} {{", prefix, cur_fn_, param_list, ret_str));
                ++indent_;
                if (ret_str == "void") {
                    writeln(std::format("cfire_method_{}({});", fn.name, pass_args));
                } else {
                    writeln(std::format("return cfire_method_{}({});", fn.name, pass_args));
                }
                --indent_;
                writeln("}");
            }
        }
    }

    void emit_stmt(const StmtPtr& s, bool in_main) {
        if (!s) return;
        if (s->line > 0) emit_line_directive(s->line);
        std::visit([&](const auto& st) {
            using T = std::decay_t<decltype(st)>;
            if constexpr (std::is_same_v<T, VarDeclStmt>) {
                local_vars_.insert(st.name);
                if (st.init) {
                    if (const auto* try_ex = std::get_if<TryExpr>(&st.init->node)) {
                        if (in_main) {
                            int id = ++try_count_;
                            std::string tmp = std::format("_try_{}", id);
                            writeln(std::format("auto {} = {};", tmp, emit_expr(try_ex->inner)));
                            writeln(std::format("if (!{}.has_value()) {{", tmp));
                            ++indent_;
                            writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", tmp));
                            writeln("return 1;");
                            --indent_;
                            writeln("}");
                            writeln(std::format("auto {} = std::move(*{});", st.name, tmp));
                            return;
                        }
                        if (in_test_) {
                            int id = ++try_count_;
                            std::string tmp = std::format("_try_{}", id);
                            writeln(std::format("auto {} = {};", tmp, emit_expr(try_ex->inner)));
                            writeln(std::format("if (!{}.has_value()) {{", tmp));
                            ++indent_;
                            writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"try\");",
                                tmp, escape_line_path(cf_.ast.path), s->line));
                            writeln("return;");
                            --indent_;
                            writeln("}");
                            writeln(std::format("auto {} = std::move(*{});", st.name, tmp));
                            return;
                        }
                        std::string cpp_type = st.type ? lower_return_type(*st.type) : "auto";
                        writeln(std::format("CFIRE_TRY_ASSIGN({} {}, {});", cpp_type, st.name, emit_expr(try_ex->inner)));
                        return;
                    }
                    if (const auto* or_ex = std::get_if<OrExpr>(&st.init->node); or_ex && !or_ex->handler_body.empty()) {
                        int id = ++try_count_;
                        std::string tmp = std::format("_res_{}", id);
                        writeln(std::format("auto {} = {};", tmp, emit_expr(or_ex->inner)));
                        writeln(std::format("if (!{}.has_value()) {{", tmp));
                        ++indent_;
                        local_vars_.insert(or_ex->err_var);
                        writeln(std::format("const cfire::Error& {} = {}.error();", or_ex->err_var, tmp));
                        for (const auto& hs : or_ex->handler_body) emit_stmt(hs, in_main);
                        --indent_;
                        writeln("}");
                        writeln(std::format("auto {} = std::move(*{});", st.name, tmp));
                        return;
                    }
                    if (const auto* call_ex = std::get_if<CallExpr>(&st.init->node)) {
                        if (const auto* id = std::get_if<IdentExpr>(&call_ex->callee->node);
                            id && id->name == "make" && call_ex->type_args.size() == 1 &&
                            call_ex->type_args[0].smart_wrap.empty()) {
                            if (call_ex->args.empty()) {
                                writeln(std::format("{} {}{{}};", lower_owning_type(call_ex->type_args[0]), st.name));
                            } else {
                                writeln(std::format("{} {}({});",
                                    lower_owning_type(call_ex->type_args[0]), st.name, emit_expr(call_ex->args[0].value)));
                            }
                            return;
                        }
                    }
                }
                if (st.type && (st.type->is_ptr || st.type->is_mut_ptr)) {
                    ptr_vars_.insert(st.name);
                }
                std::string cpp_type = st.type ? lower_return_type(*st.type) : "auto";
                if (!st.type && st.init) {
                    if (const auto* lit = std::get_if<LiteralExpr>(&st.init->node); lit && lit->kind == TokenKind::String) {
                        cpp_type = "std::string";
                    }
                }
                if (st.init) {
                    std::string init_val = emit_expr(st.init);
                    if (st.type && (st.type->is_ptr || st.type->is_mut_ptr) && init_val == "std::nullopt") {
                        init_val = "nullptr";
                    }
                    writeln(std::format("{} {} = {};", cpp_type, st.name, init_val));
                } else {
                    writeln(std::format("{} {}{{}};", cpp_type, st.name));
                }
            } else if constexpr (std::is_same_v<T, AssignStmt>) {
                std::string lhs = emit_expr(st.target);
                if (lhs == "_") {
                    if (const auto* try_ex = std::get_if<TryExpr>(&st.value->node)) {
                        writeln(std::format("CFIRE_TRY({});", emit_expr(try_ex->inner)));
                        return;
                    }
                    writeln(std::format("(void){};", emit_expr(st.value)));
                    return;
                }
                std::string rhs = emit_expr(st.value);
                if (rhs == "std::nullopt") {
                    if (const auto* id = std::get_if<IdentExpr>(&st.target->node); id && ptr_vars_.contains(id->name)) {
                        rhs = "nullptr";
                    }
                }
                writeln(std::format("{} {} {};", lhs, st.op, rhs));
            } else if constexpr (std::is_same_v<T, DeferStmt>) {
                if (!active_async_scopes_.empty()) {
                    writeln(std::format("{}.defer([&] {{ {}; }});", active_async_scopes_.back(), emit_expr(st.call)));
                } else {
                    ++defer_count_;
                    writeln(std::format("auto _defer_{} = cfire::scope_exit([&] {{ {}; }});", defer_count_, emit_expr(st.call)));
                }
            } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                if (!st.value) {
                    writeln(in_main ? "return 0;" : (in_fallible_void_ ? "return {};" : "return;"));
                } else {
                    writeln(std::format("return {};", emit_expr(st.value)));
                }
            } else if constexpr (std::is_same_v<T, IfStmt>) {
                std::string cond = emit_expr(st.cond);
                if (cond.starts_with('(') && cond.ends_with(')')) {
                    writeln(std::format("if {} {{", cond));
                } else {
                    writeln(std::format("if ({}) {{", cond));
                }
                ++indent_;
                for (const auto& b : st.then_body) emit_stmt(b, in_main);
                --indent_;
                if (!st.else_body.empty()) {
                    writeln("} else {");
                    ++indent_;
                    for (const auto& b : st.else_body) emit_stmt(b, in_main);
                    --indent_;
                }
                writeln("}");
            } else if constexpr (std::is_same_v<T, ForStmt>) {
                if (!st.item_var.empty()) {
                    local_vars_.insert(st.item_var);
                    if (!st.index_var.empty()) {
                        local_vars_.insert(st.index_var);
                        std::string col = emit_expr(st.iter_expr);
                        writeln(std::format("for (std::size_t {0} = 0; {0} < {1}.size(); ++{0}) {{", st.index_var, col));
                        ++indent_;
                        writeln(std::format("const auto& {} = {}[{}];", st.item_var, col, st.index_var));
                        for (const auto& b : st.body) emit_stmt(b, in_main);
                        --indent_;
                        writeln("}");
                    } else {
                        writeln(std::format("for (const auto& {} : {}) {{", st.item_var, emit_expr(st.iter_expr)));
                        ++indent_;
                        for (const auto& b : st.body) emit_stmt(b, in_main);
                        --indent_;
                        writeln("}");
                    }
                } else {
                    std::string cond = emit_expr(st.iter_expr);
                    if (cond.starts_with('(') && cond.ends_with(')')) {
                        writeln(std::format("while {} {{", cond));
                    } else {
                        writeln(std::format("while ({}) {{", cond));
                    }
                    ++indent_;
                    for (const auto& b : st.body) emit_stmt(b, in_main);
                    --indent_;
                    writeln("}");
                }
            } else if constexpr (std::is_same_v<T, MatchStmt>) {
                emit_match_stmt(st, in_main);
            } else if constexpr (std::is_same_v<T, AsyncScopeStmt>) {
                writeln("{");
                ++indent_;
                local_vars_.insert(st.scope_var);
                writeln(std::format("cfire::async::Scope {}({});", st.scope_var, emit_expr(st.workers)));
                active_async_scopes_.push_back(st.scope_var);
                for (const auto& b : st.body) emit_stmt(b, in_main);
                active_async_scopes_.pop_back();
                writeln(std::format("{}.join_all();", st.scope_var));
                --indent_;
                writeln("}");
            } else if constexpr (std::is_same_v<T, CppBlockStmt>) {
                writeln("{");
                ++indent_;
                std::istringstream iss(trim_str(st.raw_code));
                std::string line;
                while (std::getline(iss, line)) {
                    writeln(trim_str(line));
                }
                --indent_;
                writeln("}");
            } else if constexpr (std::is_same_v<T, ExprStmt>) {
                if (const auto* id = std::get_if<IdentExpr>(&st.expr->node)) {
                    if (id->name == "break") {
                        writeln("break;");
                        return;
                    }
                    if (id->name == "continue") {
                        writeln("continue;");
                        return;
                    }
                }
                if (const auto* try_ex = std::get_if<TryExpr>(&st.expr->node)) {
                    if (in_main) {
                        int id = ++try_count_;
                        std::string tmp = std::format("_try_v_{}", id);
                        writeln(std::format("auto {} = {};", tmp, emit_expr(try_ex->inner)));
                        writeln(std::format("if (!{}.has_value()) {{", tmp));
                        ++indent_;
                        writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", tmp));
                        writeln("return 1;");
                        --indent_;
                        writeln("}");
                        return;
                    }
                    if (in_test_) {
                        int id = ++try_count_;
                        std::string tmp = std::format("_try_v_{}", id);
                        writeln(std::format("auto {} = {};", tmp, emit_expr(try_ex->inner)));
                        writeln(std::format("if (!{}.has_value()) {{", tmp));
                        ++indent_;
                        writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"try\");",
                            tmp, escape_line_path(cf_.ast.path), s->line));
                        writeln("return;");
                        --indent_;
                        writeln("}");
                        return;
                    }
                    writeln(std::format("CFIRE_TRY({});", emit_expr(try_ex->inner)));
                    return;
                }
                if (const auto* or_ex = std::get_if<OrExpr>(&st.expr->node); or_ex && !or_ex->handler_body.empty()) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_res_v_{}", id);
                    writeln(std::format("auto {} = {};", tmp, emit_expr(or_ex->inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    local_vars_.insert(or_ex->err_var);
                    writeln(std::format("const cfire::Error& {} = {}.error();", or_ex->err_var, tmp));
                    for (const auto& hs : or_ex->handler_body) emit_stmt(hs, in_main);
                    --indent_;
                    writeln("}");
                    return;
                }
                writeln(std::format("(void)({});", emit_expr(st.expr)));
            } else if constexpr (std::is_same_v<T, DestructureVarDeclStmt>) {
                std::string names;
                for (std::size_t i = 0; i < st.bindings.size(); ++i) {
                    if (i > 0) names += ", ";
                    names += st.bindings[i].first;
                    local_vars_.insert(st.bindings[i].first);
                }
                writeln(std::format("auto [{}] = {};", names, emit_expr(st.init)));
            } else if constexpr (std::is_same_v<T, DestructureAssignStmt>) {
                std::string targs;
                for (std::size_t i = 0; i < st.targets.size(); ++i) {
                    if (i > 0) targs += ", ";
                    targs += emit_expr(st.targets[i]);
                }
                writeln(std::format("std::tie({}) = {};", targs, emit_expr(st.value)));
            }
        }, s->node);
    }

    void emit_match_stmt(const MatchStmt& ms, bool in_main) {
        bool is_slice_match = false;
        bool is_primitive_match = false;
        for (const auto& a : ms.arms) {
            if (a.is_slice_pattern) {
                is_slice_match = true;
            }
            if (!a.literal_val.empty()) {
                is_primitive_match = true;
            }
        }

        if (is_slice_match) {
            std::string target_expr = emit_expr(ms.target);
            writeln("{");
            ++indent_;
            writeln(std::format("const auto& _target = {};", target_expr));
            writeln("bool _matched = false;");

            for (const auto& arm : ms.arms) {
                if (arm.is_slice_pattern) {
                    std::size_t head_count = arm.slice_head.size();
                    std::size_t tail_count = arm.slice_tail.size();
                    std::size_t min_size = head_count + tail_count;

                    std::string cond;
                    if (!arm.slice_has_rest) {
                        cond = std::format("_target.size() == {}", min_size);
                    } else {
                        if (min_size == 0) {
                            cond = "true";
                        } else {
                            cond = std::format("_target.size() >= {}", min_size);
                        }
                    }

                    writeln(std::format("if (!_matched && {}) {{", cond));
                    ++indent_;
                    writeln("_matched = true;");

                    // Bind head elements
                    for (std::size_t i = 0; i < head_count; ++i) {
                        if (arm.slice_head[i] != "_") {
                            local_vars_.insert(arm.slice_head[i]);
                            writeln(std::format("const auto& {} = _target[{}];", arm.slice_head[i], i));
                        }
                    }

                    // Bind rest slice
                    if (arm.slice_has_rest && !arm.slice_rest.empty() && arm.slice_rest != "_") {
                        local_vars_.insert(arm.slice_rest);
                        std::string rest_start = std::to_string(head_count);
                        std::string rest_end = tail_count == 0 ? "_target.size()" : std::format("(_target.size() - {})", tail_count);
                        writeln(std::format("auto {} = cfire::slice(_target, {}, {});", arm.slice_rest, rest_start, rest_end));
                    }

                    // Bind tail elements
                    for (std::size_t i = 0; i < tail_count; ++i) {
                        if (arm.slice_tail[i] != "_") {
                            local_vars_.insert(arm.slice_tail[i]);
                            std::size_t offset_from_end = tail_count - i;
                            writeln(std::format("const auto& {} = _target[_target.size() - {}];", arm.slice_tail[i], offset_from_end));
                        }
                    }

                    for (const auto& b : arm.body) {
                        emit_stmt(b, in_main);
                    }
                    --indent_;
                    writeln("}");
                } else if (arm.variant_name == "_") {
                    writeln("if (!_matched) {");
                    ++indent_;
                    writeln("_matched = true;");
                    for (const auto& b : arm.body) {
                        emit_stmt(b, in_main);
                    }
                    --indent_;
                    writeln("}");
                }
            }
            --indent_;
            writeln("}");
            return;
        }

        std::string enum_name;
        for (const auto& arm : ms.arms) {
            if (!arm.enum_prefix.empty()) {
                enum_name = arm.enum_prefix;
                break;
            }
            if (auto it = cf_.variant_to_enum.find(arm.variant_name); it != cf_.variant_to_enum.end()) {
                enum_name = it->second;
                break;
            }
        }

        if (is_primitive_match || (enum_name.empty() && !is_slice_match)) {
            std::string target_expr = emit_expr(ms.target);
            writeln("{");
            ++indent_;
            writeln(std::format("const auto& _target = {};", target_expr));
            bool first = true;
            for (const auto& arm : ms.arms) {
                if (arm.variant_name == "_" && arm.literal_val.empty()) {
                    writeln("else {");
                    ++indent_;
                    for (const auto& b : arm.body) emit_stmt(b, in_main);
                    --indent_;
                    writeln("}");
                } else {
                    std::string pat = !arm.literal_val.empty() ? arm.literal_val : arm.variant_name;
                    if (first) {
                        writeln(std::format("if (_target == {}) {{", pat));
                        first = false;
                    } else {
                        writeln(std::format("else if (_target == {}) {{", pat));
                    }
                    ++indent_;
                    for (const auto& b : arm.body) emit_stmt(b, in_main);
                    --indent_;
                    writeln("}");
                }
            }
            --indent_;
            writeln("}");
            return;
        }

        bool is_rvalue = ms.is_move || (ms.target && std::holds_alternative<CallExpr>(ms.target->node));

        writeln(std::format("std::visit(cfire::overloaded{{"));
        ++indent_;
        for (const auto& arm : ms.arms) {
            if (arm.variant_name == "_") {
                writeln(is_rvalue ? "[&](auto&&) {" : "[&](const auto&) {");
            } else {
                if (is_rvalue) {
                    writeln(std::format("[&]({}::_{}&& _m) {{", enum_name, arm.variant_name));
                } else {
                    writeln(std::format("[&](const {}::_{}& _m) {{", enum_name, arm.variant_name));
                }
            }
            ++indent_;
            for (std::size_t i = 0; i < arm.bindings.size(); ++i) {
                local_vars_.insert(arm.bindings[i]);
                if (is_rvalue) {
                    writeln(std::format("auto&& {} = std::move(_m._{});", arm.bindings[i], i));
                } else {
                    writeln(std::format("const auto& {} = _m._{};", arm.bindings[i], i));
                }
            }
            for (const auto& b : arm.body) {
                emit_stmt(b, in_main);
            }
            --indent_;
            writeln("},");
        }
        --indent_;
        if (is_rvalue) {
            std::string t_str = emit_expr(ms.target);
            if (t_str.starts_with("std::move(") && t_str.ends_with(")")) {
                std::string inner = t_str.substr(10, t_str.size() - 11);
                writeln(std::format("}}, std::move({}._v));", inner));
            } else {
                writeln(std::format("}}, std::move({}._v));", t_str));
            }
        } else {
            writeln(std::format("}}, {}._v);", emit_expr(ms.target)));
        }
    }

    void emit_match_arm_body(std::ostringstream& sb, const std::vector<StmtPtr>& body, bool is_expr) {
        if (body.empty()) {
            if (is_expr) {
                for (int i = 0; i < indent_; ++i) sb << "    ";
                sb << "return {};\n";
            }
            return;
        }
        for (std::size_t i = 0; i < body.size(); ++i) {
            bool is_last = (i + 1 == body.size());
            if (is_expr && is_last) {
                if (std::holds_alternative<ExprStmt>(body[i]->node)) {
                    const auto& es = std::get<ExprStmt>(body[i]->node);
                    for (int j = 0; j < indent_; ++j) sb << "    ";
                    sb << "return " << emit_expr(es.expr) << ";\n";
                    continue;
                }
            }
            Emitter sub(cf_);
            sub.indent_ = indent_;
            sub.cur_fn_ = cur_fn_;
            sub.used_core_ = used_core_;
            sub.used_vendor_ = used_vendor_;
            sub.local_vars_ = local_vars_;
            sub.in_main_ = in_main_;
            sub.emit_stmt(body[i], false);
            sb << sub.buf_.str();
            local_vars_ = sub.local_vars_;
        }
    }

    [[nodiscard]] auto emit_match_expr(const MatchExpr& me) -> std::string {
        bool is_slice_match = false;
        bool is_primitive_match = false;
        for (const auto& a : me.arms) {
            if (a.is_slice_pattern) is_slice_match = true;
            if (!a.literal_val.empty()) is_primitive_match = true;
        }

        std::string enum_name;
        for (const auto& arm : me.arms) {
            if (!arm.enum_prefix.empty()) {
                enum_name = arm.enum_prefix;
                break;
            }
            if (auto it = cf_.variant_to_enum.find(arm.variant_name); it != cf_.variant_to_enum.end()) {
                enum_name = it->second;
                break;
            }
        }

        std::ostringstream sb;
        if (is_slice_match) {
            sb << "([&]() -> auto {\n";
            ++indent_;
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "const auto& _target = " << emit_expr(me.target) << ";\n";
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "bool _matched = false;\n";

            for (const auto& arm : me.arms) {
                if (arm.is_slice_pattern) {
                    std::size_t head_count = arm.slice_head.size();
                    std::size_t tail_count = arm.slice_tail.size();
                    std::size_t min_size = head_count + tail_count;

                    std::string cond;
                    if (!arm.slice_has_rest) {
                        cond = std::format("_target.size() == {}", min_size);
                    } else {
                        if (min_size == 0) cond = "true";
                        else cond = std::format("_target.size() >= {}", min_size);
                    }
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "if (!_matched && " << cond << ") {\n";
                    ++indent_;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "_matched = true;\n";
                    for (std::size_t i = 0; i < head_count; ++i) {
                        if (arm.slice_head[i] != "_") {
                            for (int j = 0; j < indent_; ++j) sb << "    ";
                            sb << "const auto& " << arm.slice_head[i] << " = _target[" << i << "];\n";
                        }
                    }
                    if (arm.slice_has_rest && !arm.slice_rest.empty() && arm.slice_rest != "_") {
                        std::string rest_start = std::to_string(head_count);
                        std::string rest_end = tail_count == 0 ? "_target.size()" : std::format("(_target.size() - {})", tail_count);
                        for (int j = 0; j < indent_; ++j) sb << "    ";
                        sb << "auto " << arm.slice_rest << " = cfire::slice(_target, " << rest_start << ", " << rest_end << ");\n";
                    }
                    for (std::size_t i = 0; i < tail_count; ++i) {
                        if (arm.slice_tail[i] != "_") {
                            std::size_t offset_from_end = tail_count - i;
                            for (int j = 0; j < indent_; ++j) sb << "    ";
                            sb << "const auto& " << arm.slice_tail[i] << " = _target[_target.size() - " << offset_from_end << "];\n";
                        }
                    }
                    emit_match_arm_body(sb, arm.body, true);
                    --indent_;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "}\n";
                } else if (arm.variant_name == "_") {
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "if (!_matched) {\n";
                    ++indent_;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "_matched = true;\n";
                    emit_match_arm_body(sb, arm.body, true);
                    --indent_;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "}\n";
                }
            }
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "cfire::assert_true(false, \"unreachable slice match\");\n";
            --indent_;
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "})()";
            return sb.str();
        }

        if (is_primitive_match || enum_name.empty()) {
            sb << "([&]() -> auto {\n";
            ++indent_;
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "const auto& _target = " << emit_expr(me.target) << ";\n";
            bool first = true;
            for (const auto& arm : me.arms) {
                if (arm.variant_name == "_" && arm.literal_val.empty()) {
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "else {\n";
                    ++indent_;
                    emit_match_arm_body(sb, arm.body, true);
                    --indent_;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "}\n";
                } else {
                    std::string pat = !arm.literal_val.empty() ? arm.literal_val : arm.variant_name;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    if (first) {
                        sb << "if (_target == " << pat << ") {\n";
                        first = false;
                    } else {
                        sb << "else if (_target == " << pat << ") {\n";
                    }
                    ++indent_;
                    emit_match_arm_body(sb, arm.body, true);
                    --indent_;
                    for (int i = 0; i < indent_; ++i) sb << "    ";
                    sb << "}\n";
                }
            }
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "cfire::assert_true(false, \"unreachable match\");\n";
            --indent_;
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "})()";
            return sb.str();
        }

        // Tagged enum match:
        bool is_rvalue = me.is_move || (me.target && std::holds_alternative<CallExpr>(me.target->node));
        sb << "std::visit(cfire::overloaded{\n";
        ++indent_;
        for (const auto& arm : me.arms) {
            for (int i = 0; i < indent_; ++i) sb << "    ";
            if (arm.variant_name == "_") {
                sb << (is_rvalue ? "[&](auto&&) -> auto {\n" : "[&](const auto&) -> auto {\n");
            } else {
                if (is_rvalue) {
                    sb << std::format("[&]({}::_{}&& _m) -> auto {{\n", enum_name, arm.variant_name);
                } else {
                    sb << std::format("[&](const {}::_{}& _m) -> auto {{\n", enum_name, arm.variant_name);
                }
            }
            ++indent_;
            for (std::size_t i = 0; i < arm.bindings.size(); ++i) {
                local_vars_.insert(arm.bindings[i]);
                for (int j = 0; j < indent_; ++j) sb << "    ";
                if (is_rvalue) {
                    sb << std::format("auto&& {} = std::move(_m._{});\n", arm.bindings[i], i);
                } else {
                    sb << std::format("const auto& {} = _m._{};\n", arm.bindings[i], i);
                }
            }
            emit_match_arm_body(sb, arm.body, true);
            --indent_;
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << "},\n";
        }
        --indent_;
        for (int i = 0; i < indent_; ++i) sb << "    ";
        if (is_rvalue) {
            std::string t_str = emit_expr(me.target);
            if (t_str.starts_with("std::move(") && t_str.ends_with(")")) {
                std::string inner = t_str.substr(10, t_str.size() - 11);
                sb << "}, std::move(" << inner << "._v))";
            } else {
                sb << "}, std::move(" << t_str << "._v))";
            }
        } else {
            sb << "}, " << emit_expr(me.target) << "._v)";
        }
        return sb.str();
    }

    struct InterpolationResult {
        std::string fmt_str{};
        std::vector<std::string> args{};
    };

    [[nodiscard]] auto is_local_type(const std::string& name) const -> bool {
        if (cf_.struct_tags.contains(name) || cf_.enums.contains(name)) return true;
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) return true;
        }
        return false;
    }

    [[nodiscard]] auto is_ident_in_scope(const std::string& ident) const -> bool {
        if (local_vars_.contains(ident)) return true;
        if (cf_.fn_sigs.contains(ident)) return true;
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] auto extract_interpolation(const LiteralExpr& lit, int line) const -> std::optional<InterpolationResult> {
        if (lit.kind != TokenKind::String) return std::nullopt;
        static const std::regex interp_re(R"(\{([a-zA-Z_][a-zA-Z0-9_.\(\)]*)\})");
        auto begin = std::sregex_iterator(lit.value.begin(), lit.value.end(), interp_re);
        if (begin == std::sregex_iterator()) return std::nullopt;

        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            std::string v = (*it)[1].str();
            std::string root = v;
            if (auto p = root.find_first_of(".("); p != std::string::npos) {
                root = root.substr(0, p);
            }
            if (!is_ident_in_scope(root)) {
                return std::nullopt;
            }
        }

        InterpolationResult res;
        res.fmt_str = std::regex_replace(lit.value, interp_re, "{}");
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            std::string v = (*it)[1].str();
            if (auto mv = cf_.last_use_move.find(cur_fn_ + ":" + v); mv != cf_.last_use_move.end() && mv->second == line) {
                v = std::format("std::move({})", v);
            }
            res.args.push_back(std::move(v));
        }
        return res;
    }

    [[nodiscard]] auto emit_expr(const ExprPtr& e) -> std::string {
        if (!e) return "";
        return std::visit([&](const auto& ex) -> std::string {
            using T = std::decay_t<decltype(ex)>;
            if constexpr (std::is_same_v<T, IdentExpr>) {
                std::string name = ex.name;
                if (name.starts_with("cpp::")) name.erase(0, 5);
                if (name.starts_with("http.")) {
                    name = "cfire::http::" + name.substr(5);
                } else if (name.starts_with("http::")) {
                    name = "cfire::http::" + name.substr(6);
                } else if (name.starts_with("url.")) {
                    name = "cfire::url::" + name.substr(4);
                } else if (name.starts_with("url::")) {
                    name = "cfire::url::" + name.substr(5);
                }
                if (auto col_pos = name.find("::"); col_pos != std::string::npos) {
                    if (is_vendor_alias(name.substr(0, col_pos))) {
                        name.erase(0, col_pos + 2);
                    }
                }
                // Check if this is an Enum Variant e.g. `Shape::Empty` (0-arg) or `Shape::Circle` (constructor)
                if (auto pos = name.find("::"); pos != std::string::npos) {
                    std::string en_name = name.substr(0, pos);
                    std::string v_name = name.substr(pos + 2);
                    if (auto it = cf_.enums.find(en_name); it != cf_.enums.end()) {
                        for (const auto& v : it->second.variants) {
                            if (v.name == v_name) {
                                if (v.payload_types.empty()) {
                                    return std::format("{}::{}()", en_name, v_name);
                                } else {
                                    return std::format("{}::{}", en_name, v_name);
                                }
                            }
                        }
                    }
                }
                if (auto it = cf_.last_use_move.find(cur_fn_ + ":" + ex.name); it != cf_.last_use_move.end() && it->second == e->line) {
                    return std::format("std::move({})", name);
                }
                return name;
            } else if constexpr (std::is_same_v<T, LiteralExpr>) {
                if (ex.kind == TokenKind::KwNone) {
                    return "std::nullopt";
                }
                if (ex.kind == TokenKind::String) {
                    if (auto interp = extract_interpolation(ex, e->line)) {
                        std::string args;
                        for (std::size_t i = 0; i < interp->args.size(); ++i) {
                            if (i > 0) args += ", ";
                            args += interp->args[i];
                        }
                        return std::format("std::format(\"{}\", {})", interp->fmt_str, args);
                    }
                    return std::format("\"{}\"", ex.value);
                }
                if (ex.kind == TokenKind::RawString) {
                    return std::format("R\"({})\"", ex.value);
                }
                return ex.value;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                if (ex.op == "move") return std::format("std::move({})", emit_expr(ex.operand));
                if (ex.op == "&mut") return "&" + emit_expr(ex.operand);
                if (ex.op == "*") return "(*" + emit_expr(ex.operand) + ")";
                return ex.op + emit_expr(ex.operand);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return std::format("({} {} {})", emit_expr(ex.left), ex.op, emit_expr(ex.right));
            } else if constexpr (std::is_same_v<T, MemberExpr>) {
                if (const auto* id = std::get_if<IdentExpr>(&ex.object->node)) {
                    if (cf_.enums.contains(id->name)) {
                        return std::format("{}::{}()", id->name, ex.field);
                    }
                    if (is_vendor_alias(id->name)) {
                        return ex.field;
                    }
                    if (id->name == "fs" || id->name == "zstd" || id->name == "json" ||
                        id->name == "sqlite" || id->name == "blake3" || id->name == "net" || id->name == "cli" ||
                        id->name == "time" || id->name == "os" || id->name == "sync" || id->name == "str" ||
                        id->name == "math" || id->name == "rand" || id->name == "log" || id->name == "regex" ||
                        id->name == "collections" || id->name == "sort" || id->name == "unicode" ||
                        id->name == "base64" || id->name == "hex" || id->name == "csv" ||
                        id->name == "aes" || id->name == "hmac" || id->name == "sha256" ||
                        id->name == "context" || id->name == "websocket" || id->name == "http" || id->name == "url" || id->name == "tls" ||
                        used_core_.contains(id->name)) {
                        return std::format("cfire::{}::{}", id->name, ex.field);
                    }
                    if (!is_ident_in_scope(id->name)) {
                        for (const auto& gv : cf_.ast.global_vars) {
                            if (gv.name == ex.field) return ex.field;
                        }
                    }
                }
                std::string obj = emit_expr(ex.object);
                if (ex.is_namespace) return obj + "::" + ex.field;
                if (obj.ends_with("::Response") || obj.ends_with("::Request") || obj.ends_with("::Router") || obj.ends_with("::HttpServer") ||
                    obj.ends_with("::ServerContext") ||
                    obj == "Response" || obj == "Request" || obj == "Router" || obj == "HttpServer" || obj == "ServerContext") {
                    return obj + "::" + ex.field;
                }
                if (ex.field == "len") return obj + ".size()";
                if (ex.field == "empty") return obj + ".empty";
                return obj + "." + ex.field;
            } else if constexpr (std::is_same_v<T, IndexExpr>) {
                return std::format("{}[{}]", emit_expr(ex.object), emit_expr(ex.index));
            } else if constexpr (std::is_same_v<T, SliceExpr>) {
                std::string obj = emit_expr(ex.object);
                std::string start_s = ex.start ? emit_expr(ex.start) : "0";
                std::string end_s = ex.end ? emit_expr(ex.end) : (obj + ".size()");
                return std::format("cfire::slice({}, {}, {})", obj, start_s, end_s);
            } else if constexpr (std::is_same_v<T, StructLitExpr>) {
                std::string tname = ex.type_name;
                if (tname.starts_with("cpp::")) tname.erase(0, 5);
                if (tname == "http.Request" || tname == "http::Request") tname = "cfire::http::Request";
                if (tname == "http.Response" || tname == "http::Response") tname = "cfire::http::Response";
                if (tname == "http.Router" || tname == "http::Router") tname = "cfire::http::Router";
                if (tname == "http.HttpServer" || tname == "http::HttpServer") tname = "cfire::http::HttpServer";
                if (tname == "url.Url" || tname == "url::Url") tname = "cfire::url::Url";
                if (auto dot_pos = tname.find('.'); dot_pos != std::string::npos) {
                    std::string mod = tname.substr(0, dot_pos);
                    std::string rest = tname.substr(dot_pos + 1);
                    if (used_core_.contains(mod)) {
                        tname = std::format("cfire::{}::{}", mod, rest);
                    } else if (is_local_type(rest) || is_vendor_alias(mod)) {
                        tname = rest;
                    } else {
                        tname = std::format("{}::{}", mod, rest);
                    }
                } else if (auto col_pos = tname.find("::"); col_pos != std::string::npos) {
                    std::string mod = tname.substr(0, col_pos);
                    std::string rest = tname.substr(col_pos + 2);
                    if (used_core_.contains(mod)) {
                        tname = std::format("cfire::{}::{}", mod, rest);
                    } else if (is_local_type(rest) || is_vendor_alias(mod)) {
                        tname = rest;
                    }
                }
                if (ex.fields.empty()) return tname + "{}";
                std::string inits;
                for (std::size_t i = 0; i < ex.fields.size(); ++i) {
                    if (i > 0) inits += ", ";
                    inits += std::format(".{} = {}", ex.fields[i].name, emit_expr(ex.fields[i].value));
                }
                return std::format("{}{{{}}}", tname, inits);
            } else if constexpr (std::is_same_v<T, ArrayLitExpr>) {
                std::string elems;
                for (std::size_t i = 0; i < ex.elements.size(); ++i) {
                    if (i > 0) elems += ", ";
                    elems += emit_expr(ex.elements[i]);
                }
                return std::format("{{{}}}", elems);
            } else if constexpr (std::is_same_v<T, ClosureExpr>) {
                return emit_closure(ex);
            } else if constexpr (std::is_same_v<T, OrExpr>) {
                if (ex.fallback_val) {
                    return std::format("{}.value_or({})", emit_expr(ex.inner), emit_expr(ex.fallback_val));
                }
                return emit_expr(ex.inner);
            } else if constexpr (std::is_same_v<T, TryExpr>) {
                std::string inner = emit_expr(ex.inner);
                if (in_main_ || in_test_) {
                    return std::format("({{\n"
                                       "    auto&& _res = ({0});\n"
                                       "    if (!_res.has_value()) {{\n"
                                       "        std::println(stderr, \"Error: {{}}\", _res.error().message());\n"
                                       "        std::exit(1);\n"
                                       "    }}\n"
                                       "    cfire::unwrap_val(std::forward<decltype(_res)>(_res));\n"
                                       "}})", inner);
                }
                return std::format("({{\n"
                                   "    auto&& _res = ({0});\n"
                                   "    if (!_res.has_value()) return std::unexpected(std::move(_res).error());\n"
                                   "    cfire::unwrap_val(std::forward<decltype(_res)>(_res));\n"
                                   "}})", inner);
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                return emit_call(ex);
            } else if constexpr (std::is_same_v<T, TupleExpr>) {
                std::string elems;
                for (std::size_t i = 0; i < ex.elements.size(); ++i) {
                    if (i > 0) elems += ", ";
                    elems += emit_expr(ex.elements[i]);
                }
                return std::format("std::make_tuple({})", elems);
            } else if constexpr (std::is_same_v<T, MatchExpr>) {
                return emit_match_expr(ex);
            }
            return "";
        }, e->node);
    }

    [[nodiscard]] auto emit_call(const CallExpr& call) -> std::string {
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if (id->name == "println" || id->name == "print") {
                std::string fn = (id->name == "print") ? "std::print" : "std::println";
                if (call.args.empty()) {
                    return std::format("{}()", fn);
                }
                const auto* first_lit = (call.args[0].value) ? std::get_if<LiteralExpr>(&call.args[0].value->node) : nullptr;
                if (first_lit && (first_lit->kind == TokenKind::String || first_lit->kind == TokenKind::RawString)) {
                    if (first_lit->kind == TokenKind::RawString) {
                        std::string trailing;
                        for (std::size_t i = 1; i < call.args.size(); ++i) {
                            trailing += ", " + emit_call_arg(call.args[i]);
                        }
                        return std::format("{}(R\"({})\"{})", fn, first_lit->value, trailing);
                    }
                    auto interp = extract_interpolation(*first_lit, call.args[0].line);
                    std::string fmt_str = interp ? interp->fmt_str : first_lit->value;
                    std::string all_args;
                    if (interp) {
                        for (const auto& a : interp->args) {
                            if (!all_args.empty()) all_args += ", ";
                            all_args += a;
                        }
                    }
                    for (std::size_t i = 1; i < call.args.size(); ++i) {
                        if (!all_args.empty()) all_args += ", ";
                        all_args += emit_call_arg(call.args[i]);
                    }
                    if (all_args.empty()) {
                        return std::format("{}(\"{}\")", fn, fmt_str);
                    }
                    return std::format("{}(\"{}\", {})", fn, fmt_str, all_args);
                }

                // First argument is not a string literal
                if (call.args.size() == 1) {
                    return std::format("{}(\"{{}}\", {})", fn, emit_call_arg(call.args[0]));
                }
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                return std::format("{}({})", fn, args);
            }
            if (id->name == "assert_eq" && call.args.size() >= 2) {
                std::string lhs = emit_call_arg(call.args[0]);
                std::string rhs = emit_call_arg(call.args[1]);
                std::string cond = std::format("(({}) == ({}))", lhs, rhs);
                std::string msg = (call.args.size() >= 3) ? emit_call_arg(call.args[2]) : "\"assert_eq failed\"";
                std::string expr_str = cond;
                std::string escaped_expr;
                for (char ch : expr_str) {
                    if (ch == '\\') escaped_expr += "\\\\";
                    else if (ch == '"') escaped_expr += "\\\"";
                    else escaped_expr += ch;
                }
                int line = call.args[0].value ? call.args[0].value->line : 0;
                std::string file_str = cf_.ast.path;
                std::string escaped_file;
                for (char ch : file_str) {
                    if (ch == '\\') escaped_file += "\\\\";
                    else if (ch == '"') escaped_file += "\\\"";
                    else escaped_file += ch;
                }
                return std::format("cfire::assert_true_at({}, {}, \"{}\", {}, \"{}\")",
                    cond, msg, escaped_file, line, escaped_expr);
            }
            if (id->name == "assert" && !call.args.empty()) {
                std::string cond = emit_call_arg(call.args[0]);
                std::string msg = (call.args.size() >= 2) ? emit_call_arg(call.args[1]) : "\"assertion failed\"";
                std::string expr_str = cond;
                std::string escaped_expr;
                for (char ch : expr_str) {
                    if (ch == '\\') escaped_expr += "\\\\";
                    else if (ch == '"') escaped_expr += "\\\"";
                    else escaped_expr += ch;
                }
                int line = call.args[0].value ? call.args[0].value->line : 0;
                std::string file_str = cf_.ast.path;
                std::string escaped_file;
                for (char ch : file_str) {
                    if (ch == '\\') escaped_file += "\\\\";
                    else if (ch == '"') escaped_file += "\\\"";
                    else escaped_file += ch;
                }
                return std::format("cfire::assert_true_at({}, {}, \"{}\", {}, \"{}\")",
                    cond, msg, escaped_file, line, escaped_expr);
            }
            if (id->name == "make" && call.type_args.size() == 1) {
                const auto& ta = call.type_args[0];
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                if (ta.smart_wrap == "own" && ta.elem_type) {
                    return std::format("cfire::make_own<{}>({})", lower_owning_type(*ta.elem_type), args);
                }
                if (ta.smart_wrap == "shared" && ta.elem_type) {
                    return std::format("cfire::make_shared_ptr<{}>({})", lower_owning_type(*ta.elem_type), args);
                }
                return std::format("{}{{{}}}", lower_owning_type(ta), args);
            }
            if ((id->name == "error" || id->name == "Err") && call.args.size() == 1) {
                return std::format("cfire::make_error({})", emit_call_arg(call.args[0]));
            }
        }

        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (mem->field == "len" && call.args.empty()) {
                return std::format("{}.size()", emit_expr(mem->object));
            }
            if (mem->field == "byte_ptr" && call.args.empty()) {
                used_core_.insert("str");
                return std::format("cfire::str::byte_ptr({})", emit_expr(mem->object));
            }
            if (mem->field == "byte_ptr_at" && call.args.size() == 1) {
                used_core_.insert("str");
                return std::format("cfire::str::byte_ptr_at({}, {})", emit_expr(mem->object), emit_call_arg(call.args[0]));
            }
            if ((mem->field == "data" || mem->field == "data_ptr") && call.args.empty()) {
                return std::format("({}).data()", emit_expr(mem->object));
            }
            // Check if this is an Enum Variant constructor e.g. `Shape.Circle(2.0)`
            if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && cf_.enums.contains(id->name)) {
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                return std::format("{}::{}({})", id->name, mem->field, args);
            }

            // Check if this is a user-defined receiver method `obj.method(args...)` -> `cfire_method_<name>(obj, args...)`
            // Overloaded cfire_method_<name> resolves via C++ overload resolution natively across multiple receiver types!
            if (auto owners_it = cf_.method_owners.find(mem->field); owners_it != cf_.method_owners.end()) {
                std::string args = emit_expr(mem->object);
                for (const auto& a : call.args) {
                    args += ", " + emit_call_arg(a);
                }
                return std::format("cfire_method_{}({})", mem->field, args);
            }
            if (auto owner_it = cf_.method_owner.find(mem->field); owner_it != cf_.method_owner.end()) {
                std::string args = emit_expr(mem->object);
                for (const auto& a : call.args) {
                    args += ", " + emit_call_arg(a);
                }
                return std::format("cfire_method_{}({})", mem->field, args);
            }

            // Check if `pkg.fn(args...)` refers to a merged local package function in `cf_.fn_sigs`
            if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && cf_.fn_sigs.contains(mem->field)) {
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                return std::format("{}({})", mem->field, args);
            }

            std::string obj = emit_expr(mem->object);
            if (mem->field == "push" && call.args.size() == 1) {
                return std::format("{}.push_back({})", obj, emit_call_arg(call.args[0]));
            }
            if (mem->field == "pop" && call.args.empty()) {
                return std::format("{}.pop_back()", obj);
            }
            if (mem->field == "with_ext" && call.args.size() == 1) {
                return std::format("cfire::path_with_ext({}, {})", obj, emit_call_arg(call.args[0]));
            }
            if (mem->field == "bytes" && call.args.empty()) {
                return std::format("cfire::to_bytes({})", obj);
            }
            if (mem->field == "parse" && call.type_args.size() == 1) {
                if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && id->name == "cli") {
                    return std::format("cfire::cli::parse<{}>(argc, argv)", call.type_args[0].name);
                }
            }
            if (mem->field == "tx" && call.args.empty() && call.block_arg && call.block_arg->params.size() == 1) {
                std::string tx_param = call.block_arg->params[0];
                std::ostringstream sb;
                sb << std::format("{}.tx([&](cfire::sqlite::Conn& {}) -> std::expected<void, cfire::Error> {{\n", obj, tx_param);
                ++indent_;
                Emitter sub(cf_);
                sub.indent_ = indent_;
                sub.cur_fn_ = cur_fn_;
                sub.used_core_ = used_core_;
                sub.used_vendor_ = used_vendor_;
                sub.local_vars_ = local_vars_;
                sub.local_vars_.insert(tx_param);
                sub.in_fallible_void_ = true;
                for (const auto& st : call.block_arg->body) {
                    sub.emit_stmt(st, false);
                }
                sb << sub.buf_.str();
                for (int i = 0; i < indent_; ++i) sb << "    ";
                sb << "return {};\n";
                --indent_;
                for (int i = 0; i < indent_; ++i) sb << "    ";
                sb << "})";
                return sb.str();
            }
            if (mem->field == "spawn" && call.block_arg) {
                std::ostringstream sb;
                sb << std::format("{}.spawn([=]() {{\n", obj);
                ++indent_;
                Emitter sub(cf_);
                sub.indent_ = indent_;
                sub.cur_fn_ = cur_fn_;
                sub.used_core_ = used_core_;
                sub.used_vendor_ = used_vendor_;
                sub.local_vars_ = local_vars_;
                for (const auto& st : call.block_arg->body) {
                    sub.emit_stmt(st, false);
                }
                sb << sub.buf_.str();
                --indent_;
                for (int i = 0; i < indent_; ++i) sb << "    ";
                sb << "})";
                return sb.str();
            }
        }

        bool is_vendor_call = false;
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && is_vendor_alias(id->name)) {
                is_vendor_call = true;
            }
        } else if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if (auto col_pos = id->name.find("::"); col_pos != std::string::npos) {
                if (is_vendor_alias(id->name.substr(0, col_pos))) {
                    is_vendor_call = true;
                }
            }
        }

        std::string callee = emit_expr(call.callee);
        std::string lookup_name = callee;
        if (auto colon_pos = lookup_name.rfind("::"); colon_pos != std::string::npos) {
            lookup_name = lookup_name.substr(colon_pos + 2);
        }
        const std::vector<std::string>* cpp_params = nullptr;
        if (auto it = cf_.cpp_fn_params.find(lookup_name); it != cf_.cpp_fn_params.end()) {
            cpp_params = &it->second;
        }

        if (!call.type_args.empty()) {
            std::string targs;
            for (std::size_t i = 0; i < call.type_args.size(); ++i) {
                if (i > 0) targs += ", ";
                targs += lower_owning_type(call.type_args[i]);
            }
            callee = std::format("{}<{}>", callee, targs);
        }
        std::string args;
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            if (i > 0) args += ", ";
            std::string arg_val = emit_call_arg(call.args[i]);
            if (cpp_params && i < cpp_params->size()) {
                const std::string& ptype = (*cpp_params)[i];
                if ((ptype == "const char *" || ptype == "const char*") &&
                    !arg_val.starts_with("\"") && !arg_val.ends_with(".c_str()") && !arg_val.ends_with(".data()")) {
                    arg_val = std::format("cfire::c_str({})", arg_val);
                } else if ((ptype.contains("basic_string<char>") || ptype.contains("std::string")) &&
                           !ptype.contains("string_view") && !arg_val.starts_with("std::string(")) {
                    arg_val = std::format("std::string({})", arg_val);
                }
            } else if (is_vendor_call) {
                bool is_plain_lit_or_const = false;
                if (call.args[i].value) {
                    if (const auto* lit = std::get_if<LiteralExpr>(&call.args[i].value->node)) {
                        if (lit->kind != TokenKind::String || !arg_val.starts_with("std::format(")) {
                            is_plain_lit_or_const = true;
                        }
                    } else if (const auto* mem = std::get_if<MemberExpr>(&call.args[i].value->node)) {
                        if (const auto* mid = std::get_if<IdentExpr>(&mem->object->node); mid && is_vendor_alias(mid->name)) {
                            is_plain_lit_or_const = true;
                        }
                    }
                }
                if (!is_plain_lit_or_const && !arg_val.starts_with("&")) {
                    arg_val = std::format("cfire::c_arg({})", arg_val);
                }
            }
            args += arg_val;
        }
        if (call.block_arg) {
            if (!args.empty()) args += ", ";
            args += emit_closure(*call.block_arg);
        }
        return std::format("{}({})", callee, args);
    }

    static auto stmt_contains_try_without_val_return(const std::vector<StmtPtr>& stmts, bool& has_try, bool& has_val_ret) -> void {
        for (const auto& s : stmts) {
            if (!s) continue;
            if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
                if (vd->init && std::holds_alternative<TryExpr>(vd->init->node)) has_try = true;
            } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
                if (es->expr && std::holds_alternative<TryExpr>(es->expr->node)) has_try = true;
            } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
                if (rs->value) has_val_ret = true;
            } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
                stmt_contains_try_without_val_return(ifs->then_body, has_try, has_val_ret);
                stmt_contains_try_without_val_return(ifs->else_body, has_try, has_val_ret);
            } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
                stmt_contains_try_without_val_return(fs->body, has_try, has_val_ret);
            }
        }
    }

    [[nodiscard]] auto emit_closure(const ClosureExpr& cl) -> std::string {
        std::ostringstream sb;
        sb << "[&](";
        for (std::size_t i = 0; i < cl.params.size(); ++i) {
            if (i > 0) sb << ", ";
            if (i < cl.param_types.size() && cl.param_types[i].has_value()) {
                std::string p_intent = (i < cl.param_intents.size()) ? cl.param_intents[i] : "in";
                sb << lower_param_type(p_intent, *cl.param_types[i]) << " " << cl.params[i];
            } else {
                sb << "auto&& " << cl.params[i];
            }
        }
        bool is_fallible_void = false;
        if (cl.return_type) {
            sb << ") -> " << lower_return_type(*cl.return_type) << " {\n";
            if (cl.return_type->name == "void" && cl.return_type->is_fallible) {
                is_fallible_void = true;
            }
        } else {
            bool has_try = false;
            bool has_val_ret = false;
            stmt_contains_try_without_val_return(cl.body, has_try, has_val_ret);
            if (has_try && !has_val_ret) {
                sb << ") -> std::expected<void, cfire::Error> {\n";
                is_fallible_void = true;
            } else {
                sb << ") {\n";
            }
        }
        ++indent_;
        Emitter sub(cf_);
        sub.indent_ = indent_;
        sub.cur_fn_ = cur_fn_;
        sub.used_core_ = used_core_;
        sub.used_vendor_ = used_vendor_;
        sub.local_vars_ = local_vars_;
        for (const auto& p : cl.params) sub.local_vars_.insert(p);
        sub.in_main_ = false;
        sub.in_test_ = false;
        sub.in_fallible_void_ = is_fallible_void;
        for (const auto& st : cl.body) {
            sub.emit_stmt(st, false);
        }
        sb << sub.buf_.str();
        if (is_fallible_void) {
            bool has_ret = !cl.body.empty() && std::holds_alternative<ReturnStmt>(cl.body.back()->node);
            if (!has_ret) {
                for (int i = 0; i < indent_; ++i) sb << "    ";
                sb << "return {};\n";
            }
        }
        --indent_;
        for (int i = 0; i < indent_; ++i) sb << "    ";
        sb << "}";
        return sb.str();
    }

    [[nodiscard]] auto emit_call_arg(const CallArg& a) -> std::string {
        std::string val = emit_expr(a.value);
        if (a.intent == "move" && !val.starts_with("std::move(")) {
            return std::format("std::move({})", val);
        }
        return val;
    }

    [[nodiscard]] auto lower_raw_owning_type(const TypeRef& tr) const -> std::string {
        if (tr.is_mut_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<{}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string&";
            if (tr.name == "bytes") return "std::span<std::byte>";
            return (tr.elem_type ? lower_owning_type(*tr.elem_type) : tr.name) + "&";
        }
        if (tr.is_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<const {}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string_view";
            if (tr.name == "bytes") return "std::span<const std::byte>";
            return "const " + (tr.elem_type ? lower_owning_type(*tr.elem_type) : tr.name) + "&";
        }
        if (tr.smart_wrap == "own" && tr.elem_type) {
            return std::format("std::unique_ptr<{}>", lower_owning_type(*tr.elem_type));
        }
        if (tr.smart_wrap == "shared" && tr.elem_type) {
            return std::format("std::shared_ptr<{}>", lower_owning_type(*tr.elem_type));
        }
        if (tr.is_tuple) {
            std::string s = "std::tuple<";
            for (std::size_t i = 0; i < tr.tuple_types.size(); ++i) {
                if (i > 0) s += ", ";
                s += lower_owning_type(tr.tuple_types[i]);
            }
            s += ">";
            return s;
        }
        if (tr.is_mut_ptr && tr.elem_type) return lower_owning_type(*tr.elem_type) + "*";
        if (tr.is_ptr && tr.elem_type) return "const " + lower_owning_type(*tr.elem_type) + "*";
        if (tr.is_map && tr.key_type && tr.elem_type) {
            return std::format("cfire::Map<{}, {}>", lower_owning_type(*tr.key_type), lower_owning_type(*tr.elem_type));
        }
        if (tr.is_array && tr.elem_type) {
            return std::format("std::array<{}, {}>", lower_owning_type(*tr.elem_type), tr.array_len);
        }
        if (tr.is_slice && tr.elem_type) {
            return std::format("std::vector<{}>", lower_owning_type(*tr.elem_type));
        }
        if (tr.name == "str") return "std::string";
        if (tr.name == "path") return "std::filesystem::path";
        if (tr.name == "bytes") return "std::vector<std::byte>";
        if (tr.name == "i8") return "std::int8_t";
        if (tr.name == "i16") return "std::int16_t";
        if (tr.name == "i32") return "std::int32_t";
        if (tr.name == "i64") return "std::int64_t";
        if (tr.name == "u8" || tr.name == "byte") return "std::uint8_t";
        if (tr.name == "c_char" || tr.name == "char") return "char";
        if (tr.name == "u16") return "std::uint16_t";
        if (tr.name == "u32") return "std::uint32_t";
        if (tr.name == "u64") return "std::uint64_t";
        if (tr.name == "usize") return "std::size_t";
        if (tr.name == "isize") return "std::ptrdiff_t";
        if (tr.name == "f32") return "float";
        if (tr.name == "f64") return "double";
        if (tr.name == "bool") return "bool";
        if (tr.name == "void") return "void";
        if (tr.name == "sqlite::Conn") return "cfire::sqlite::Conn";
        if (tr.name == "context::Context" || tr.name == "Context") return "cfire::sync::context::Context";
        if (tr.name == "websocket::Conn" || tr.name == "Conn") return "cfire::websocket::Conn";
        if (tr.name == "websocket::Server") return "cfire::websocket::Server";
        if (tr.name == "http::Request" || tr.name == "Request") return "cfire::http::Request";
        if (tr.name == "http::Response" || tr.name == "Response") return "cfire::http::Response";
        if (tr.name == "http::Router" || tr.name == "Router") return "cfire::http::Router";
        if (tr.name == "http::HttpServer" || tr.name == "HttpServer") return "cfire::http::HttpServer";
        if (tr.name == "url::Url" || tr.name == "Url") return "cfire::url::Url";
        if (tr.name == "tls::ServerContext" || tr.name == "ServerContext") return "cfire::tls::ServerContext";
        if (tr.name == "tls::CertKeyPair" || tr.name == "CertKeyPair") return "cfire::tls::CertKeyPair";
        if (tr.name == "net::TcpConn" || tr.name == "TcpConn") return "cfire::net::TcpConn";
        if (tr.name == "net::TcpListener" || tr.name == "TcpListener") return "cfire::net::TcpListener";
        if (tr.name == "sync::Channel" && tr.elem_type) {
            return std::format("cfire::sync::Channel<{}>", lower_owning_type(*tr.elem_type));
        }
        std::string clean = tr.name;
        if (clean.starts_with("cpp::")) clean.erase(0, 5);
        if (auto col_pos = clean.find("::"); col_pos != std::string::npos) {
            std::string mod = clean.substr(0, col_pos);
            std::string rest = clean.substr(col_pos + 2);
            if (used_core_.contains(mod)) {
                return std::format("cfire::{}::{}", mod, rest);
            }
            if (is_local_type(rest) || is_vendor_alias(mod)) {
                return rest;
            }
        }
        return clean;
    }

    [[nodiscard]] auto is_vendor_alias(std::string_view mod) const -> bool {
        for (const auto& [_, alias] : used_vendor_) {
            if (alias == mod) return true;
        }
        return false;
    }

    [[nodiscard]] auto lower_owning_type(const TypeRef& tr) const -> std::string {
        std::string t = lower_raw_owning_type(tr);
        if (tr.is_optional) t = std::format("std::optional<{}>", t);
        if (tr.is_fallible) t = std::format("std::expected<{}, cfire::Error>", t);
        return t;
    }

    [[nodiscard]] auto lower_param_type(std::string_view intent, const TypeRef& tr) const -> std::string {
        if (intent == "mut" || intent == "out") return lower_owning_type(tr) + "&";
        if (intent == "move") return lower_owning_type(tr) + "&&";
        if (tr.is_mut_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<{}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string&";
            if (tr.name == "bytes") return "std::span<std::byte>";
            return lower_owning_type(tr);
        }
        if (tr.is_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<const {}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string_view";
            if (tr.name == "bytes") return "std::span<const std::byte>";
            return lower_owning_type(tr);
        }
        if (tr.name == "str") return "const std::string&";
        if (tr.is_slice && tr.elem_type) {
            return std::format("const std::vector<{}>&", lower_owning_type(*tr.elem_type));
        }
        if (tr.name == "bytes") return "const std::vector<std::byte>&";
        if (tr.name == "path") return "const std::filesystem::path&";
        if (tr.name == "i8" || tr.name == "i16" || tr.name == "i32" || tr.name == "i64" ||
            tr.name == "u8" || tr.name == "byte" || tr.name == "u16" || tr.name == "u32" ||
            tr.name == "u64" || tr.name == "usize" || tr.name == "isize" ||
            tr.name == "f32" || tr.name == "f64" || tr.name == "bool") {
            return lower_owning_type(tr);
        }
        return "const " + lower_owning_type(tr) + "&";
    }

    [[nodiscard]] auto lower_return_type(const TypeRef& tr) const -> std::string {
        return lower_owning_type(tr);
    }
};


[[nodiscard]] inline auto emit_cpp(const CheckedFile& cf, bool test_mode = false) -> std::string {
    Emitter em(cf, test_mode);
    return em.emit_cpp();
}

} // namespace cfire::frontend
