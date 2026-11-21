#pragma once
// src/lsp/type_query.hpp — Type Lowering Previews, Qualifier Resolution & Expression Type Inference for LSP

#include "../ast.hpp"
#include "../clang_bridge.hpp"
#include "../docgen/catalog_types.hpp"
#include "../formatter.hpp"
#include "../sema/types.hpp"
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef CFIRE_LSP_RPC_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_LSP_RPC_INLINE
#else
#define CFIRE_LSP_RPC_INLINE inline
#endif
#endif

namespace cfire::lsp::detail {

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

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto map_cf_named_type_to_cpp(std::string_view n, bool param_borrow = false) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto lower_type_preview(const cfire::frontend::TypeRef& t, bool is_param = false, std::string_view intent = "in") -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto lower_fn_preview(const cfire::frontend::FnDecl& fn) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto cpp_return_to_cf(std::string_view cpp_t) -> std::string;

struct StrippedTypeInfo {
    std::string base;
    bool was_optional = false;
    bool was_fallible = false;
};

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto strip_wrappers_with_info(std::string t) -> StrippedTypeInfo;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto strip_wrappers(std::string t) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_stdlib_type_doc(std::string_view ns, std::string_view name)
    -> std::pair<const cfire::doc::StdModuleDoc*, const cfire::doc::StdTypeDoc*>;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto resolve_stdlib_module_by_alias(
    std::string_view alias,
    const cfire::frontend::FileAST* ast = nullptr
) -> const cfire::doc::StdModuleDoc*;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto match_stdlib_handle_type(
    std::string_view rtype,
    const cfire::doc::StdModuleDoc& mod,
    std::string_view htype
) -> bool;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_generic_arg(std::string_view rtype) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto qualify_module_types(std::string_view t, const cfire::doc::StdModuleDoc& mod) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto resolve_qualifier_type(
    std::string_view qualifier,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env,
    bool preserve_wrappers = false
) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto format_sema_type_id(
    uint32_t tid_idx,
    const cfire::frontend::TypeTable* sema_types
) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto sema_type_id_to_cpp_preview(
    uint32_t tid_idx,
    const cfire::frontend::TypeTable* sema_types
) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto infer_expr_cf_type(
    const cfire::frontend::ExprPtr& e,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env,
    const cfire::frontend::TypeTable* sema_types = nullptr
) -> std::string;

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto cf_type_string_to_cpp(std::string_view cf_t) -> std::string;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)

