#pragma once
// src/lsp/type_query.hpp — Type Lowering Previews, Qualifier Resolution & Expression Type Inference for LSP

#include "../ast.hpp"
#include "../clang_bridge.hpp"
#include "../docgen/catalog_types.hpp"
#include "../formatter.hpp"
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cfire::lsp::detail {

[[nodiscard]] inline auto map_cf_named_type_to_cpp(std::string_view n, bool param_borrow = false) -> std::string {
    if (n == "i8") return "std::int8_t";
    if (n == "i16") return "std::int16_t";
    if (n == "i32" || n == "int") return "std::int32_t";
    if (n == "i64") return "std::int64_t";
    if (n == "u8") return "std::uint8_t";
    if (n == "byte") return "std::uint8_t";
    if (n == "u16") return "std::uint16_t";
    if (n == "u32") return "std::uint32_t";
    if (n == "u64") return "std::uint64_t";
    if (n == "usize") return "std::size_t";
    if (n == "isize") return "std::ptrdiff_t";
    if (n == "f32") return "float";
    if (n == "f64") return "double";
    if (n == "bool") return "bool";
    if (n == "void") return "void";
    if (n == "str") return param_borrow ? "std::string_view" : "std::string";
    if (n == "path") return param_borrow ? "const std::filesystem::path&" : "std::filesystem::path";
    if (n == "fs.File" || n == "File") return "cfire::fs::File";
    if (n == "sync.Mutex" || n == "Mutex") return "cfire::sync::Mutex";
    if (n == "net.TcpListener") return "cfire::net::TcpListener";
    if (n == "net.TcpConn") return "cfire::net::TcpConn";
    if (n == "sqlite.Conn") return "cfire::sqlite::Conn";
    if (n == "webui.App" || n == "App") return "cfire::webui::App";
    if (n == "ssh.Session" || n == "Session") return "cfire::ssh::Session";
    if (n == "ssh.Channel") return "cfire::ssh::Channel";
    if (n == "ssh.ExecResult" || n == "ExecResult") return "cfire::ssh::ExecResult";
    if (n == "ssh.SftpEntry" || n == "SftpEntry") return "cfire::ssh::SftpEntry";
    if (n == "context.Context" || n == "Context") return "cfire::sync::context::Context";
    if (n == "websocket.Conn") return "cfire::websocket::Conn";
    return std::string(n);
}

// Lowers a `.cf` TypeRef to its human-readable ISO C++23 representation for hover/completion cards
[[nodiscard]] inline auto lower_type_preview(const cfire::frontend::TypeRef& t, bool is_param = false, std::string_view intent = "in") -> std::string {
    bool borrow = is_param && (intent == "in" || intent.empty());
    std::string base;
    if (t.smart_wrap == "own" && t.elem_type) {
        base = std::format("std::unique_ptr<{}>", lower_type_preview(*t.elem_type));
    } else if (t.smart_wrap == "shared" && t.elem_type) {
        base = std::format("std::shared_ptr<{}>", lower_type_preview(*t.elem_type));
    } else if (t.is_slice && t.elem_type) {
        std::string el = lower_type_preview(*t.elem_type);
        base = borrow ? std::format("std::span<const {}>", el) : std::format("std::vector<{}>", el);
    } else if (t.is_array && t.elem_type) {
        base = std::format("std::array<{}, {}>", lower_type_preview(*t.elem_type), t.array_len);
    } else if (t.is_map && t.key_type && t.elem_type) {
        base = std::format("cfire::Map<{}, {}>", lower_type_preview(*t.key_type), lower_type_preview(*t.elem_type));
    } else if (t.is_tuple) {
        std::string inner;
        for (std::size_t i = 0; i < t.tuple_types.size(); ++i) {
            if (i > 0) inner += ", ";
            inner += lower_type_preview(t.tuple_types[i]);
        }
        base = std::format("std::tuple<{}>", inner);
    } else if (!t.type_args.empty()) {
        std::string inner;
        for (std::size_t i = 0; i < t.type_args.size(); ++i) {
            if (i > 0) inner += ", ";
            inner += lower_type_preview(t.type_args[i]);
        }
        std::string raw = map_cf_named_type_to_cpp(t.name, false);
        base = std::format("{}<{}>", raw, inner);
    } else {
        base = map_cf_named_type_to_cpp(t.name, borrow);
    }
    if (t.is_optional) base = std::format("std::optional<{}>", base);
    if (t.is_fallible) base = std::format("std::expected<{}, cfire::Error>", base);
    if (is_param) {
        if (intent == "mut" || intent == "out") return base + "&";
        if (intent == "move") return base + "&&";
        if (!base.starts_with("std::string_view") && !base.starts_with("std::span") &&
            (t.is_array || t.is_map || (!t.smart_wrap.empty()) || base == t.name)) {
            return "const " + base + "&";
        }
    }
    return base;
}

[[nodiscard]] inline auto lower_fn_preview(const cfire::frontend::FnDecl& fn) -> std::string {
    std::string params;
    if (fn.receiver) {
        params += std::format("{} {}", lower_type_preview(fn.receiver->type, true, fn.receiver->intent), fn.receiver->name);
    }
    for (const auto& p : fn.params) {
        if (!params.empty()) params += ", ";
        params += std::format("{} {}", lower_type_preview(p.type, true, p.intent), p.name);
    }
    std::string ret = lower_type_preview(fn.return_type);
    std::string nodiscard = (ret == "void") ? "" : "[[nodiscard]] ";
    return std::format("{}auto {}({}) -> {}", nodiscard, fn.name, params, ret);
}

[[nodiscard]] inline auto cpp_return_to_cf(std::string_view cpp_t) -> std::string {
    std::string s(cfire::bridge::trim_qual(cpp_t));
    if (s == "void") return "void";
    if (s == "bool" || s == "_Bool") return "bool";
    if (s == "int" || s == "int32_t" || s == "std::int32_t") return "i32";
    if (s == "unsigned int" || s == "uint32_t" || s == "std::uint32_t") return "u32";
    if (s == "long" || s == "long long" || s == "int64_t" || s == "std::int64_t") return "i64";
    if (s == "unsigned long" || s == "unsigned long long" || s == "size_t" || s == "std::size_t" || s == "uint64_t") return "u64";
    if (s == "float") return "f32";
    if (s == "double") return "f64";
    if (s == "const char *") return "str";
    return s;
}

// Cross-package & Clang AST Bridge context for a document
struct ImportedPkgFile {
    std::string alias{};
    std::string import_path{};
    std::string file_path{};
    std::string uri{};
    cfire::frontend::FileAST ast{};
};

struct WorkspaceContext {
    std::vector<ImportedPkgFile> local_packages{};
    std::unordered_map<std::string, cfire::bridge::CppSymbol> cpp_symbols{};
    std::unordered_map<std::string, std::string> cpp_symbol_header{};
};

// Information about a local variable or parameter inferred inside a function/test
struct LocalBindingInfo {
    std::string name;
    std::string cf_type;
    std::string cpp_type;
    bool is_mut = false;
    bool is_param = false;
    std::string intent = "in";
    int decl_line = 1;
    int name_end_col = 1;
    bool inferred_colon_eq = false;
};

[[nodiscard]] inline auto strip_wrappers(std::string t) -> std::string {
    while (t.ends_with("!") || t.ends_with("?")) t.pop_back();
    if (t.starts_with("&mut ")) t = t.substr(5);
    else if (t.starts_with("&")) t = t.substr(1);
    if (t.starts_with("own[") && t.ends_with("]")) return t.substr(4, t.size() - 5);
    if (t.starts_with("shared[") && t.ends_with("]")) return t.substr(7, t.size() - 8);
    if (t.starts_with("*mut ")) return t.substr(5);
    if (t.starts_with("*")) return t.substr(1);
    return t;
}

[[nodiscard]] inline auto infer_expr_cf_type(
    const cfire::frontend::ExprPtr& e,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env
) -> std::string;

// Resolves a potentially chained qualifier (e.g. `m_out`, `order.customer`, `os.stdin()`, `(*owned_box)`) to its `.cf` struct/type name
// Qualifies the bare names of `mod`'s own types in a catalog type string: `ExecResult!` in `core:os` is
// `os.ExecResult!`, in `ext:net/ssh` `ssh.ExecResult!` (CF-188).
[[nodiscard]] inline auto qualify_module_types(std::string_view t, const cfire::doc::StdModuleDoc& mod) -> std::string {
    std::string out;
    std::size_t i = 0;
    while (i < t.size()) {
        if (std::isalpha(static_cast<unsigned char>(t[i])) || t[i] == '_') {
            std::size_t j = i;
            while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_')) ++j;
            const std::string word(t.substr(i, j - i));
            const bool qualified = i > 0 && (t[i - 1] == '.' || t[i - 1] == ':');
            if (!qualified && std::ranges::find(mod.types, word) != mod.types.end()) out += mod.ns_alias + ".";
            out += word;
            i = j;
        } else {
            out.push_back(t[i++]);
        }
    }
    return out;
}

[[nodiscard]] inline auto resolve_qualifier_type(
    std::string_view qualifier,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env
) -> std::string {
    while (!qualifier.empty() && (qualifier.front() == ' ' || qualifier.front() == '\t')) qualifier.remove_prefix(1);
    while (!qualifier.empty() && (qualifier.back() == ' ' || qualifier.back() == '\t')) qualifier.remove_suffix(1);
    if (qualifier.starts_with("try ")) {
        qualifier.remove_prefix(4);
        while (!qualifier.empty() && (qualifier.front() == ' ' || qualifier.front() == '\t')) qualifier.remove_prefix(1);
    }
    if (qualifier.starts_with("(*") && qualifier.ends_with(")") && qualifier.size() > 3) {
        qualifier = qualifier.substr(2, qualifier.size() - 3);
    }

    if (qualifier.find('(') != std::string_view::npos || qualifier.find('[') != std::string_view::npos) {
        std::string wrapped = std::format("package __lsp\nfn __q() {{\n    __tmp := {}\n}}\n", qualifier);
        if (auto q_ast = cfire::frontend::parse_source("<qualifier>", wrapped)) {
            if (!q_ast->functions.empty() && !q_ast->functions[0].body.empty()) {
                if (const auto* vd = std::get_if<cfire::frontend::VarDeclStmt>(&q_ast->functions[0].body[0]->node)) {
                    if (vd->init) {
                        std::string inferred = strip_wrappers(infer_expr_cf_type(vd->init, ast, ws, env));
                        if (!inferred.empty() && inferred != "auto") {
                            return inferred;
                        }
                    }
                }
            }
        }
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < qualifier.size()) {
        auto dot = qualifier.find('.', start);
        if (dot == std::string_view::npos) {
            parts.emplace_back(qualifier.substr(start));
            break;
        }
        parts.emplace_back(qualifier.substr(start, dot - start));
        start = dot + 1;
    }
    if (parts.empty()) return "";

    std::string cur_type = parts[0];
    if (auto it = env.find(parts[0]); it != env.end()) {
        cur_type = strip_wrappers(it->second.cf_type);
    }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& fld = parts[i];
        bool found = false;
        auto find_in_structs = [&](const std::vector<cfire::frontend::StructDecl>& structs) {
            for (const auto& st : structs) {
                if (st.name == cur_type) {
                    for (const auto& f : st.fields) {
                        if (f.name == fld) {
                            cur_type = strip_wrappers(cfire::fmt::format_type_ref(f.type));
                            found = true;
                            return;
                        }
                    }
                }
            }
        };
        find_in_structs(ast.structs);
        for (const auto& pkg : ws.local_packages) {
            if (found) break;
            find_in_structs(pkg.ast.structs);
        }
        if (!found) break;
    }
    return cur_type;
}

