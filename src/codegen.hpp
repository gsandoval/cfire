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
    bool in_ptr_return_ = false;
    std::string cur_fn_raw_return_type_{};
    std::string source_path_{};
    int last_source_line_ = -1;
    std::ostringstream buf_;
    int indent_ = 0;
    std::string cur_fn_{};
    std::string cur_pkg_{};
    std::unordered_set<std::string> colliding_fn_names_{};
    std::unordered_set<std::string> colliding_gv_names_{};
    int defer_count_ = 0;
    int try_count_ = 0;
    std::unordered_set<std::string> used_core_{};
    std::unordered_map<std::string, std::string> used_vendor_{};
    std::vector<std::string> active_async_scopes_{};
    std::unordered_set<std::string> local_vars_{};
    std::unordered_set<std::string> ptr_vars_{};
    std::unordered_set<std::string> ptr_field_names_{};
    std::unordered_set<std::string> optional_vars_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, std::size_t> fixed_array_vars_{};
    std::unordered_map<std::string, std::string> loop_index_to_col_{};
    std::unordered_map<const Expr*, std::string> hoisted_tries_{};

    [[nodiscard]] static auto sanitize_cpp_ident(std::string_view id) -> std::string {
        static const std::unordered_set<std::string_view> kCppReserved = {
            "short", "long", "signed", "unsigned", "class", "template", "operator",
            "export", "delete", "new", "private", "protected", "public", "virtual",
            "override", "final", "typename", "namespace", "using", "this", "throw",
            "catch", "union", "goto", "register", "volatile", "friend", "explicit",
            "mutable", "inline", "constexpr", "consteval", "constinit", "co_await",
            "co_return", "co_yield", "concept", "requires", "stdout", "stderr", "stdin"
        };
        if (kCppReserved.contains(id)) {
            return std::string(id) + "_cf_";
        }
        return std::string(id);
    }

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
        std::unordered_map<std::string, std::size_t> fn_counts;
        for (const auto& fn : cf_.ast.functions) {
            if (!fn.receiver) fn_counts[fn.name]++;
        }
        for (const auto& [name, count] : fn_counts) {
            if (count > 1) colliding_fn_names_.insert(name);
        }
        std::unordered_map<std::string, std::size_t> gv_counts;
        for (const auto& gv : cf_.ast.global_vars) {
            gv_counts[gv.name]++;
        }
        for (const auto& [name, count] : gv_counts) {
            if (count > 1) colliding_gv_names_.insert(name);
        }
        for (const auto& st : cf_.ast.structs) {
            for (const auto& fld : st.fields) {
                if (fld.type.is_ptr || fld.type.is_mut_ptr) {
                    ptr_field_names_.insert(fld.name);
                }
            }
        }
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
            } else if (path == "core:crypto/sodium" || path == "core:sodium") {
                used_core_.insert("sodium");
            } else if (path == "core:keychain" || path == "core:os/keychain") {
                used_core_.insert("keychain");
            } else if (path == "core:net/ssh" || path == "core:ssh") {
                used_core_.insert("ssh");
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
            } else if (path == "core:crypto/sodium" || path == "core:sodium") {
                used_core_.insert("sodium");
                add_unique(link_tags, "@pkg:libsodium");
            } else if (path == "core:keychain" || path == "core:os/keychain") {
                used_core_.insert("keychain");
#if defined(__APPLE__)
                add_unique(link_tags, "-framework Security");
                add_unique(link_tags, "-framework CoreFoundation");
#endif
            } else if (path == "core:net/ssh" || path == "core:ssh") {
                used_core_.insert("ssh");
                add_unique(link_tags, "@pkg:libssh");
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

        // Forward declare all structs & enums so recursive shared_ptr/pointers work
        for (const auto& en : ast.enums) {
            writeln(std::format("struct {};", en.name));
        }
        for (const auto& st : ast.structs) {
            writeln(std::format("struct {};", st.name));
        }
        if (!ast.structs.empty() || !ast.enums.empty()) writeln();

        // Emit Structs & Enums in dependency-resolved topological order (preserving source order when independent)
        struct TypeItem {
            int line;
            bool is_enum;
            std::size_t idx;
            std::string name;
        };
        std::vector<TypeItem> items;
        for (std::size_t i = 0; i < ast.enums.size(); ++i) {
            items.push_back(TypeItem{ast.enums[i].line, true, i, ast.enums[i].name});
        }
        for (std::size_t i = 0; i < ast.structs.size(); ++i) {
            items.push_back(TypeItem{ast.structs[i].line, false, i, ast.structs[i].name});
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            return a.line < b.line;
        });

        std::unordered_map<std::string, std::size_t> name_to_item;
        for (std::size_t i = 0; i < items.size(); ++i) {
            name_to_item[items[i].name] = i;
        }
        auto collect_type_deps = [&](const auto& self, const TypeRef& tr, std::vector<std::size_t>& deps) -> void {
            if (tr.is_ptr || tr.is_mut_ptr || !tr.smart_wrap.empty()) {
                return;
            }
            std::string clean = tr.name;
            if (auto dot_pos = clean.rfind('.'); dot_pos != std::string::npos) clean = clean.substr(dot_pos + 1);
            if (auto col_pos = clean.rfind("::"); col_pos != std::string::npos) clean = clean.substr(col_pos + 2);
            if (auto it = name_to_item.find(clean); it != name_to_item.end()) {
                deps.push_back(it->second);
            }
            if (tr.elem_type) self(self, *tr.elem_type, deps);
            if (tr.key_type) self(self, *tr.key_type, deps);
            for (const auto& tt : tr.tuple_types) self(self, tt, deps);
        };

        std::vector<int> visit_state(items.size(), 0); // 0 = unvisited, 1 = visiting, 2 = emitted
        auto emit_item_dfs = [&](const auto& self, std::size_t k) -> void {
            if (visit_state[k] != 0) return;
            visit_state[k] = 1;
            std::vector<std::size_t> deps;
            if (items[k].is_enum) {
                for (const auto& v : ast.enums[items[k].idx].variants) {
                    for (const auto& pt : v.payload_types) collect_type_deps(collect_type_deps, pt, deps);
                }
            } else {
                for (const auto& fld : ast.structs[items[k].idx].fields) {
                    collect_type_deps(collect_type_deps, fld.type, deps);
                }
            }
            for (std::size_t d : deps) {
                if (d != k && visit_state[d] == 0) {
                    self(self, d);
                }
            }
            visit_state[k] = 2;
            if (items[k].is_enum) {
                emit_enum(ast.enums[items[k].idx]);
            } else {
                emit_struct(ast.structs[items[k].idx]);
            }
            writeln();
        };

        for (std::size_t k = 0; k < items.size(); ++k) {
            emit_item_dfs(emit_item_dfs, k);
        }

        // 4. Forward Declarations for Functions & Receiver Methods (before global vars so globals can reference functions)
        std::set<std::string> emitted_method_fallbacks;
        for (const auto& fn : ast.functions) {
            if (fn.receiver && !emitted_method_fallbacks.contains(fn.name)) {
                emitted_method_fallbacks.insert(fn.name);
                writeln(std::format(
                    "template <typename Self, typename... Args> requires requires(Self&& s, Args&&... a) {{ std::forward<Self>(s).{0}(std::forward<Args>(a)...); }} inline auto cfire_method_{0}(Self&& s, Args&&... a) -> decltype(auto) {{ return std::forward<Self>(s).{0}(std::forward<Args>(a)...); }}",
                    fn.name));
                if (fn.name == "push") {
                    writeln("template <typename T, typename Arg> inline void cfire_method_push(std::vector<T>& s, Arg&& a) { cfire::vec_push(s, std::forward<Arg>(a)); }");
                } else if (fn.name == "pop") {
                    writeln("template <typename T> inline void cfire_method_pop(std::vector<T>& s) { s.pop_back(); }");
                }
            }
            emit_function_forward_decl(fn);
        }
        if (!ast.functions.empty()) writeln();

        // 5a. Emit Global / Package-level Variables
        for (const auto& gv : ast.global_vars) {
            cur_pkg_ = gv.package_name;
            std::string type_str = gv.type ? lower_owning_type(*gv.type) : "auto";
            std::string mut_str = gv.is_mut ? "" : "const ";
            std::string gname = (!gv.package_name.empty() && colliding_gv_names_.contains(gv.name))
                ? std::format("cfire_pkg_{}__{}", gv.package_name, sanitize_cpp_ident(gv.name))
                : sanitize_cpp_ident(gv.name);
            if (gv.value) {
                writeln(std::format("inline {}{} {} = {};", mut_str, type_str, gname, emit_expr(gv.value)));
            } else {
                writeln(std::format("inline {}{} {}{{}};", mut_str, type_str, gname));
            }
            cur_pkg_.clear();
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
            writeln("cfire::detail::set_process_args(argc, argv);");
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
            if (used_core_.contains("sqlite")) {
                writeln("#if defined(CFIRE_USE_SQLITE)");
                writeln("{ unsigned char _sq_warm = 0; sqlite3_randomness(1, &_sq_warm); }");
                writeln("#endif");
            }
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
                                     !stmts_reference_ident(td.body, "run_webview_test") &&
                                     !stmts_reference_ident(td.body, "keychain");
                if (check_fd_leak) {
                    for (const auto& fn : ast.functions) {
                        if (!fn.receiver && stmts_reference_ident(td.body, fn.name)) {
                            if (stmts_reference_ident(fn.body, "InitWindow") ||
                                stmts_reference_ident(fn.body, "run_webview_test") ||
                                stmts_reference_ident(fn.body, "keychain")) {
                                check_fd_leak = false;
                                break;
                            }
                        }
                    }
                }
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
            std::string fname = sanitize_cpp_ident(fld.name);
            if (fld.default_val) {
                std::string dval = emit_expr(fld.default_val);
                if ((fld.type.is_ptr || fld.type.is_mut_ptr) && dval == "std::nullopt") {
                    dval = "nullptr";
                }
                writeln(std::format("{} {} = {};", cpp_type, fname, dval));
            } else {
                writeln(std::format("{} {}{{}};", cpp_type, fname));
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
                std::string fname = sanitize_cpp_ident(st.fields[fi].name);
                writeln(std::format("out.{} = cfire::testing_detail::Arbitrary<std::decay_t<decltype(out.{})>>::generate(rng, iter + {});",
                    fname, fname, fi));
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
        (void)has_json; (void)has_desc;

        if (has_cli && used_core_.contains("cli")) {
            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::cli::try_parse<{}>(int argc, char** argv) -> std::optional<{}> {{", st.name, st.name));
            ++indent_;
            writeln(std::format("{} out{{}};", st.name));
            writeln("cfire::cli::FlagReader reader(argc, argv);");
            for (const auto& t : tags) {
                if (!t.cli_long.empty()) {
                    writeln(std::format("reader.bind(\"{}\", \"{}\", out.{});", t.cli_long, t.cli_short, sanitize_cpp_ident(t.field_name)));
                }
            }
            writeln("if (reader.has_errors()) return std::nullopt;");
            writeln("return out;");
            --indent_;
            writeln("}");

            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::cli::parse<{}>(int argc, char** argv) -> {} {{", st.name, st.name));
            ++indent_;
            writeln(std::format("{} out{{}};", st.name));
            writeln("cfire::cli::FlagReader reader(argc, argv);");
            for (const auto& t : tags) {
                if (!t.cli_long.empty()) {
                    writeln(std::format("reader.bind(\"{}\", \"{}\", out.{});", t.cli_long, t.cli_short, sanitize_cpp_ident(t.field_name)));
                }
            }
            writeln("return out;");
            --indent_;
            writeln("}");
        }

        if (can_emit_arbitrary && used_core_.contains("json")) {
            writeln();
            writeln("template <>");
            writeln(std::format("inline auto cfire::json::parse_struct<{}>(simdjson::ondemand::object obj) -> std::expected<{}, cfire::Error> {{", st.name, st.name));
            ++indent_;
            writeln("(void)obj;");
            writeln(std::format("{} out{{}};", st.name));
            for (const auto& t : tags) {
                std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
                std::string fname = sanitize_cpp_ident(t.field_name);
                if (!jkey.empty()) {
                    if (t.field_type.is_optional) {
                        std::string inner_cpp = lower_raw_owning_type(t.field_type);
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_opt<{}>(obj, \"{}\"));", fname, inner_cpp, jkey));
                    } else if (t.field_type.is_slice && t.field_type.elem_type) {
                        std::string elem_cpp = lower_owning_type(*t.field_type.elem_type);
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_vec<{}>(obj, \"{}\"));", fname, elem_cpp, jkey));
                    } else if (t.field_type.name == "str") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_str(obj, \"{}\"));", fname, jkey));
                    } else if (t.field_type.name == "u64" || t.field_type.name == "usize") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_u64(obj, \"{}\"));", fname, jkey));
                    } else if (t.field_type.name == "i64" || t.field_type.name == "isize") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_i64(obj, \"{}\"));", fname, jkey));
                    } else if (t.field_type.name == "i32") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_i32(obj, \"{}\"));", fname, jkey));
                    } else if (t.field_type.name == "f64") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_f64(obj, \"{}\"));", fname, jkey));
                    } else if (t.field_type.name == "f32") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_f32(obj, \"{}\"));", fname, jkey));
                    } else if (t.field_type.name == "bool") {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_bool(obj, \"{}\"));", fname, jkey));
                    } else if (is_local_type(t.field_type.name)) {
                        writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_obj<{}>(obj, \"{}\"));", fname, lower_raw_owning_type(t.field_type), jkey));
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
            writeln("(void)val;");
            writeln("std::string out = \"{\";");
            bool first_j = true;
            for (const auto& t : tags) {
                std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
                std::string fname = sanitize_cpp_ident(t.field_name);
                if (!jkey.empty()) {
                    std::string prefix = first_j ? "" : ",";
                    first_j = false;
                    if (t.field_type.is_optional || t.field_type.is_slice || is_local_type(t.field_type.name)) {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::stringify(val.{}));", prefix, jkey, fname));
                    } else if (t.field_type.name == "str") {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":\\\"{{}}\\\"\", cfire::json::escape_string(val.{}));", prefix, jkey, fname));
                    } else if (t.field_type.name == "bool") {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{} ? \"true\" : \"false\");", prefix, jkey, fname));
                    } else {
                        writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{});", prefix, jkey, fname));
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
                std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
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
                        else if (t.field_type.elem_type->name == "byte") col_fn = "col_bytes";
                        else if (t.field_type.elem_type->name == "u8") col_fn = "col_u8_vec";
                    } else if (t.field_type.name == "u64" || t.field_type.name == "usize") col_fn = "col_u64";
                    else if (t.field_type.name == "i64" || t.field_type.name == "isize") col_fn = "col_i64";
                    else if (t.field_type.name == "i32") col_fn = "col_i32";
                    else if (t.field_type.name == "f64") col_fn = "col_f64";
                    else if (t.field_type.name == "f32") col_fn = "col_f32";
                    else if (t.field_type.name == "bool") col_fn = "col_bool";
                    writeln(std::format(".{} = cfire::sqlite::{}(stmt, {}),", sanitize_cpp_ident(t.field_name), col_fn, col_idx++));
                }
            }
            --indent_;
            writeln("};");
            --indent_;
            writeln("}");
        }
    }

    [[nodiscard]] auto emitted_fn_symbol(const FnDecl& fn) const -> std::string {
        if (fn.receiver) return fn.receiver->type.name + "__" + fn.name;
        if (!fn.package_name.empty() && colliding_fn_names_.contains(fn.name)) {
            return std::format("cfire_pkg_{}__{}", fn.package_name, sanitize_cpp_ident(fn.name));
        }
        return sanitize_cpp_ident(fn.name);
    }

    void emit_function_forward_decl(const FnDecl& fn) {
        if (fn.name == "main" && !fn.receiver) return;
        std::string cur_fn = emitted_fn_symbol(fn);

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
            param_list += std::format("{} {}", lower_param_type(fn.receiver->intent, fn.receiver->type), sanitize_cpp_ident(fn.receiver->name));
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (!param_list.empty()) param_list += ", ";
            param_list += std::format("{} {}", lower_param_type(fn.params[i].intent, fn.params[i].type), sanitize_cpp_ident(fn.params[i].name));
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
        cur_pkg_ = fn.package_name;
        cur_fn_ = emitted_fn_symbol(fn);
        defer_count_ = 0;
        local_vars_.clear();
        ptr_vars_.clear();
        optional_vars_.clear();
        map_vars_.clear();
        fixed_array_vars_.clear();
        if (fn.receiver) {
            local_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_ptr || fn.receiver->type.is_mut_ptr) ptr_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_optional) optional_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_map) map_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_array && !fn.receiver->type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(fn.receiver->type.array_len)) {
                    fixed_array_vars_[fn.receiver->name] = *n;
                }
            }
        }
        for (const auto& p : fn.params) {
            local_vars_.insert(p.name);
            if (p.type.is_ptr || p.type.is_mut_ptr) ptr_vars_.insert(p.name);
            if (p.type.is_optional) optional_vars_.insert(p.name);
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
            in_ptr_return_ = false;
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
                writeln("cfire::detail::set_process_args(argc, argv);");
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
                writeln("cfire::detail::set_process_args(argc, argv);");
                for (const auto& st : fn.body) emit_stmt(st, true);
                writeln("return 0;");
                --indent_;
                writeln("}");
            }
            in_main_ = false;
            local_vars_.clear();
            ptr_vars_.clear();
            optional_vars_.clear();
            return;
        }
        in_main_ = false;
        in_fallible_void_ = (fn.return_type.name == "void" && fn.return_type.is_fallible);
        in_ptr_return_ = (fn.return_type.is_ptr || fn.return_type.is_mut_ptr);
        cur_fn_raw_return_type_ = (fn.return_type.is_fallible || fn.return_type.is_optional)
            ? lower_raw_owning_type(fn.return_type)
            : "";

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
            std::string rname = sanitize_cpp_ident(fn.receiver->name);
            param_list += std::format("{} {}", lower_param_type(fn.receiver->intent, fn.receiver->type), rname);
            pass_args += rname;
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            std::string pname = sanitize_cpp_ident(fn.params[i].name);
            if (!param_list.empty()) param_list += ", ";
            param_list += std::format("{} {}", lower_param_type(fn.params[i].intent, fn.params[i].type), pname);
            if (!pass_args.empty()) pass_args += ", ";
            pass_args += (fn.params[i].intent == "move") ? std::format("std::move({})", pname) : pname;
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
        in_ptr_return_ = false;
        cur_fn_raw_return_type_.clear();
        cur_pkg_.clear();
        local_vars_.clear();
        ptr_vars_.clear();
        optional_vars_.clear();

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

    [[nodiscard]] auto is_core_ns_ident(std::string_view name) const -> bool {
        return name == "fs" || name == "zstd" || name == "json" ||
               name == "sqlite" || name == "blake3" || name == "net" || name == "cli" ||
               name == "time" || name == "os" || name == "sync" || name == "str" ||
               name == "math" || name == "rand" || name == "log" || name == "regex" ||
               name == "collections" || name == "sort" || name == "unicode" ||
               name == "base64" || name == "hex" || name == "csv" ||
               name == "aes" || name == "hmac" || name == "sha256" ||
               name == "sodium" || name == "keychain" || name == "ssh" || name == "webui" ||
               name == "context" || name == "websocket" || name == "http" || name == "url" || name == "tls" ||
               name == "audio" || name == "ai" ||
               used_core_.contains(std::string(name));
    }

    [[nodiscard]] auto expr_returns_optional(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return expr_returns_optional(tr->inner);
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "readln") {
                    return true;
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    return it->second.return_type.is_optional;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (recv_id->name == "os" && (mem->field == "getenv" || mem->field == "getenv_opt")) {
                        return true;
                    }
                    if (recv_id->name == "cli" && mem->field == "try_parse") {
                        return true;
                    }
                    if (auto it = cf_.fn_sigs.find(recv_id->name + "." + mem->field); it != cf_.fn_sigs.end()) {
                        return it->second.return_type.is_optional;
                    }
                }
                if (auto oit = cf_.method_owner.find(mem->field); oit != cf_.method_owner.end()) {
                    if (auto mit = cf_.methods.find(oit->second + "::" + mem->field); mit != cf_.methods.end()) {
                        return mit->second.return_type.is_optional;
                    }
                }
                if (mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                    mem->field == "parse_f64" || mem->field == "parse_bool" ||
                    mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "getenv_opt") {
                    return true;
                }
            }
        }
        return false;
    }

    void emit_stmt(const StmtPtr& s, bool in_main);
    void emit_match_stmt(const MatchStmt& ms, bool in_main);
    void emit_match_arm_body(std::ostringstream& sb, const std::vector<StmtPtr>& body, bool is_expr);
    [[nodiscard]] auto emit_match_expr(const MatchExpr& me) -> std::string;

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
        if (is_core_ns_ident(ident)) return true;
        if (is_vendor_alias(ident)) return true;
        if (is_local_type(ident)) return true;
        if (ident == "i8" || ident == "i16" || ident == "i32" || ident == "i64" ||
            ident == "u8" || ident == "u16" || ident == "u32" || ident == "u64" ||
            ident == "usize" || ident == "isize" || ident == "f32" || ident == "f64" ||
            ident == "bool" || ident == "byte" || ident == "str") {
            return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] static auto escape_unescaped_quotes(std::string_view sv) -> std::string {
        std::string out;
        out.reserve(sv.size());
        for (std::size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\')) {
                out += "\\\"";
            } else {
                out.push_back(sv[i]);
            }
        }
        return out;
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

    [[nodiscard]] auto extract_interpolation(const LiteralExpr& lit, int line) -> std::optional<InterpolationResult>;
    [[nodiscard]] auto emit_expr(const ExprPtr& e) -> std::string;
    [[nodiscard]] auto emit_call(const CallExpr& call) -> std::string;
    [[nodiscard]] auto infer_closure_expr_cpp_type(
        const ExprPtr& e,
        const std::unordered_map<std::string, std::string>& local_types
    ) const -> std::string;
    auto analyze_closure_returns(
        const std::vector<StmtPtr>& stmts,
        std::unordered_map<std::string, std::string>& local_types,
        bool& has_fallible,
        bool& has_val_ret,
        std::string& val_ret_cpp
    ) const -> void;
    [[nodiscard]] auto emit_closure(const ClosureExpr& cl) -> std::string;
    [[nodiscard]] auto emit_call_arg(const CallArg& a) -> std::string;

    [[nodiscard]] auto lower_raw_owning_type(const TypeRef& tr) const -> std::string {
        if (tr.is_mut_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<{}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string&";
            return (tr.elem_type ? lower_owning_type(*tr.elem_type) : tr.name) + "&";
        }
        if (tr.is_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<const {}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string_view";
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
        if (tr.name == "i8") return "std::int8_t";
        if (tr.name == "i16") return "std::int16_t";
        if (tr.name == "i32") return "std::int32_t";
        if (tr.name == "i64") return "std::int64_t";
        if (tr.name == "u8") return "std::uint8_t";
        if (tr.name == "byte") return "std::byte";
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
        if (tr.is_ptr || tr.is_mut_ptr) return lower_owning_type(tr);
        if (tr.is_mut_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<{}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string&";
            return lower_owning_type(tr);
        }
        if (tr.is_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<const {}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string_view";
            return lower_owning_type(tr);
        }
        if (tr.name == "str") return "const std::string&";
        if (tr.is_slice && tr.elem_type) {
            return std::format("const std::vector<{}>&", lower_owning_type(*tr.elem_type));
        }
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


} // namespace cfire::frontend

#include "codegen/stmt.hpp"
#include "codegen/expr.hpp"

namespace cfire::frontend {

[[nodiscard]] inline auto emit_cpp(const CheckedFile& cf, bool test_mode = false) -> std::string {
    Emitter em(cf, test_mode);
    return em.emit_cpp();
}

} // namespace cfire::frontend
