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
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, std::size_t> fixed_array_vars_{};
    std::unordered_map<std::string, std::string> loop_index_to_col_{};
    std::unordered_map<const Expr*, std::string> hoisted_tries_{};

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
            std::string path = imp.path.starts_with("ext:") ? ("core:" + imp.path.substr(4)) : imp.path;
            if (path == "core:fs") {
                used_core_.insert("fs");
            } else if (path == "core:cli") {
                used_core_.insert("cli");
            } else if (path.starts_with("core:compress/") || path == "core:compress" || path == "core:zstd") {
                used_core_.insert("compress");
            } else if (path == "core:json") {
                used_core_.insert("json");
            } else if (path == "core:db/sqlite" || path == "core:sqlite") {
                used_core_.insert("sqlite");
            } else if (path.starts_with("core:crypto/") || path == "core:crypto") {
                used_core_.insert("crypto");
            } else if (path.starts_with("core:net/") || path == "core:net" || path == "core:http" || path == "core:websocket" || path == "core:tls") {
                used_core_.insert("net");
            } else if (path == "core:time") {
                used_core_.insert("time");
            } else if (path == "core:os") {
                used_core_.insert("os");
            } else if (path.starts_with("core:sync/") || path == "core:sync" || path == "core:context") {
                used_core_.insert("sync");
            } else if (path == "core:str") {
                used_core_.insert("str");
            } else if (path == "core:math" || path == "core:rand") {
                used_core_.insert("math");
            } else if (path.starts_with("core:encoding/") || path == "core:encoding" || path == "core:base64" || path == "core:hex" || path == "core:csv") {
                used_core_.insert("encoding");
            } else if (path == "core:regex") {
                used_core_.insert("regex");
            } else if (path == "core:collections" || path == "core:sort") {
                used_core_.insert("collections");
            } else if (path == "core:log") {
                used_core_.insert("log");
            } else if (path == "core:unicode") {
                used_core_.insert("unicode");
            } else if (path == "core:audio") {
                used_core_.insert("audio");
            } else if (path == "core:ai" || path.starts_with("core:ai/")) {
                used_core_.insert("ai");
            } else if (path == "core:tako") {
                used_core_.insert("tako");
                used_core_.insert("json");
            } else if (path == "core:webui") {
                used_core_.insert("webui");
                used_core_.insert("json");
            } else if (path.starts_with("core:")) {
                used_core_.insert(path.substr(5));
            } else if (path == "vendor:raylib") {
                std::string alias = imp.alias.empty() ? "raylib" : imp.alias;
                used_vendor_["raylib"] = alias;
            } else if (path == "vendor:raygui") {
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

        auto add_unique = [](std::vector<std::string>& vec, std::string val) {
            if (std::find(vec.begin(), vec.end(), val) == vec.end()) {
                vec.push_back(std::move(val));
            }
        };

        for (const auto& imp : ast.imports) {
            if (imp.is_cpp) {
                CppHeader header;
                header.path = imp.path;
                for (const auto& attr : imp.attrs) {
                    if (attr.name == "pkg" && !attr.args.empty()) {
                        add_unique(link_tags, "@pkg:" + attr.args[0]);
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
            std::string path = imp.path.starts_with("ext:") ? ("core:" + imp.path.substr(4)) : imp.path;
            if (path == "core:fs") {
                used_core_.insert("fs");
            } else if (path == "core:cli") {
                used_core_.insert("cli");
            } else if (path.starts_with("core:compress/") || path == "core:compress" || path == "core:zstd") {
                used_core_.insert("compress");
                add_unique(defines, "#define CFIRE_USE_ZSTD 1");
                add_unique(link_tags, "-lzstd");
            } else if (path == "core:json") {
                used_core_.insert("json");
                add_unique(defines, "#define CFIRE_USE_SIMDJSON 1");
                add_unique(link_tags, "-lsimdjson");
            } else if (path == "core:db/sqlite" || path == "core:sqlite") {
                used_core_.insert("sqlite");
                add_unique(defines, "#define CFIRE_USE_SQLITE 1");
                add_unique(link_tags, "-lsqlite3");
            } else if (path.starts_with("core:crypto/") || path == "core:crypto") {
                used_core_.insert("crypto");
            } else if (path.starts_with("core:net/") || path == "core:net" || path == "core:http" || path == "core:websocket" || path == "core:tls") {
                used_core_.insert("net");
            } else if (path == "core:time") {
                used_core_.insert("time");
            } else if (path == "core:os") {
                used_core_.insert("os");
            } else if (path.starts_with("core:sync/") || path == "core:sync" || path == "core:context") {
                used_core_.insert("sync");
            } else if (path == "core:str") {
                used_core_.insert("str");
            } else if (path == "core:math" || path == "core:rand") {
                used_core_.insert("math");
            } else if (path.starts_with("core:encoding/") || path == "core:encoding" || path == "core:base64" || path == "core:hex" || path == "core:csv") {
                used_core_.insert("encoding");
            } else if (path == "core:regex") {
                used_core_.insert("regex");
            } else if (path == "core:collections" || path == "core:sort") {
                used_core_.insert("collections");
            } else if (path == "core:log") {
                used_core_.insert("log");
            } else if (path == "core:unicode") {
                used_core_.insert("unicode");
            } else if (path == "core:audio") {
                used_core_.insert("audio");
            } else if (path == "core:ai" || path.starts_with("core:ai/")) {
                used_core_.insert("ai");
                add_unique(defines, "#define CFIRE_USE_SIMDJSON 1");
                add_unique(link_tags, "-lsimdjson");
            } else if (path == "core:tako") {
                used_core_.insert("tako");
                used_core_.insert("json");
                add_unique(defines, "#define CFIRE_USE_SIMDJSON 1");
                add_unique(link_tags, "-lsimdjson");
            } else if (path == "core:webui") {
                used_core_.insert("webui");
                used_core_.insert("json");
                add_unique(defines, "#define CFIRE_USE_SIMDJSON 1");
                add_unique(link_tags, "-lsimdjson");
            } else if (path.starts_with("core:")) {
                used_core_.insert(path.substr(5));
            } else if (path == "vendor:raylib") {
                std::string alias = imp.alias.empty() ? "raylib" : imp.alias;
                used_vendor_["raylib"] = alias;
                add_unique(link_tags, "-lraylib");
            } else if (path == "vendor:raygui") {
                std::string alias = imp.alias.empty() ? "raygui" : imp.alias;
                used_vendor_["raygui"] = alias;
                if (!used_vendor_.contains("raylib")) {
                    used_vendor_["raylib"] = "raylib";
                }
                add_unique(link_tags, "-lraylib");
            }
        }

        bool any_json_tag = false;
        for (const auto& [sname, tags] : cf_.struct_tags) {
            for (const auto& t : tags) {
                if (!t.json_key.empty() || t.custom_tags.contains("desc")) { any_json_tag = true; break; }
            }
            if (any_json_tag) break;
        }
        if (any_json_tag && !used_core_.contains("json")) {
            used_core_.insert("json");
        }
        if (used_core_.contains("json")) {
            if (std::find(defines.begin(), defines.end(), "#define CFIRE_USE_SIMDJSON 1") == defines.end()) {
                defines.push_back("#define CFIRE_USE_SIMDJSON 1");
            }
            if (std::find(link_tags.begin(), link_tags.end(), "-lsimdjson") == link_tags.end()) {
                link_tags.push_back("-lsimdjson");
            }
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
                else if (mod == "audio") core_headers.insert("core/audio.hpp");
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
            writeln("#include \"core/desktop_raylib.hpp\"");
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

        // 5. Emit `test "..."`, `fuzz "..."`, `bench "..."`, and `[auto]` Invariant Blocks & Test Harness Runner
        std::vector<std::pair<std::string, int>> auto_json_structs;
        if (test_mode_ && used_core_.contains("json")) {
            for (const auto& st : ast.structs) {
                auto it = cf_.struct_tags.find(st.name);
                if (it == cf_.struct_tags.end()) continue;
                bool has_json_or_desc = false;
                for (const auto& t : it->second) {
                    if (!t.json_key.empty() || t.custom_tags.contains("desc")) {
                        has_json_or_desc = true;
                        break;
                    }
                }
                if (!has_json_or_desc) continue;
                bool supported = true;
                for (const auto& fld : st.fields) {
                    if (fld.type.is_ptr || fld.type.is_mut_ptr || fld.type.is_borrow ||
                        fld.type.is_mut_borrow || fld.type.is_fn || !fld.type.smart_wrap.empty()) {
                        supported = false;
                        break;
                    }
                }
                if (supported) {
                    auto_json_structs.emplace_back(st.name, st.line);
                }
            }
        }

        for (std::size_t i = 0; i < ast.tests.size(); ++i) {
            emit_test_decl(ast.tests[i], i);
            writeln();
        }
        for (std::size_t j = 0; j < auto_json_structs.size(); ++j) {
            const auto& [sname, sline] = auto_json_structs[j];
            writeln(std::format("inline void __cfire_auto_json_{}(std::uint64_t seed) {{", j));
            ++indent_;
            writeln("cfire::testing_detail::FuzzRng rng(seed);");
            writeln("for (std::size_t iter = 0; iter < 16; ++iter) {");
            ++indent_;
            writeln(std::format("auto val = cfire::testing_detail::Arbitrary<{}>::generate(rng, iter);", sname));
            writeln("std::string encoded = cfire::json::stringify(val);");
            writeln(std::format("auto decoded = cfire::json::parse<{}>(encoded);", sname));
            writeln(std::format("cfire::assert_true_at(decoded.has_value(), \"auto JSON round-trip parse failed for '{}'\", \"{}\", {}, \"json.parse(json.stringify(val))\");",
                sname, escape_line_path(source_path_), sline));
            writeln("if (decoded.has_value()) {");
            ++indent_;
            writeln("std::string reencoded = cfire::json::stringify(*decoded);");
            writeln(std::format("cfire::assert_binop_at(reencoded, encoded, [](const auto& _l, const auto& _r) {{ return _l == _r; }}, \"==\", \"auto JSON round-trip mismatch for '{}'\", \"{}\", {}, \"json.stringify(decoded) == json.stringify(val)\");",
                sname, escape_line_path(source_path_), sline));
            --indent_;
            writeln("}");
            --indent_;
            writeln("}");
            writeln(std::format("auto bad = cfire::json::parse<{}>(\"{{malformed_json\");", sname));
            writeln(std::format("cfire::assert_true_at(!bad.has_value(), \"expected malformed JSON to return Err for '{}'\", \"{}\", {}, \"!json.parse(\\\"{{malformed\\\").has_value()\");",
                sname, escape_line_path(source_path_), sline));
            --indent_;
            writeln("}");
            writeln();
        }

        if ((test_mode_ || !has_user_main) && (!ast.tests.empty() || !auto_json_structs.empty())) {
            writeln("auto main(int argc, char** argv) -> int {");
            ++indent_;
            writeln("bool json_mode = false;");
            writeln("bool bench_mode = false;");
            writeln("double bench_time_ms = 200.0;");
            writeln("std::size_t fuzz_runs = 100;");
            writeln("std::uint64_t fuzz_seed = 0x9E3779B97F4A7C15ULL;");
            writeln("std::string filter;");
            writeln("for (int i = 1; i < argc; ++i) {");
            writeln("    std::string_view a = argv[i];");
            writeln("    if (a == \"--cfire-test-json\") json_mode = true;");
            writeln("    else if (a == \"--cfire-bench\") bench_mode = true;");
            writeln("    else if (a.starts_with(\"--cfire-test-filter=\")) filter = a.substr(20);");
            writeln("    else if (a.starts_with(\"--cfire-fuzz-runs=\")) {");
            writeln("        std::size_t v = 100;");
            writeln("        auto sv = a.substr(18);");
            writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), v);");
            writeln("        if (v > 0) fuzz_runs = v;");
            writeln("    } else if (a.starts_with(\"--cfire-seed=\")) {");
            writeln("        std::uint64_t s = 0;");
            writeln("        auto sv = a.substr(13);");
            writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), s);");
            writeln("        if (s > 0) fuzz_seed = s;");
            writeln("    } else if (a.starts_with(\"--cfire-bench-time=\")) {");
            writeln("        std::uint64_t ms = 200;");
            writeln("        auto sv = a.substr(19);");
            writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), ms);");
            writeln("        if (ms > 0) bench_time_ms = static_cast<double>(ms);");
            writeln("    }");
            writeln("}");
            writeln("(void)bench_mode; (void)bench_time_ms; (void)fuzz_runs; (void)fuzz_seed;");
            writeln("cfire::testing_detail::in_test_harness = true;");
            writeln("int passed_count = 0;");
            writeln("int failed_count = 0;");
            writeln("struct TestResultRecord { std::string name; bool passed; double duration_ms; std::string file; int line; std::string error; };");
            writeln("std::vector<TestResultRecord> records;");
            writeln("std::vector<cfire::testing_detail::BenchStats> bench_records;");
            for (std::size_t i = 0; i < ast.tests.size(); ++i) {
                const auto& td = ast.tests[i];
                std::string t_name = escape_line_path(td.name);
                if (td.is_bench) {
                    writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
                    ++indent_;
                    writeln(std::format("if (!json_mode) std::println(\"=== BENCH {}\");", t_name));
                    writeln("cfire::testing_detail::current_test_failed = false;");
                    writeln("cfire::testing_detail::failure_file.clear();");
                    writeln("cfire::testing_detail::failure_line = 0;");
                    writeln("cfire::testing_detail::failure_expr.clear();");
                    writeln("cfire::testing_detail::failure_msg.clear();");
                    writeln("auto start_t = std::chrono::steady_clock::now();");
                    writeln("if (setjmp(cfire::testing_detail::test_env) == 0) {");
                    writeln(std::format("    __cfire_bench_runner_{}(!bench_mode, bench_time_ms, json_mode, bench_records);", i));
                    writeln("}");
                    writeln("auto end_t = std::chrono::steady_clock::now();");
                    writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
                    writeln("if (!cfire::testing_detail::current_test_failed) {");
                    writeln("    passed_count++;");
                    writeln(std::format("    if (!json_mode && !bench_mode) std::println(\"--- PASS: {}\");", t_name));
                    writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
                    writeln("} else {");
                    writeln("    failed_count++;");
                    writeln("    std::string fail_detail = cfire::testing_detail::failure_msg.empty() ? cfire::testing_detail::failure_expr : cfire::testing_detail::failure_msg;");
                    writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
                    writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", t_name));
                    writeln("}");
                    --indent_;
                    writeln("}");
                    continue;
                }

                writeln("if (!bench_mode) {");
                ++indent_;
                writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
                ++indent_;
                writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", t_name));
                writeln("cfire::testing_detail::current_test_failed = false;");
                writeln("cfire::testing_detail::failure_file.clear();");
                writeln("cfire::testing_detail::failure_line = 0;");
                writeln("cfire::testing_detail::failure_expr.clear();");
                writeln("cfire::testing_detail::failure_msg.clear();");
                bool check_fd_leak = !stmts_reference_ident(td.body, "InitWindow") &&
                                     !stmts_reference_ident(td.body, "run_webview_test");
                if (check_fd_leak) {
                    writeln("const std::size_t fds_before = cfire::testing_detail::count_open_fds();");
                }
                writeln("auto start_t = std::chrono::steady_clock::now();");
                if (td.is_fuzz || !td.params.empty()) {
                    writeln(std::format("__cfire_fuzz_runner_{}(fuzz_runs, fuzz_seed);", i));
                } else {
                    writeln("if (setjmp(cfire::testing_detail::test_env) == 0) {");
                    writeln(std::format("    __cfire_test_{}();", i));
                    writeln("}");
                }
                writeln("auto end_t = std::chrono::steady_clock::now();");
                if (check_fd_leak) {
                    writeln("const std::size_t fds_after = cfire::testing_detail::count_open_fds();");
                    writeln("if (!cfire::testing_detail::current_test_failed && fds_before > 0 && fds_after > fds_before) {");
                    writeln("    cfire::testing_detail::current_test_failed = true;");
                    writeln(std::format("    cfire::testing_detail::failure_file = \"{}\";", escape_line_path(source_path_)));
                    writeln(std::format("    cfire::testing_detail::failure_line = {};", td.line));
                    writeln("    cfire::testing_detail::failure_expr = \"resource leak canary\";");
                    writeln("    cfire::testing_detail::failure_msg = std::format(\"test leaked {} open file descriptor(s) (missing 'defer file.close()' or 'defer db.close()'?)\", fds_after - fds_before);");
                    writeln("}");
                }
                writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
                writeln("bool test_pass = !cfire::testing_detail::current_test_failed;");
                writeln("if (test_pass) {");
                writeln("    passed_count++;");
                writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", t_name));
                writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
                writeln("} else {");
                writeln("    failed_count++;");
                writeln("    std::string fail_detail;");
                writeln("    if (!cfire::testing_detail::failure_expr.empty() && !cfire::testing_detail::failure_msg.empty()) {");
                writeln("        fail_detail = cfire::testing_detail::failure_expr + \" (\" + cfire::testing_detail::failure_msg + \")\";");
                writeln("    } else if (!cfire::testing_detail::failure_expr.empty()) {");
                writeln("        fail_detail = cfire::testing_detail::failure_expr;");
                writeln("    } else {");
                writeln("        fail_detail = cfire::testing_detail::failure_msg;");
                writeln("    }");
                writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
                writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", t_name));
                writeln("}");
                --indent_;
                writeln("}");
                --indent_;
                writeln("}");
            }
            for (std::size_t j = 0; j < auto_json_structs.size(); ++j) {
                const auto& [sname, sline] = auto_json_structs[j];
                std::string auto_name = std::format("[auto] json round-trip: {}", sname);
                writeln("if (!bench_mode) {");
                ++indent_;
                writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", auto_name));
                ++indent_;
                writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", auto_name));
                writeln("cfire::testing_detail::current_test_failed = false;");
                writeln("cfire::testing_detail::failure_file.clear();");
                writeln("cfire::testing_detail::failure_line = 0;");
                writeln("cfire::testing_detail::failure_expr.clear();");
                writeln("cfire::testing_detail::failure_msg.clear();");
                writeln("auto start_t = std::chrono::steady_clock::now();");
                writeln("if (setjmp(cfire::testing_detail::test_env) == 0) {");
                writeln(std::format("    __cfire_auto_json_{}(fuzz_seed);", j));
                writeln("}");
                writeln("auto end_t = std::chrono::steady_clock::now();");
                writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
                writeln("if (!cfire::testing_detail::current_test_failed) {");
                writeln("    passed_count++;");
                writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", auto_name));
                writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", auto_name));
                writeln("} else {");
                writeln("    failed_count++;");
                writeln("    std::string fail_detail = cfire::testing_detail::failure_expr + \" (\" + cfire::testing_detail::failure_msg + \")\";");
                writeln("    if (!json_mode) std::println(\"--- FAIL: " + auto_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
                writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", auto_name));
                writeln("}");
                --indent_;
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
            writeln("    std::print(\"], \\\"benchmarks\\\": [\");");
            writeln("    for (std::size_t b = 0; b < bench_records.size(); ++b) {");
            writeln("        if (b > 0) std::print(\", \");");
            writeln("        const auto& br = bench_records[b];");
            writeln("        std::print(\"{{\\\"name\\\": \\\"{}\\\", \\\"iters\\\": {}, \\\"min_ns\\\": {:.2f}, \\\"p50_ns\\\": {:.2f}, \\\"mean_ns\\\": {:.2f}, \\\"p99_ns\\\": {:.2f}, \\\"stddev_pct\\\": {:.2f}, \\\"ops_per_sec\\\": {:.1f}}}\", escape_json(br.name), br.total_iters, br.min_ns, br.p50_ns, br.mean_ns, br.p99_ns, br.stddev_pct, br.ops_per_sec);");
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
        ptr_vars_.clear();
        map_vars_.clear();
        fixed_array_vars_.clear();

        std::string param_sig;
        for (std::size_t i = 0; i < td.params.size(); ++i) {
            if (!param_sig.empty()) param_sig += ", ";
            param_sig += std::format("{} {}", lower_param_type("in", td.params[i].type), td.params[i].name);
            local_vars_.insert(td.params[i].name);
        }
        for (std::size_t i = 0; i < td.bench_variations.size(); ++i) {
            if (!param_sig.empty()) param_sig += ", ";
            param_sig += std::format("{} {}", lower_param_type("in", td.bench_variations[i].inferred_type), td.bench_variations[i].name);
            local_vars_.insert(td.bench_variations[i].name);
        }

        writeln(std::format("inline void {}({}) {{", cur_fn_, param_sig));
        ++indent_;
        for (const auto& st : td.body) emit_stmt(st, false);
        --indent_;
        writeln("}");
        in_test_ = false;
        local_vars_.clear();
        ptr_vars_.clear();
        map_vars_.clear();
        fixed_array_vars_.clear();

        if (td.is_bench) {
            writeln(std::format("inline void __cfire_bench_runner_{}(bool smoke_only, double budget_ms, bool json_mode, std::vector<cfire::testing_detail::BenchStats>& out_stats) {{", idx));
            ++indent_;
            std::string base_name = escape_line_path(td.name);
            if (!td.bench_variations.empty()) {
                for (std::size_t v = 0; v < td.bench_variations.size(); ++v) {
                    const auto& bv = td.bench_variations[v];
                    std::string elem_t = lower_owning_type(bv.inferred_type);
                    std::string vals;
                    for (std::size_t k = 0; k < bv.values.size(); ++k) {
                        if (k > 0) vals += ", ";
                        vals += emit_expr(bv.values[k]);
                    }
                    writeln(std::format("for (const {}& _bv_{} : std::vector<{}>{{{}}}) {{", elem_t, v, elem_t, vals));
                    ++indent_;
                }
                std::string label_fmt = base_name;
                std::string label_args;
                std::string call_args;
                for (std::size_t v = 0; v < td.bench_variations.size(); ++v) {
                    label_fmt += std::format("/{}={{}}", td.bench_variations[v].name);
                    if (!label_args.empty()) label_args += ", ";
                    label_args += std::format("cfire::testing_detail::format_fuzz_arg(_bv_{})", v);
                    if (!call_args.empty()) call_args += ", ";
                    call_args += std::format("_bv_{}", v);
                }
                writeln(std::format("std::string _bname = std::format(\"{}\", {});", label_fmt, label_args));
                writeln(std::format("auto _st = cfire::testing_detail::run_benchmark(_bname, [&]() {{ {}({}); }}, budget_ms, smoke_only);", cur_fn_, call_args));
                writeln("out_stats.push_back(_st);");
                writeln("if (!smoke_only && !json_mode) cfire::testing_detail::print_bench_row(_st);");
                for (std::size_t v = 0; v < td.bench_variations.size(); ++v) {
                    --indent_;
                    writeln("}");
                }
            } else if (!td.params.empty()) {
                for (std::size_t p = 0; p < td.params.size(); ++p) {
                    std::string elem_t = lower_owning_type(td.params[p].type);
                    if (td.params[p].type.name == "str") {
                        writeln(std::format("for (const std::string& _bp_{} : std::vector<std::string>{{\"tiny\", \"medium_payload_string_64_bytes\", std::string(512, 'x')}}) {{", p));
                    } else {
                        writeln(std::format("for ({} _bp_{} : std::vector<{}>{{8, 64, 512, 4096}}) {{", elem_t, p, elem_t));
                    }
                    ++indent_;
                }
                std::string label_fmt = base_name;
                std::string label_args;
                std::string call_args;
                for (std::size_t p = 0; p < td.params.size(); ++p) {
                    label_fmt += std::format("/{}={{}}", td.params[p].name);
                    if (!label_args.empty()) label_args += ", ";
                    label_args += std::format("cfire::testing_detail::format_fuzz_arg(_bp_{})", p);
                    if (!call_args.empty()) call_args += ", ";
                    call_args += std::format("_bp_{}", p);
                }
                writeln(std::format("std::string _bname = std::format(\"{}\", {});", label_fmt, label_args));
                writeln(std::format("auto _st = cfire::testing_detail::run_benchmark(_bname, [&]() {{ {}({}); }}, budget_ms, smoke_only);", cur_fn_, call_args));
                writeln("out_stats.push_back(_st);");
                writeln("if (!smoke_only && !json_mode) cfire::testing_detail::print_bench_row(_st);");
                for (std::size_t p = 0; p < td.params.size(); ++p) {
                    --indent_;
                    writeln("}");
                }
            } else {
                writeln(std::format("auto _st = cfire::testing_detail::run_benchmark(\"{}\", [&]() {{ {}(); }}, budget_ms, smoke_only);", base_name, cur_fn_));
                writeln("out_stats.push_back(_st);");
                writeln("if (!smoke_only && !json_mode) cfire::testing_detail::print_bench_row(_st);");
            }
            --indent_;
            writeln("}");
            return;
        }

        if (td.is_fuzz || !td.params.empty()) {
            writeln(std::format("inline void __cfire_fuzz_runner_{}(std::size_t runs, std::uint64_t seed) {{", idx));
            ++indent_;
            writeln("cfire::testing_detail::FuzzRng rng(seed);");
            writeln("for (std::size_t iter = 0; iter < runs; ++iter) {");
            ++indent_;
            std::string call_args;
            for (std::size_t p = 0; p < td.params.size(); ++p) {
                std::string p_cpp = lower_owning_type(td.params[p].type);
                writeln(std::format("{} arg_{} = cfire::testing_detail::Arbitrary<{}>::generate(rng, iter);", p_cpp, p, p_cpp));
                if (p > 0) call_args += ", ";
                call_args += std::format("arg_{}", p);
            }
            writeln("if (setjmp(cfire::testing_detail::test_env) == 0) {");
            writeln(std::format("    {}({});", cur_fn_, call_args));
            writeln("} else {");
            ++indent_;
            writeln("std::string orig_file = cfire::testing_detail::failure_file;");
            writeln("int orig_line = cfire::testing_detail::failure_line;");
            writeln("std::string orig_expr = cfire::testing_detail::failure_expr;");
            writeln("std::string orig_msg = cfire::testing_detail::failure_msg;");
            if (!td.params.empty()) {
                writeln("for (int shrink_round = 0; shrink_round < 128; ++shrink_round) {");
                ++indent_;
                writeln("bool shrunk_any = false;");
                for (std::size_t p = 0; p < td.params.size(); ++p) {
                    writeln(std::format("auto cands_{} = cfire::testing_detail::shrink_candidates(arg_{});", p, p));
                    writeln(std::format("auto saved_{} = arg_{};", p, p));
                    writeln(std::format("for (const auto& cand : cands_{}) {{", p));
                    ++indent_;
                    writeln(std::format("arg_{} = cand;", p));
                    writeln("cfire::testing_detail::current_test_failed = false;");
                    writeln("if (setjmp(cfire::testing_detail::test_env) == 0) {");
                    writeln(std::format("    {}({});", cur_fn_, call_args));
                    writeln(std::format("    arg_{} = saved_{};", p, p));
                    writeln("} else {");
                    writeln("    orig_file = cfire::testing_detail::failure_file;");
                    writeln("    orig_line = cfire::testing_detail::failure_line;");
                    writeln("    orig_expr = cfire::testing_detail::failure_expr;");
                    writeln("    orig_msg = cfire::testing_detail::failure_msg;");
                    writeln("    shrunk_any = true;");
                    writeln("    break;");
                    writeln("}");
                    --indent_;
                    writeln("}");
                }
                writeln("if (!shrunk_any) break;");
                --indent_;
                writeln("}");
            }
            std::string ce_fmt;
            std::string ce_args;
            for (std::size_t p = 0; p < td.params.size(); ++p) {
                if (p > 0) {
                    ce_fmt += ", ";
                    ce_args += ", ";
                }
                ce_fmt += std::format("{}={{}}", td.params[p].name);
                ce_args += std::format("cfire::testing_detail::format_fuzz_arg(arg_{})", p);
            }
            writeln("cfire::testing_detail::current_test_failed = true;");
            writeln("cfire::testing_detail::failure_file = orig_file;");
            writeln("cfire::testing_detail::failure_line = orig_line;");
            writeln("cfire::testing_detail::failure_expr = orig_expr;");
            if (!td.params.empty()) {
                writeln(std::format("std::string ce_str = std::format(\"{}\", {});", ce_fmt, ce_args));
                writeln("cfire::testing_detail::failure_msg = std::format(\"{} [shrunk counterexample after {} run(s): ({}) | reproduce: --seed {}]\", orig_msg, iter + 1, ce_str, seed);");
            } else {
                writeln("cfire::testing_detail::failure_msg = std::format(\"{} [failed at run {} | reproduce: --seed {}]\", orig_msg, iter + 1, seed);");
            }
            writeln("return;");
            --indent_;
            writeln("}");
            --indent_;
            writeln("}");
            --indent_;
            writeln("}");
        }
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

        bool can_emit_arbitrary = true;
        for (const auto& fld : st.fields) {
            if (fld.type.is_ptr || fld.type.is_mut_ptr || fld.type.is_borrow ||
                fld.type.is_mut_borrow || fld.type.is_fn || !fld.type.smart_wrap.empty()) {
                can_emit_arbitrary = false;
                break;
            }
        }
        if (can_emit_arbitrary) {
            writeln();
            writeln("template <>");
            writeln(std::format("struct cfire::testing_detail::Arbitrary<{}> {{", st.name));
            ++indent_;
            writeln(std::format("static auto generate(cfire::testing_detail::FuzzRng& rng, std::size_t iter) -> {} {{", st.name));
            ++indent_;
            writeln(std::format("{} out{{}};", st.name));
            for (std::size_t fi = 0; fi < st.fields.size(); ++fi) {
                writeln(std::format("out.{} = cfire::testing_detail::Arbitrary<std::decay_t<decltype(out.{})>>::generate(rng, iter + {});",
                    st.fields[fi].name, st.fields[fi].name, fi));
            }
            writeln("return out;");
            --indent_;
            writeln("}");
            --indent_;
            writeln("};");
        }

        auto it = cf_.struct_tags.find(st.name);
        if (it == cf_.struct_tags.end()) return;
        const auto& tags = it->second;

        bool has_cli = false, has_json = false, has_desc = false, has_db = false;
        for (const auto& t : tags) {
            if (!t.cli_long.empty()) has_cli = true;
            if (!t.json_key.empty()) has_json = true;
            if (t.custom_tags.contains("desc")) has_desc = true;
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

        if ((has_json || has_desc || used_core_.contains("webui")) && used_core_.contains("json")) {
            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::json::parse_struct<{}>(simdjson::ondemand::object obj) -> std::expected<{}, cfire::Error> {{", st.name, st.name));
            ++indent_;
            writeln(std::format("{} out{{}};", st.name));
            for (const auto& t : tags) {
                std::string jkey = !t.json_key.empty() ? t.json_key : (has_json ? "" : t.field_name);
                if (!jkey.empty()) {
                    if (t.field_type.is_slice && t.field_type.elem_type) {
                        std::string elem_cpp = lower_owning_type(*t.field_type.elem_type);
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_vec<{}>(obj, \"{}\"));", t.field_name, elem_cpp, jkey));
                    } else if (t.field_type.name == "str") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_str(obj, \"{}\"));", t.field_name, jkey));
                    } else if (t.field_type.name == "u64" || t.field_type.name == "usize") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_u64(obj, \"{}\"));", t.field_name, jkey));
                    } else if (t.field_type.name == "i64" || t.field_type.name == "isize") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_i64(obj, \"{}\"));", t.field_name, jkey));
                    } else if (t.field_type.name == "i32") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_i32(obj, \"{}\"));", t.field_name, jkey));
                    } else if (t.field_type.name == "f64") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_f64(obj, \"{}\"));", t.field_name, jkey));
                    } else if (t.field_type.name == "f32") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_f32(obj, \"{}\"));", t.field_name, jkey));
                    } else if (t.field_type.name == "bool") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_bool(obj, \"{}\"));", t.field_name, jkey));
                    } else if (is_local_type(t.field_type.name)) {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_obj<{}>(obj, \"{}\"));", t.field_name, lower_raw_owning_type(t.field_type), jkey));
                    }
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
                std::string jkey = !t.json_key.empty() ? t.json_key : (has_json ? "" : t.field_name);
                if (!jkey.empty()) {
                    std::string prefix = first_j ? "" : ",";
                    first_j = false;
                    if (t.field_type.is_slice || is_local_type(t.field_type.name)) {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::stringify(val.{}));", prefix, jkey, t.field_name));
                    } else if (t.field_type.name == "str") {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":\\\"{{}}\\\"\", cfire::json::escape_string(val.{}));", prefix, jkey, t.field_name));
                    } else if (t.field_type.name == "bool") {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{} ? \"true\" : \"false\");", prefix, jkey, t.field_name));
                    } else {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{});", prefix, jkey, t.field_name));
                    }
                }
            }
            writeln("out += \"}\";");
            writeln("return out;");
            --indent_;
            writeln("}");

            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::json::schema_struct<{}>() -> std::string {{", st.name));
            ++indent_;
            writeln("std::string props = \"{\";");
            writeln("std::string req = \"[\";");
            bool first_p = true;
            bool first_r = true;
            for (const auto& t : tags) {
                std::string jkey = !t.json_key.empty() ? t.json_key : (has_json ? "" : t.field_name);
                if (jkey.empty()) continue;
                std::string p_prefix = first_p ? "" : ",";
                first_p = false;
                std::string f_cpp = lower_raw_owning_type(t.field_type);
                std::string desc_str;
                if (auto dit = t.custom_tags.find("desc"); dit != t.custom_tags.end()) {
                    desc_str = dit->second;
                }
                if (!desc_str.empty()) {
                    writeln(std::format("props += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::with_description(cfire::json::schema<{}>(), R\"desc({})desc\"));",
                        p_prefix, jkey, f_cpp, desc_str));
                } else {
                    writeln(std::format("props += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::schema<{}>());",
                        p_prefix, jkey, f_cpp));
                }
                if (!t.field_type.is_optional) {
                    std::string r_prefix = first_r ? "" : ",";
                    first_r = false;
                    writeln(std::format("req += \"{}\\\"{}\\\"\";", r_prefix, jkey));
                }
            }
            writeln("props += \"}\";");
            writeln("req += \"]\";");
            writeln("return std::format(R\"({{\"type\":\"object\",\"properties\":{},\"required\":{},\"additionalProperties\":false}})\", props, req);");
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
                    if (t.field_type.is_slice && t.field_type.elem_type) {
                        if (t.field_type.elem_type->name == "f32") col_fn = "col_f32_vec";
                        else if (t.field_type.elem_type->name == "byte" || t.field_type.elem_type->name == "u8") col_fn = "col_bytes";
                    } else if (t.field_type.name == "u64" || t.field_type.name == "usize") col_fn = "col_u64";
                    else if (t.field_type.name == "i64" || t.field_type.name == "isize") col_fn = "col_i64";
                    else if (t.field_type.name == "i32") col_fn = "col_i32";
                    else if (t.field_type.name == "f64") col_fn = "col_f64";
                    else if (t.field_type.name == "f32") col_fn = "col_f32";
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
        map_vars_.clear();
        fixed_array_vars_.clear();
        if (fn.receiver) {
            local_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_map) map_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_array && !fn.receiver->type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(fn.receiver->type.array_len)) {
                    fixed_array_vars_[fn.receiver->name] = *n;
                }
            }
        }
        for (const auto& p : fn.params) {
            local_vars_.insert(p.name);
            if (p.type.is_map) map_vars_.insert(p.name);
            if (p.type.is_array && !p.type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(p.type.array_len)) {
                    fixed_array_vars_[p.name] = *n;
                }
            }
        }

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

    [[nodiscard]] static auto expr_references_ident(const ExprPtr& e, std::string_view ident) -> bool {
        if (!e || ident.empty()) return false;
        return std::visit([&](const auto& ex) -> bool {
            using T = std::decay_t<decltype(ex)>;
            if constexpr (std::is_same_v<T, IdentExpr>) {
                return ex.name == ident;
            } else if constexpr (std::is_same_v<T, LiteralExpr>) {
                if (ex.kind == TokenKind::String && ex.value.contains('{')) {
                    return ex.value.find(ident) != std::string::npos;
                }
                return false;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return expr_references_ident(ex.operand, ident);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return expr_references_ident(ex.left, ident) || expr_references_ident(ex.right, ident);
            } else if constexpr (std::is_same_v<T, MemberExpr>) {
                return ex.field == ident || expr_references_ident(ex.object, ident);
            } else if constexpr (std::is_same_v<T, IndexExpr>) {
                return expr_references_ident(ex.object, ident) || expr_references_ident(ex.index, ident);
            } else if constexpr (std::is_same_v<T, SliceExpr>) {
                return expr_references_ident(ex.object, ident) ||
                       expr_references_ident(ex.start, ident) ||
                       expr_references_ident(ex.end, ident);
            } else if constexpr (std::is_same_v<T, StructLitExpr>) {
                for (const auto& f : ex.fields) {
                    if (expr_references_ident(f.value, ident)) return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, ArrayLitExpr> || std::is_same_v<T, TupleExpr>) {
                for (const auto& el : ex.elements) {
                    if (expr_references_ident(el, ident)) return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, ClosureExpr>) {
                return stmts_reference_ident(ex.body, ident);
            } else if constexpr (std::is_same_v<T, OrExpr>) {
                return expr_references_ident(ex.inner, ident) ||
                       expr_references_ident(ex.fallback_val, ident) ||
                       stmts_reference_ident(ex.handler_body, ident);
            } else if constexpr (std::is_same_v<T, TryExpr>) {
                return expr_references_ident(ex.inner, ident);
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                if (expr_references_ident(ex.callee, ident)) return true;
                for (const auto& a : ex.args) {
                    if (expr_references_ident(a.value, ident)) return true;
                }
                if (ex.block_arg && stmts_reference_ident(ex.block_arg->body, ident)) return true;
                return false;
            } else if constexpr (std::is_same_v<T, MatchExpr>) {
                if (expr_references_ident(ex.target, ident)) return true;
                for (const auto& arm : ex.arms) {
                    if (stmts_reference_ident(arm.body, ident)) return true;
                }
                return false;
            }
            return false;
        }, e->node);
    }

    [[nodiscard]] static auto stmts_reference_ident(const std::vector<StmtPtr>& stmts, std::string_view ident) -> bool {
        if (ident.empty()) return false;
        for (const auto& s : stmts) {
            if (!s) continue;
            bool hit = std::visit([&](const auto& st) -> bool {
                using T = std::decay_t<decltype(st)>;
                if constexpr (std::is_same_v<T, VarDeclStmt>) {
                    return expr_references_ident(st.init, ident);
                } else if constexpr (std::is_same_v<T, DestructureVarDeclStmt>) {
                    return expr_references_ident(st.init, ident);
                } else if constexpr (std::is_same_v<T, AssignStmt>) {
                    return expr_references_ident(st.target, ident) || expr_references_ident(st.value, ident);
                } else if constexpr (std::is_same_v<T, DestructureAssignStmt>) {
                    for (const auto& t : st.targets) {
                        if (expr_references_ident(t, ident)) return true;
                    }
                    return expr_references_ident(st.value, ident);
                } else if constexpr (std::is_same_v<T, DeferStmt>) {
                    return expr_references_ident(st.call, ident) ||
                           stmts_reference_ident(st.body, ident);
                } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                    return expr_references_ident(st.value, ident);
                } else if constexpr (std::is_same_v<T, IfStmt>) {
                    return expr_references_ident(st.cond, ident) ||
                           stmts_reference_ident(st.then_body, ident) ||
                           stmts_reference_ident(st.else_body, ident);
                } else if constexpr (std::is_same_v<T, ForStmt>) {
                    return expr_references_ident(st.iter_expr, ident) ||
                           expr_references_ident(st.range_end, ident) ||
                           stmts_reference_ident(st.body, ident);
                } else if constexpr (std::is_same_v<T, MatchStmt>) {
                    if (expr_references_ident(st.target, ident)) return true;
                    for (const auto& arm : st.arms) {
                        if (stmts_reference_ident(arm.body, ident)) return true;
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, AsyncScopeStmt>) {
                    return expr_references_ident(st.workers, ident) ||
                           stmts_reference_ident(st.body, ident);
                } else if constexpr (std::is_same_v<T, CppBlockStmt>) {
                    return st.raw_code.find(ident) != std::string::npos;
                } else if constexpr (std::is_same_v<T, ExprStmt>) {
                    return expr_references_ident(st.expr, ident);
                }
                return false;
            }, s->node);
            if (hit) return true;
        }
        return false;
    }

    void hoist_subexpr_tries(const ExprPtr& e, bool in_main, int stmt_line, bool is_top_handled = false) {
        if (!e) return;
        std::visit([&](const auto& ex) {
            using T = std::decay_t<decltype(ex)>;
            if constexpr (std::is_same_v<T, UnaryExpr>) {
                hoist_subexpr_tries(ex.operand, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                hoist_subexpr_tries(ex.left, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.right, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, MemberExpr>) {
                hoist_subexpr_tries(ex.object, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, IndexExpr>) {
                hoist_subexpr_tries(ex.object, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.index, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, SliceExpr>) {
                hoist_subexpr_tries(ex.object, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.start, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.end, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, StructLitExpr>) {
                for (const auto& f : ex.fields) {
                    hoist_subexpr_tries(f.value, in_main, stmt_line, false);
                }
            } else if constexpr (std::is_same_v<T, ArrayLitExpr> || std::is_same_v<T, TupleExpr>) {
                for (const auto& el : ex.elements) {
                    hoist_subexpr_tries(el, in_main, stmt_line, false);
                }
            } else if constexpr (std::is_same_v<T, OrExpr>) {
                hoist_subexpr_tries(ex.inner, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.fallback_val, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                hoist_subexpr_tries(ex.callee, in_main, stmt_line, false);
                for (const auto& a : ex.args) {
                    hoist_subexpr_tries(a.value, in_main, stmt_line, false);
                }
            } else if constexpr (std::is_same_v<T, TryExpr>) {
                hoist_subexpr_tries(ex.inner, in_main, stmt_line, false);
                if (!is_top_handled) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_try_{}", id);
                    writeln(std::format("auto {} = {};", tmp, emit_expr(ex.inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    if (in_main) {
                        writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", tmp));
                        writeln("return 1;");
                    } else if (in_test_) {
                        writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"try\");",
                            tmp, escape_line_path(cf_.ast.path), stmt_line));
                        writeln("return;");
                    } else {
                        writeln(std::format("return std::unexpected(std::move({}).error());", tmp));
                    }
                    --indent_;
                    writeln("}");
                    hoisted_tries_[e] = std::format("cfire::unwrap_val(std::move({}))", tmp);
                }
            }
        }, e->node);
    }

    void emit_stmt(const StmtPtr& s, bool in_main) {
        if (!s) return;
        if (s->line > 0) emit_line_directive(s->line);
        std::visit([&](const auto& st) {
            using T = std::decay_t<decltype(st)>;
            if constexpr (std::is_same_v<T, VarDeclStmt>) {
                if (st.init) {
                    bool top_try = std::holds_alternative<TryExpr>(st.init->node);
                    hoist_subexpr_tries(st.init, in_main, s->line, top_try);
                }
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
                if (st.type) {
                    if (st.type->is_map) map_vars_.insert(st.name);
                    if (st.type->is_array && !st.type->array_len.empty()) {
                        if (auto n = cfire::detail::parse_int<std::size_t>(st.type->array_len)) {
                            fixed_array_vars_[st.name] = *n;
                        }
                    }
                }
                if (st.init) {
                    if (const auto* call_ex = std::get_if<CallExpr>(&st.init->node)) {
                        if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                            cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].is_map) {
                            map_vars_.insert(st.name);
                        }
                    }
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
                    if (st.name == "_") {
                        writeln(std::format("(void)({});", init_val));
                    } else {
                        writeln(std::format("{} {} = {};", cpp_type, st.name, init_val));
                    }
                } else {
                    writeln(std::format("{} {}{{}};", cpp_type, st.name));
                }
            } else if constexpr (std::is_same_v<T, AssignStmt>) {
                hoist_subexpr_tries(st.target, in_main, s->line, false);
                bool top_try = st.value && std::holds_alternative<TryExpr>(st.value->node);
                hoist_subexpr_tries(st.value, in_main, s->line, top_try);
                std::string lhs = emit_expr(st.target);
                if (lhs == "_") {
                    if (const auto* try_ex = std::get_if<TryExpr>(&st.value->node)) {
                        if (in_main || in_test_) {
                            hoist_subexpr_tries(st.value, in_main, s->line, false);
                            return;
                        }
                        writeln(std::format("CFIRE_TRY({});", emit_expr(try_ex->inner)));
                        return;
                    }
                    writeln(std::format("(void){};", emit_expr(st.value)));
                    return;
                }
                if (top_try) {
                    hoist_subexpr_tries(st.value, in_main, s->line, false);
                }
                std::string rhs = emit_expr(st.value);
                if (rhs == "std::nullopt") {
                    if (const auto* id = std::get_if<IdentExpr>(&st.target->node); id && ptr_vars_.contains(id->name)) {
                        rhs = "nullptr";
                    }
                }
                writeln(std::format("{} {} {};", lhs, st.op, rhs));
            } else if constexpr (std::is_same_v<T, DeferStmt>) {
                if (!st.body.empty()) {
                    if (!active_async_scopes_.empty()) {
                        writeln(std::format("{}.defer([&]() {{", active_async_scopes_.back()));
                        ++indent_;
                        for (const auto& b : st.body) emit_stmt(b, false);
                        --indent_;
                        writeln("});");
                    } else {
                        ++defer_count_;
                        writeln(std::format("auto _defer_{} = cfire::scope_exit([&]() {{", defer_count_));
                        ++indent_;
                        for (const auto& b : st.body) emit_stmt(b, false);
                        --indent_;
                        writeln("});");
                    }
                } else if (!active_async_scopes_.empty()) {
                    writeln(std::format("{}.defer([&] {{ {}; }});", active_async_scopes_.back(), emit_expr(st.call)));
                } else {
                    ++defer_count_;
                    writeln(std::format("auto _defer_{} = cfire::scope_exit([&] {{ {}; }});", defer_count_, emit_expr(st.call)));
                }
            } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                hoist_subexpr_tries(st.value, in_main, s->line, false);
                if (!st.value) {
                    writeln(in_main ? "return 0;" : (in_fallible_void_ ? "return {};" : "return;"));
                } else {
                    writeln(std::format("return {};", emit_expr(st.value)));
                }
            } else if constexpr (std::is_same_v<T, IfStmt>) {
                hoist_subexpr_tries(st.cond, in_main, s->line, false);
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
                    hoist_subexpr_tries(st.iter_expr, in_main, s->line, false);
                    if (st.range_end) {
                        hoist_subexpr_tries(st.range_end, in_main, s->line, false);
                    }
                    local_vars_.insert(st.item_var);
                    if (st.range_end) {
                        std::string start_s = emit_expr(st.iter_expr);
                        std::string end_s = emit_expr(st.range_end);
                        std::string cmp_op = st.range_inclusive ? "<=" : "<";
                        writeln(std::format(
                            "for (auto {0} = static_cast<std::common_type_t<decltype({1}), decltype({2})>>({1}), _range_end_{0} = static_cast<std::common_type_t<decltype({1}), decltype({2})>>({2}); {0} {3} _range_end_{0}; ++{0}) {{",
                            st.item_var, start_s, end_s, cmp_op));
                        ++indent_;
                        for (const auto& b : st.body) emit_stmt(b, in_main);
                        --indent_;
                        writeln("}");
                    } else if (!st.index_var.empty()) {
                        local_vars_.insert(st.index_var);
                        std::string col = emit_expr(st.iter_expr);
                        loop_index_to_col_[st.index_var] = col;
                        writeln(std::format("for (std::size_t {0} = 0; {0} < {1}.size(); ++{0}) {{", st.index_var, col));
                        ++indent_;
                        writeln(std::format("const auto& {} = {}[{}];", st.item_var, col, st.index_var));
                        for (const auto& b : st.body) emit_stmt(b, in_main);
                        --indent_;
                        writeln("}");
                        loop_index_to_col_.erase(st.index_var);
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
                hoist_subexpr_tries(st.target, in_main, s->line, false);
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
                bool top_try = st.expr && std::holds_alternative<TryExpr>(st.expr->node);
                hoist_subexpr_tries(st.expr, in_main, s->line, top_try);
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
                bool top_try = st.init && std::holds_alternative<TryExpr>(st.init->node);
                hoist_subexpr_tries(st.init, in_main, s->line, top_try);
                std::string names;
                for (std::size_t i = 0; i < st.bindings.size(); ++i) {
                    if (i > 0) names += ", ";
                    names += st.bindings[i].first;
                    local_vars_.insert(st.bindings[i].first);
                }
                if (const auto* try_ex = std::get_if<TryExpr>(&st.init->node)) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_try_{}", id);
                    writeln(std::format("auto {} = {};", tmp, emit_expr(try_ex->inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    if (in_main) {
                        writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", tmp));
                        writeln("return 1;");
                    } else if (in_test_) {
                        writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"try\");",
                            tmp, escape_line_path(cf_.ast.path), s->line));
                        writeln("return;");
                    } else {
                        writeln(std::format("return std::unexpected(std::move({}).error());", tmp));
                    }
                    --indent_;
                    writeln("}");
                    writeln(std::format("auto [{}] = std::move(*{});", names, tmp));
                    return;
                }
                writeln(std::format("auto [{}] = {};", names, emit_expr(st.init)));
            } else if constexpr (std::is_same_v<T, DestructureAssignStmt>) {
                for (const auto& t : st.targets) {
                    hoist_subexpr_tries(t, in_main, s->line, false);
                }
                hoist_subexpr_tries(st.value, in_main, s->line, false);
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
                        if (arm.slice_head[i] != "_" && stmts_reference_ident(arm.body, arm.slice_head[i])) {
                            local_vars_.insert(arm.slice_head[i]);
                            writeln(std::format("const auto& {} = _target[{}];", arm.slice_head[i], i));
                        }
                    }

                    // Bind rest slice
                    if (arm.slice_has_rest && !arm.slice_rest.empty() && arm.slice_rest != "_" &&
                        stmts_reference_ident(arm.body, arm.slice_rest)) {
                        local_vars_.insert(arm.slice_rest);
                        std::string rest_start = std::to_string(head_count);
                        std::string rest_end = tail_count == 0 ? "_target.size()" : std::format("(_target.size() - {})", tail_count);
                        writeln(std::format("auto {} = cfire::slice(_target, {}, {});", arm.slice_rest, rest_start, rest_end));
                    }

                    // Bind tail elements
                    for (std::size_t i = 0; i < tail_count; ++i) {
                        if (arm.slice_tail[i] != "_" && stmts_reference_ident(arm.body, arm.slice_tail[i])) {
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
            sub.ptr_vars_ = ptr_vars_;
            sub.map_vars_ = map_vars_;
            sub.fixed_array_vars_ = fixed_array_vars_;
            sub.in_main_ = in_main_;
            sub.emit_stmt(body[i], false);
            sb << sub.buf_.str();
            local_vars_ = sub.local_vars_;
            ptr_vars_ = sub.ptr_vars_;
            map_vars_ = sub.map_vars_;
            fixed_array_vars_ = sub.fixed_array_vars_;
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
                        if (arm.slice_head[i] != "_" && stmts_reference_ident(arm.body, arm.slice_head[i])) {
                            for (int j = 0; j < indent_; ++j) sb << "    ";
                            sb << "const auto& " << arm.slice_head[i] << " = _target[" << i << "];\n";
                        }
                    }
                    if (arm.slice_has_rest && !arm.slice_rest.empty() && arm.slice_rest != "_" &&
                        stmts_reference_ident(arm.body, arm.slice_rest)) {
                        std::string rest_start = std::to_string(head_count);
                        std::string rest_end = tail_count == 0 ? "_target.size()" : std::format("(_target.size() - {})", tail_count);
                        for (int j = 0; j < indent_; ++j) sb << "    ";
                        sb << "auto " << arm.slice_rest << " = cfire::slice(_target, " << rest_start << ", " << rest_end << ");\n";
                    }
                    for (std::size_t i = 0; i < tail_count; ++i) {
                        if (arm.slice_tail[i] != "_" && stmts_reference_ident(arm.body, arm.slice_tail[i])) {
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

    [[nodiscard]] static auto expr_root_ident(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return expr_root_ident(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return expr_root_ident(idx->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return expr_root_ident(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return expr_root_ident(un->operand);
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            auto l = expr_root_ident(bin->left);
            return !l.empty() ? l : expr_root_ident(bin->right);
        }
        return "";
    }

    [[nodiscard]] auto extract_interpolation(const LiteralExpr& lit, int line) -> std::optional<InterpolationResult> {
        if (lit.kind != TokenKind::String || !lit.value.contains('{')) return std::nullopt;
        auto placeholders = scan_interpolation_placeholders(lit.value);
        if (placeholders.empty()) return std::nullopt;

        std::vector<ExprSnippetResult> parsed_exprs;
        parsed_exprs.reserve(placeholders.size());
        for (const auto& ph : placeholders) {
            auto parsed = parse_expr_snippet(cf_.ast.path, ph.expr_text, line);
            if (!parsed) return std::nullopt;
            std::string root = expr_root_ident(parsed->expr);
            if (root.empty() || !is_ident_in_scope(root)) {
                return std::nullopt;
            }
            parsed_exprs.push_back(std::move(*parsed));
        }

        InterpolationResult res;
        std::size_t cursor = 0;
        for (std::size_t i = 0; i < placeholders.size(); ++i) {
            res.fmt_str.append(lit.value.substr(cursor, placeholders[i].start_pos - cursor));
            res.fmt_str += "{}";
            cursor = placeholders[i].end_pos + 1;
            res.args.push_back(emit_expr(parsed_exprs[i].expr));
        }
        res.fmt_str.append(lit.value.substr(cursor));
        return res;
    }

    [[nodiscard]] auto emit_expr(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (auto it = hoisted_tries_.find(e); it != hoisted_tries_.end()) {
            return it->second;
        }
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
                        id->name == "audio" || id->name == "ai" ||
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
                    obj.ends_with("::ServerContext") || obj.ends_with("::Waveform") || obj.ends_with("::SfxPreset") || obj.ends_with("::Provider") ||
                    obj == "Response" || obj == "Request" || obj == "Router" || obj == "HttpServer" || obj == "ServerContext" ||
                    obj == "Waveform" || obj == "SfxPreset" || obj == "Provider") {
                    return obj + "::" + ex.field;
                }
                if (ex.field == "len") return obj + ".size()";
                if (ex.field == "empty") return obj + ".empty";
                return obj + "." + ex.field;
            } else if constexpr (std::is_same_v<T, IndexExpr>) {
                bool use_raw_index = false;
                if (ex.object) {
                    if (const auto* obj_id = std::get_if<IdentExpr>(&ex.object->node)) {
                        if (map_vars_.contains(obj_id->name)) {
                            use_raw_index = true;
                        } else if (fixed_array_vars_.contains(obj_id->name) && ex.index) {
                            if (const auto* lit = std::get_if<LiteralExpr>(&ex.index->node); lit && lit->kind == TokenKind::Int) {
                                use_raw_index = true;
                            }
                        }
                        if (!use_raw_index && ex.index) {
                            if (const auto* idx_id = std::get_if<IdentExpr>(&ex.index->node)) {
                                if (auto it = loop_index_to_col_.find(idx_id->name); it != loop_index_to_col_.end() && it->second == obj_id->name) {
                                    use_raw_index = true;
                                }
                            }
                        }
                    }
                }
                if (use_raw_index) {
                    return std::format("{}[{}]", emit_expr(ex.object), emit_expr(ex.index));
                }
                return std::format(
                    "cfire::index_at({}, {}, \"{}\", {})",
                    emit_expr(ex.object),
                    emit_expr(ex.index),
                    escape_line_path(source_path_),
                    e->line
                );
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
                return std::format("([&]() -> decltype(auto) {{\n"
                                   "    auto&& _res = ({0});\n"
                                   "    if (!_res.has_value()) {{\n"
                                   "        std::println(stderr, \"Error: {{}}\", _res.error().message());\n"
                                   "        std::abort();\n"
                                   "    }}\n"
                                   "    return cfire::unwrap_val(std::forward<decltype(_res)>(_res));\n"
                                   "}})()", inner);
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
            if ((id->name == "assert_eq" || id->name == "assert_ne") && call.args.size() >= 2) {
                std::string op_str = (id->name == "assert_ne") ? "!=" : "==";
                std::string default_msg = (id->name == "assert_ne") ? "\"assert_ne failed\"" : "\"assert_eq failed\"";
                std::string lhs = emit_call_arg(call.args[0]);
                std::string rhs = emit_call_arg(call.args[1]);
                std::string cond = std::format("(({}) {} ({}))", lhs, op_str, rhs);
                std::string msg = (call.args.size() >= 3) ? emit_call_arg(call.args[2]) : default_msg;
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
                return std::format(
                    "cfire::assert_binop_at({}, {}, [](const auto& _l, const auto& _r) {{ return _l {} _r; }}, \"{}\", {}, \"{}\", {}, \"{}\")",
                    lhs, rhs, op_str, op_str, msg, escaped_file, line, escaped_expr);
            }
            if (id->name == "assert" && !call.args.empty()) {
                std::string msg = (call.args.size() >= 2) ? emit_call_arg(call.args[1]) : "\"assertion failed\"";
                int line = call.args[0].value ? call.args[0].value->line : 0;
                std::string file_str = cf_.ast.path;
                std::string escaped_file;
                for (char ch : file_str) {
                    if (ch == '\\') escaped_file += "\\\\";
                    else if (ch == '"') escaped_file += "\\\"";
                    else escaped_file += ch;
                }
                if (call.args[0].value) {
                    if (const auto* bin = std::get_if<BinaryExpr>(&call.args[0].value->node)) {
                        if (bin->op == "==" || bin->op == "!=" ||
                            bin->op == "<"  || bin->op == "<=" ||
                            bin->op == ">"  || bin->op == ">=") {
                            // Do not use assert_binop_at if either side is `none` (std::nullopt) where optional/pointer comparison may require contextual conversion
                            const auto* l_lit = bin->left ? std::get_if<LiteralExpr>(&bin->left->node) : nullptr;
                            const auto* r_lit = bin->right ? std::get_if<LiteralExpr>(&bin->right->node) : nullptr;
                            bool has_none = (l_lit && l_lit->kind == TokenKind::KwNone) ||
                                            (r_lit && r_lit->kind == TokenKind::KwNone);
                            if (!has_none) {
                                std::string lhs = emit_expr(bin->left);
                                std::string rhs = emit_expr(bin->right);
                                std::string expr_str = std::format("({} {} {})", lhs, bin->op, rhs);
                                std::string escaped_expr;
                                for (char ch : expr_str) {
                                    if (ch == '\\') escaped_expr += "\\\\";
                                    else if (ch == '"') escaped_expr += "\\\"";
                                    else escaped_expr += ch;
                                }
                                return std::format(
                                    "cfire::assert_binop_at({}, {}, [](const auto& _l, const auto& _r) {{ return _l {} _r; }}, \"{}\", {}, \"{}\", {}, \"{}\")",
                                    lhs, rhs, bin->op, bin->op, msg, escaped_file, line, escaped_expr);
                            }
                        }
                    }
                }
                std::string cond = emit_call_arg(call.args[0]);
                std::string expr_str = cond;
                std::string escaped_expr;
                for (char ch : expr_str) {
                    if (ch == '\\') escaped_expr += "\\\\";
                    else if (ch == '"') escaped_expr += "\\\"";
                    else escaped_expr += ch;
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
            if (id->name == "black_box" && call.args.size() == 1) {
                return std::format("cfire::black_box({})", emit_call_arg(call.args[0]));
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
                if (call.block_arg) {
                    args += ", " + emit_closure(*call.block_arg);
                }
                return std::format("cfire_method_{}({})", mem->field, args);
            }
            if (auto owner_it = cf_.method_owner.find(mem->field); owner_it != cf_.method_owner.end()) {
                std::string args = emit_expr(mem->object);
                for (const auto& a : call.args) {
                    args += ", " + emit_call_arg(a);
                }
                if (call.block_arg) {
                    args += ", " + emit_closure(*call.block_arg);
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
                if (call.block_arg) {
                    if (!args.empty()) args += ", ";
                    args += emit_closure(*call.block_arg);
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
                sub.ptr_vars_ = ptr_vars_;
                sub.map_vars_ = map_vars_;
                sub.fixed_array_vars_ = fixed_array_vars_;
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
                sub.ptr_vars_ = ptr_vars_;
                sub.map_vars_ = map_vars_;
                sub.fixed_array_vars_ = fixed_array_vars_;
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

    [[nodiscard]] auto infer_closure_expr_cpp_type(
        const ExprPtr& e,
        const std::unordered_map<std::string, std::string>& local_types
    ) const -> std::string {
        if (!e) return "";
        if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
            return sl->type_name;
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return "std::string";
            if (lit->kind == TokenKind::Float) return "double";
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return "bool";
            return "std::int64_t";
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = local_types.find(id->name); it != local_types.end()) {
                return it->second;
            }
        }
        if (const auto* c = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&c->callee->node)) {
                if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end()) {
                    TypeRef rt = fit->second.return_type;
                    rt.is_fallible = false;
                    return lower_owning_type(rt);
                }
            }
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return infer_closure_expr_cpp_type(tr->inner, local_types);
        }
        return "";
    }

    auto analyze_closure_returns(
        const std::vector<StmtPtr>& stmts,
        std::unordered_map<std::string, std::string>& local_types,
        bool& has_fallible,
        bool& has_val_ret,
        std::string& val_ret_cpp
    ) const -> void {
        for (const auto& s : stmts) {
            if (!s) continue;
            if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
                if (vd->init && std::holds_alternative<TryExpr>(vd->init->node)) has_fallible = true;
                if (vd->type) {
                    local_types[vd->name] = lower_owning_type(*vd->type);
                } else if (vd->init) {
                    std::string t = infer_closure_expr_cpp_type(vd->init, local_types);
                    if (!t.empty()) local_types[vd->name] = t;
                }
            } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
                if (es->expr && std::holds_alternative<TryExpr>(es->expr->node)) has_fallible = true;
            } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
                if (rs->value) {
                    bool is_err_call = false;
                    if (const auto* c = std::get_if<CallExpr>(&rs->value->node)) {
                        if (const auto* cid = std::get_if<IdentExpr>(&c->callee->node)) {
                            if (cid->name == "error" || cid->name == "Err") {
                                is_err_call = true;
                                has_fallible = true;
                            }
                        }
                    }
                    if (!is_err_call) {
                        has_val_ret = true;
                        if (val_ret_cpp.empty()) {
                            val_ret_cpp = infer_closure_expr_cpp_type(rs->value, local_types);
                        }
                    }
                }
            } else if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
                analyze_closure_returns(ifs->then_body, local_types, has_fallible, has_val_ret, val_ret_cpp);
                analyze_closure_returns(ifs->else_body, local_types, has_fallible, has_val_ret, val_ret_cpp);
            } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
                analyze_closure_returns(fs->body, local_types, has_fallible, has_val_ret, val_ret_cpp);
            } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
                for (const auto& arm : ms->arms) {
                    analyze_closure_returns(arm.body, local_types, has_fallible, has_val_ret, val_ret_cpp);
                }
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
            std::unordered_map<std::string, std::string> cl_local_types;
            bool has_fallible = false;
            bool has_val_ret = false;
            std::string val_ret_cpp;
            analyze_closure_returns(cl.body, cl_local_types, has_fallible, has_val_ret, val_ret_cpp);
            if (has_fallible && !has_val_ret) {
                sb << ") -> std::expected<void, cfire::Error> {\n";
                is_fallible_void = true;
            } else if (has_fallible && !val_ret_cpp.empty()) {
                sb << ") -> std::expected<" << val_ret_cpp << ", cfire::Error> {\n";
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
        sub.ptr_vars_ = ptr_vars_;
        sub.map_vars_ = map_vars_;
        sub.fixed_array_vars_ = fixed_array_vars_;
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
        if (tr.is_fn) {
            std::string ret_s = tr.fn_ret ? lower_return_type(*tr.fn_ret) : "void";
            std::string params_s;
            for (std::size_t i = 0; i < tr.fn_params.size(); ++i) {
                if (i > 0) params_s += ", ";
                params_s += lower_param_type("in", tr.fn_params[i]);
            }
            return std::format("std::function<{}({})>", ret_s, params_s);
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
        if (!is_local_type(tr.name)) {
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
            if (tr.name == "audio::Wave") return "cfire::audio::Wave";
            if (tr.name == "audio::Sound") return "cfire::audio::Sound";
            if (tr.name == "audio::Stream") return "cfire::audio::Stream";
            if (tr.name == "audio::Waveform") return "cfire::audio::Waveform";
            if (tr.name == "audio::SfxPreset") return "cfire::audio::SfxPreset";
            if (tr.name == "http::SseEvent" || tr.name == "SseEvent") return "cfire::http::SseEvent";
            if (tr.name == "ai::Provider") return "cfire::ai::Provider";
            if (tr.name == "ai::Client") return "cfire::ai::Client";
            if (tr.name == "ai::Message") return "cfire::ai::Message";
            if (tr.name == "ai::Tool") return "cfire::ai::Tool";
            if (tr.name == "ai::ToolCall") return "cfire::ai::ToolCall";
            if (tr.name == "ai::ChatResponse") return "cfire::ai::ChatResponse";
            if (tr.name == "ai::McpServer") return "cfire::ai::McpServer";
            if (tr.name == "webui::App" || (tr.name == "App" && used_core_.contains("webui"))) return "cfire::webui::App";
        }
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
