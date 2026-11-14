#pragma once
// src/codegen/expr.hpp — Expression, Call, Closure & String Interpolation Lowering for cfire::frontend::Emitter

namespace cfire::frontend {

[[nodiscard]] inline auto Emitter::extract_interpolation(const LiteralExpr& lit, int line) -> std::optional<InterpolationResult> {
    if (lit.kind != TokenKind::String || !lit.value.contains('{')) return std::nullopt;
    auto placeholders = scan_interpolation_placeholders(lit.value);
    if (placeholders.empty()) return std::nullopt;

    std::vector<ExprSnippetResult> parsed_exprs;
    if (lit.interp_snippets.size() == placeholders.size()) {
        for (const auto& snip : lit.interp_snippets) {
            std::string root = expr_root_ident(snip.expr);
            if (!root.empty() && !is_ident_in_scope(root) && !is_package_ident(root)) {
                return std::nullopt;
            }
            parsed_exprs.push_back(snip);
        }
    } else {
        parsed_exprs.reserve(placeholders.size());
        for (const auto& ph : placeholders) {
            auto parsed = parse_expr_snippet(source_path_, ph.expr_text, line);
            if (!parsed) return std::nullopt;
            std::string root = expr_root_ident(parsed->expr);
            if (!root.empty() && !is_ident_in_scope(root) && !is_package_ident(root)) {
                return std::nullopt;
            }
            parsed_exprs.push_back(std::move(*parsed));
        }
    }

    InterpolationResult res;
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < placeholders.size(); ++i) {
        res.fmt_str.append(escape_unescaped_quotes(lit.value.substr(cursor, placeholders[i].start_pos - cursor)));
        if (!placeholders[i].fmt_spec.empty()) {
            res.fmt_str += "{:" + placeholders[i].fmt_spec + "}";
        } else {
            res.fmt_str += "{}";
        }
        cursor = placeholders[i].end_pos + 1;
        res.args.push_back(emit_expr(parsed_exprs[i].expr));
    }
    res.fmt_str.append(escape_unescaped_quotes(lit.value.substr(cursor)));
    return res;
}