[[nodiscard]] inline auto infer_expr_cf_type(
    const cfire::frontend::ExprPtr& e,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env
) -> std::string {
    using namespace cfire::frontend;
    if (!e) return "auto";
    if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
        if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return "str";
        if (lit->kind == TokenKind::Char) return "byte";
        if (lit->kind == TokenKind::Float) return "f64";
        if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return "bool";
        return "i64";
    }
    if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        return sl->type_name;
    }
    if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
        if (!arr->elements.empty()) {
            return std::format("[{}]{}", arr->elements.size(), infer_expr_cf_type(arr->elements[0], ast, ws, env));
        }
        return "[]auto";
    }
    if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
        std::string res = "(";
        for (std::size_t i = 0; i < tup->elements.size(); ++i) {
            if (i > 0) res += ", ";
            res += infer_expr_cf_type(tup->elements[i], ast, ws, env);
        }
        res += ")";
        return res;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        if (un->op == "!") return "bool";
        std::string inner = infer_expr_cf_type(un->operand, ast, ws, env);
        if (un->op == "*") return strip_wrappers(inner);
        if (un->op == "&mut") return "*mut " + inner;
        if (un->op == "&") return "*" + inner;
        return inner;
    }
    if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
            bin->op == ">" || bin->op == ">=" || bin->op == "&&" || bin->op == "||") {
            return "bool";
        }
        return infer_expr_cf_type(bin->left, ast, ws, env);
    }
    if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        std::string obj_t = strip_wrappers(infer_expr_cf_type(idx->object, ast, ws, env));
        if (obj_t.starts_with("[]")) return obj_t.substr(2);
        if (obj_t.starts_with("[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return obj_t.substr(rb + 1);
        }
        if (obj_t.starts_with("map[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return obj_t.substr(rb + 1);
        }
        return "auto";
    }
    if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        std::string obj_t = strip_wrappers(infer_expr_cf_type(slc->object, ast, ws, env));
        if (obj_t == "str") return "str";
        if (obj_t.starts_with("[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return "[]" + obj_t.substr(rb + 1);
        }
        return obj_t;
    }
    if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        std::string inner = infer_expr_cf_type(tr->inner, ast, ws, env);
        if (inner.ends_with("!")) inner.pop_back();
        return inner;
    }
    if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        std::string inner = infer_expr_cf_type(orex->inner, ast, ws, env);
        if (inner.ends_with("!") || inner.ends_with("?")) inner.pop_back();
        return inner;
    }
    if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
        if (auto it = env.find(id->name); it != env.end()) return it->second.cf_type;
    }
    if (const auto* call = std::get_if<CallExpr>(&e->node)) {
        if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
            if (cid->name == "[]byte" || cid->name == "[]u8" || cid->name == "str" ||
                cid->name == "i8" || cid->name == "i16" || cid->name == "i32" || cid->name == "i64" ||
                cid->name == "u8" || cid->name == "byte" || cid->name == "u16" || cid->name == "u32" ||
                cid->name == "u64" || cid->name == "usize" || cid->name == "isize" ||
                cid->name == "f32" || cid->name == "f64" || cid->name == "bool") {
                return cid->name;
            }
            if (cid->name == "make" && !call->type_args.empty()) {
                return cfire::fmt::format_type_ref(call->type_args[0]);
            }
            for (const auto& fn : ast.functions) {
                if (fn.name == cid->name) {
                    return cfire::fmt::format_type_ref(fn.return_type);
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.name == cid->name) {
                        return cfire::fmt::format_type_ref(fn.return_type);
                    }
                }
            }
        }
        if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
            if (const auto* mid = std::get_if<IdentExpr>(&mem->object->node)) {
                // 1. Check `core:*` module functions
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    if (mod.ns_alias == mid->name) {
                        for (const auto& sfn : mod.functions) {
                            if (sfn.name == mem->field) {
                                if (auto arrow = sfn.cf_sig.find("-> "); arrow != std::string::npos) {
                                    std::string ret = sfn.cf_sig.substr(arrow + 3);
                                    if (ret == "T!" && !call->type_args.empty()) {
                                        return cfire::fmt::format_type_ref(call->type_args[0]) + "!";
                                    }
                                    if (ret == "Channel[T]" && !call->type_args.empty()) {
                                        return std::format("sync.Channel[{}]", cfire::fmt::format_type_ref(call->type_args[0]));
                                    }
                                    if (ret == "TcpListener!") return "net.TcpListener!";
                                    if (ret == "TcpConn!") return "net.TcpConn!";
                                    if (ret == "File") return "fs.File";
                                    if (ret == "File!") return "fs.File!";
                                    if (ret == "(File, File)!") return "(fs.File, fs.File)!";
                                    if (ret == "(TcpConn, TcpConn)!") return "(net.TcpConn, net.TcpConn)!";
                                    // The module's own types are written bare in its signatures (`-> ExecResult!`).
                                    return qualify_module_types(ret, mod);
                                }
                                return "void";
                            }
                        }
                    }
                }
                // 2. Check imported local `.cf` packages (`checksum.compute_tag(...)`)
                for (const auto& pkg : ws.local_packages) {
                    if (pkg.alias == mid->name) {
                        for (const auto& fn : pkg.ast.functions) {
                            if (fn.name == mem->field) {
                                return cfire::fmt::format_type_ref(fn.return_type);
                            }
                        }
                    }
                }
                // 3. Check `cpp::fn(...)` Clang AST Bridge functions
                if (mid->name == "cpp" || mem->is_namespace) {
                    if (auto cit = ws.cpp_symbols.find(mem->field); cit != ws.cpp_symbols.end()) {
                        return cpp_return_to_cf(cit->second.return_type);
                    }
                }
                // 4. Check Enum variant constructor (`Shape.Circle(10.0)`)
                for (const auto& en : ast.enums) {
                    if (en.name == mid->name) return en.name;
                }
            }
            // 5. Check receiver methods (`os.stdin().read_lines()`, `listener.accept()`, `s.trim()`, `ch.recv()`, user methods)
            std::string rtype = strip_wrappers(infer_expr_cf_type(mem->object, ast, ws, env));
            if (!rtype.empty() && rtype != "auto") {
                // W4: builtin method return types come from the single table; `T`/`K`/`V`/`Self` resolve here.
                if (std::uint8_t bits = cfire::frontend::builtin_recv_bits_for_type_name(rtype); bits != 0) {
                    if (const auto* bm = cfire::frontend::find_builtin_method(bits, mem->field); bm && !bm->ret.empty()) {
                        std::string self = rtype;
                        while (self.starts_with("&")) self.erase(0, 1);
                        std::string elem = "u8";
                        std::string key;
                        std::string val;
                        if (self == "str") elem = "byte";
                        else if (self.starts_with("map[")) {
                            if (auto rb = self.find(']'); rb != std::string::npos) {
                                key = self.substr(4, rb - 4);
                                val = self.substr(rb + 1);
                            }
                        } else if (auto rb = self.find(']'); self.starts_with("[") && rb != std::string::npos) {
                            elem = self.substr(rb + 1);
                        }
                        std::string ret(bm->ret);
                        auto subst = [&](std::string_view ph, const std::string& with) {
                            std::string out;
                            for (std::size_t i = 0; i < ret.size();) {
                                const bool at = ret.compare(i, ph.size(), ph) == 0 &&
                                    (i == 0 || !std::isalnum(static_cast<unsigned char>(ret[i - 1]))) &&
                                    (i + ph.size() >= ret.size() || !std::isalnum(static_cast<unsigned char>(ret[i + ph.size()])));
                                if (at) { out += with; i += ph.size(); }
                                else out += ret[i++];
                            }
                            ret = std::move(out);
                        };
                        subst("Self", self);
                        subst("T", elem);
                        if (!key.empty()) subst("K", key);
                        if (!val.empty()) subst("V", val);
                        return ret;
                    }
                }
                if (rtype == "net.TcpListener" || rtype == "TcpListener") {
                    if (mem->field == "port") return "u16";
                    if (mem->field == "accept") return "net.TcpConn!";
                }
                if (rtype == "net.TcpConn" || rtype == "TcpConn" || rtype == "fs.File" || rtype == "File") {
                    if (mem->field == "write_bytes" || mem->field == "write_text" || mem->field == "write_line" || mem->field == "read_into") return "usize!";
                    if (mem->field == "read_bytes" || mem->field == "read_exact" || mem->field == "read_bytes_until") return "[]byte!";
                    if (mem->field == "read_text" || mem->field == "read_text_until") return "str!";
                    if (mem->field == "read_line") return "str?";
                    if (mem->field == "read_lines") return "[]str!";
                    if (mem->field == "is_open" || mem->field == "is_tty" || mem->field == "poll") return "bool";
                    if (mem->field == "fd") return "i32";
                    if (mem->field == "seek" || mem->field == "tell") return "i64!";
                    if (mem->field == "flush") return "void!";
                    if (mem->field == "close" || mem->field == "set_timeout_ms") return "void";
                }
                if (rtype.starts_with("sync.Channel[") || rtype.starts_with("Channel[")) {
                    auto b1 = rtype.find('[');
                    auto b2 = rtype.rfind(']');
                    std::string elem = (b1 != std::string::npos && b2 != std::string::npos)
                        ? rtype.substr(b1 + 1, b2 - b1 - 1) : "auto";
                    if (mem->field == "recv") return elem + "!";
                    return "void";
                }
                if (rtype == "sqlite.Conn" || rtype == "Conn") {
                    if (mem->field == "query" && !call->type_args.empty()) {
                        return "[]" + cfire::fmt::format_type_ref(call->type_args[0]) + "!";
                    }
                    return "void!";
                }
                if (rtype == "webui.App" || rtype == "App") {
                    if (mem->field == "get_state" || mem->field == "open_file_dialog" ||
                        mem->field == "save_file_dialog" || mem->field == "last_emitted_payload" ||
                        mem->field == "clipboard_get") {
                        return "str";
                    }
                    if (mem->field == "call_rpc") return "str!";
                    if (mem->field == "emitted_events_count") return "i64";
                    if (mem->field == "run_webview_test") return "bool!";
                    if (mem->field == "is_clipboard_concealed") return "bool";
                    if (mem->field == "monospace_fonts") return "[]str";
                    return "void";
                }
                if (rtype == "ssh.Session" || rtype == "Session") {
                    if (mem->field == "host_key_fingerprint") return "str";
                    if (mem->field == "sftp_read_text") return "str!";
                    if (mem->field == "sftp_read_bytes") return "[]byte!";
                    if (mem->field == "is_known_host") return "bool";
                    if (mem->field == "exec") return "ssh.ExecResult!";
                    if (mem->field == "open_pty" || mem->field == "open_forward") return "ssh.Channel!";
                    if (mem->field == "sftp_list_dir") return "[]ssh.SftpEntry!";
                    if (mem->field == "close") return "void";
                    return "void!";
                }
                if (rtype == "ssh.Channel" || rtype == "Channel") {
                    if (mem->field == "read_text") return "str!";
                    if (mem->field == "read_bytes") return "[]byte!";
                    if (mem->field == "write_text" || mem->field == "write_bytes") return "usize!";
                    if (mem->field == "is_eof") return "bool";
                    if (mem->field == "resize_pty") return "void!";
                    return "void";
                }
                if (mem->field == "size") return "usize";
                if (mem->field == "empty") return "bool";
                if (mem->field == "clone") return rtype;
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    for (const auto& sfn : mod.functions) {
                        if (auto dot = sfn.name.find('.'); dot != std::string::npos) {
                            std::string htype = sfn.name.substr(0, dot);
                            std::string mname = sfn.name.substr(dot + 1);
                            if ((rtype == htype || rtype == mod.ns_alias + "." + htype || rtype.starts_with("sync." + htype)) &&
                                mname == mem->field) {
                                if (auto arrow = sfn.cf_sig.find("-> "); arrow != std::string::npos) {
                                    std::string ret = sfn.cf_sig.substr(arrow + 3);
                                    if (ret == "File") return "fs.File";
                                    if (ret == "File!") return "fs.File!";
                                    if (ret == "TcpConn!") return "net.TcpConn!";
                                    if (ret == "TcpListener!") return "net.TcpListener!";
                                    return ret;
                                }
                                return "void";
                            }
                        }
                    }
                }
                for (const auto& fn : ast.functions) {
                    if (fn.receiver && fn.receiver->type.name == rtype && fn.name == mem->field) {
                        return cfire::fmt::format_type_ref(fn.return_type);
                    }
                }
                for (const auto& pkg : ws.local_packages) {
                    for (const auto& fn : pkg.ast.functions) {
                        if (fn.receiver && (fn.receiver->type.name == rtype || pkg.alias + "." + fn.receiver->type.name == rtype) &&
                            fn.name == mem->field) {
                            return cfire::fmt::format_type_ref(fn.return_type);
                        }
                    }
                }
            }
        }
    }
    if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
        if (mem->field == "len") return "usize";
        std::string obj_t = strip_wrappers(infer_expr_cf_type(mem->object, ast, ws, env));
        for (const auto& st : ast.structs) {
            if (st.name == obj_t) {
                for (const auto& f : st.fields) {
                    if (f.name == mem->field) return cfire::fmt::format_type_ref(f.type);
                }
            }
        }
        for (const auto& pkg : ws.local_packages) {
            for (const auto& st : pkg.ast.structs) {
                if (st.name == obj_t) {
                    for (const auto& f : st.fields) {
                        if (f.name == mem->field) return cfire::fmt::format_type_ref(f.type);
                    }
                }
            }
        }
        for (const auto& en : ast.enums) {
            if (en.name == obj_t) return en.name;
        }
    }
    return "auto";
}

