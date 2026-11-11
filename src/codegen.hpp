#pragma once
// src/codegen.hpp — C++23 Emitter for cfire (.cf)

#include "module_registry.hpp"
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
        std::string out = buf_.str();
        if ((used_core_.contains("str") || out.contains("cfire::str::")) && !out.contains("#include \"core/str.hpp\"")) {
            std::string needle = "#include \"cfire_prelude.hpp\"\n";
            if (auto pos = out.find(needle); pos != std::string::npos) {
                out.insert(pos + needle.size(), "#include \"core/str.hpp\"\n");
            }
        }
        return out;
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
    std::unordered_map<std::string, std::string> core_aliases_{};
    std::vector<std::string> active_async_scopes_{};
    std::unordered_set<std::string> local_vars_{};
    std::unordered_set<std::string> ptr_vars_{};
    std::unordered_set<std::string> ptr_field_names_{};
    std::unordered_set<std::string> smart_ptr_vars_{};
    std::unordered_set<std::string> smart_ptr_field_names_{};
    std::unordered_set<std::string> optional_vars_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, std::size_t> fixed_array_vars_{};
    std::unordered_map<std::string, std::string> loop_index_to_col_{};
    std::unordered_map<std::string, std::string> var_types_{};
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

    void set_source_path(std::string_view p) {
        if (!p.empty() && p != source_path_) {
            source_path_ = std::string(p);
            last_source_line_ = -1;
        }
    }

    void emit_line_directive(int line) {
        if (line > 0 && line != last_source_line_ && !source_path_.empty()) {
            last_source_line_ = line;
            writeln(std::format("#line {} \"{}\"", line, escape_line_path(source_path_)));
        }
    }

    void reset_line_directive() {
        last_source_line_ = -1;
    }

    void emit_synthetic_line_directive() {
        if (!source_path_.empty()) {
            writeln("#line 1 \"<cfire-synthetic>\"");
            last_source_line_ = -1;
        }
    }

    [[nodiscard]] auto make_sub_emitter(bool inherit_return_context = true) const -> Emitter {
        Emitter sub(cf_, test_mode_);
        sub.indent_ = indent_;
        sub.cur_fn_ = cur_fn_;
        sub.cur_pkg_ = cur_pkg_;
        sub.source_path_ = source_path_;
        sub.last_source_line_ = last_source_line_;
        sub.colliding_fn_names_ = colliding_fn_names_;
        sub.colliding_gv_names_ = colliding_gv_names_;
        sub.used_core_ = used_core_;
        sub.used_vendor_ = used_vendor_;
        sub.core_aliases_ = core_aliases_;
        sub.local_vars_ = local_vars_;
        sub.ptr_vars_ = ptr_vars_;
        sub.ptr_field_names_ = ptr_field_names_;
        sub.smart_ptr_vars_ = smart_ptr_vars_;
        sub.smart_ptr_field_names_ = smart_ptr_field_names_;
        sub.optional_vars_ = optional_vars_;
        sub.map_vars_ = map_vars_;
        sub.fixed_array_vars_ = fixed_array_vars_;
        sub.loop_index_to_col_ = loop_index_to_col_;
        sub.var_types_ = var_types_;
        if (inherit_return_context) {
            sub.in_main_ = in_main_;
            sub.in_test_ = in_test_;
            sub.in_fallible_void_ = in_fallible_void_;
            sub.in_ptr_return_ = in_ptr_return_;
            sub.cur_fn_raw_return_type_ = cur_fn_raw_return_type_;
        }
        return sub;
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
                if (!fld.type.smart_wrap.empty()) {
                    smart_ptr_field_names_.insert(fld.name);
                }
            }
        }
        for (const auto& imp : cf_.ast.imports) {
            if (imp.is_cpp) continue;
            auto resolved = cfire::modules::resolve_import(imp.path);
            for (const auto& ck : resolved.core_keys) {
                used_core_.insert(ck);
            }
            if (resolved.is_core && !imp.alias.empty()) {
                auto colon = imp.path.rfind(':');
                auto slash = imp.path.rfind('/');
                std::size_t sep = std::string::npos;
                if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                else if (colon != std::string::npos) sep = colon;
                else sep = slash;
                std::string tail = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                core_aliases_[imp.alias] = tail;
            }
            if (resolved.is_vendor) {
                std::string alias = imp.alias.empty() ? resolved.vendor_key : imp.alias;
                used_vendor_[resolved.vendor_key] = alias;
                for (const auto& iv : resolved.implied_vendors) {
                    if (!used_vendor_.contains(iv)) {
                        used_vendor_[iv] = iv;
                    }
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
            auto resolved = cfire::modules::resolve_import(imp.path);
            for (const auto& ck : resolved.core_keys) {
                used_core_.insert(ck);
            }
            for (const auto& def : resolved.defines) {
                add_unique(defines, def);
            }
            for (const auto& lt : resolved.link_tags) {
                add_unique(link_tags, lt);
            }
            if (resolved.is_core && !imp.alias.empty()) {
                auto colon = imp.path.rfind(':');
                auto slash = imp.path.rfind('/');
                std::size_t sep = std::string::npos;
                if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                else if (colon != std::string::npos) sep = colon;
                else sep = slash;
                std::string tail = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                core_aliases_[imp.alias] = tail;
            }
            if (resolved.is_vendor) {
                std::string alias = imp.alias.empty() ? resolved.vendor_key : imp.alias;
                used_vendor_[resolved.vendor_key] = alias;
                for (const auto& iv : resolved.implied_vendors) {
                    if (!used_vendor_.contains(iv)) {
                        used_vendor_[iv] = iv;
                    }
                }
            }
        }

        bool any_json_tag = false;
        for (const auto& [sname, tags] : cf_.struct_tags) {
            for (const auto& t : tags) {
                if (!t.json_key.empty() || t.custom_tags.contains("desc") || t.custom_tags.contains("validate")) { any_json_tag = true; break; }
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
                core_headers.insert(cfire::modules::core_module_header(mod));
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
            if (!en.type_params.empty()) {
                std::string tparams;
                for (std::size_t i = 0; i < en.type_params.size(); ++i) {
                    if (i > 0) tparams += ", ";
                    if (!en.type_params[i].constraint.empty()) {
                        tparams += std::format("{} {}", en.type_params[i].constraint, en.type_params[i].name);
                    } else {
                        tparams += std::format("typename {}", en.type_params[i].name);
                    }
                }
                writeln(std::format("template <{}> struct {};", tparams, en.name));
            } else {
                writeln(std::format("struct {};", en.name));
            }
        }
        for (const auto& st : ast.structs) {
            if (!st.type_params.empty()) {
                std::string tparams;
                for (std::size_t i = 0; i < st.type_params.size(); ++i) {
                    if (i > 0) tparams += ", ";
                    if (!st.type_params[i].constraint.empty()) {
                        tparams += std::format("{} {}", st.type_params[i].constraint, st.type_params[i].name);
                    } else {
                        tparams += std::format("typename {}", st.type_params[i].name);
                    }
                }
                writeln(std::format("template <{}> struct {};", tparams, st.name));
            } else {
                writeln(std::format("struct {};", st.name));
            }
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
            for (const auto& ta : tr.type_args) self(self, ta, deps);
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
        for (const auto& mname : emitted_method_fallbacks) {
            writeln(std::format(
                "template <typename T, typename... Args> inline auto cfire_method_{0}(const std::shared_ptr<T>& s, Args&&... a) -> decltype(auto) {{ return cfire_method_{0}(*s, std::forward<Args>(a)...); }}",
                mname));
            writeln(std::format(
                "template <typename T, typename... Args> inline auto cfire_method_{0}(const std::unique_ptr<T>& s, Args&&... a) -> decltype(auto) {{ return cfire_method_{0}(*s, std::forward<Args>(a)...); }}",
                mname));
        }
        if (!ast.functions.empty()) writeln();

        // 5a. Emit Global / Package-level Variables
        for (const auto& gv : ast.global_vars) {
            set_source_path(gv.source_path);
            if (gv.line > 0) emit_line_directive(gv.line);
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
        emit_test_runner(ast, has_user_main);
    }

    void emit_test_runner(const FileAST& ast, bool has_user_main);
    void emit_test_decl(const TestDecl& td, std::size_t idx);

    void emit_interface(const InterfaceDecl& iface) {
        writeln(std::format("// Interface '{}' -> C++20 concept", iface.name));
        writeln(std::format("template <typename T>"));
        writeln(std::format("concept {} = true;", iface.name));
    }

    void emit_enum(const EnumDecl& en) {
        set_source_path(en.source_path);
        if (en.line > 0) emit_line_directive(en.line);
        if (!en.type_params.empty()) {
            std::string tparams;
            for (std::size_t i = 0; i < en.type_params.size(); ++i) {
                if (i > 0) tparams += ", ";
                if (!en.type_params[i].constraint.empty()) {
                    tparams += std::format("{} {}", en.type_params[i].constraint, en.type_params[i].name);
                } else {
                    tparams += std::format("typename {}", en.type_params[i].name);
                }
            }
            writeln(std::format("template <{}>", tparams));
        }
        writeln(std::format("struct {} {{", en.name));
        ++indent_;
        std::vector<std::string> var_types;
        for (const auto& v : en.variants) {
            writeln(std::format("struct _{} {{", v.name));
            ++indent_;
            for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                writeln(std::format("{} _{}{{}};", lower_owning_type(v.payload_types[i]), i));
            }
            writeln(std::format("bool operator==(const _{}&) const = default;", v.name));
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
        writeln(std::format("bool operator==(const {}&) const = default;", en.name));

        // Explicit integer conversion operators
        writeln("explicit operator std::int8_t() const noexcept { return static_cast<std::int8_t>(_v.index()); }");
        writeln("explicit operator std::int16_t() const noexcept { return static_cast<std::int16_t>(_v.index()); }");
        writeln("explicit operator std::int32_t() const noexcept { return static_cast<std::int32_t>(_v.index()); }");
        writeln("explicit operator std::int64_t() const noexcept { return static_cast<std::int64_t>(_v.index()); }");
        writeln("explicit operator std::uint8_t() const noexcept { return static_cast<std::uint8_t>(_v.index()); }");
        writeln("explicit operator std::uint16_t() const noexcept { return static_cast<std::uint16_t>(_v.index()); }");
        writeln("explicit operator std::uint32_t() const noexcept { return static_cast<std::uint32_t>(_v.index()); }");
        writeln("explicit operator std::uint64_t() const noexcept { return static_cast<std::uint64_t>(_v.index()); }");
        writeln("template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, std::int8_t> && !std::is_same_v<T, std::int16_t> && !std::is_same_v<T, std::int32_t> && !std::is_same_v<T, std::int64_t> && !std::is_same_v<T, std::uint8_t> && !std::is_same_v<T, std::uint16_t> && !std::is_same_v<T, std::uint32_t> && !std::is_same_v<T, std::uint64_t>)");
        writeln("explicit operator T() const noexcept { return static_cast<T>(_v.index()); }");
        writeln("[[nodiscard]] auto to_i32() const noexcept -> std::int32_t { return static_cast<std::int32_t>(_v.index()); }");
        writeln("[[nodiscard]] auto to_i64() const noexcept -> std::int64_t { return static_cast<std::int64_t>(_v.index()); }");

        // String representation
        writeln("[[nodiscard]] auto to_string() const -> std::string_view {");
        ++indent_;
        writeln("switch (_v.index()) {");
        for (std::size_t i = 0; i < en.variants.size(); ++i) {
            writeln(std::format("case {}: return \"{}\";", i, en.variants[i].name));
        }
        writeln("default: return \"\";");
        writeln("}");
        --indent_;
        writeln("}");

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

        // Static factory from integer
        writeln(std::format("[[nodiscard]] static auto from_i32(std::int32_t val) -> std::optional<{}> {{", en.name));
        ++indent_;
        writeln("switch (val) {");
        for (std::size_t i = 0; i < en.variants.size(); ++i) {
            if (en.variants[i].payload_types.empty()) {
                writeln(std::format("case {}: return {}();", i, en.variants[i].name));
            }
        }
        writeln("default: return std::nullopt;");
        writeln("}");
        --indent_;
        writeln("}");

        writeln(std::format("[[nodiscard]] static auto from_i64(std::int64_t val) -> std::optional<{}> {{", en.name));
        writeln(std::format("    return from_i32(static_cast<std::int32_t>(val));"));
        writeln("}");

        // Static factory from string
        writeln(std::format("[[nodiscard]] static auto from_string(std::string_view s) -> std::optional<{}> {{", en.name));
        ++indent_;
        for (const auto& v : en.variants) {
            if (v.payload_types.empty()) {
                writeln(std::format("if (s == \"{}\") return {}();", v.name, v.name));
            }
        }
        writeln("return std::nullopt;");
        --indent_;
        writeln("}");

        --indent_;
        writeln("};");

        writeln();
        if (!en.type_params.empty()) {
            std::string tparams;
            std::string targs;
            for (std::size_t i = 0; i < en.type_params.size(); ++i) {
                if (i > 0) {
                    tparams += ", ";
                    targs += ", ";
                }
                tparams += std::format("typename {}", en.type_params[i].name);
                targs += en.type_params[i].name;
            }
            writeln(std::format("template <{}>", tparams));
            writeln(std::format("struct std::formatter<{}<{}> > : std::formatter<std::string_view> {{", en.name, targs));
        } else {
            writeln("template <>");
            writeln(std::format("struct std::formatter<{}> : std::formatter<std::string_view> {{", en.name));
        }
        writeln(std::format("    auto format(const {}& e, std::format_context& ctx) const {{", en.name));
        writeln("        return std::formatter<std::string_view>::format(e.to_string(), ctx);");
        writeln("    }");
        writeln("};");

        emit_enum_reflection(en);
    }

    void emit_struct(const StructDecl& st) {
        set_source_path(st.source_path);
        if (st.line > 0) emit_line_directive(st.line);
        if (!st.type_params.empty()) {
            std::string tparams;
            for (std::size_t i = 0; i < st.type_params.size(); ++i) {
                if (i > 0) tparams += ", ";
                if (!st.type_params[i].constraint.empty()) {
                    tparams += std::format("{} {}", st.type_params[i].constraint, st.type_params[i].name);
                } else {
                    tparams += std::format("typename {}", st.type_params[i].name);
                }
            }
            writeln(std::format("template <{}>", tparams));
        }
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

        emit_struct_reflection(st);
    }

    void emit_struct_reflection(const StructDecl& st);
    void emit_enum_reflection(const EnumDecl& en);

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
        set_source_path(fn.source_path);
        if (fn.line > 0) emit_line_directive(fn.line);
        cur_pkg_ = fn.package_name;
        cur_fn_ = emitted_fn_symbol(fn);
        defer_count_ = 0;
        local_vars_.clear();
        ptr_vars_.clear();
        smart_ptr_vars_.clear();
        optional_vars_.clear();
        map_vars_.clear();
        fixed_array_vars_.clear();
        var_types_.clear();
        if (fn.receiver) {
            local_vars_.insert(fn.receiver->name);
            var_types_[fn.receiver->name] = fn.receiver->type.name;
            if (fn.receiver->type.is_ptr || fn.receiver->type.is_mut_ptr) ptr_vars_.insert(fn.receiver->name);
            if (!fn.receiver->type.smart_wrap.empty()) smart_ptr_vars_.insert(fn.receiver->name);
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
            var_types_[p.name] = p.type.name;
            if (p.type.is_ptr || p.type.is_mut_ptr) ptr_vars_.insert(p.name);
            if (!p.type.smart_wrap.empty()) smart_ptr_vars_.insert(p.name);
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
            smart_ptr_vars_.clear();
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
        smart_ptr_vars_.clear();
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

    void emit_try_unwrap_check(std::string_view tmp, int stmt_line, bool in_main) {
        writeln(std::format("if (!{}.has_value()) {{", tmp));
        ++indent_;
        if (in_main) {
            writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", tmp));
            writeln("return 1;");
        } else if (in_test_) {
            writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"try\");",
                tmp, escape_line_path(source_path_), stmt_line));
            writeln("return;");
        } else {
            writeln(std::format("return std::unexpected(std::move({}).error());", tmp));
        }
        --indent_;
        writeln("}");
        last_source_line_ = -1;
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
                if (!ex.handler_body.empty() && !is_top_handled) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_res_{}", id);
                    writeln(std::format("auto {} = {};", tmp, emit_expr(ex.inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    emit_or_handler_body(ex, tmp, in_main);
                    --indent_;
                    writeln("}");
                    hoisted_tries_[e] = std::format("cfire::unwrap_val(std::move({}))", tmp);
                    last_source_line_ = -1;
                }
            } else if constexpr (std::is_same_v<T, MatchExpr>) {
                hoist_subexpr_tries(ex.target, in_main, stmt_line, false);
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
                    emit_try_unwrap_check(tmp, stmt_line, in_main);
                    hoisted_tries_[e] = std::format("cfire::unwrap_val(std::move({}))", tmp);
                    last_source_line_ = -1;
                }
            }
        }, e->node);
    }

    [[nodiscard]] auto is_core_ns_ident(std::string_view name) const -> bool {
        return cfire::modules::is_known_core_namespace(name) ||
               used_core_.contains(std::string(name)) ||
               core_aliases_.contains(std::string(name));
    }

    [[nodiscard]] auto expr_returns_optional(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return expr_returns_optional(tr->inner);
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    return it->second.return_type.is_optional;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (const auto* sfn = cfire::frontend::StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                        if (sfn->is_optional) return true;
                    }
                    if (auto it = cf_.fn_sigs.find(recv_id->name + "." + mem->field); it != cf_.fn_sigs.end()) {
                        return it->second.return_type.is_optional;
                    }
                } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                    if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                        if (const auto* sfn = cfire::frontend::StdlibCatalogIndex::instance().find_ns_fn(sub_id->name, sub_mem->field + "." + mem->field)) {
                            if (sfn->is_optional) return true;
                        }
                    }
                }
                if (auto oit = cf_.method_owner.find(mem->field); oit != cf_.method_owner.end()) {
                    if (auto mit = cf_.methods.find(oit->second + "::" + mem->field); mit != cf_.methods.end()) {
                        return mit->second.return_type.is_optional;
                    }
                }
                if (auto uit = cfire::frontend::StdlibCatalogIndex::instance().unambiguous_methods.find(mem->field);
                    uit != cfire::frontend::StdlibCatalogIndex::instance().unambiguous_methods.end()) {
                    if (uit->second.is_optional) return true;
                }
                if (mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                    mem->field == "parse_f64" || mem->field == "parse_bool" ||
                    mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "getenv_opt" || mem->field == "read_line") {
                    return true;
                }
            }
        }
        return false;
    }

    void emit_stmt(const StmtPtr& s, bool in_main);
    void emit_or_handler_body(const OrExpr& or_ex, std::string_view tmp, bool in_main);
    void emit_match_common(const ExprPtr& target, bool is_move, const std::vector<MatchArm>& arms, bool is_expr, bool in_main, std::ostringstream& sb);
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

    [[nodiscard]] auto is_package_ident(const std::string& ident) const -> bool {
        if (ident.empty()) return false;
        std::string prefix = ident + ".";
        for (const auto& [k, _] : cf_.fn_sigs) {
            if (k.starts_with(prefix)) return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.package_name == ident) return true;
        }
        for (const auto& st : cf_.ast.structs) {
            if (st.package_name == ident) return true;
        }
        for (const auto& en : cf_.ast.enums) {
            if (en.package_name == ident) return true;
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
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return expr_root_ident(sl->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return expr_root_ident(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return expr_root_ident(un->operand);
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) return expr_root_ident(tr->inner);
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return expr_root_ident(or_ex->inner);
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
    [[nodiscard]] auto emit_closure(const ClosureExpr& cl, int expr_line = -1) -> std::string;
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
            if (tr.name == "websocket::Conn" || tr.name == "Conn" || tr.name == "websocket::WebSocket" || tr.name == "websocket.WebSocket" || tr.name == "WebSocket") return "cfire::websocket::Conn";
            if (tr.name == "websocket::Server") return "cfire::websocket::Server";
            if (tr.name == "http::Request" || tr.name == "Request") return "cfire::http::Request";
            if (tr.name == "http::Response" || tr.name == "Response") return "cfire::http::Response";
            if (tr.name == "http::Router" || tr.name == "Router") return "cfire::http::Router";
            if (tr.name == "http::HttpServer" || tr.name == "HttpServer") return "cfire::http::HttpServer";
            if (tr.name == "url::Url" || tr.name == "Url") return "cfire::url::Url";
            if (tr.name == "tls::ServerContext" || tr.name == "ServerContext") return "cfire::tls::ServerContext";
            if (tr.name == "tls::CertKeyPair" || tr.name == "CertKeyPair") return "cfire::tls::CertKeyPair";
            if (tr.name == "fs::File" || tr.name == "fs.File" || (tr.name == "File" && used_core_.contains("fs"))) return "cfire::fs::File";
            if (tr.name == "net::TcpConn" || tr.name == "net.TcpConn" || tr.name == "TcpConn") return "cfire::net::TcpConn";
            if (tr.name == "net::TcpListener" || tr.name == "net.TcpListener" || tr.name == "TcpListener") return "cfire::net::TcpListener";
            if (tr.name == "ssh::Channel" || tr.name == "ssh.Channel") return "cfire::ssh::Channel";
            if (tr.name == "ssh::Session" || tr.name == "ssh.Session") return "cfire::ssh::Session";
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
            if (tr.name == "ai::Agent") return "cfire::ai::Agent";
            if (tr.name == "webui::App" || (tr.name == "App" && used_core_.contains("webui"))) return "cfire::webui::App";
        }
        std::string clean = tr.name;
        if (clean.starts_with("cpp.")) clean.erase(0, 4);
        else if (clean.starts_with("cpp::")) clean.erase(0, 5);
        for (const auto& q : cf_.cpp_qualifiers) {
            if (clean.starts_with(q + ".")) {
                clean.erase(0, q.size() + 1);
                break;
            }
            if (clean.starts_with(q + "::")) {
                clean.erase(0, q.size() + 2);
                break;
            }
        }
        auto sep_pos = clean.find_first_of(".:");
        if (sep_pos != std::string::npos) {
            bool is_dcolon = (clean[sep_pos] == ':' && sep_pos + 1 < clean.size() && clean[sep_pos + 1] == ':');
            std::string mod = clean.substr(0, sep_pos);
            std::string rest = clean.substr(sep_pos + (is_dcolon ? 2 : 1));
            if (auto it = core_aliases_.find(mod); it != core_aliases_.end()) {
                mod = it->second;
            }
            if (auto it = cf_.core_aliases.find(mod); it != cf_.core_aliases.end()) {
                mod = it->second;
            }
            if (used_core_.contains(mod)) {
                clean = std::format("cfire::{}::{}", mod, rest);
            } else if (is_local_type(rest) || is_vendor_alias(mod) || is_cpp_qualifier(mod)) {
                clean = rest;
            }
        } else {
            if (clean == "Channel" && used_core_.contains("sync")) {
                clean = "cfire::sync::Channel";
            } else if (clean == "Mutex" && used_core_.contains("sync")) {
                clean = tr.type_args.empty() ? "cfire::sync::Mutex" : "cfire::sync::MutexT";
            } else if (clean == "Atomic" && used_core_.contains("sync")) {
                clean = "cfire::sync::Atomic";
            } else if (clean == "Task" && used_core_.contains("async")) {
                clean = "cfire::async::Task";
            } else if ((clean == "Set" || clean == "PriorityQueue" || clean == "Deque" || clean == "BTreeMap" || clean == "BitSet") && used_core_.contains("collections")) {
                clean = std::format("cfire::collections::{}", clean);
            }
        }
        if (!tr.type_args.empty()) {
            if (clean == "cfire::sync::Mutex" || clean == "sync.Mutex" || clean == "sync::Mutex") {
                clean = "cfire::sync::MutexT";
            }
            std::string args;
            for (std::size_t i = 0; i < tr.type_args.size(); ++i) {
                if (i > 0) args += ", ";
                args += lower_owning_type(tr.type_args[i]);
            }
            return std::format("{}<{}>", clean, args);
        }
        if (tr.elem_type) {
            return std::format("{}<{}>", clean, lower_owning_type(*tr.elem_type));
        }
        return clean;
    }

    [[nodiscard]] auto is_vendor_alias(std::string_view mod) const -> bool {
        if (cf_.vendor_aliases.contains(std::string(mod))) return true;
        for (const auto& [_, alias] : used_vendor_) {
            if (alias == mod) return true;
        }
        return false;
    }

    [[nodiscard]] auto is_cpp_qualifier(std::string_view mod) const -> bool {
        return mod == "cpp" || cf_.cpp_qualifiers.contains(std::string(mod));
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

    [[nodiscard]] auto is_non_value_field_type(const TypeRef& tr, int depth = 0) const -> bool {
        if (depth > 8) return true;
        if (tr.is_ptr || tr.is_mut_ptr || tr.is_borrow ||
            tr.is_mut_borrow || tr.is_fn || !tr.smart_wrap.empty()) {
            return true;
        }
        if (tr.is_slice && tr.elem_type) return is_non_value_field_type(*tr.elem_type, depth + 1);
        std::string n = tr.name;
        if (n.starts_with("cpp::") || n.starts_with("webui.") || n.starts_with("webui::") ||
            n.starts_with("sync.") || n.starts_with("sync::") ||
            n.starts_with("sqlite.") || n.starts_with("sqlite::") ||
            n.starts_with("ssh.") || n.starts_with("ssh::") ||
            n.starts_with("fs.") || n.starts_with("fs::") ||
            n.starts_with("net.") || n.starts_with("net::") ||
            n.starts_with("http.") || n.starts_with("http::") ||
            n == "App" || n == "Mutex" || n == "WaitGroup" || n == "Channel" ||
            n == "Conn" || n == "Session" || n == "File" || n == "TcpConn" || n == "TcpListener") {
            return true;
        }
        for (const auto& sub_st : cf_.ast.structs) {
            if (sub_st.name == n) {
                for (const auto& sf : sub_st.fields) {
                    if (is_non_value_field_type(sf.type, depth + 1)) return true;
                }
                break;
            }
        }
        return false;
    }

    [[nodiscard]] auto can_emit_json_field_type(const TypeRef& tr, int depth = 0) const -> bool {
        if (depth > 8) return false;
        if (is_non_value_field_type(tr, depth)) return false;
        if (tr.is_map || tr.is_tuple || tr.is_array) return false;
        if (tr.is_slice) return tr.elem_type && can_emit_json_field_type(*tr.elem_type, depth + 1);
        if (tr.is_optional) {
            TypeRef inner = tr;
            inner.is_optional = false;
            return can_emit_json_field_type(inner, depth + 1);
        }
        const std::string& n = tr.name;
        if (n == "i8" || n == "i16" || n == "i32" || n == "i64" || n == "isize" ||
            n == "u8" || n == "u16" || n == "u32" || n == "u64" || n == "usize" ||
            n == "f32" || n == "f64" || n == "bool" || n == "str") {
            return true;
        }
        if (cf_.enums.contains(n)) return true;
        for (const auto& sub_st : cf_.ast.structs) {
            if (sub_st.name == n) {
                for (const auto& sf : sub_st.fields) {
                    if (!can_emit_json_field_type(sf.type, depth + 1)) return false;
                }
                return true;
            }
        }
        return false;
    }
};


} // namespace cfire::frontend

#include "codegen/stmt.hpp"
#include "codegen/expr.hpp"
#include "codegen/reflect.hpp"
#include "codegen/test_runner.hpp"

namespace cfire::frontend {

[[nodiscard]] inline auto emit_cpp(const CheckedFile& cf, bool test_mode = false) -> std::string {
    Emitter em(cf, test_mode);
    return em.emit_cpp();
}

} // namespace cfire::frontend