[[nodiscard]] inline auto Emitter::emit_expr(const ExprPtr& e) -> std::string {
    if (!e) return "";
    if (auto it = hoisted_tries_.find(e); it != hoisted_tries_.end()) {
        return it->second;
    }
    return std::visit([&](const auto& ex) -> std::string {
        using T = std::decay_t<decltype(ex)>;
        if constexpr (std::is_same_v<T, IdentExpr>) {
            if (ex.resolution) {
                const auto& res = *ex.resolution;
                if (res.kind == SymbolKind::Local || res.kind == SymbolKind::Param) {
                    std::string sname = res.canonical_name.empty() ? sanitize_cpp_ident(ex.name) : res.canonical_name;
                    if (auto it = cf_.last_use_move.find(cur_fn_ + ":" + ex.name); it != cf_.last_use_move.end() && it->second == e->id) {
                        return std::format("std::move({})", sname);
                    }
                    return sname;
                }
                if (res.kind == SymbolKind::Global || res.kind == SymbolKind::Function) {
                    std::string sname = res.canonical_name.empty() ? sanitize_cpp_ident(ex.name) : res.canonical_name;
                    if (auto it = cf_.last_use_move.find(cur_fn_ + ":" + ex.name); it != cf_.last_use_move.end() && it->second == e->id) {
                        return std::format("std::move({})", sname);
                    }
                    return sname;
                }
                if (res.kind == SymbolKind::EnumVariant) {
                    if (auto it = cf_.enums.find(res.owner_type); it != cf_.enums.end()) {
                        for (const auto& v : it->second.variants) {
                            if (v.name == ex.name) {
                                if (v.payload_types.empty()) {
                                    return std::format("{}::{}()", res.owner_type, v.name);
                                } else {
                                    return std::format("{}::{}", res.owner_type, v.name);
                                }
                            }
                        }
                    }
                    return res.canonical_name;
                }
                if (res.kind == SymbolKind::CoreNamespace) {
                    if (res.canonical_name == "cpp" || is_cpp_qualifier(res.canonical_name)) return "";
                    return res.canonical_name;
                }
            }
            std::string name = ex.name;
            if (name.starts_with("cpp.")) name.erase(0, 4);
            else if (name.starts_with("cpp::")) name.erase(0, 5);
            for (const auto& q : cf_.cpp_qualifiers) {
                if (name.starts_with(q + ".")) {
                    name.erase(0, q.size() + 1);
                    break;
                }
                if (name.starts_with(q + "::")) {
                    name.erase(0, q.size() + 2);
                    break;
                }
            }
            if (name.starts_with("http.")) {
                name = "cfire::http::" + name.substr(5);
            } else if (name.starts_with("http::")) {
                name = "cfire::http::" + name.substr(6);
            } else if (name.starts_with("url.")) {
                name = "cfire::url::" + name.substr(4);
            } else if (name.starts_with("url::")) {
                name = "cfire::url::" + name.substr(5);
            } else if (!local_vars_.contains("log") && (name == "log.Level" || name == "log.Format")) {
                name = "cfire::log::" + name.substr(4);
            } else if (!local_vars_.contains("log") && name.starts_with("log::")) {
                name = "cfire::log::" + name.substr(5);
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
            std::string sname = sanitize_cpp_ident(name);
            if (!local_vars_.contains(name) && !cur_pkg_.empty() && colliding_gv_names_.contains(name)) {
                for (const auto& gv : cf_.ast.global_vars) {
                    if (gv.name == name && gv.package_name == cur_pkg_) {
                        sname = std::format("cfire_pkg_{}__{}", cur_pkg_, sname);
                        break;
                    }
                }
            }
            if (auto it = cf_.last_use_move.find(cur_fn_ + ":" + ex.name); it != cf_.last_use_move.end() && it->second == e->id) {
                return std::format("std::move({})", sname);
            }
            return sname;
        } else if constexpr (std::is_same_v<T, LiteralExpr>) {
            if (ex.kind == TokenKind::KwNone) {
                return "cfire::none";
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
                return std::format("\"{}\"", escape_unescaped_quotes(ex.value));
            }
            if (ex.kind == TokenKind::RawString) {
                return std::format("R\"({})\"", ex.value);
            }
            // The lexer only accepts escapes C++ spells the same way, so the literal passes through.
            if (ex.kind == TokenKind::Char) return std::format("std::uint8_t{{static_cast<unsigned char>({})}}", ex.value);
            return ex.value;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            if (ex.op == "move") return std::format("std::move({})", emit_expr(ex.operand));
            if (ex.op == "&mut") return "&" + emit_expr(ex.operand);
            if (ex.op == "*") return "(*" + emit_expr(ex.operand) + ")";
            return ex.op + emit_expr(ex.operand);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            if (ex.op == "/") {
                return std::format("cfire::div({}, {})", emit_expr(ex.left), emit_expr(ex.right));
            }
            if (ex.op == "%") {
                return std::format("cfire::mod({}, {})", emit_expr(ex.left), emit_expr(ex.right));
            }
            if (ex.op == "+") {
                // CF-148: `"a" + "b"` must concatenate strings, not add two `const char[]` (which clang rejects).
                const auto* llit = ex.left ? std::get_if<LiteralExpr>(&ex.left->node) : nullptr;
                const auto* rlit = ex.right ? std::get_if<LiteralExpr>(&ex.right->node) : nullptr;
                if (llit && rlit && llit->kind == TokenKind::String && rlit->kind == TokenKind::String) {
                    return std::format("(std::string({}) + {})", emit_expr(ex.left), emit_expr(ex.right));
                }
            }
            if (ex.op == "<<" || ex.op == ">>") {  // Q11 (CF-166): out-of-range counts panic instead of UB
                // CF-183: a bare integer literal is 64-bit here, as Sema's constant shift check assumes (`1 << 50`).
                const auto* llit = ex.left ? std::get_if<LiteralExpr>(&ex.left->node) : nullptr;
                std::string lhs = emit_expr(ex.left);
                if (llit && llit->kind == TokenKind::Int) lhs = std::format("static_cast<std::int64_t>({})", lhs);
                return std::format("cfire::{}({}, {})", ex.op == "<<" ? "shl" : "shr", lhs, emit_expr(ex.right));
            }
            if (ex.op == "<" || ex.op == "<=" || ex.op == ">" || ex.op == ">=" || ex.op == "==" || ex.op == "!=") {
                // CF-183: `r < xs.len()` with a negative `r` must be true; a literal operand converts exactly.
                const bool lit = (ex.left && std::holds_alternative<LiteralExpr>(ex.left->node)) ||
                                 (ex.right && std::holds_alternative<LiteralExpr>(ex.right->node));
                if (!lit) {
                    const char* fn = ex.op == "<" ? "lt" : ex.op == "<=" ? "le" : ex.op == ">" ? "gt" : ex.op == ">=" ? "ge" : ex.op == "==" ? "eq" : "ne";
                    return std::format("cfire::cmp_{}({}, {})", fn, emit_expr(ex.left), emit_expr(ex.right));
                }
            }
            return std::format("({} {} {})", emit_expr(ex.left), ex.op, emit_expr(ex.right));
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
            if (cf_.guard_value_members.contains(&ex)) return std::format("(*{})", emit_expr(ex.object));  // Q52 `g.value`
            if (ex.resolution) {
                const auto& res = *ex.resolution;
                if (res.kind == SymbolKind::CoreNamespace) {
                    if (res.canonical_name == "cpp" || is_cpp_qualifier(res.canonical_name)) return ex.field;
                    return res.canonical_name;
                }
                if (res.kind == SymbolKind::Function || res.kind == SymbolKind::Global) {
                    return res.canonical_name;
                }
                if (res.kind == SymbolKind::EnumVariant) {
                    if (auto it = cf_.enums.find(res.owner_type); it != cf_.enums.end()) {
                        for (const auto& v : it->second.variants) {
                            if (v.name == ex.field) {
                                if (v.payload_types.empty()) {
                                    return std::format("{}::{}()", res.owner_type, v.name);
                                } else {
                                    return std::format("{}::{}", res.owner_type, v.name);
                                }
                            }
                        }
                    }
                    return res.canonical_name;
                }
                if (res.kind == SymbolKind::Field) {
                    std::string obj = emit_expr(ex.object);
                    if (res.is_auto_deref) {
                        return std::format("(*{}).{}", obj, res.canonical_name);
                    }
                    return std::format("{}.{}", obj, res.canonical_name);
                }
            }
            if (const auto* sub_mem = std::get_if<MemberExpr>(&ex.object->node)) {
                if (const auto* pkg_id = std::get_if<IdentExpr>(&sub_mem->object->node);
                    pkg_id && !local_vars_.contains(pkg_id->name) && cf_.enums.contains(sub_mem->field)) {
                    return std::format("{}::{}()", sub_mem->field, ex.field);
                }
            }
            if (const auto* id = std::get_if<IdentExpr>(&ex.object->node)) {
                if (id->name == "cpp" || is_cpp_qualifier(id->name)) {
                    return ex.field;
                }
                if (cf_.enums.contains(id->name)) {
                    return std::format("{}::{}()", id->name, ex.field);
                }
                if (is_vendor_alias(id->name)) {
                    return ex.field;
                }
                if (!local_vars_.contains(id->name) && is_core_ns_ident(id->name)) {
                    std::string actual_ns = id->name;
                    if (auto it = core_aliases_.find(id->name); it != core_aliases_.end()) {
                        actual_ns = it->second;
                    } else if (auto it2 = cf_.core_aliases.find(id->name); it2 != cf_.core_aliases.end()) {
                        actual_ns = it2->second;
                    }
                    if (actual_ns == "os" && (ex.field == "stdin" || ex.field == "stdout" || ex.field == "stderr")) {
                        return std::format("cfire::os::{}_file", ex.field);
                    }
                    return std::format("cfire::{}::{}", actual_ns, ex.field);
                }
                if (!local_vars_.contains(id->name)) {
                    for (const auto& gv : cf_.ast.global_vars) {
                        if (gv.name == ex.field && gv.package_name == id->name) {
                            if (colliding_gv_names_.contains(ex.field)) {
                                return std::format("cfire_pkg_{}__{}", id->name, sanitize_cpp_ident(ex.field));
                            }
                            return sanitize_cpp_ident(ex.field);
                        }
                    }
                    for (const auto& gv : cf_.ast.global_vars) {
                        if (gv.name == ex.field) return sanitize_cpp_ident(ex.field);
                    }
                }
            }
            if (std::string prim = primitive_prefix_cpp(ex.object); !prim.empty()) {
                const bool is_float = prim == "float" || prim == "double";
                if (const auto* c = find_builtin_assoc_const(is_float ? BmFloat : BmInt, ex.field)) {
                    return std::vformat(c->cpp, std::make_format_args(prim));
                }
            }
            std::string obj = emit_expr(ex.object);
            if (obj == "cpp") {
                return ex.field;
            }
            if (obj.starts_with("cpp::")) {
                obj = obj.substr(5);
            }
            if (const auto* id = std::get_if<IdentExpr>(&ex.object->node)) {
                if ((optional_vars_.contains(id->name) || smart_ptr_vars_.contains(id->name) || ptr_vars_.contains(id->name)) &&
                    ex.field != "has_value" && ex.field != "value" && ex.field != "value_or" &&
                    ex.field != "reset" && ex.field != "get" && ex.field != "use_count" && ex.field != "release") {
                    obj = std::format("(*{})", obj);
                }
            } else if (const auto* sub_mem = std::get_if<MemberExpr>(&ex.object->node)) {
                if ((smart_ptr_field_names_.contains(sub_mem->field) || ptr_field_names_.contains(sub_mem->field)) &&
                    ex.field != "reset" && ex.field != "get" && ex.field != "use_count" && ex.field != "release") {
                    obj = std::format("(*{})", obj);
                }
            }
            if (ex.is_namespace) return obj + "::" + ex.field;
            if (obj.ends_with("::Response") || obj.ends_with("::Request") || obj.ends_with("::Router") || obj.ends_with("::HttpServer") ||
                obj.ends_with("::ServerContext") || obj.ends_with("::Waveform") || obj.ends_with("::SfxPreset") || obj.ends_with("::Provider") ||
                obj == "Response" || obj == "Request" || obj == "Router" || obj == "HttpServer" || obj == "ServerContext" ||
                obj == "Waveform" || obj == "SfxPreset" || obj == "Provider") {
                return obj + "::" + ex.field;
            }
            bool is_user_field = false;
            for (const auto& st : cf_.ast.structs) {
                for (const auto& f : st.fields) {
                    if (f.name == ex.field) {
                        is_user_field = true;
                        break;
                    }
                }
                if (is_user_field) break;
            }
            if (!is_user_field && ex.field == "len") return obj + ".size()";
            if (!is_user_field && ex.field == "empty") return obj + ".empty";
            return obj + "." + sanitize_cpp_ident(ex.field);
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
                return std::format("cfire::detail::as_byte_elem({}[{}])", emit_expr(ex.object), emit_expr(ex.index));
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
            if (tname.starts_with("cpp.")) tname.erase(0, 4);
            else if (tname.starts_with("cpp::")) tname.erase(0, 5);
            for (const auto& q : cf_.cpp_qualifiers) {
                if (tname.starts_with(q + ".")) {
                    tname.erase(0, q.size() + 1);
                    break;
                }
                if (tname.starts_with(q + "::")) {
                    tname.erase(0, q.size() + 2);
                    break;
                }
            }
            if (tname == "http.Request" || tname == "http::Request") tname = "cfire::http::Request";
            if (tname == "http.Response" || tname == "http::Response") tname = "cfire::http::Response";
            if (tname == "http.Router" || tname == "http::Router") tname = "cfire::http::Router";
            if (tname == "http.HttpServer" || tname == "http::HttpServer") tname = "cfire::http::HttpServer";
            if (tname == "url.Url" || tname == "url::Url") tname = "cfire::url::Url";
            auto sep_pos = tname.find_first_of(".:");
            if (sep_pos != std::string::npos) {
                bool is_dcolon = (tname[sep_pos] == ':' && sep_pos + 1 < tname.size() && tname[sep_pos + 1] == ':');
                std::string mod = tname.substr(0, sep_pos);
                std::string rest = tname.substr(sep_pos + (is_dcolon ? 2 : 1));
                if (auto it = core_aliases_.find(mod); it != core_aliases_.end()) {
                    mod = it->second;
                } else if (auto it2 = cf_.core_aliases.find(mod); it2 != cf_.core_aliases.end()) {
                    mod = it2->second;
                }
                if (used_core_.contains(mod)) {
                    tname = std::format("cfire::{}::{}", mod, rest);
                } else if (is_local_type(rest) || is_vendor_alias(mod) || is_cpp_qualifier(mod)) {
                    tname = rest;
                } else if (cfire::frontend::StdlibCatalogIndex::instance().has_namespace(mod)) {
                    tname = std::format("cfire::{}::{}", mod, rest);  // CF-188: `http.ServerLimits{}`, `aes.GcmResult{}`
                } else {
                    tname = std::format("{}::{}", mod, rest);
                }
            } else {
                if (tname == "Channel" && used_core_.contains("sync")) {
                    tname = "cfire::sync::Channel";
                } else if ((tname == "Set" || tname == "PriorityQueue" || tname == "MinQueue" || tname == "Deque") && used_core_.contains("collections")) {
                    tname = std::format("cfire::collections::{}", tname);
                }
            }
            if (!ex.type_args.empty()) {
                std::string targs;
                for (std::size_t i = 0; i < ex.type_args.size(); ++i) {
                    if (i > 0) targs += ", ";
                    targs += lower_owning_type(ex.type_args[i]);
                }
                tname = std::format("{}<{}>", tname, targs);
            }
            if (ex.fields.empty()) return tname + "{}";
            // C++ designated initializers must follow declaration order; cfire literals may list fields in any order.
            std::vector<std::string> decl_order;
            if (auto sit = cf_.struct_tags.find(ex.type_name); sit != cf_.struct_tags.end()) {
                for (const auto& t : sit->second) decl_order.push_back(t.field_name);
            } else if (auto sep = ex.type_name.find_first_of(".:"); sep != std::string::npos) {
                std::string mod = ex.type_name.substr(0, sep);
                std::string rest = ex.type_name.substr(ex.type_name.find_first_not_of(".:", sep));
                if (auto it = core_aliases_.find(mod); it != core_aliases_.end()) mod = it->second;
                if (const auto* sst = find_stdlib_struct(mod, rest)) {
                    tname = std::string(sst->cpp);
                    for (const auto& f : stdlib_struct_fields(*sst)) decl_order.emplace_back(f.name);
                }
            }
            std::vector<std::size_t> order(ex.fields.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            if (!decl_order.empty()) {
                auto rank = [&](std::size_t i) {
                    auto it = std::find(decl_order.begin(), decl_order.end(), ex.fields[i].name);
                    return static_cast<std::size_t>(it - decl_order.begin());
                };
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return rank(a) < rank(b); });
            }
            std::string inits;
            for (std::size_t k = 0; k < order.size(); ++k) {
                const auto& fld = ex.fields[order[k]];
                if (k > 0) inits += ", ";
                std::string fval = emit_expr(fld.value);
                if (fval == "std::nullopt" && ptr_field_names_.contains(fld.name)) {
                    fval = "nullptr";
                }
                inits += std::format(".{} = {}", sanitize_cpp_ident(fld.name), fval);
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
            return emit_closure(ex, e ? e->line : -1);
        } else if constexpr (std::is_same_v<T, OrExpr>) {
            if (ex.fallback_val) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&ex.fallback_val->node);
                    arr_lit && arr_lit->elements.empty()) {
                    return std::format("cfire::unwrap_or_default({})", emit_expr(ex.inner));
                }
                return std::format("cfire::unwrap_or({}, {})", emit_expr(ex.inner), emit_expr(ex.fallback_val));
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

[[nodiscard]] inline auto Emitter::emit_call(const CallExpr& call) -> std::string {
    if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
        if (id->name == "println" || id->name == "print" || id->name == "eprintln" || id->name == "eprint") {
            bool is_err = (id->name == "eprintln" || id->name == "eprint");
            std::string fn = (id->name == "print" || id->name == "eprint") ? "std::print" : "std::println";
            std::string stream_prefix = is_err ? "stderr, " : "";
            if (call.args.empty()) {
                return is_err ? std::format("{}(stderr)", fn) : std::format("{}()", fn);
            }
            const auto* first_lit = (call.args[0].value) ? std::get_if<LiteralExpr>(&call.args[0].value->node) : nullptr;
            if (first_lit && (first_lit->kind == TokenKind::String || first_lit->kind == TokenKind::RawString)) {
                if (first_lit->kind == TokenKind::RawString) {
                    std::string trailing;
                    for (std::size_t i = 1; i < call.args.size(); ++i) {
                        trailing += ", " + emit_call_arg(call.args[i]);
                    }
                    return std::format("{}({}R\"({})\"{})", fn, stream_prefix, first_lit->value, trailing);
                }
                auto interp = extract_interpolation(*first_lit, call.args[0].line);
                std::string fmt_str = interp ? interp->fmt_str : escape_unescaped_quotes(first_lit->value);
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
                    return std::format("{}({}\"{}\")", fn, stream_prefix, fmt_str);
                }
                return std::format("{}({}\"{}\", {})", fn, stream_prefix, fmt_str, all_args);
            }

            // First argument is not a string literal
            if (call.args.size() == 1) {
                return std::format("{}({}\"{{}}\", {})", fn, stream_prefix, emit_call_arg(call.args[0]));
            }
            std::string runtime_fn = (id->name == "print" || id->name == "eprint") ? "cfire::runtime_print" : "cfire::runtime_println";
            std::string trailing;
            for (std::size_t i = 1; i < call.args.size(); ++i) {
                trailing += ", " + emit_call_arg(call.args[i]);
            }
            return std::format("{}({}{}{})", runtime_fn, stream_prefix, emit_call_arg(call.args[0]), trailing);
        }
        if (id->name == "format") {
            if (call.args.empty()) return "\"\"";
            const auto* first_lit = (call.args[0].value) ? std::get_if<LiteralExpr>(&call.args[0].value->node) : nullptr;
            if (first_lit && (first_lit->kind == TokenKind::String || first_lit->kind == TokenKind::RawString)) {
                if (first_lit->kind == TokenKind::RawString) {
                    std::string trailing;
                    for (std::size_t i = 1; i < call.args.size(); ++i) {
                        trailing += ", " + emit_call_arg(call.args[i]);
                    }
                    return std::format("std::format(R\"({})\"{})", first_lit->value, trailing);
                }
                auto interp = extract_interpolation(*first_lit, call.args[0].line);
                std::string fmt_str = interp ? interp->fmt_str : escape_unescaped_quotes(first_lit->value);
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
                    return std::format("std::format(\"{}\")", fmt_str);
                }
                return std::format("std::format(\"{}\", {})", fmt_str, all_args);
            }
            // First argument is not a literal
            if (call.args.size() == 1) {
                return std::format("std::format(\"{{}}\", {})", emit_call_arg(call.args[0]));
            }
            std::string trailing;
            for (std::size_t i = 1; i < call.args.size(); ++i) {
                trailing += ", " + emit_call_arg(call.args[i]);
            }
            return std::format("cfire::runtime_format({}{})", emit_call_arg(call.args[0]), trailing);
        }
        if (id->name == "panic") {
            int line = call.callee ? call.callee->line : 0;
            std::string escaped_file = escape_line_path(source_path_);
            if (call.args.empty()) {
                return std::format("cfire::panic(\"explicit panic\", \"{}\", {})", escaped_file, line);
            }
            std::string msg_expr = emit_call_arg(call.args[0]);
            return std::format("cfire::panic({}, \"{}\", {})", msg_expr, escaped_file, line);
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
            std::string file_str = source_path_;
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
            std::string file_str = source_path_;
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
        if (id->name == "make" && call.type_args.size() == 1 && !local_vars_.contains("make")) {
            return emit_make(call);
        }
        if (id->name == "bool" && call.args.size() == 1) {
            return std::format("cfire::bool_({})", emit_call_arg(call.args[0]));
        }
        if (id->name == "byte" && call.args.size() == 1) {
            return std::format("cfire::byte({})", emit_call_arg(call.args[0]));
        }
        if (id->name == "str" && call.args.size() == 1) {
            return std::format("cfire::str_ctor({})", emit_call_arg(call.args[0]));
        }
        if (id->name == "[]byte" && call.args.size() == 1) {
            return std::format("cfire::to_byte_vec({})", emit_call_arg(call.args[0]));
        }
        if (id->name == "[]u8" && call.args.size() == 1) {
            return std::format("cfire::to_u8_vec({})", emit_call_arg(call.args[0]));
        }
        if ((id->name == "error" || id->name == "Err") && (call.args.size() == 1 || call.args.size() == 2)) {
            if (call.args.size() == 1) {
                return std::format("cfire::make_error({})", emit_call_arg(call.args[0]));
            } else {
                return std::format("cfire::make_error({}, {})", emit_call_arg(call.args[0]), emit_call_arg(call.args[1]));
            }
        }
        if (id->name == "black_box" && call.args.size() == 1) {
            return std::format("cfire::black_box({})", emit_call_arg(call.args[0]));
        }
        if (!local_vars_.contains(id->name) && !cur_pkg_.empty() && colliding_fn_names_.contains(id->name)) {
            if (cf_.fn_sigs.contains(cur_pkg_ + "." + id->name)) {
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                if (call.block_arg) {
                    if (!args.empty()) args += ", ";
                    args += emit_closure(*call.block_arg);
                }
                return std::format("cfire_pkg_{}__{}({})", cur_pkg_, sanitize_cpp_ident(id->name), args);
            }
        }
    }

    if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
        if (mem->resolution && mem->resolution->kind == SymbolKind::Method) {
            std::string args = emit_expr(mem->object);
            if (const auto* oid = std::get_if<IdentExpr>(&mem->object->node); oid && optional_vars_.contains(oid->name)) {
                args = std::format("(*{})", args);
            }
            for (const auto& a : call.args) {
                args += ", " + emit_call_arg(a);
            }
            if (call.block_arg) {
                args += ", " + emit_closure(*call.block_arg);
            }
            return std::format("{}({})", mem->resolution->canonical_name, args);
        }
        // Vendor (`rl.Fn`) and `cpp.Fn` calls must fall through to the generic path below,
        // which applies C FFI argument coercion (`cfire::c_arg` / `cfire::c_str`).
        bool is_foreign_object = false;
        if (const auto* oid = std::get_if<IdentExpr>(&mem->object->node)) {
            is_foreign_object = !local_vars_.contains(oid->name) && (is_cpp_qualifier(oid->name) || is_vendor_alias(oid->name));
        }
        if (mem->resolution && mem->resolution->kind == SymbolKind::Function && !is_foreign_object) {
            std::string args;
            for (std::size_t i = 0; i < call.args.size(); ++i) {
                if (i > 0) args += ", ";
                args += emit_call_arg(call.args[i]);
            }
            if (call.block_arg) {
                if (!args.empty()) args += ", ";
                args += emit_closure(*call.block_arg);
            }
            return std::format("{}({})", mem->resolution->canonical_name, args);
        }
        if (mem->field == "from_raw" && call.args.size() == 1) {
            if (const auto* ix = std::get_if<IndexExpr>(&mem->object->node)) {
                if (const auto* base = ix->object ? std::get_if<IdentExpr>(&ix->object->node) : nullptr; base && base->name == "own" && ix->index) {
                    if (const auto* targ = std::get_if<IdentExpr>(&ix->index->node)) {
                        TypeRef tr;
                        tr.name = targ->name;
                        return std::format("std::unique_ptr<{}>({})", lower_owning_type(tr), emit_call_arg(call.args[0]));
                    }
                }
            }
        }
        if (mem->field == "parse" && call.args.size() == 1) {
            if (std::string prim = primitive_prefix_cpp(mem->object); !prim.empty()) {
                return std::format("cfire::bm::parse<{}>({})", prim, emit_call_arg(call.args[0]));
            }
        }
        // W4: methods on builtin types (src/sema/builtin_methods.hpp) lower to `cfire::bm::<name>(recv, args...)`.
        if (mem->resolution && mem->resolution->kind == SymbolKind::Method && mem->resolution->owner_type == "builtin") {
            std::string args = emit_expr(mem->object);
            if (const auto* oid = std::get_if<IdentExpr>(&mem->object->node); oid && optional_vars_.contains(oid->name)) {
                args = std::format("(*{})", args);
            }
            for (const auto& a : call.args) args += ", " + emit_call_arg(a);
            if (call.block_arg) args += ", " + emit_closure(*call.block_arg);
            return std::format("{}({})", mem->resolution->canonical_name, args);
        }
        bool is_user_method = cf_.method_owners.contains(mem->field) || cf_.method_owner.contains(mem->field);
        if (!is_user_method && mem->field == "len" && call.args.empty()) {
            return std::format("{}.size()", emit_expr(mem->object));
        }
        if (!is_user_method && mem->field == "is_empty" && call.args.empty()) {
            return std::format("{}.empty()", emit_expr(mem->object));
        }
        if (!is_user_method && mem->field == "byte_ptr" && call.args.empty()) {
            used_core_.insert("str");
            return std::format("cfire::str::byte_ptr({})", emit_expr(mem->object));
        }
        if (!is_user_method && mem->field == "byte_ptr_at" && call.args.size() == 1) {
            used_core_.insert("str");
            return std::format("cfire::str::byte_ptr_at({}, {})", emit_expr(mem->object), emit_call_arg(call.args[0]));
        }
        if (!is_user_method && (mem->field == "data" || mem->field == "data_ptr") && call.args.empty()) {
            return std::format("({}).data()", emit_expr(mem->object));
        }
        // Check if this is an Enum Variant constructor e.g. `Shape.Circle(2.0)` or `p.Shape.Circle(2.0)`
        if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
            if (const auto* pkg_id = std::get_if<IdentExpr>(&sub_mem->object->node);
                pkg_id && !local_vars_.contains(pkg_id->name) && cf_.enums.contains(sub_mem->field)) {
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                return std::format("{}::{}({})", sub_mem->field, mem->field, args);
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && cf_.enums.contains(id->name)) {
            std::string args;
            for (std::size_t i = 0; i < call.args.size(); ++i) {
                if (i > 0) args += ", ";
                args += emit_call_arg(call.args[i]);
            }
            return std::format("{}::{}({})", id->name, mem->field, args);
        }

        bool is_ns_object = false;
        if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
            if (!local_vars_.contains(id->name) && (is_core_ns_ident(id->name) || is_vendor_alias(id->name))) {
                is_ns_object = true;
            }
            // Check if `pkg.fn(args...)` refers to a merged local package function
            if (!is_ns_object && !local_vars_.contains(id->name) && cf_.fn_sigs.contains(id->name + "." + mem->field)) {
                std::string args;
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i > 0) args += ", ";
                    args += emit_call_arg(call.args[i]);
                }
                if (call.block_arg) {
                    if (!args.empty()) args += ", ";
                    args += emit_closure(*call.block_arg);
                }
                std::string target_fn = colliding_fn_names_.contains(mem->field)
                    ? std::format("cfire_pkg_{}__{}", id->name, sanitize_cpp_ident(mem->field))
                    : sanitize_cpp_ident(mem->field);
                return std::format("{}({})", target_fn, args);
            }
        }

        // Check if this is a user-defined receiver method `obj.method(args...)` -> `cfire_method_<name>(obj, args...)`
        // Overloaded cfire_method_<name> resolves via C++ overload resolution natively across multiple receiver types!
        if (!is_ns_object) {
            if (auto owners_it = cf_.method_owners.find(mem->field); owners_it != cf_.method_owners.end()) {
                std::string args = emit_expr(mem->object);
                if (const auto* oid = std::get_if<IdentExpr>(&mem->object->node); oid && optional_vars_.contains(oid->name)) {
                    args = std::format("(*{})", args);
                }
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
                if (const auto* oid = std::get_if<IdentExpr>(&mem->object->node); oid && optional_vars_.contains(oid->name)) {
                    args = std::format("(*{})", args);
                }
                for (const auto& a : call.args) {
                    args += ", " + emit_call_arg(a);
                }
                if (call.block_arg) {
                    args += ", " + emit_closure(*call.block_arg);
                }
                return std::format("cfire_method_{}({})", mem->field, args);
            }
        }

        // Check if `pkg.fn(args...)` refers to a merged local package function in `cf_.fn_sigs`
        if (!is_ns_object) {
            if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && !local_vars_.contains(id->name) && cf_.fn_sigs.contains(mem->field)) {
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
        }

        // W4: a `str` or numeric method on a receiver Sema could not type (a field of a stdlib struct such as
        // `res.stdout.trim()`) still lowers to `cfire::bm`. Names that containers also use (`insert`, `contains`,
        // `len`, ...) keep the member call, since the receiver may be a stdlib collection.
        if (!is_ns_object && !is_user_method) {
            bool scalar_only = false;
            for (const auto& bm : kBuiltinMethods) {
                if (bm.name != mem->field) continue;
                if ((bm.recv & (BmSlice | BmArray | BmMap)) != 0 || !bm.lowered) { scalar_only = false; break; }
                scalar_only = true;
            }
            for (const auto& bm : kBuiltinMethods) {
                if (!scalar_only || bm.name != mem->field) continue;
                if (StdlibCatalogIndex::instance().unambiguous_methods.contains(mem->field)) break;
                std::string args = emit_expr(mem->object);
                for (const auto& a : call.args) args += ", " + emit_call_arg(a);
                if (call.block_arg) args += ", " + emit_closure(*call.block_arg);
                return std::format("cfire::bm::{}({})", bm.name, args);
            }
        }
        std::string obj = emit_expr(mem->object);
        if (!is_ns_object && (mem->field == "index_of" || mem->field == "last_index_of" ||
                              mem->field == "parse_i64" || mem->field == "parse_i32" ||
                              mem->field == "parse_u64" || mem->field == "parse_f64" ||
                              mem->field == "parse_bool")) {
            used_core_.insert("str");
            std::string args = obj;
            for (const auto& a : call.args) {
                args += ", " + emit_call_arg(a);
            }
            return std::format("cfire::str::{}({})", mem->field, args);
        }
        if (mem->field == "push" && call.args.size() == 1) {
            return std::format("cfire::vec_push({}, {})", obj, emit_call_arg(call.args[0]));
        }
        if (mem->field == "pop" && call.args.empty()) {
            // CF-183: an untyped receiver may be a slice, a string or a stdlib queue; `bm::pop` handles all three.
            return std::format("cfire::bm::pop({})", obj);
        }
        if (mem->field == "with_ext" && call.args.size() == 1) {
            return std::format("cfire::path_with_ext({}, {})", obj, emit_call_arg(call.args[0]));
        }
        if (mem->field == "bytes" && call.args.empty()) {
            return std::format("cfire::to_bytes({})", obj);
        }
        if ((mem->field == "wrapping_shl" || mem->field == "wrapping_shr" ||
             mem->field == "checked_shl" || mem->field == "checked_shr") && call.args.size() == 1 && call.type_args.empty()) {
            return std::format("cfire::{}({}, {})", mem->field, obj, emit_call_arg(call.args[0]));
        }
        if ((mem->field == "wrapping_as" || mem->field == "saturating_as" || mem->field == "try_as") &&
            call.type_args.size() == 1 && call.args.empty()) {
            // Q9 (CF-165): explicit numeric conversions with defined out-of-range behavior.
            return std::format("cfire::{}<{}>({})", mem->field, lower_owning_type(call.type_args[0]), obj);
        }
        if ((mem->field == "parse" || mem->field == "try_parse") && call.type_args.size() == 1) {
            if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && id->name == "cli") {
                if (call.args.empty()) {
                    return std::format("cfire::cli::{}<{}>(argc, argv)", mem->field, call.type_args[0].name);
                }
                std::string arg0 = emit_expr(call.args[0].value);
                return std::format("cfire::cli::{}<{}>({})", mem->field, call.type_args[0].name, arg0);
            }
        }
        if (mem->field == "tx" && call.args.empty() && call.block_arg && call.block_arg->params.size() == 1) {
            std::string tx_param = call.block_arg->params[0];
            std::ostringstream sb;
            sb << std::format("{}.tx([&](cfire::sqlite::Conn& {}) -> std::expected<void, cfire::Error> {{\n", obj, tx_param);
            ++indent_;
            Emitter sub = make_sub_emitter(false);
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
        if (mem->field == "spawn") {
            const ClosureExpr* spawn_cl = nullptr;
            if (call.block_arg) {
                spawn_cl = &(*call.block_arg);
            } else if (call.args.size() == 1 && call.args[0].value) {
                spawn_cl = std::get_if<ClosureExpr>(&call.args[0].value->node);
            }
            if (spawn_cl) {
                std::ostringstream sb;
                // Q51 slot captures: the task writes `xs[i]` of the real slice (Sema proved the slot is its own).
                std::string caps = view_param_captures(spawn_cl->body);
                for (const auto& sc : spawn_cl->slot_captures) caps += ", &" + sc.first;
                const auto vsr = value_scope_results_.find(obj);
                if (vsr != value_scope_results_.end()) {
                    // Q51: the task's value goes to the scope's result, in spawn order.
                    sb << std::format("{}.add({}.spawn([={}]() mutable -> {} {{\n", vsr->second.first, obj, caps, vsr->second.second);
                } else {
                    sb << std::format("{}.spawn([={}]() mutable {{\n", obj, caps);
                }
                ++indent_;
                Emitter sub = make_sub_emitter(false);
                for (const auto& st : spawn_cl->body) {
                    sub.emit_stmt(st, false);
                }
                sb << sub.buf_.str();
                --indent_;
                for (int i = 0; i < indent_; ++i) sb << "    ";
                sb << (vsr != value_scope_results_.end() ? "}))" : "})");
                last_source_line_ = -1;
                return sb.str();
            }
        }
    }

    bool is_vendor_call = false;
    if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
        if (const auto* id = std::get_if<IdentExpr>(&mem->object->node); id && (is_cpp_qualifier(id->name) || is_vendor_alias(id->name))) {
            is_vendor_call = true;
        }
    } else if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
        if (id->name.starts_with("cpp.") || id->name.starts_with("cpp::") || is_cpp_qualifier(id->name.substr(0, id->name.find('.'))) || is_vendor_alias(id->name.substr(0, id->name.find("::")))) {
            is_vendor_call = true;
        }
    }

    std::string callee = emit_expr(call.callee);
    if (callee.starts_with("cpp.")) {
        callee = callee.substr(4);
    } else if (callee.starts_with("cpp::")) {
        callee = callee.substr(5);
    }
    for (const auto& q : cf_.cpp_qualifiers) {
        if (callee.starts_with(q + ".")) {
            callee = callee.substr(q.size() + 1);
            break;
        }
    }
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
        if (callee == "cfire::sync::Mutex" || callee == "sync.Mutex") {
            callee = "cfire::sync::MutexT";
        }
        callee = std::format("{}<{}>", callee, targs);
    }
    // Labeled arguments to stdlib functions (`http.get(url, verify_peer: false)`) are placed by parameter name,
    // and skipped parameters before them get their declared defaults.
    if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node);
        mem && std::any_of(call.args.begin(), call.args.end(), [](const CallArg& a) { return !a.label.empty(); })) {
        if (const auto* oid = std::get_if<IdentExpr>(&mem->object->node); oid && !local_vars_.contains(oid->name)) {
            std::string ns = oid->name;
            if (auto it = core_aliases_.find(ns); it != core_aliases_.end()) ns = it->second;
            if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(ns, mem->field)) {
                std::vector<std::string> vals(sig->params.size());
                std::size_t next_pos = 0;
                std::size_t last = 0;
                for (const auto& a : call.args) {
                    std::size_t slot = next_pos;
                    if (!a.label.empty()) {
                        for (std::size_t k = 0; k < sig->params.size(); ++k) {
                            if (sig->params[k].name == a.label) { slot = k; break; }
                        }
                    } else {
                        ++next_pos;
                    }
                    if (slot < vals.size()) {
                        vals[slot] = emit_call_arg(a);
                        last = std::max(last, slot + 1);
                    }
                }
                std::string out;
                for (std::size_t k = 0; k < last; ++k) {
                    std::string v = vals[k];
                    if (v.empty()) {
                        const std::string& d = sig->params[k].default_text;
                        v = (d.empty() || d == "[]") ? std::string("{}") : d;
                    }
                    out += (k > 0 ? ", " : "") + v;
                }
                if (call.block_arg) out += (out.empty() ? "" : ", ") + emit_closure(*call.block_arg);
                return std::format("{}({})", callee, out);
            }
        }
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
            } else if (ptype.ends_with('*') && !ptype.contains("(*") && ptype != "const char *" && ptype != "const char*") {
                arg_val = std::format("cfire::c_ptr<{}>({})", ptype, arg_val);  // Q54: a slice / str is its buffer pointer
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

[[nodiscard]] inline auto Emitter::infer_closure_expr_cpp_type(
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
        if (lit->kind == TokenKind::Char) return "std::uint8_t";
        return "std::int64_t";
    }
    if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
        if (auto it = local_types.find(id->name); it != local_types.end()) {
            return it->second;
        }
    }
    if (const auto* c = std::get_if<CallExpr>(&e->node)) {
        if (const auto* cid = std::get_if<IdentExpr>(&c->callee->node)) {
            if (cid->name == "str") return "std::string";
            if (cid->name == "i64") return "std::int64_t";
            if (cid->name == "i32") return "std::int32_t";
            if (cid->name == "f64") return "double";
            if (cid->name == "bool" || cid->name == "bool_") return "bool";
            if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end()) {
                TypeRef rt = fit->second.return_type;
                rt.is_fallible = false;
                return lower_owning_type(rt);
            }
        } else if (const auto* mem = std::get_if<MemberExpr>(&c->callee->node)) {
            if (mem->field == "stringify" || mem->field == "encode" || mem->field == "getenv" ||
                mem->field == "read_text" || mem->field == "read_text_until" || mem->field == "trim" || mem->field == "to_lower" ||
                mem->field == "to_upper" || mem->field == "replace" || mem->field == "join") {
                return "std::string";
            }
            if (mem->field == "len" || mem->field == "size") return "std::size_t";
            if (mem->field == "contains" || mem->field == "starts_with" || mem->field == "ends_with" || mem->field == "exists") {
                return "bool";
            }
            if (auto oit = cf_.method_owner.find(mem->field); oit != cf_.method_owner.end()) {
                if (auto mit = cf_.methods.find(oit->second + "::" + mem->field); mit != cf_.methods.end()) {
                    TypeRef rt = mit->second.return_type;
                    rt.is_fallible = false;
                    return lower_owning_type(rt);
                }
            }
        }
    }
    if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
        if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
            bin->op == ">" || bin->op == ">=" || bin->op == "&&" || bin->op == "||") {
            return "bool";
        }
        return infer_closure_expr_cpp_type(bin->left, local_types);
    }
    if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
        if (un->op == "!") return "bool";
        return infer_closure_expr_cpp_type(un->operand, local_types);
    }
    if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
        std::string t = infer_closure_expr_cpp_type(or_ex->inner, local_types);
        return !t.empty() ? t : infer_closure_expr_cpp_type(or_ex->fallback_val, local_types);
    }
    if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
        return infer_closure_expr_cpp_type(tr->inner, local_types);
    }
    return "";
}