[[nodiscard]] inline auto cf_type_string_to_cpp(std::string_view cf_t) -> std::string {
    std::string s(cf_t);
    bool fallible = false;
    bool optional = false;
    if (s.ends_with("!")) {
        fallible = true;
        s.pop_back();
    }
    if (s.ends_with("?")) {
        optional = true;
        s.pop_back();
    }
    auto map_inner = [](std::string_view v) -> std::string {
        if (v.starts_with("[]")) {
            return std::format("std::vector<{}>", map_cf_named_type_to_cpp(v.substr(2)));
        }
        if (v.starts_with("own[") && v.ends_with("]")) {
            return std::format("std::unique_ptr<{}>", v.substr(4, v.size() - 5));
        }
        if (v.starts_with("shared[") && v.ends_with("]")) {
            return std::format("std::shared_ptr<{}>", v.substr(7, v.size() - 8));
        }
        if (v.starts_with("sync.Channel[") && v.ends_with("]")) {
            return std::format("cfire::sync::Channel<{}>", v.substr(13, v.size() - 14));
        }
        return map_cf_named_type_to_cpp(v, false);
    };
    std::string cpp;
    if (s.starts_with("(") && s.ends_with(")")) {
        std::string inner = s.substr(1, s.size() - 2);
        std::string tuple_cpp = "std::tuple<";
        std::size_t start = 0;
        bool first = true;
        while (start < inner.size()) {
            auto comma = inner.find(',', start);
            std::string part = (comma == std::string::npos) ? inner.substr(start) : inner.substr(start, comma - start);
            start = (comma == std::string::npos) ? inner.size() : comma + 1;
            while (!part.empty() && part.front() == ' ') part.erase(part.begin());
            while (!part.empty() && part.back() == ' ') part.pop_back();
            if (!first) tuple_cpp += ", ";
            first = false;
            tuple_cpp += map_inner(part);
        }
        tuple_cpp += ">";
        cpp = tuple_cpp;
    } else {
        cpp = map_inner(s);
    }
    if (optional) {
        cpp = std::format("std::optional<{}>", cpp);
    }
    if (fallible) {
        return std::format("std::expected<{}, cfire::Error>", cpp);
    }
    return cpp;
}

} // namespace cfire::lsp::detail
