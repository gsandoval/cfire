#include "cfire_prelude.hpp"

struct CppHeaderInfo;
struct TypeDeclItem;
struct CodegenEmitter;

struct CppHeaderInfo {
    std::string path{};
    std::vector<std::string> defines{};
};

template <>
struct cfire::reflect_struct<CppHeaderInfo> {
    static constexpr std::string_view name = "CppHeaderInfo";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "path",
        "defines",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct TypeDeclItem {
    std::int32_t line{};
    bool is_enum{};
    std::size_t idx{};
};

template <>
struct cfire::reflect_struct<TypeDeclItem> {
    static constexpr std::string_view name = "TypeDeclItem";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "line",
        "is_enum",
        "idx",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct CodegenEmitter {
    std::string buf{};
    std::int32_t indent_level{};
    bool test_mode{};
};

template <>
struct cfire::reflect_struct<CodegenEmitter> {
    static constexpr std::string_view name = "CodegenEmitter";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "buf",
        "indent_level",
        "test_mode",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] inline auto codegen_starts_with(const std::string& s, const std::string& prefix) -> bool;
[[nodiscard]] inline auto clean_header_path(const std::string& path) -> std::string;
inline auto CodegenEmitter__write(CodegenEmitter& e, const std::string& s) -> void;
inline auto CodegenEmitter__writeln(CodegenEmitter& e, const std::string& s) -> void;
[[nodiscard]] inline auto CodegenEmitter__emit_file(CodegenEmitter& e, const cfire::frontend::CheckedFile& cf) -> std::string;
[[nodiscard]] inline auto selfhost_emit_cpp_file(const cfire::frontend::CheckedFile& cf, bool test_mode) -> std::string;

[[nodiscard]] inline auto codegen_starts_with(const std::string& s, const std::string& prefix) -> bool {
    auto n = s.size();
    auto pn = prefix.size();
    if (pn > n) {
        return false;
    }
    return (cfire::slice(s, 0, pn) == prefix);
}

[[nodiscard]] inline auto clean_header_path(const std::string& path) -> std::string {
    auto p = path;
    while ((p.size() > 0) && ((p[0] == 60) || (p[0] == 34))) {
        p = cfire::slice(p, 1, p.size());
    }
    while ((p.size() > 0) && ((p[(p.size() - 1)] == 62) || (p[(p.size() - 1)] == 34))) {
        p = cfire::slice(p, 0, (p.size() - 1));
    }
    return p;
}

inline auto CodegenEmitter__write(CodegenEmitter& e, const std::string& s) -> void {
    e.buf += s;
}

inline auto CodegenEmitter__writeln(CodegenEmitter& e, const std::string& s) -> void {
    if (s.size() == 0) {
        e.buf += "\n";
        return;
    }
    std::int32_t i = 0;
    while (i < e.indent_level) {
        e.buf += "    ";
        i = (i + 1);
    }
    e.buf += (s + "\n");
}

[[nodiscard]] inline auto CodegenEmitter__emit_file(CodegenEmitter& e, const cfire::frontend::CheckedFile& cf) -> std::string {
    std::vector<std::string> link_tags = {};
    std::vector<std::string> defines = {};
    std::vector<CppHeaderInfo> cpp_headers = {};
    cfire::Map<std::string, bool> used_core{};
    cfire::Map<std::string, std::string> used_vendor{};
    std::size_t imp_i = 0;
    auto n_imp = cf.ast.imports.size();
    while (imp_i < n_imp) {
        auto imp = cf.ast.imports[imp_i];
        if (imp.is_cpp) {
            std::vector<std::string> h_defines = {};
            std::size_t at_i = 0;
            auto n_at = imp.attrs.size();
            while (at_i < n_at) {
                auto attr = imp.attrs[at_i];
                if ((attr.name == "pkg") && (attr.args.size() > 0)) {
                    (void)(link_tags.push_back(("@pkg:" + attr.args[0])));
                } else {
                    if ((attr.name == "define") && (attr.args.size() > 0)) {
                        if (attr.args.size() == 1) {
                            (void)(h_defines.push_back(("#define " + attr.args[0])));
                        } else {
                            if (attr.args.size() >= 2) {
                                (void)(h_defines.push_back(((("#define " + attr.args[0]) + " ") + attr.args[1])));
                            }
                        }
                    }
                }
                at_i = (at_i + 1);
            }
            (void)(cpp_headers.push_back(CppHeaderInfo{.path = imp.path, .defines = h_defines}));
        } else {
            auto ipath = imp.path;
            if (ipath == "core:fs") {
                used_core["fs"] = true;
            } else {
                if (ipath == "core:cli") {
                    used_core["cli"] = true;
                } else {
                    if ((((ipath == "core:compress/zstd") || (ipath == "core:compress")) || (ipath == "core:zstd")) || codegen_starts_with(ipath, "core:compress/")) {
                        used_core["compress"] = true;
                        (void)(defines.push_back("#define CFIRE_USE_ZSTD 1"));
                        (void)(link_tags.push_back("-lzstd"));
                    } else {
                        if (ipath == "core:json") {
                            used_core["json"] = true;
                            (void)(defines.push_back("#define CFIRE_USE_SIMDJSON 1"));
                            (void)(link_tags.push_back("-lsimdjson"));
                        } else {
                            if ((ipath == "core:db/sqlite") || (ipath == "core:sqlite")) {
                                used_core["sqlite"] = true;
                                (void)(defines.push_back("#define CFIRE_USE_SQLITE 1"));
                                (void)(link_tags.push_back("-lsqlite3"));
                            } else {
                                if ((ipath == "core:crypto") || codegen_starts_with(ipath, "core:crypto/")) {
                                    used_core["crypto"] = true;
                                } else {
                                    if ((((ipath == "core:net") || (ipath == "core:http")) || (ipath == "core:websocket")) || codegen_starts_with(ipath, "core:net/")) {
                                        used_core["net"] = true;
                                    } else {
                                        if (ipath == "core:time") {
                                            used_core["time"] = true;
                                        } else {
                                            if (ipath == "core:os") {
                                                used_core["os"] = true;
                                            } else {
                                                if (((ipath == "core:sync") || (ipath == "core:context")) || codegen_starts_with(ipath, "core:sync/")) {
                                                    used_core["sync"] = true;
                                                } else {
                                                    if (ipath == "core:str") {
                                                        used_core["str"] = true;
                                                    } else {
                                                        if ((ipath == "core:math") || (ipath == "core:rand")) {
                                                            used_core["math"] = true;
                                                        } else {
                                                            if ((((codegen_starts_with(ipath, "core:encoding/") || (ipath == "core:encoding")) || (ipath == "core:base64")) || (ipath == "core:hex")) || (ipath == "core:csv")) {
                                                                used_core["encoding"] = true;
                                                            } else {
                                                                if (ipath == "core:regex") {
                                                                    used_core["regex"] = true;
                                                                } else {
                                                                    if ((ipath == "core:collections") || (ipath == "core:sort")) {
                                                                        used_core["collections"] = true;
                                                                    } else {
                                                                        if (ipath == "core:log") {
                                                                            used_core["log"] = true;
                                                                        } else {
                                                                            if (ipath == "core:unicode") {
                                                                                used_core["unicode"] = true;
                                                                            } else {
                                                                                if (codegen_starts_with(ipath, "core:")) {
                                                                                    auto mod_name = cfire::slice(ipath, 5, ipath.size());
                                                                                    used_core[mod_name] = true;
                                                                                } else {
                                                                                    if (ipath == "vendor:raylib") {
                                                                                        std::string alias = "raylib";
                                                                                        if (imp.alias.size() > 0) {
                                                                                            alias = imp.alias;
                                                                                        }
                                                                                        used_vendor["raylib"] = alias;
                                                                                        (void)(link_tags.push_back("-lraylib"));
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
        imp_i = (imp_i + 1);
    }
    if (link_tags.size() > 0) {
        std::string joined = "";
        std::size_t lti = 0;
        auto n_lt = link_tags.size();
        while (lti < n_lt) {
            if (lti > 0) {
                joined = (joined + " ");
            }
            joined = (joined + link_tags[lti]);
            lti = (lti + 1);
        }
        (void)(CodegenEmitter__writeln(e, ("// @cfire-link: " + joined)));
    }
    std::size_t di = 0;
    auto n_def = defines.size();
    while (di < n_def) {
        (void)(CodegenEmitter__writeln(e, defines[di]));
        di = (di + 1);
    }
    (void)(CodegenEmitter__writeln(e, "#include \"cfire_prelude.hpp\""));
    if (used_core.size() > 0) {
        if (used_core["cli"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/cli.hpp\""));
        }
        if (used_core["collections"] || used_core["sort"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/collections.hpp\""));
        }
        if (used_core["zstd"] || used_core["compress"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/compress.hpp\""));
        }
        if (used_core["crypto"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/crypto.hpp\""));
        }
        if (((used_core["encoding"] || used_core["base64"]) || used_core["hex"]) || used_core["csv"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/encoding.hpp\""));
        }
        if (used_core["fs"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/fs.hpp\""));
        }
        if (used_core["json"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/json.hpp\""));
        }
        if (used_core["log"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/log.hpp\""));
        }
        if (used_core["math"] || used_core["rand"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/math.hpp\""));
        }
        if (((used_core["net"] || used_core["http"]) || used_core["websocket"]) || used_core["url"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/net.hpp\""));
        }
        if (used_core["os"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/os.hpp\""));
        }
        if (used_core["regex"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/regex.hpp\""));
        }
        if (used_core["sqlite"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/sqlite.hpp\""));
        }
        if (used_core["str"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/str.hpp\""));
        }
        if (used_core["sync"] || used_core["context"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/sync.hpp\""));
        }
        if (used_core["time"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/time.hpp\""));
        }
        if (used_core["unicode"]) {
            (void)(CodegenEmitter__writeln(e, "#include \"core/unicode.hpp\""));
        }
    }
    std::size_t chi = 0;
    auto n_ch = cpp_headers.size();
    while (chi < n_ch) {
        auto ch = cpp_headers[chi];
        std::size_t cdi = 0;
        auto n_cdef = ch.defines.size();
        while (cdi < n_cdef) {
            (void)(CodegenEmitter__writeln(e, ch.defines[cdi]));
            cdi = (cdi + 1);
        }
        auto clean_path = clean_header_path(ch.path);
        (void)(CodegenEmitter__writeln(e, (("#include <" + clean_path) + ">")));
        chi = (chi + 1);
    }
    if (used_vendor.contains("raylib")) {
        (void)(CodegenEmitter__writeln(e, "#include <raylib.h>"));
    }
    (void)(CodegenEmitter__writeln(e, ""));
    std::size_t ifi = 0;
    auto n_ifaces = cf.ast.interfaces.size();
    while (ifi < n_ifaces) {
        auto iface_str = cfire::frontend::codegen_emit_interface(cf.ast.interfaces[ifi]);
        (void)(CodegenEmitter__write(e, iface_str));
        (void)(CodegenEmitter__writeln(e, ""));
        ifi = (ifi + 1);
    }
    std::size_t sdi = 0;
    auto n_structs = cf.ast.structs.size();
    while (sdi < n_structs) {
        (void)(CodegenEmitter__writeln(e, (("struct " + cf.ast.structs[sdi].name) + ";")));
        sdi = (sdi + 1);
    }
    if (n_structs > 0) {
        (void)(CodegenEmitter__writeln(e, ""));
    }
    std::vector<TypeDeclItem> items = {};
    std::size_t ei = 0;
    auto n_enums = cf.ast.enums.size();
    while (ei < n_enums) {
        (void)(items.push_back(TypeDeclItem{.line = cf.ast.enums[ei].line, .is_enum = true, .idx = ei}));
        ei = (ei + 1);
    }
    std::size_t si = 0;
    while (si < n_structs) {
        (void)(items.push_back(TypeDeclItem{.line = cf.ast.structs[si].line, .is_enum = false, .idx = si}));
        si = (si + 1);
    }
    std::size_t sort_i = 1;
    auto n_items = items.size();
    while (sort_i < n_items) {
        auto sort_j = sort_i;
        while ((sort_j > 0) && (items[(sort_j - 1)].line > items[sort_j].line)) {
            auto tmp = items[(sort_j - 1)];
            items[(sort_j - 1)] = items[sort_j];
            items[sort_j] = tmp;
            sort_j = (sort_j - 1);
        }
        sort_i = (sort_i + 1);
    }
    std::size_t item_i = 0;
    while (item_i < n_items) {
        auto it = items[item_i];
        if (it.is_enum) {
            auto en_str = cfire::frontend::codegen_emit_enum(cf.ast.enums[it.idx]);
            (void)(CodegenEmitter__write(e, en_str));
        } else {
            auto st_str = cfire::frontend::codegen_emit_struct(cf.ast.structs[it.idx], cf);
            (void)(CodegenEmitter__write(e, st_str));
        }
        (void)(CodegenEmitter__writeln(e, ""));
        item_i = (item_i + 1);
    }
    std::size_t gvi = 0;
    auto n_gvs = cf.ast.global_vars.size();
    while (gvi < n_gvs) {
        auto gv_str = cfire::frontend::codegen_emit_global_var(cf.ast.global_vars[gvi], cf);
        (void)(CodegenEmitter__write(e, gv_str));
        gvi = (gvi + 1);
    }
    if (n_gvs > 0) {
        (void)(CodegenEmitter__writeln(e, ""));
    }
    std::size_t ffi = 0;
    auto n_fns = cf.ast.functions.size();
    while (ffi < n_fns) {
        auto fn_fwd = cfire::frontend::codegen_emit_fn_forward_decl(cf.ast.functions[ffi]);
        (void)(CodegenEmitter__write(e, fn_fwd));
        ffi = (ffi + 1);
    }
    if (n_fns > 0) {
        (void)(CodegenEmitter__writeln(e, ""));
    }
    auto has_user_main = false;
    std::size_t fi = 0;
    while (fi < n_fns) {
        auto fn_decl = cf.ast.functions[fi];
        auto has_receiver = cfire::frontend::ast_fn_has_receiver(cf.ast, fi);
        if ((fn_decl.name == "main") && !has_receiver) {
            has_user_main = true;
            if (e.test_mode) {
                fi = (fi + 1);
                continue;
            }
        }
        auto fn_str = cfire::frontend::codegen_emit_function(fn_decl, cf);
        (void)(CodegenEmitter__write(e, fn_str));
        if (((fi + 1) < n_fns) || (cf.ast.tests.size() > 0)) {
            (void)(CodegenEmitter__writeln(e, ""));
        }
        fi = (fi + 1);
    }
    std::size_t ti = 0;
    auto n_tests = cf.ast.tests.size();
    while (ti < n_tests) {
        auto td_str = cfire::frontend::codegen_emit_test_decl(cf.ast.tests[ti], ti, cf);
        (void)(CodegenEmitter__write(e, td_str));
        (void)(CodegenEmitter__writeln(e, ""));
        ti = (ti + 1);
    }
    if ((e.test_mode || !has_user_main) && (n_tests > 0)) {
        (void)(CodegenEmitter__writeln(e, "auto main() -> int {"));
        e.indent_level = (e.indent_level + 1);
        std::size_t ri = 0;
        while (ri < n_tests) {
            auto t_name = cf.ast.tests[ri].name;
            (void)(CodegenEmitter__writeln(e, (("std::println(\"=== RUN   " + t_name) + "\");")));
            (void)(CodegenEmitter__writeln(e, (("__cfire_test_" + cfire::frontend::codegen_usize_to_str(ri)) + "();")));
            (void)(CodegenEmitter__writeln(e, (("std::println(\"--- PASS: " + t_name) + "\");")));
            ri = (ri + 1);
        }
        (void)(CodegenEmitter__writeln(e, (("std::println(\"PASS (" + cfire::frontend::codegen_usize_to_str(n_tests)) + " cfire tests succeeded)\");")));
        (void)(CodegenEmitter__writeln(e, "return 0;"));
        e.indent_level = (e.indent_level - 1);
        (void)(CodegenEmitter__writeln(e, "}"));
    }
    return e.buf;
}

[[nodiscard]] inline auto selfhost_emit_cpp_file(const cfire::frontend::CheckedFile& cf, bool test_mode) -> std::string {
    auto emitter = CodegenEmitter{.buf = "", .indent_level = 0, .test_mode = test_mode};
    return CodegenEmitter__emit_file(emitter, cf);
}