inline auto Emitter::analyze_closure_returns(
    const std::vector<StmtPtr>& stmts,
    std::unordered_map<std::string, std::string>& local_types,
    bool& has_fallible,
    bool& has_val_ret,
    std::string& val_ret_cpp
) const -> void {
    auto scan_expr = [&](auto& self, const ExprPtr& e) -> void {
        if (!e) return;
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            has_fallible = true;
            self(self, tr->inner);
        } else if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            self(self, or_ex->inner);
            self(self, or_ex->fallback_val);
            if (!or_ex->handler_body.empty()) {
                analyze_closure_returns(or_ex->handler_body, local_types, has_fallible, has_val_ret, val_ret_cpp);
            }
        } else if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            self(self, un->operand);
        } else if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            self(self, bin->left);
            self(self, bin->right);
        } else if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            self(self, mem->object);
        } else if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            self(self, idx->object);
            self(self, idx->index);
        } else if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            self(self, sl->object);
            self(self, sl->start);
            self(self, sl->end);
        } else if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            for (const auto& f : st->fields) self(self, f.value);
        } else if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            for (const auto& el : arr->elements) self(self, el);
        } else if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
            for (const auto& el : tup->elements) self(self, el);
        } else if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            self(self, call->callee);
            for (const auto& a : call->args) self(self, a.value);
        } else if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            self(self, me->target);
        }
    };

    for (const auto& s : stmts) {
        if (!s) continue;
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            scan_expr(scan_expr, vd->init);
            if (vd->type) {
                local_types[vd->name] = lower_owning_type(*vd->type);
            } else if (vd->init) {
                std::string t = infer_closure_expr_cpp_type(vd->init, local_types);
                if (!t.empty()) local_types[vd->name] = t;
            }
        } else if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            scan_expr(scan_expr, dvs->init);
        } else if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            scan_expr(scan_expr, as->target);
            scan_expr(scan_expr, as->value);
        } else if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            for (const auto& t : das->targets) scan_expr(scan_expr, t);
            scan_expr(scan_expr, das->value);
        } else if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            scan_expr(scan_expr, es->expr);
        } else if (const auto* rs = std::get_if<ReturnStmt>(&s->node)) {
            scan_expr(scan_expr, rs->value);
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
            scan_expr(scan_expr, ifs->cond);
            analyze_closure_returns(ifs->then_body, local_types, has_fallible, has_val_ret, val_ret_cpp);
            analyze_closure_returns(ifs->else_body, local_types, has_fallible, has_val_ret, val_ret_cpp);
        } else if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            scan_expr(scan_expr, fs->iter_expr);
            scan_expr(scan_expr, fs->range_end);
            analyze_closure_returns(fs->body, local_types, has_fallible, has_val_ret, val_ret_cpp);
        } else if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            scan_expr(scan_expr, ms->target);
            for (const auto& arm : ms->arms) {
                analyze_closure_returns(arm.body, local_types, has_fallible, has_val_ret, val_ret_cpp);
            }
        }
    }
}