[[nodiscard]] inline auto unmangle_cf_type_display(std::string s) -> std::string {
    std::size_t pos = 0;
    while ((pos = s.find("cfire_pkg_", pos)) != std::string::npos) {
        std::size_t mod_start = pos + 10;
        std::size_t dunder = s.find("__", mod_start);
        if (dunder == std::string::npos) break;
        std::string_view mod = std::string_view(s).substr(mod_start, dunder - mod_start);
        s.replace(pos, (dunder + 2) - pos, std::string(mod) + ".");
        pos += mod.size() + 1;
    }
    return s;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto map_cf_named_type_to_cpp(std::string_view n, bool param_borrow) -> std::string {
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
    if (n == "void" || n == "c_void") return "void";
    if (n == "c_char" || n == "char") return "char";
    if (n == "str") return param_borrow ? "std::string_view" : "std::string";
    if (n == "path") return param_borrow ? "const std::filesystem::path&" : "std::filesystem::path";
    if (n == "fs.FileInfo" || n == "FileInfo") return "cfire::fs::FileInfo";
    if (n == "fs.File" || n == "File") return "cfire::fs::File";
    if (n == "ipc.Listener" || n == "Listener") return "cfire::ipc::Listener";
    if (n == "ipc.Conn") return "cfire::ipc::Conn";
    if (n == "ipc.PeerCred" || n == "PeerCred") return "cfire::ipc::PeerCred";
    if (n == "Error") return "cfire::Error";
    if (n == "sync.Mutex" || n == "Mutex") return "cfire::sync::Mutex";
    if (n == "sync.MutexGuard" || n == "MutexGuard") return "cfire::sync::MutexGuard";
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
    if (n == "websocket.TlsConfig") return "cfire::http::TlsConfig";
    if (auto dot = n.find('.'); dot != std::string_view::npos) {
        std::string_view ns = n.substr(0, dot);
        std::string_view rest = n.substr(dot + 1);
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            if (mod.ns_alias == ns) {
                return std::format("cfire::{}::{}", ns, rest);
            }
        }
    }
    return std::string(n);
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto format_sema_type_id(
    uint32_t tid_idx,
    const cfire::frontend::TypeTable* sema_types
) -> std::string {
    using namespace cfire::frontend;
    if (!sema_types || tid_idx == 0) return "";
    auto is_concrete = [&](const auto& self, TypeId tid, int depth) -> bool {
        if (depth > 32) return false;
        if (!tid.is_valid() || tid.is_unknown() || tid.is_error() || tid.is_foreign() || tid.is_never()) {
            return false;
        }
        const TypeDesc& d = sema_types->get(tid);
        switch (d.kind) {
            case TypeKind::Unknown:
            case TypeKind::Error:
            case TypeKind::Foreign:
            case TypeKind::Never:
                return false;
            case TypeKind::Slice:
            case TypeKind::Array:
            case TypeKind::Optional:
            case TypeKind::Fallible:
            case TypeKind::Own:
            case TypeKind::Shared:
            case TypeKind::Borrow:
            case TypeKind::MutBorrow:
            case TypeKind::RawPtr:
            case TypeKind::MutPtr:
                return d.elem_type.is_valid() && (d.kind == TypeKind::Fallible && d.elem_type.is_void() ? true : self(self, d.elem_type, depth + 1));
            case TypeKind::Map:
                return self(self, d.key_type, depth + 1) && self(self, d.elem_type, depth + 1);
            case TypeKind::Tuple:
                if (d.param_types.empty()) return false;
                for (TypeId pt : d.param_types) {
                    if (!self(self, pt, depth + 1)) return false;
                }
                return true;
            case TypeKind::Function:
                if (!d.ret_type.is_void() && !self(self, d.ret_type, depth + 1)) return false;
                for (TypeId pt : d.param_types) {
                    if (!self(self, pt, depth + 1)) return false;
                }
                return true;
            case TypeKind::GenericInst:
                for (TypeId ta : d.type_args) {
                    if (!self(self, ta, depth + 1)) return false;
                }
                return !d.base_name.empty();
            default:
                return true;
        }
    };
    TypeId tid{tid_idx};
    if (!is_concrete(is_concrete, tid, 0)) return "";
    std::string formatted = sema_types->to_string(tid);
    if (formatted.empty() || formatted == "unknown" || formatted == "<error>" ||
        formatted == "<foreign>" || formatted == "never" || formatted == "!") {
        return "";
    }
    return unmangle_cf_type_display(std::move(formatted));
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto sema_type_id_to_cpp_preview(
    uint32_t tid_idx,
    const cfire::frontend::TypeTable* sema_types
) -> std::string {
    using namespace cfire::frontend;
    if (!sema_types || tid_idx == 0) return "";
    auto lower = [&](const auto& self, TypeId tid, int depth) -> std::string {
        if (depth > 32) return "";
        if (!tid.is_valid() || tid.is_unknown() || tid.is_error() || tid.is_foreign() || tid.is_never()) {
            return "";
        }
        const TypeDesc& d = sema_types->get(tid);
        switch (d.kind) {
            case TypeKind::Void: return "void";
            case TypeKind::Bool: return "bool";
            case TypeKind::Int8: return "std::int8_t";
            case TypeKind::Int16: return "std::int16_t";
            case TypeKind::Int32: return "std::int32_t";
            case TypeKind::Int64: return "std::int64_t";
            case TypeKind::Uint8:
            case TypeKind::Byte: return "std::uint8_t";
            case TypeKind::Uint16: return "std::uint16_t";
            case TypeKind::Uint32: return "std::uint32_t";
            case TypeKind::Uint64: return "std::uint64_t";
            case TypeKind::Isize: return "std::ptrdiff_t";
            case TypeKind::Usize: return "std::size_t";
            case TypeKind::Float32: return "float";
            case TypeKind::Float64: return "double";
            case TypeKind::Str: return "std::string";
            case TypeKind::Error: return "cfire::Error";
            case TypeKind::Slice: {
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("std::vector<{}>", el);
            }
            case TypeKind::Array: {
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("std::array<{}, {}>", el, d.array_len);
            }
            case TypeKind::Map: {
                std::string k = self(self, d.key_type, depth + 1);
                std::string v = self(self, d.elem_type, depth + 1);
                return (k.empty() || v.empty()) ? "" : std::format("cfire::Map<{}, {}>", k, v);
            }
            case TypeKind::Optional: {
                const TypeDesc& ed = sema_types->get(d.elem_type);
                std::string el = (ed.kind == TypeKind::Borrow && ed.elem_type == TypeId::Str())
                    ? std::string("std::string")
                    : self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("std::optional<{}>", el);
            }
            case TypeKind::Fallible: {
                std::string el = d.elem_type.is_void() ? std::string("void") : self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("std::expected<{}, cfire::Error>", el);
            }
            case TypeKind::Own: {
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("std::unique_ptr<{}>", el);
            }
            case TypeKind::Shared: {
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("std::shared_ptr<{}>", el);
            }
            case TypeKind::Borrow: {
                if (d.elem_type == TypeId::Str()) return "std::string_view";
                const TypeDesc& ed = sema_types->get(d.elem_type);
                if (ed.kind == TypeKind::Slice) {
                    std::string inner = self(self, ed.elem_type, depth + 1);
                    return inner.empty() ? "" : std::format("cfire::slice_ref<{}>", inner);
                }
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("const {}&", el);
            }
            case TypeKind::MutBorrow: {
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : (el + "&");
            }
            case TypeKind::RawPtr: {
                const TypeDesc& ed = sema_types->get(d.elem_type);
                if (ed.name == "c_void" || d.elem_type.is_void()) return "const void*";
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : std::format("const {}*", el);
            }
            case TypeKind::MutPtr: {
                const TypeDesc& ed = sema_types->get(d.elem_type);
                if (ed.name == "c_void" || d.elem_type.is_void()) return "void*";
                std::string el = self(self, d.elem_type, depth + 1);
                return el.empty() ? "" : (el + "*");
            }
            case TypeKind::Tuple: {
                if (d.param_types.empty()) return "";
                std::string inner;
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) inner += ", ";
                    std::string pt = self(self, d.param_types[i], depth + 1);
                    if (pt.empty()) return "";
                    inner += pt;
                }
                return std::format("std::tuple<{}>", inner);
            }
            case TypeKind::Function: {
                std::string ret = d.ret_type.is_void() ? std::string("void") : self(self, d.ret_type, depth + 1);
                if (ret.empty()) return "";
                std::string params;
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) params += ", ";
                    std::string pt = self(self, d.param_types[i], depth + 1);
                    if (pt.empty()) return "";
                    params += pt;
                }
                return std::format("std::function<{}({})>", ret, params);
            }
            case TypeKind::TypeParam:
                return d.name;
            case TypeKind::NominalStruct:
            case TypeKind::NominalEnum:
            case TypeKind::Interface: {
                std::string clean = unmangle_cf_type_display(d.name);
                return map_cf_named_type_to_cpp(clean, false);
            }
            case TypeKind::GenericInst: {
                std::string base = unmangle_cf_type_display(d.base_name);
                if (base == "sync.Mutex" || base == "Mutex") {
                    base = d.type_args.empty() ? "cfire::sync::Mutex" : "cfire::sync::MutexT";
                } else if (base == "sync.Channel" || base == "Channel") {
                    base = "cfire::sync::Channel";
                } else {
                    base = map_cf_named_type_to_cpp(base, false);
                }
                if (d.type_args.empty()) return base;
                std::string args;
                for (std::size_t i = 0; i < d.type_args.size(); ++i) {
                    if (i > 0) args += ", ";
                    std::string at = self(self, d.type_args[i], depth + 1);
                    if (at.empty()) return "";
                    args += at;
                }
                return std::format("{}<{}>", base, args);
            }
            default:
                return "";
        }
    };
    return lower(lower, TypeId{tid_idx}, 0);
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto lower_type_preview(const cfire::frontend::TypeRef& t, bool is_param, std::string_view intent) -> std::string {
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

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto lower_fn_preview(const cfire::frontend::FnDecl& fn) -> std::string {
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

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto cpp_return_to_cf(std::string_view cpp_t) -> std::string {
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

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto strip_wrappers_with_info(std::string t) -> StrippedTypeInfo {
    StrippedTypeInfo info;
    while (t.ends_with("!") || t.ends_with("?")) {
        if (t.back() == '?') info.was_optional = true;
        if (t.back() == '!') info.was_fallible = true;
        t.pop_back();
    }
    if (t.starts_with("mut ")) t = t.substr(4);
    if (t.starts_with("&mut ")) t = t.substr(5);
    else if (t.starts_with("&")) t = t.substr(1);
    if (t.starts_with("own[") && t.ends_with("]")) t = t.substr(4, t.size() - 5);
    else if (t.starts_with("shared[") && t.ends_with("]")) t = t.substr(7, t.size() - 8);
    else if (t.starts_with("*mut ")) t = t.substr(5);
    else if (t.starts_with("*")) t = t.substr(1);
    info.base = std::move(t);
    return info;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto strip_wrappers(std::string t) -> std::string {
    return strip_wrappers_with_info(std::move(t)).base;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto find_stdlib_type_doc(std::string_view ns, std::string_view name)
    -> std::pair<const cfire::doc::StdModuleDoc*, const cfire::doc::StdTypeDoc*> {
    if (auto br = name.find('['); br != std::string_view::npos) name = name.substr(0, br);
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        if (mod.ns_alias != ns) continue;
        for (const auto& td : mod.type_docs) {
            if (td.bare_name() == name) return {&mod, &td};
        }
    }
    return {nullptr, nullptr};
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto resolve_stdlib_module_by_alias(
    std::string_view alias,
    const cfire::frontend::FileAST* ast
) -> const cfire::doc::StdModuleDoc* {
    if (alias.empty()) return nullptr;
    if (ast) {
        for (const auto& imp : ast->imports) {
            if (imp.is_cpp) continue;
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                bool path_match = (mod.import_path == imp.path) ||
                    std::ranges::find(mod.aliases, imp.path) != mod.aliases.end();
                if (!path_match) continue;
                std::string_view eff_alias = !imp.alias.empty() ? std::string_view(imp.alias) : std::string_view(mod.ns_alias);
                if (eff_alias == alias) return &mod;
            }
        }
    }
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        if (mod.ns_alias == alias) return &mod;
    }
    return nullptr;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto match_stdlib_handle_type(
    std::string_view rtype,
    const cfire::doc::StdModuleDoc& mod,
    std::string_view htype
) -> bool {
    std::string_view base = rtype;
    if (auto br = base.find('['); br != std::string_view::npos) {
        base = base.substr(0, br);
    }
    if (auto dot = base.find('.'); dot != std::string_view::npos) {
        std::string_view rns = base.substr(0, dot);
        std::string_view rname = base.substr(dot + 1);
        if (rns != mod.ns_alias) return false;
        if (rname == htype) return true;
        if (mod.ns_alias == "http" &&
            ((rname == "Server" && htype == "HttpServer") || (rname == "HttpServer" && htype == "Server"))) {
            return true;
        }
        return false;
    }
    if (base == htype) return true;
    if (mod.ns_alias == "http" && base == "Server" && htype == "HttpServer") return true;
    return false;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto extract_generic_arg(std::string_view rtype) -> std::string {
    auto b1 = rtype.find('[');
    auto b2 = rtype.rfind(']');
    if (b1 != std::string_view::npos && b2 != std::string_view::npos && b2 > b1 + 1) {
        return std::string(rtype.substr(b1 + 1, b2 - b1 - 1));
    }
    return "";
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto qualify_module_types(std::string_view t, const cfire::doc::StdModuleDoc& mod) -> std::string {
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

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto resolve_qualifier_type(
    std::string_view qualifier,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env,
    bool preserve_wrappers
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

    if (qualifier.size() >= 2 &&
        ((qualifier.front() == '"' && qualifier.back() == '"') ||
         (qualifier.front() == '`' && qualifier.back() == '`'))) {
        return "str";
    }
    if (qualifier.size() >= 2 && qualifier.front() == '\'' && qualifier.back() == '\'') {
        return "byte";
    }
    if (qualifier == "true" || qualifier == "false") {
        return "bool";
    }
    if (!qualifier.empty() && std::isdigit(static_cast<unsigned char>(qualifier.front()))) {
        bool is_hex = (qualifier.size() > 2 && qualifier[0] == '0' && (qualifier[1] == 'x' || qualifier[1] == 'X'));
        bool is_num = true;
        bool has_dot_or_exp = false;
        for (std::size_t k = is_hex ? 2 : 0; k < qualifier.size(); ++k) {
            char c = qualifier[k];
            if (is_hex) {
                if (!std::isxdigit(static_cast<unsigned char>(c)) && c != '_') {
                    is_num = false;
                    break;
                }
            } else {
                if (std::isdigit(static_cast<unsigned char>(c)) || c == '_') continue;
                if (c == '.') {
                    if (k + 1 == qualifier.size()) {
                        is_num = false;
                        break;
                    }
                    has_dot_or_exp = true;
                } else if (c == 'e' || c == 'E') {
                    has_dot_or_exp = true;
                    if (k + 1 < qualifier.size() && (qualifier[k + 1] == '+' || qualifier[k + 1] == '-')) ++k;
                } else {
                    is_num = false;
                    break;
                }
            }
        }
        if (is_num) return (!is_hex && has_dot_or_exp) ? "f64" : "i64";
    }

    if (qualifier.find('(') != std::string_view::npos || qualifier.find('[') != std::string_view::npos) {
        std::string wrapped = std::format("package __lsp\nfn __q() {{\n    __tmp := {}\n}}\n", qualifier);
        if (auto q_ast = cfire::frontend::parse_source("<qualifier>", wrapped)) {
            if (!q_ast->functions.empty() && !q_ast->functions[0].body.empty()) {
                if (const auto* vd = std::get_if<cfire::frontend::VarDeclStmt>(&q_ast->functions[0].body[0]->node)) {
                    if (vd->init) {
                        std::string inferred = infer_expr_cf_type(vd->init, ast, ws, env);
                        if (!preserve_wrappers) inferred = strip_wrappers(inferred);
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
    const cfire::doc::StdModuleDoc* cur_mod = nullptr;
    if (auto it = env.find(parts[0]); it != env.end()) {
        cur_type = preserve_wrappers ? it->second.cf_type : strip_wrappers(it->second.cf_type);
    } else {
        cur_mod = resolve_stdlib_module_by_alias(parts[0], &ast);
    }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        cur_type = strip_wrappers(cur_type);
        const std::string& fld = parts[i];
        bool found = false;
        std::string base_cur = cur_type;
        if (auto br = base_cur.find('['); br != std::string::npos) base_cur.erase(br);
        auto find_in_structs = [&](const std::vector<cfire::frontend::StructDecl>& structs) {
            for (const auto& st : structs) {
                if (st.name == cur_type || st.name == base_cur) {
                    for (const auto& f : st.fields) {
                        if (f.name == fld) {
                            cur_type = cfire::fmt::format_type_ref(f.type);
                            if (!preserve_wrappers) cur_type = strip_wrappers(cur_type);
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
        if (!found) {
            if (auto dot = base_cur.find('.'); dot != std::string::npos) {
                std::string_view ns = std::string_view(base_cur).substr(0, dot);
                std::string_view tn = std::string_view(base_cur).substr(dot + 1);
                if (auto [m, td] = find_stdlib_type_doc(ns, tn); m != nullptr && td != nullptr) {
                    for (const auto& f : td->fields) {
                        if (f.name == fld) {
                            cur_type = qualify_module_types(f.type, *m);
                            if (!preserve_wrappers) cur_type = strip_wrappers(cur_type);
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    if (const auto* sst = cfire::frontend::find_stdlib_struct(ns, tn)) {
                        for (const auto& sf : cfire::frontend::stdlib_struct_fields(*sst)) {
                            if (sf.name == fld) {
                                cur_type = std::string(sf.type);
                                if (!preserve_wrappers) cur_type = strip_wrappers(cur_type);
                                found = true;
                                break;
                            }
                        }
                    }
                }
            } else if (i == 1 && cur_mod != nullptr) {
                for (const auto& td : cur_mod->type_docs) {
                    if (td.bare_name() == fld) {
                        cur_type = cur_mod->ns_alias + "." + std::string(td.bare_name());
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found) break;
    }
    return cur_type;
}

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto infer_expr_cf_type(
    const cfire::frontend::ExprPtr& e,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    const std::unordered_map<std::string, LocalBindingInfo>& env,
    const cfire::frontend::TypeTable* sema_types
) -> std::string {
    using namespace cfire::frontend;
    if (!e) return "auto";
    if (sema_types && e->resolved_type_id != 0) {
        if (std::string st = format_sema_type_id(e->resolved_type_id, sema_types);
            !st.empty() && st != "void") {
            return st;
        }
    }
    if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
        if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return "str";
        if (lit->kind == TokenKind::Char) return "byte";
        if (lit->kind == TokenKind::Float) return "f64";
        if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return "bool";
        return "i64";
    }
    if (const auto* sl = std::get_if<StructLitExpr>(&e->node)) {
        std::string tname = sl->type_name;
        if (auto dot = tname.find('.'); dot != std::string::npos) {
            std::string_view al = std::string_view(tname).substr(0, dot);
            if (const auto* mod = resolve_stdlib_module_by_alias(al, &ast)) {
                tname = mod->ns_alias + tname.substr(dot);
            }
        }
        if (sl->type_name == "map" && sl->type_args.size() == 2) {
            return std::format("map[{}]{}",
                cfire::fmt::format_type_ref(sl->type_args[0]),
                cfire::fmt::format_type_ref(sl->type_args[1]));
        }
        if (!sl->type_args.empty()) {
            std::string targs;
            for (std::size_t i = 0; i < sl->type_args.size(); ++i) {
                if (i > 0) targs += ", ";
                targs += cfire::fmt::format_type_ref(sl->type_args[i]);
            }
            return std::format("{}[{}]", tname, targs);
        }
        return tname;
    }
    if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
        if (!me->result_type.empty()) return me->result_type;
        for (const auto& arm : me->arms) {
            if (arm.body.empty()) continue;
            const auto& last_s = arm.body.back();
            if (!last_s) continue;
            ExprPtr val_expr = nullptr;
            if (const auto* es = std::get_if<ExprStmt>(&last_s->node)) val_expr = es->expr;
            else if (const auto* rs = std::get_if<ReturnStmt>(&last_s->node)) val_expr = rs->value;
            if (val_expr) {
                std::string t = infer_expr_cf_type(val_expr, ast, ws, env, sema_types);
                if (!t.empty() && t != "auto" && t != "void") return t;
            }
        }
        return "auto";
    }
    if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
        if (!arr->elements.empty()) {
            return std::format("[]{}", infer_expr_cf_type(arr->elements[0], ast, ws, env, sema_types));
        }
        return "[]auto";
    }
    if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
        std::string res = "(";
        for (std::size_t i = 0; i < tup->elements.size(); ++i) {
            if (i > 0) res += ", ";
            res += infer_expr_cf_type(tup->elements[i], ast, ws, env, sema_types);
        }
        res += ")";
        return res;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        if (un->op == "!") return "bool";
        std::string inner = infer_expr_cf_type(un->operand, ast, ws, env, sema_types);
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
        return infer_expr_cf_type(bin->left, ast, ws, env, sema_types);
    }
    if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
        std::string obj_t = strip_wrappers(infer_expr_cf_type(idx->object, ast, ws, env, sema_types));
        if (obj_t == "str") return "byte";
        if (obj_t.starts_with("[]")) return obj_t.substr(2);
        if (obj_t.starts_with("[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return obj_t.substr(rb + 1);
        }
        if (obj_t.starts_with("map[")) {
            int bdepth = 0;
            for (std::size_t k = 3; k < obj_t.size(); ++k) {
                if (obj_t[k] == '[') ++bdepth;
                else if (obj_t[k] == ']') {
                    --bdepth;
                    if (bdepth == 0) return obj_t.substr(k + 1);
                }
            }
        }
        return "auto";
    }
    if (const auto* slc = std::get_if<SliceExpr>(&e->node)) {
        std::string obj_t = strip_wrappers(infer_expr_cf_type(slc->object, ast, ws, env, sema_types));
        if (obj_t == "str") return "str";
        if (obj_t.starts_with("[")) {
            if (auto rb = obj_t.find(']'); rb != std::string::npos) return "[]" + obj_t.substr(rb + 1);
        }
        return obj_t;
    }
    if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        std::string inner = infer_expr_cf_type(tr->inner, ast, ws, env, sema_types);
        if (inner.ends_with("!")) inner.pop_back();
        return inner;
    }
    if (const auto* orex = std::get_if<OrExpr>(&e->node)) {
        std::string inner = infer_expr_cf_type(orex->inner, ast, ws, env, sema_types);
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
                std::string mt = cfire::fmt::format_type_ref(call->type_args[0]);
                if (auto dot = mt.find('.'); dot != std::string::npos) {
                    std::string_view al = std::string_view(mt).substr(0, dot);
                    if (const auto* mod = resolve_stdlib_module_by_alias(al, &ast)) {
                        mt = mod->ns_alias + mt.substr(dot);
                    }
                }
                return mt;
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
                if (mem->field == "parse" && !env.contains(mid->name)) {
                    std::uint16_t pbits = cfire::frontend::builtin_recv_bits_for_type_name(mid->name);
                    if ((pbits & (cfire::frontend::BmInt | cfire::frontend::BmFloat | cfire::frontend::BmBool)) != 0) {
                        return mid->name + "!";
                    }
                }
                // 1. Check `core:*` / `ext:*` module functions (supports import aliases like `import "core:fs" as f`)
                if (!env.contains(mid->name)) {
                    if (const auto* mod = resolve_stdlib_module_by_alias(mid->name, &ast)) {
                        for (const auto& sfn : mod->functions) {
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
                                    return qualify_module_types(ret, *mod);
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
            std::string raw_rtype = infer_expr_cf_type(mem->object, ast, ws, env, sema_types);
            while (!raw_rtype.empty() && raw_rtype.back() == ' ') raw_rtype.pop_back();
            if (mem->field == "or_error" && raw_rtype.size() > 1 && raw_rtype.ends_with('?')) {
                return raw_rtype.substr(0, raw_rtype.size() - 1) + "!";
            }
            if (mem->field == "ignore_error" && raw_rtype.size() > 1 && raw_rtype.ends_with('!')) {
                std::string inner = raw_rtype.substr(0, raw_rtype.size() - 1);
                if (inner != "void") return inner + "?";
            }
            std::string rtype = strip_wrappers(raw_rtype);
            if (!rtype.empty() && rtype != "auto") {
                // builtin method return types come from the single table; `T`/`K`/`V`/`Self` resolve here.
                if (std::uint16_t bits = cfire::frontend::builtin_recv_bits_for_type_name(rtype); bits != 0) {
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
                if (rtype == "ipc.Listener" || rtype == "Listener") {
                    if (mem->field == "accept") return "ipc.Conn!";
                    if (mem->field == "endpoint") return "str";
                    if (mem->field == "is_open") return "bool";
                    if (mem->field == "close") return "void";
                }
                if (rtype == "ipc.Conn") {
                    if (mem->field == "peer_credentials") return "ipc.PeerCred!";
                    if (mem->field == "write_bytes" || mem->field == "write_text" || mem->field == "write_line" || mem->field == "read_into") return "usize!";
                    if (mem->field == "read_bytes" || mem->field == "read_exact" || mem->field == "read_bytes_until") return "[]byte!";
                    if (mem->field == "read_text" || mem->field == "read_all_text" || mem->field == "read_text_until") return "str!";
                    if (mem->field == "read_line") return "str?";
                    if (mem->field == "is_open" || mem->field == "poll") return "bool";
                    if (mem->field == "close" || mem->field == "set_timeout_ms") return "void";
                }
                if (rtype == "time.Time" || rtype == "Time") {
                    if (mem->field == "local" || mem->field == "utc" || mem->field == "add" || mem->field == "add_ms") return "time.Time";
                    if (mem->field == "format" || mem->field == "format_rfc3339") return "str";
                }
                if (rtype == "Error") {
                    if (mem->field == "code") return "i32";
                    if (mem->field == "message") return "str";
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
                    if (mem->field == "flush" || mem->field == "sync") return "void!";
                    if (mem->field == "close" || mem->field == "set_timeout_ms") return "void";
                }
                if (rtype.starts_with("sync.Channel[") || rtype.starts_with("Channel[")) {
                    auto b1 = rtype.find('[');
                    auto b2 = rtype.rfind(']');
                    std::string elem = (b1 != std::string::npos && b2 != std::string::npos)
                        ? rtype.substr(b1 + 1, b2 - b1 - 1) : "auto";
                    if (mem->field == "recv") return elem + "?";
                    return "void";
                }
                if (rtype == "sqlite.Conn" || rtype == "Conn") {
                    if (mem->field == "query" && !call->type_args.empty()) {
                        return "[]" + cfire::fmt::format_type_ref(call->type_args[0]) + "!";
                    }
                    return "void!";
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
                std::string generic_arg = extract_generic_arg(rtype);
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    for (const auto& sfn : mod.functions) {
                        if (auto dot = sfn.name.find('.'); dot != std::string::npos) {
                            std::string htype = sfn.name.substr(0, dot);
                            std::string mname = sfn.name.substr(dot + 1);
                            if (match_stdlib_handle_type(rtype, mod, htype) && mname == mem->field) {
                                if (auto arrow = sfn.cf_sig.find("-> "); arrow != std::string::npos) {
                                    std::string ret = sfn.cf_sig.substr(arrow + 3);
                                    if (!generic_arg.empty()) {
                                        if (ret == "T") return generic_arg;
                                        if (ret == "T?") return generic_arg + "?";
                                        if (ret == "T!") return generic_arg + "!";
                                        if (ret == "[]T") return "[]" + generic_arg;
                                    }
                                    if (ret == "File") return "fs.File";
                                    if (ret == "File!") return "fs.File!";
                                    if (ret == "TcpConn!") return "net.TcpConn!";
                                    if (ret == "TcpListener!") return "net.TcpListener!";
                                    return qualify_module_types(ret, mod);
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
        if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
            oid && !env.contains(oid->name)) {
            if (const auto* mod = resolve_stdlib_module_by_alias(oid->name, &ast)) {
                const std::string prefix = "const " + mem->field + ": ";
                for (const auto& sfn : mod->functions) {
                    if (sfn.name == mem->field && sfn.cf_sig.starts_with(prefix)) {
                        return qualify_module_types(sfn.cf_sig.substr(prefix.size()), *mod);
                    }
                }
            }
        }
        std::string obj_t = strip_wrappers(infer_expr_cf_type(mem->object, ast, ws, env, sema_types));
        if (auto dot = obj_t.find('.'); dot != std::string::npos) {
            std::string_view ns = std::string_view(obj_t).substr(0, dot);
            std::string_view tn = std::string_view(obj_t).substr(dot + 1);
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                if (mod.ns_alias != ns) continue;
                for (const auto& td : mod.type_docs) {
                    if (td.bare_name() != tn) continue;
                    for (const auto& f : td.fields) {
                        if (f.name == mem->field) return qualify_module_types(f.type, mod);
                    }
                }
            }
        }
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

[[nodiscard]] CFIRE_LSP_RPC_INLINE auto cf_type_string_to_cpp(std::string_view cf_t) -> std::string {
    while (!cf_t.empty() && (cf_t.front() == ' ' || cf_t.front() == '\t')) cf_t.remove_prefix(1);
    while (!cf_t.empty() && (cf_t.back() == ' ' || cf_t.back() == '\t')) cf_t.remove_suffix(1);
    if (cf_t.empty()) return "";
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
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
        if (v == "&str") return "std::string_view";
        if (v.starts_with("&mut ")) {
            return cf_type_string_to_cpp(v.substr(5)) + "&";
        }
        if (v.starts_with("&")) {
            std::string_view inner = v.substr(1);
            if (inner.starts_with("[]")) {
                return std::format("cfire::slice_ref<{}>", cf_type_string_to_cpp(inner.substr(2)));
            }
            return std::format("const {}&", cf_type_string_to_cpp(inner));
        }
        if (v.starts_with("*mut ")) {
            std::string_view inner = v.substr(5);
            return inner == "c_void" || inner == "void" ? "void*" : (cf_type_string_to_cpp(inner) + "*");
        }
        if (v.starts_with("*")) {
            std::string_view inner = v.substr(1);
            return inner == "c_void" || inner == "void" ? "const void*" : std::format("const {}*", cf_type_string_to_cpp(inner));
        }
        if (v.starts_with("[]")) {
            return std::format("std::vector<{}>", cf_type_string_to_cpp(v.substr(2)));
        }
        if (v.starts_with("[")) {
            auto rb = v.find(']');
            if (rb != std::string_view::npos && rb > 1 && rb + 1 < v.size()) {
                std::string_view len_sv = v.substr(1, rb - 1);
                return std::format("std::array<{}, {}>", cf_type_string_to_cpp(v.substr(rb + 1)), len_sv);
            }
        }
        if (v.starts_with("map[")) {
            int depth = 0;
            for (std::size_t k = 3; k < v.size(); ++k) {
                if (v[k] == '[') ++depth;
                else if (v[k] == ']') {
                    --depth;
                    if (depth == 0 && k + 1 < v.size()) {
                        return std::format("cfire::Map<{}, {}>",
                            cf_type_string_to_cpp(v.substr(4, k - 4)),
                            cf_type_string_to_cpp(v.substr(k + 1)));
                    }
                }
            }
        }
        if (v.starts_with("own[") && v.ends_with("]")) {
            return std::format("std::unique_ptr<{}>", cf_type_string_to_cpp(v.substr(4, v.size() - 5)));
        }
        if (v.starts_with("shared[") && v.ends_with("]")) {
            return std::format("std::shared_ptr<{}>", cf_type_string_to_cpp(v.substr(7, v.size() - 8)));
        }
        if (v.starts_with("sync.Channel[") && v.ends_with("]")) {
            return std::format("cfire::sync::Channel<{}>", cf_type_string_to_cpp(v.substr(13, v.size() - 14)));
        }
        if (auto br = v.find('['); br != std::string_view::npos && v.ends_with(']')) {
            std::string_view base = v.substr(0, br);
            std::string_view args_sv = v.substr(br + 1, v.size() - br - 2);
            std::string mapped_base = map_cf_named_type_to_cpp(base, false);
            if (mapped_base == "cfire::sync::Mutex") mapped_base = "cfire::sync::MutexT";
            std::string args_cpp;
            int depth = 0;
            std::size_t pstart = 0;
            bool first_arg = true;
            for (std::size_t i = 0; i < args_sv.size(); ++i) {
                char c = args_sv[i];
                if (c == '[' || c == '(') ++depth;
                else if (c == ']' || c == ')') --depth;
                else if (c == ',' && depth == 0) {
                    if (!first_arg) args_cpp += ", ";
                    first_arg = false;
                    args_cpp += cf_type_string_to_cpp(args_sv.substr(pstart, i - pstart));
                    pstart = i + 1;
                }
            }
            if (!first_arg) args_cpp += ", ";
            args_cpp += cf_type_string_to_cpp(args_sv.substr(pstart));
            return std::format("{}<{}>", mapped_base, args_cpp);
        }
        return map_cf_named_type_to_cpp(v, false);
    };
    std::string cpp;
    if (s.starts_with("(") && s.ends_with(")")) {
        std::string inner = s.substr(1, s.size() - 2);
        std::string tuple_cpp = "std::tuple<";
        std::size_t start = 0;
        int depth = 0;
        bool first = true;
        for (std::size_t i = 0; i <= inner.size(); ++i) {
            if (i < inner.size()) {
                char c = inner[i];
                if (c == '(' || c == '[') ++depth;
                else if (c == ')' || c == ']') --depth;
                if (c != ',' || depth != 0) continue;
            }
            std::string part = inner.substr(start, i - start);
            start = i + 1;
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
        if (cpp == "std::string_view") cpp = "std::string";
        cpp = std::format("std::optional<{}>", cpp);
    }
    if (fallible) {
        return std::format("std::expected<{}, cfire::Error>", cpp);
    }
    return cpp;
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)

} // namespace cfire::lsp::detail

#undef CFIRE_LSP_RPC_INLINE