[[nodiscard]] inline auto Emitter::emit_closure(const ClosureExpr& cl, int expr_line) -> std::string {
    (void)expr_line;
    std::ostringstream sb;
    sb << (cl.is_escaping ? "[=" + view_param_captures(cl.body) + "](" : std::string("[&]("));
    for (std::size_t i = 0; i < cl.params.size(); ++i) {
        if (i > 0) sb << ", ";
        if (i < cl.param_types.size() && cl.param_types[i].has_value()) {
            std::string p_intent = (i < cl.param_intents.size()) ? cl.param_intents[i] : "in";
            sb << lower_param_type(p_intent, *cl.param_types[i]) << " " << cl.params[i];
        } else {
            sb << "auto&& " << cl.params[i];
        }
    }
    std::string mut_spec = cl.is_escaping ? " mutable" : "";
    bool is_fallible_void = false;
    if (cl.return_type) {
        sb << ")" << mut_spec << " -> " << lower_return_type(*cl.return_type) << " {\n";
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
            sb << ")" << mut_spec << " -> std::expected<void, cfire::Error> {\n";
            is_fallible_void = true;
        } else if (has_fallible && !val_ret_cpp.empty()) {
            sb << ")" << mut_spec << " -> std::expected<" << val_ret_cpp << ", cfire::Error> {\n";
        } else {
            sb << ")" << mut_spec << " {\n";
        }
    }
    ++indent_;
    Emitter sub = make_sub_emitter(false);
    for (const auto& p : cl.params) sub.local_vars_.insert(p);
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
    last_source_line_ = -1;
    return sb.str();
}

[[nodiscard]] inline auto Emitter::emit_call_arg(const CallArg& a) -> std::string {
    std::string val = emit_expr(a.value);
    if (a.intent == "move" && !val.starts_with("std::move(")) {
        return std::format("std::move({})", val);
    }
    return val;
}


} // namespace cfire::frontend
