#pragma once
// src/codegen/stmt.hpp — Statement & Pattern-Matching Lowering for cfire::frontend::Emitter

namespace cfire::frontend {

CFIRE_CG_INLINE auto Emitter::lower_var_decl_cpp_type(const VarDeclStmt& st) const -> std::string {
    if (st.type) return lower_return_type(*st.type);
    if (cf_.owned_text_decls.contains(&st)) return "std::string";
    if (cf_.int64_decls.contains(&st)) return "std::int64_t";
    if (st.init && std::holds_alternative<ClosureExpr>(st.init->node)) return "auto";
    if (st.init) {
        if (const auto* un = std::get_if<UnaryExpr>(&st.init->node); un && (un->op == "&" || un->op == "&mut")) {
            return "auto";
        }
        if (!st.is_mut && !cf_.owned_slice_decls.contains(&st) && st.resolved_type_id != 0) {
            const TypeDesc& vd = cf_.type_table.get(TypeId{st.resolved_type_id});
            if (vd.kind == TypeKind::Slice) {
                bool is_slice_view_init = std::holds_alternative<SliceExpr>(st.init->node);
                if (!is_slice_view_init) {
                    if (const auto* c = std::get_if<CallExpr>(&st.init->node)) {
                        if (const auto* m = c->callee ? std::get_if<MemberExpr>(&c->callee->node) : nullptr;
                            m && m->field == "bytes" && c->args.empty()) {
                            is_slice_view_init = true;
                        }
                    } else if (const auto* id = std::get_if<IdentExpr>(&st.init->node)) {
                        for (const auto& [vn, _] : view_params_) {
                            if (vn == id->name) {
                                is_slice_view_init = true;
                                break;
                            }
                        }
                    }
                }
                if (is_slice_view_init) {
                    if (auto elem = lower_type_id(vd.elem_type, LowerContext::Return); elem && *elem != "bool") {
                        return std::format("cfire::slice_ref<{}>", *elem);
                    }
                }
            }
        }
    }
    if (auto t = lower_type_id(TypeId{st.resolved_type_id}, LowerContext::Value)) {
        return *t;
    }
    if (st.init) {
        if (const auto* lit = std::get_if<LiteralExpr>(&st.init->node);
            lit && (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString)) {
            return "std::string";
        }
        if (is_int_const_expr(st.init)) {
            return "std::int64_t";
        }
        if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&st.init->node); arr_lit && !arr_lit->elements.empty()) {
            std::string inferred = infer_array_lit_cpp_type(*arr_lit);
            if (!inferred.empty()) return inferred;
        }
    }
    return "auto";
}

CFIRE_CG_INLINE void Emitter::emit_or_handler_body(const OrExpr& or_ex, std::string_view tmp, bool in_main) {
    bool added_err = false;
    if (!or_ex.err_var.empty()) {
        local_vars_.insert(or_ex.err_var);
        added_err = error_vars_.insert(or_ex.err_var).second;
        writeln(std::format("const cfire::Error& {} = cfire::extract_or_error({});", sanitize_cpp_ident(or_ex.err_var), tmp));
    }
    for (const auto& hs : or_ex.handler_body) emit_stmt(hs, in_main);
    if (added_err) error_vars_.erase(or_ex.err_var);
    last_source_line_ = -1;
}

CFIRE_CG_INLINE void Emitter::emit_stmt(const StmtPtr& s, bool in_main) {
    if (!s) return;
    if (s->line > 0) emit_line_directive(s->line);
    std::visit([&](const auto& st) {
        using T = std::decay_t<decltype(st)>;
        if constexpr (std::is_same_v<T, VarDeclStmt>) {
            if (st.init) {
                bool top_handled = std::holds_alternative<TryExpr>(st.init->node) ||
                    (std::holds_alternative<OrExpr>(st.init->node) && !std::get<OrExpr>(st.init->node).handler_body.empty());
                std::size_t pre_hoist = hoisted_tries_.size();
                hoist_subexpr_tries(st.init, in_main, s->line, top_handled);
                if (hoisted_tries_.size() > pre_hoist && s->line > 0) {
                    emit_line_directive(s->line);
                }
            }
            local_vars_.insert(st.name);
            if (st.type) var_types_[st.name] = st.type->name;
            if ((st.type && st.type->is_optional) || (!st.type && expr_returns_optional(st.init))) {
                optional_vars_.insert(st.name);
            }
            bool is_smart_var = (st.type && !st.type->smart_wrap.empty());
            if (!is_smart_var && st.init) {
                const Expr* cur_init = st.init;
                if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
                else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
                if (cur_init) {
                    if (const auto* id = std::get_if<IdentExpr>(&cur_init->node)) {
                        if (smart_ptr_vars_.contains(id->name)) is_smart_var = true;
                    } else if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                        if (const auto* fmem = std::get_if<MemberExpr>(&call_ex->callee->node); fmem && fmem->field == "from_raw") {
                            if (const auto* ix = std::get_if<IndexExpr>(&fmem->object->node)) {
                                if (const auto* base = ix->object ? std::get_if<IdentExpr>(&ix->object->node) : nullptr; base && base->name == "own") is_smart_var = true;
                            }
                        }
                        if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                            if (cid->name == "make" && call_ex->type_args.size() == 1 && !call_ex->type_args[0].smart_wrap.empty()) {
                                is_smart_var = true;
                            } else if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end() && !fit->second.return_type.smart_wrap.empty()) {
                                is_smart_var = true;
                            }
                        }
                    }
                }
            }
            if (is_smart_var) smart_ptr_vars_.insert(st.name);
            else smart_ptr_vars_.erase(st.name);
            std::string vname = sanitize_cpp_ident(st.name);
            if (st.init) {
                if (const auto* try_ex = std::get_if<TryExpr>(&st.init->node)) {
                    if (in_main || in_test_) {
                        int id = ++try_count_;
                        std::string tmp = std::format("_try_{}", id);
                        std::string tmp_t = lower_type_id(try_ex->inner ? TypeId{try_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                        writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(try_ex->inner)));
                        emit_try_unwrap_check(tmp, s->line, in_main);
                        std::string cpp_type = lower_var_decl_cpp_type(st);
                        writeln(std::format("{} {} = std::move(*{});", cpp_type, vname, tmp));
                        return;
                    }
                    std::string cpp_type = lower_var_decl_cpp_type(st);
                    writeln(std::format("CFIRE_TRY_ASSIGN({} {}, {});", cpp_type, vname, emit_expr(try_ex->inner)));
                    return;
                }
                if (const auto* or_ex = std::get_if<OrExpr>(&st.init->node); or_ex && !or_ex->handler_body.empty()) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_res_{}", id);
                    std::string tmp_t = lower_type_id(or_ex->inner ? TypeId{or_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                    writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(or_ex->inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    emit_or_handler_body(*or_ex, tmp, in_main);
                    --indent_;
                    writeln("}");
                    std::string cpp_type = lower_var_decl_cpp_type(st);
                    writeln(std::format("{} {} = std::move(*{});", cpp_type, vname, tmp));
                    return;
                }

            }
            if (st.type && (st.type->is_ptr || st.type->is_mut_ptr)) {
                ptr_vars_.insert(st.name);
            }
            map_vars_.erase(st.name);
            if (st.type) {
                if (st.type->is_map && st.is_mut) map_vars_.insert(st.name);
                if (st.type->is_array && !st.type->array_len.empty()) {
                    if (auto n = parse_int_literal<std::size_t>(st.type->array_len)) {
                        fixed_array_vars_[st.name] = *n;
                    }
                }
            }
            if (st.init) {
                if (const auto* call_ex = std::get_if<CallExpr>(&st.init->node)) {
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                        cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].is_map) {
                        if (st.is_mut) map_vars_.insert(st.name);
                    }
                }
            }
            std::string cpp_type = lower_var_decl_cpp_type(st);
            auto tuple_has_array_lit = [](const TupleExpr& tup) -> bool {
                for (const auto& el : tup.elements) {
                    if (el && std::holds_alternative<ArrayLitExpr>(el->node)) return true;
                }
                return false;
            };
            if (st.init) {
                std::string init_val;
                if (const auto* tup = std::get_if<TupleExpr>(&st.init->node);
                    tup && st.type && st.type->is_tuple && tuple_has_array_lit(*tup)) {
                    init_val = emit_typed_tuple(*tup, *st.type);
                } else {
                    init_val = emit_expr(st.init);
                }
                if (cf_.owned_slice_decls.contains(&st)) init_val = std::format("cfire::own_copy({})", init_val);
                if (st.type && (st.type->is_ptr || st.type->is_mut_ptr) && init_val == "std::nullopt") {
                    init_val = "nullptr";
                }
                if (st.name == "_") {
                    writeln(std::format("(void)({});", init_val));
                } else {
                    writeln(std::format("{} {} = {};", cpp_type, vname, init_val));
                }
            } else {
                writeln(std::format("{} {}{{}};", cpp_type, vname));
            }
        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            hoist_subexpr_tries(st.target, in_main, s->line, false);
            bool top_try = st.value && std::holds_alternative<TryExpr>(st.value->node);
            bool top_or_block = st.value && std::holds_alternative<OrExpr>(st.value->node) &&
                                !std::get<OrExpr>(st.value->node).handler_body.empty();
            hoist_subexpr_tries(st.value, in_main, s->line, top_try || top_or_block);
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
                if (const auto* or_ex = std::get_if<OrExpr>(&st.value->node); or_ex && !or_ex->handler_body.empty()) {
                    hoist_subexpr_tries(st.value, in_main, s->line, false);
                    return;
                }
                writeln(std::format("(void){};", emit_expr(st.value)));
                return;
            }
            if (st.value) {
                if (const auto* or_ex = std::get_if<OrExpr>(&st.value->node); or_ex && !or_ex->handler_body.empty()) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_res_{}", id);
                    std::string tmp_t = lower_type_id(or_ex->inner ? TypeId{or_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                    writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(or_ex->inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    emit_or_handler_body(*or_ex, tmp, in_main);
                    --indent_;
                    writeln("} else {");
                    ++indent_;
                    writeln(std::format("{} {} std::move(*{});", lhs, st.op, tmp));
                    --indent_;
                    writeln("}");
                    return;
                }
            }
            if (top_try) {
                hoist_subexpr_tries(st.value, in_main, s->line, false);
            }
            std::string rhs = emit_expr(st.value);
            if (rhs == "std::nullopt") {
                if (const auto* id = std::get_if<IdentExpr>(&st.target->node); id && ptr_vars_.contains(id->name)) {
                    rhs = "nullptr";
                } else if (const auto* mem = std::get_if<MemberExpr>(&st.target->node); mem && ptr_field_names_.contains(mem->field)) {
                    rhs = "nullptr";
                }
            }
            if (st.op == "/=") {
                writeln(std::format("{} = cfire::div({}, {});", lhs, lhs, rhs));
            } else if (st.op == "%=") {
                writeln(std::format("{} = cfire::mod({}, {});", lhs, lhs, rhs));
            } else if (st.op == "<<=" || st.op == ">>=") {
                writeln(std::format("{} = cfire::{}({}, {});", lhs, st.op == "<<=" ? "shl" : "shr", lhs, rhs));
            } else {
                writeln(std::format("{} {} {};", lhs, st.op, rhs));
            }
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
                writeln(std::format("{}.defer([&] {{ (void)({}); }});", active_async_scopes_.back(), emit_expr(st.call)));
            } else {
                ++defer_count_;
                writeln(std::format("auto _defer_{} = cfire::scope_exit([&] {{ (void)({}); }});", defer_count_, emit_expr(st.call)));
            }
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            hoist_subexpr_tries(st.value, in_main, s->line, false);
            if (!st.value) {
                writeln(in_main ? "return 0;" : (in_fallible_void_ ? "return {};" : "return;"));
            } else {
                std::string rval;
                auto tuple_has_array_lit = [](const TupleExpr& tup) -> bool {
                    for (const auto& el : tup.elements) {
                        if (el && std::holds_alternative<ArrayLitExpr>(el->node)) return true;
                    }
                    return false;
                };
                if (const auto* tup = std::get_if<TupleExpr>(&st.value->node);
                    tup && has_cur_fn_return_typeref_ && cur_fn_return_typeref_.is_tuple && tuple_has_array_lit(*tup)) {
                    rval = emit_typed_tuple(*tup, cur_fn_return_typeref_);
                } else {
                    rval = emit_expr(st.value);
                }
                if (in_ptr_return_ && rval == "std::nullopt") {
                    rval = "nullptr";
                } else if (std::holds_alternative<ArrayLitExpr>(st.value->node) && !cur_fn_raw_return_type_.empty()) {
                    rval = std::format("{}{}", cur_fn_raw_return_type_, rval);
                } else if (std::holds_alternative<ArrayLitExpr>(st.value->node) &&
                           has_cur_fn_return_typeref_ && (cur_fn_return_typeref_.is_slice || cur_fn_return_typeref_.is_array)) {
                    rval = std::format("{}{}", lower_raw_owning_type(cur_fn_return_typeref_), rval);
                }
                if (const auto* ret_id = std::get_if<IdentExpr>(&st.value->node); ret_id && error_vars_.contains(ret_id->name)) {
                    rval = std::format("cfire::make_error({})", rval);
                }
                if (in_main && rval.starts_with("cfire::make_error(")) {
                    writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", rval));
                    writeln("return 1;");
                } else if (in_test_ && rval.starts_with("cfire::make_error(")) {
                    writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"return error\");",
                        rval, escape_line_path(source_path_), s->line));
                    writeln("return;");
                } else {
                    writeln(std::format("return {};", rval));
                }
            }
        } else if constexpr (std::is_same_v<T, IfStmt>) {
            if (st.unwrap_init) {
                hoist_subexpr_tries(st.unwrap_init, in_main, s->line, false);
                std::string init_s = emit_expr(st.unwrap_init);
                local_vars_.insert(st.unwrap_var);
                std::string opt_cpp = lower_type_id(st.unwrap_init ? TypeId{st.unwrap_init->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                auto unwrap_cpp = lower_type_id(TypeId{st.resolved_unwrap_type_id}, LowerContext::Value);
                std::string bind_t = unwrap_cpp
                    ? (st.unwrap_mut ? *unwrap_cpp : ("const " + *unwrap_cpp))
                    : (st.unwrap_mut ? std::string("auto") : std::string("const auto"));
                if (st.cond) {
                    hoist_subexpr_tries(st.cond, in_main, s->line, false);
                    std::string cond_s = emit_expr(st.cond);
                    writeln("{");
                    ++indent_;
                    writeln(std::format("{} _opt = {};", opt_cpp, init_s));
                    writeln("bool _taken = false;");
                    writeln("if (_opt.has_value()) {");
                    ++indent_;
                    writeln(std::format("{}& {} = *_opt;", bind_t, st.unwrap_var));
                    writeln(std::format("if ({}) {{", cond_s));
                    ++indent_;
                    writeln("_taken = true;");
                    for (const auto& b : st.then_body) emit_stmt(b, in_main);
                    --indent_;
                    writeln("}");
                    --indent_;
                    writeln("}");
                    if (!st.else_body.empty()) {
                        writeln("if (!_taken) {");
                        ++indent_;
                        for (const auto& b : st.else_body) emit_stmt(b, in_main);
                        --indent_;
                        writeln("}");
                    }
                    --indent_;
                    writeln("}");
                } else {
                    writeln(std::format("if ({} _opt = {}; _opt.has_value()) {{", opt_cpp, init_s));
                    ++indent_;
                    writeln(std::format("{}& {} = *_opt;", bind_t, st.unwrap_var));
                    for (const auto& b : st.then_body) emit_stmt(b, in_main);
                    --indent_;
                    if (!st.else_body.empty()) {
                        writeln("} else {");
                        ++indent_;
                        for (const auto& b : st.else_body) emit_stmt(b, in_main);
                        --indent_;
                    }
                    writeln("}");
                }
            } else {
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
            }
        } else if constexpr (std::is_same_v<T, ForStmt>) {
            if (st.is_unwrap) {
                hoist_subexpr_tries(st.iter_expr, in_main, s->line, false);
                std::string iter_s = emit_expr(st.iter_expr);
                local_vars_.insert(st.item_var);
                std::string opt_cpp = lower_type_id(st.iter_expr ? TypeId{st.iter_expr->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                auto unwrap_cpp = lower_type_id(TypeId{st.resolved_unwrap_type_id}, LowerContext::Value);
                std::string bind_t = unwrap_cpp
                    ? (st.unwrap_mut ? *unwrap_cpp : ("const " + *unwrap_cpp))
                    : (st.unwrap_mut ? std::string("auto") : std::string("const auto"));
                writeln("while (true) {");
                ++indent_;
                writeln(std::format("{} _opt = {};", opt_cpp, iter_s));
                writeln("if (!_opt.has_value()) break;");
                writeln(std::format("{}& {} = *_opt;", bind_t, st.item_var));
                for (const auto& b : st.body) emit_stmt(b, in_main);
                --indent_;
                writeln("}");
            } else if (!st.item_var.empty()) {
                hoist_subexpr_tries(st.iter_expr, in_main, s->line, false);
                if (st.range_end) {
                    hoist_subexpr_tries(st.range_end, in_main, s->line, false);
                }
                local_vars_.insert(st.item_var);
                if (st.range_end) {
                    std::string start_s = emit_expr(st.iter_expr);
                    std::string end_s = emit_expr(st.range_end);
                    std::string cmp_op = st.range_inclusive ? "<=" : "<";
                    std::string idx_t = lower_type_id(TypeId{st.resolved_index_type_id}, LowerContext::RangeIndex).value_or("std::int64_t");
                    writeln(std::format(
                        "for ({4} {0} = static_cast<{4}>({1}), _range_end_{0} = static_cast<{4}>({2}); {0} {3} _range_end_{0}; ++{0}) {{",
                        st.item_var, start_s, end_s, cmp_op, idx_t));
                    ++indent_;
                    for (const auto& b : st.body) emit_stmt(b, in_main);
                    --indent_;
                    writeln("}");
                } else if (!st.index_var.empty()) {
                    local_vars_.insert(st.index_var);
                    local_vars_.insert(st.item_var);
                    std::string col = emit_expr(st.iter_expr);
                    loop_index_to_col_[st.index_var] = col;
                    writeln(std::format("auto&& _col_{0} = {1};", st.index_var, col));
                    writeln(std::format("for (auto&& [{0}, {1}] : cfire::detail::iter_pairs(_col_{0})) {{", st.index_var, st.item_var));
                    ++indent_;
                    for (const auto& b : st.body) emit_stmt(b, in_main);
                    --indent_;
                    writeln("}");
                    loop_index_to_col_.erase(st.index_var);
                } else {
                    // `iter_items` yields a `str`'s elements as `byte`s and keeps a temporary alive. An
                    // array literal lowers to a braced list, which range-for iterates via `std::initializer_list`.
                    const bool braced = std::holds_alternative<ArrayLitExpr>(st.iter_expr->node);
                    const std::string range = emit_expr(st.iter_expr);
                    std::optional<std::string> item_cpp;
                    if (st.iter_expr && st.iter_expr->resolved_type_id != 0) {
                        const TypeDesc& ctd = cf_.type_table.get(TypeId{st.iter_expr->resolved_type_id});
                        if (ctd.kind == TypeKind::Map) {
                            auto k = lower_type_id(ctd.key_type, LowerContext::Return);
                            auto v = lower_type_id(ctd.elem_type, LowerContext::Return);
                            if (k && v) item_cpp = std::format("std::pair<const {}, {}>", *k, *v);
                        }
                    }
                    if (!item_cpp) {
                        item_cpp = lower_type_id(TypeId{st.resolved_item_type_id}, LowerContext::Value);
                    }
                    std::string item_bind = item_cpp ? std::format("const {}&", *item_cpp) : std::string("const auto&");
                    if (braced) {
                        if (item_cpp) {
                            writeln(std::format("for ({} {} : std::initializer_list<{}>{}) {{", item_bind, st.item_var, *item_cpp, range));
                        } else {
                            writeln(std::format("for (const auto& {} : {}) {{", st.item_var, range));
                        }
                    } else {
                        writeln(std::format("for ({} {} : cfire::detail::iter_items({})) {{", item_bind, st.item_var, range));
                    }
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
            // a value-producing scope collects each spawned task and yields their values in spawn order.
            const bool yields = (st.resolved_result_elem_type_id != 0 || !st.result_elem_type.empty()) &&
                                (!st.result_var.empty() || st.result_return);
            std::string elem_cpp;
            if (yields) {
                elem_cpp = lower_type_id(TypeId{st.resolved_result_elem_type_id}, LowerContext::Value).value_or("auto");
                if (!st.result_var.empty()) {
                    local_vars_.insert(st.result_var);
                    writeln(std::format("std::vector<{}> {};", elem_cpp, st.result_var));
                }
            }
            writeln("{");
            ++indent_;
            local_vars_.insert(st.scope_var);
            writeln(std::format("cfire::async::Scope {}({});", st.scope_var, emit_expr(st.workers)));
            const std::string results = "cf_results_" + st.scope_var;
            if (yields) {
                writeln(std::format("cfire::async::Results<{}> {};", elem_cpp, results));
                value_scope_results_[st.scope_var] = {results, elem_cpp};
            }
            active_async_scopes_.push_back(st.scope_var);
            for (const auto& b : st.body) emit_stmt(b, in_main);
            active_async_scopes_.pop_back();
            writeln(std::format("{}.join_all();", st.scope_var));
            if (yields) {
                value_scope_results_.erase(st.scope_var);
                if (st.result_return) writeln(std::format("return {}.take();", results));
                else writeln(std::format("{} = {}.take();", st.result_var, results));
            }
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
        } else if constexpr (std::is_same_v<T, BreakStmt>) {
            writeln("break;");
        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
            writeln("continue;");
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
            bool top_handled = st.expr && (std::holds_alternative<TryExpr>(st.expr->node) ||
                (std::holds_alternative<OrExpr>(st.expr->node) && !std::get<OrExpr>(st.expr->node).handler_body.empty()));
            std::size_t pre_hoist = hoisted_tries_.size();
            hoist_subexpr_tries(st.expr, in_main, s->line, top_handled);
            if (hoisted_tries_.size() > pre_hoist && s->line > 0) {
                emit_line_directive(s->line);
            }
            if (const auto* try_ex = std::get_if<TryExpr>(&st.expr->node)) {
                if (in_main || in_test_) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_try_v_{}", id);
                    std::string tmp_t = lower_type_id(try_ex->inner ? TypeId{try_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                    writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(try_ex->inner)));
                    emit_try_unwrap_check(tmp, s->line, in_main);
                    return;
                }
                writeln(std::format("CFIRE_TRY({});", emit_expr(try_ex->inner)));
                return;
            }
            if (const auto* or_ex = std::get_if<OrExpr>(&st.expr->node); or_ex && !or_ex->handler_body.empty()) {
                int id = ++try_count_;
                std::string tmp = std::format("_res_v_{}", id);
                std::string tmp_t = lower_type_id(or_ex->inner ? TypeId{or_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(or_ex->inner)));
                writeln(std::format("if (!{}.has_value()) {{", tmp));
                ++indent_;
                emit_or_handler_body(*or_ex, tmp, in_main);
                --indent_;
                writeln("}");
                return;
            }
            writeln(std::format("(void)({});", emit_expr(st.expr)));
        } else if constexpr (std::is_same_v<T, DestructureVarDeclStmt>) {
            bool top_handled = st.init && (std::holds_alternative<TryExpr>(st.init->node) ||
                (std::holds_alternative<OrExpr>(st.init->node) && !std::get<OrExpr>(st.init->node).handler_body.empty()));
            std::size_t pre_hoist = hoisted_tries_.size();
            hoist_subexpr_tries(st.init, in_main, s->line, top_handled);
            if (hoisted_tries_.size() > pre_hoist && s->line > 0) {
                emit_line_directive(s->line);
            }
            std::string names;
            for (std::size_t i = 0; i < st.bindings.size(); ++i) {
                if (i > 0) names += ", ";
                names += st.bindings[i].first;
                local_vars_.insert(st.bindings[i].first);
            }
            if (const auto* try_ex = std::get_if<TryExpr>(&st.init->node)) {
                int id = ++try_count_;
                std::string tmp = std::format("_try_{}", id);
                std::string tmp_t = lower_type_id(try_ex->inner ? TypeId{try_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(try_ex->inner)));
                emit_try_unwrap_check(tmp, s->line, in_main);
                writeln(std::format("auto [{}] = std::move(*{});", names, tmp));
                return;
            }
            if (const auto* or_ex = std::get_if<OrExpr>(&st.init->node); or_ex && !or_ex->handler_body.empty()) {
                int id = ++try_count_;
                std::string tmp = std::format("_res_{}", id);
                std::string tmp_t = lower_type_id(or_ex->inner ? TypeId{or_ex->inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(or_ex->inner)));
                writeln(std::format("if (!{}.has_value()) {{", tmp));
                ++indent_;
                emit_or_handler_body(*or_ex, tmp, in_main);
                --indent_;
                writeln("}");
                writeln(std::format("auto [{}] = std::move(*{});", names, tmp));
                return;
            }
            writeln(std::format("auto [{}] = {};", names, emit_expr(st.init)));
        } else if constexpr (std::is_same_v<T, DestructureAssignStmt>) {
            std::size_t pre_hoist = hoisted_tries_.size();
            for (const auto& t : st.targets) {
                hoist_subexpr_tries(t, in_main, s->line, false);
            }
            hoist_subexpr_tries(st.value, in_main, s->line, false);
            if (hoisted_tries_.size() > pre_hoist && s->line > 0) {
                emit_line_directive(s->line);
            }
            std::string targs;
            for (std::size_t i = 0; i < st.targets.size(); ++i) {
                if (i > 0) targs += ", ";
                targs += emit_expr(st.targets[i]);
            }
            writeln(std::format("std::tie({}) = {};", targs, emit_expr(st.value)));
        }
    }, s->node);
}

// explicit lambda return type for match expressions whose arms resolve cleanly.
CFIRE_CG_INLINE auto Emitter::match_lambda_header() const -> std::string {
    if (auto ret = lower_type_id(match_expr_result_tid_, LowerContext::Return)) {
        return std::format("([&]() -> {} {{", *ret);
    }
    if (!match_expr_result_type_.empty()) {
        TypeRef tr;
        tr.name = match_expr_result_type_;
        return std::format("([&]() -> {} {{", lower_owning_type(tr));
    }
    return "([&]() -> auto {";
}

CFIRE_CG_INLINE void Emitter::emit_match_common(
    const ExprPtr& target,
    bool is_move,
    const std::vector<MatchArm>& arms,
    bool is_expr,
    bool in_main,
    std::ostringstream& sb
) {
    auto write_ln = [&](std::string_view line) {
        if (is_expr) {
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << line << "\n";
        } else {
            writeln(line);
        }
    };
    auto write_tail = [&](std::string_view line) {
        if (is_expr) {
            for (int i = 0; i < indent_; ++i) sb << "    ";
            sb << line;
        } else {
            writeln(std::format("{};", line));
        }
    };
    auto emit_arm = [&](const std::vector<StmtPtr>& body) {
        if (is_expr) {
            emit_match_arm_body(sb, body, true);
        } else {
            for (const auto& b : body) emit_stmt(b, in_main);
        }
    };

    bool is_slice_match = false;
    bool is_primitive_match = false;
    bool is_opt_match = false;
    bool is_res_match = false;
    for (const auto& a : arms) {
        if (a.is_slice_pattern) is_slice_match = true;
        if (!a.literal_val.empty() || a.is_range) is_primitive_match = true;
        if (a.is_some || a.is_none || a.variant_name == "Some" || a.variant_name == "none") is_opt_match = true;
        if (a.is_ok || a.is_err || a.variant_name == "Ok" || a.variant_name == "Err") is_res_match = true;
    }

    if (is_slice_match) {
        std::string target_expr = emit_expr(target);
        std::optional<std::string> slice_elem_cpp;
        bool is_str_slice = false;
        if (target && target->resolved_type_id != 0) {
            TypeId tid{target->resolved_type_id};
            const TypeDesc& td = cf_.type_table.get(tid);
            if (tid == TypeId::Str() || (td.kind == TypeKind::Borrow && td.elem_type == TypeId::Str())) {
                is_str_slice = true;
                slice_elem_cpp = "std::uint8_t";
            } else if (td.kind == TypeKind::Slice || td.kind == TypeKind::Array) {
                slice_elem_cpp = lower_type_id(td.elem_type, LowerContext::Value);
            }
        }
        write_ln(is_expr ? match_lambda_header() : std::string("{"));
        ++indent_;
        write_ln(std::format("const auto& _target = {};", target_expr));
        write_ln("bool _matched = false;");

        for (const auto& arm : arms) {
            if (arm.is_slice_pattern) {
                std::size_t head_count = arm.slice_head.size();
                std::size_t tail_count = arm.slice_tail.size();
                std::size_t min_size = head_count + tail_count;

                std::string cond;
                if (!arm.slice_has_rest) {
                    cond = std::format("_target.size() == {}", min_size);
                } else {
                    cond = (min_size == 0) ? "true" : std::format("_target.size() >= {}", min_size);
                }

                write_ln(std::format("if (!_matched && {}) {{", cond));
                ++indent_;

                std::string elem_bind = slice_elem_cpp ? std::format("const {}&", *slice_elem_cpp) : std::string("const auto&");
                for (std::size_t i = 0; i < head_count; ++i) {
                    if (arm.slice_head[i] != "_" &&
                        (stmts_reference_ident(arm.body, arm.slice_head[i]) || expr_references_ident(arm.guard, arm.slice_head[i]))) {
                        local_vars_.insert(arm.slice_head[i]);
                        write_ln(std::format("{} {} = _target[{}];", elem_bind, arm.slice_head[i], i));
                    }
                }
                if (arm.slice_has_rest && !arm.slice_rest.empty() && arm.slice_rest != "_" &&
                    (stmts_reference_ident(arm.body, arm.slice_rest) || expr_references_ident(arm.guard, arm.slice_rest))) {
                    local_vars_.insert(arm.slice_rest);
                    std::string rest_start = std::to_string(head_count);
                    std::string rest_end = tail_count == 0 ? "_target.size()" : std::format("(_target.size() - {})", tail_count);
                    std::string rest_type = "auto";
                    if (is_str_slice) {
                        rest_type = "cfire::str_ref";
                    } else if (slice_elem_cpp && *slice_elem_cpp != "bool") {
                        rest_type = std::format("cfire::slice_ref<{}>", *slice_elem_cpp);
                    }
                    write_ln(std::format("{} {} = cfire::slice(_target, {}, {});", rest_type, arm.slice_rest, rest_start, rest_end));
                }
                for (std::size_t i = 0; i < tail_count; ++i) {
                    if (arm.slice_tail[i] != "_" &&
                        (stmts_reference_ident(arm.body, arm.slice_tail[i]) || expr_references_ident(arm.guard, arm.slice_tail[i]))) {
                        local_vars_.insert(arm.slice_tail[i]);
                        std::size_t offset_from_end = tail_count - i;
                        write_ln(std::format("{} {} = _target[_target.size() - {}];", elem_bind, arm.slice_tail[i], offset_from_end));
                    }
                }

                if (arm.guard) {
                    write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                    ++indent_;
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                    --indent_;
                    write_ln("}");
                } else {
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                }
                --indent_;
                write_ln("}");
            } else if (arm.variant_name == "_") {
                write_ln("if (!_matched) {");
                ++indent_;
                if (arm.guard) {
                    write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                    ++indent_;
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                    --indent_;
                    write_ln("}");
                } else {
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                }
                --indent_;
                write_ln("}");
            }
        }
        if (is_expr) {
            write_ln("cfire::assert_true(false, \"unreachable slice match\");");
        }
        --indent_;
        if (is_expr) {
            write_tail("})()");
        } else {
            write_ln("}");
        }
        return;
    }

    if (is_opt_match) {
        std::string target_expr = emit_expr(target);
        TypeId opt_tid = target ? TypeId{target->resolved_type_id} : TypeId::Unknown();
        auto opt_cpp = lower_type_id(opt_tid, LowerContext::Value);
        auto some_cpp = lower_type_id(cf_.type_table.unwrap_optional(opt_tid), LowerContext::Value);
        write_ln(is_expr ? match_lambda_header() : std::string("{"));
        ++indent_;
        write_ln(std::format("const {}& _target = {};", opt_cpp.value_or("auto"), target_expr));
        write_ln("bool _matched = false;");
        for (const auto& arm : arms) {
            bool is_none_arm = arm.is_none || arm.variant_name == "none";
            bool is_some_arm = arm.is_some || arm.variant_name == "Some";
            std::string cond = is_none_arm ? "!_target.has_value()" : (is_some_arm ? "_target.has_value()" : "true");
            write_ln(std::format("if (!_matched && ({})) {{", cond));
            ++indent_;
            if (is_some_arm && !arm.bindings.empty() && arm.bindings[0] != "_") {
                local_vars_.insert(arm.bindings[0]);
                write_ln(std::format("const {}& {} = *_target;", some_cpp.value_or("auto"), arm.bindings[0]));
            }
            if (arm.guard) {
                write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                ++indent_;
                write_ln("_matched = true;");
                emit_arm(arm.body);
                --indent_;
                write_ln("}");
            } else {
                write_ln("_matched = true;");
                emit_arm(arm.body);
            }
            --indent_;
            write_ln("}");
        }
        if (is_expr) {
            write_ln("cfire::assert_true(false, \"unreachable optional match\");");
        }
        --indent_;
        if (is_expr) {
            write_tail("})()");
        } else {
            write_ln("}");
        }
        return;
    }

    if (is_res_match) {
        std::string target_expr = emit_expr(target);
        TypeId res_tid = target ? TypeId{target->resolved_type_id} : TypeId::Unknown();
        auto res_cpp = lower_type_id(res_tid, LowerContext::Value);
        auto ok_cpp = lower_type_id(cf_.type_table.unwrap_fallible(res_tid), LowerContext::Value);
        write_ln(is_expr ? match_lambda_header() : std::string("{"));
        ++indent_;
        write_ln(std::format("const {}& _target = {};", res_cpp.value_or("auto"), target_expr));
        write_ln("bool _matched = false;");
        for (const auto& arm : arms) {
            bool is_err_arm = arm.is_err || arm.variant_name == "Err";
            bool is_ok_arm = arm.is_ok || arm.variant_name == "Ok";
            std::string cond = is_err_arm ? "!_target.has_value()" : (is_ok_arm ? "_target.has_value()" : "true");
            write_ln(std::format("if (!_matched && ({})) {{", cond));
            ++indent_;
            bool added_err_binding = false;
            std::string err_binding_name;
            if (is_ok_arm && !arm.bindings.empty() && arm.bindings[0] != "_") {
                local_vars_.insert(arm.bindings[0]);
                write_ln(std::format("const {}& {} = _target.value();", ok_cpp.value_or("auto"), arm.bindings[0]));
            } else if (is_err_arm && !arm.bindings.empty() && arm.bindings[0] != "_") {
                err_binding_name = arm.bindings[0];
                local_vars_.insert(err_binding_name);
                added_err_binding = error_vars_.insert(err_binding_name).second;
                write_ln(std::format("const cfire::Error& {} = _target.error();", err_binding_name));
            }
            if (arm.guard) {
                write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                ++indent_;
                write_ln("_matched = true;");
                emit_arm(arm.body);
                --indent_;
                write_ln("}");
            } else {
                write_ln("_matched = true;");
                emit_arm(arm.body);
            }
            if (added_err_binding) error_vars_.erase(err_binding_name);
            --indent_;
            write_ln("}");
        }
        if (is_expr) {
            write_ln("cfire::assert_true(false, \"unreachable result match\");");
        }
        --indent_;
        if (is_expr) {
            write_tail("})()");
        } else {
            write_ln("}");
        }
        return;
    }

    std::string enum_name;
    if (target) {
        if (const auto* id = std::get_if<IdentExpr>(&target->node)) {
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                std::string resolved = resolve_enum_cpp_name(it->second);
                if (cf_.enums.contains(resolved)) enum_name = resolved;
            }
        }
    }
    if (enum_name.empty()) {
        for (const auto& arm : arms) {
            if (!arm.enum_prefix.empty()) {
                enum_name = resolve_enum_cpp_name(arm.enum_prefix);
                break;
            }
            if (auto it = cf_.variant_to_enum.find(arm.variant_name); it != cf_.variant_to_enum.end()) {
                if (!cf_.ambiguous_variants.contains(arm.variant_name)) {
                    enum_name = it->second;
                    break;
                }
            }
        }
    }

    if (is_primitive_match || enum_name.empty()) {
        std::string target_expr = emit_expr(target);
        TypeId prim_tid = target ? TypeId{target->resolved_type_id} : TypeId::Unknown();
        const TypeDesc& prim_td = cf_.type_table.get(prim_tid);
        std::string target_decl = "const auto&";
        std::string catchall_decl = "const auto&";
        if (prim_td.kind == TypeKind::Borrow && prim_td.elem_type == TypeId::Str()) {
            target_decl = "cfire::str_ref";
            catchall_decl = "cfire::str_ref";
        } else if (prim_tid == TypeId::Str()) {
            if (target && std::holds_alternative<IdentExpr>(target->node)) {
                target_decl = "cfire::str_ref";
                catchall_decl = "cfire::str_ref";
            } else {
                target_decl = "const std::string&";
                catchall_decl = "const std::string&";
            }
        } else if (auto t_cpp = lower_type_id(prim_tid, LowerContext::Value)) {
            target_decl = std::format("const {}&", *t_cpp);
            catchall_decl = target_decl;
        }
        write_ln(is_expr ? match_lambda_header() : std::string("{"));
        ++indent_;
        write_ln(std::format("{} _target = {};", target_decl, target_expr));
        write_ln("bool _matched = false;");
        auto lower_match_lit = [](std::string_view v) -> std::string {
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                return lower_string_literal_cpp(v.substr(1, v.size() - 2));
            }
            return lower_cpp_int_literal(v);
        };
        for (const auto& arm : arms) {
            std::vector<MatchPattern> pats = arm.patterns.empty()
                ? std::vector<MatchPattern>{arm.to_pattern()}
                : arm.patterns;
            const bool is_catchall_bind = cf_.match_catchall_bindings.contains(&arm) && !arm.variant_name.empty();
            if ((pats.size() == 1 && pats[0].variant_name == "_" && pats[0].literal_val.empty() && !pats[0].is_range) || is_catchall_bind) {
                write_ln("if (!_matched) {");
                ++indent_;
                if (is_catchall_bind) {
                    local_vars_.insert(arm.variant_name);
                    write_ln(std::format("{} {} = _target;", catchall_decl, sanitize_cpp_ident(arm.variant_name)));
                }
                if (arm.guard) {
                    write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                    ++indent_;
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                    --indent_;
                    write_ln("}");
                } else {
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                }
                --indent_;
                write_ln("}");
            } else {
                std::string cond;
                for (std::size_t pi = 0; pi < pats.size(); ++pi) {
                    if (pi > 0) cond += " || ";
                    if (pats[pi].is_range) {
                        std::string op = pats[pi].range_inclusive ? "<=" : "<";
                        cond += std::format("(_target >= {} && _target {} {})",
                            lower_match_lit(pats[pi].range_start), op, lower_match_lit(pats[pi].range_end));
                    } else {
                        std::string pat = !pats[pi].literal_val.empty() ? lower_match_lit(pats[pi].literal_val) : pats[pi].variant_name;
                        cond += std::format("(_target == {})", pat);
                    }
                }
                write_ln(std::format("if (!_matched && ({})) {{", cond));
                ++indent_;
                if (arm.guard) {
                    write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                    ++indent_;
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                    --indent_;
                    write_ln("}");
                } else {
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                }
                --indent_;
                write_ln("}");
            }
        }
        if (is_expr) {
            write_ln("cfire::assert_true(false, \"unreachable match\");");
        }
        --indent_;
        if (is_expr) {
            write_tail("})()");
        } else {
            write_ln("}");
        }
        return;
    }

    bool is_rvalue = is_move || (target && std::holds_alternative<CallExpr>(target->node));
    const EnumDecl* matched_enum = nullptr;
    if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
        matched_enum = &eit->second;
    }
    write_ln(is_expr ? match_lambda_header() : std::string("{"));
    ++indent_;
    if (is_rvalue) {
        write_ln(std::format("auto&& _target = {};", emit_expr(target)));
    } else {
        std::optional<std::string> enum_cpp;
        if (target && target->resolved_type_id != 0) {
            enum_cpp = lower_type_id(TypeId{target->resolved_type_id}, LowerContext::Value);
        }
        std::string bind_t = enum_cpp.value_or(enum_name);
        write_ln(std::format("const {}& _target = {};", bind_t.empty() ? std::string("auto") : bind_t, emit_expr(target)));
    }
    write_ln("bool _matched = false;");

    for (const auto& arm : arms) {
        std::vector<MatchPattern> pats = arm.patterns.empty()
            ? std::vector<MatchPattern>{arm.to_pattern()}
            : arm.patterns;
        for (const auto& pat : pats) {
            if (pat.variant_name == "_") {
                write_ln("if (!_matched) {");
                ++indent_;
                if (arm.guard) {
                    write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                    ++indent_;
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                    --indent_;
                    write_ln("}");
                } else {
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                }
                --indent_;
                write_ln("}");
            } else {
                write_ln("if (!_matched) {");
                ++indent_;
                write_ln(std::format("if (auto* _p = std::get_if<{}::_{}>(&_target._v)) {{", enum_name, pat.variant_name));
                ++indent_;
                const EnumVariant* matched_var = nullptr;
                if (matched_enum && matched_enum->type_params.empty()) {
                    for (const auto& v : matched_enum->variants) {
                        if (v.name == pat.variant_name) { matched_var = &v; break; }
                    }
                }
                for (std::size_t i = 0; i < pat.bindings.size(); ++i) {
                    if (pat.bindings[i] == "_") continue;
                    local_vars_.insert(pat.bindings[i]);
                    if (is_rvalue) {
                        write_ln(std::format("auto&& {} = std::move(_p->_{});", pat.bindings[i], i));
                    } else if (matched_var && i < matched_var->payload_types.size()) {
                        std::string p_cpp = lower_owning_type(matched_var->payload_types[i]);
                        write_ln(std::format("const {}& {} = _p->_{};", p_cpp, pat.bindings[i], i));
                    } else {
                        write_ln(std::format("const auto& {} = _p->_{};", pat.bindings[i], i));
                    }
                }
                if (arm.guard) {
                    write_ln(std::format("if ({}) {{", emit_expr(arm.guard)));
                    ++indent_;
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                    --indent_;
                    write_ln("}");
                } else {
                    write_ln("_matched = true;");
                    emit_arm(arm.body);
                }
                --indent_;
                write_ln("}");
                --indent_;
                write_ln("}");
            }
        }
    }
    if (is_expr) {
        write_ln("cfire::assert_true(false, \"unreachable enum match\");");
    }
    --indent_;
    if (is_expr) {
        write_tail("})()");
    } else {
        write_ln("}");
    }
}

CFIRE_CG_INLINE void Emitter::emit_match_stmt(const MatchStmt& ms, bool in_main) {
    std::ostringstream dummy;
    emit_match_common(ms.target, ms.is_move, ms.arms, false, in_main, dummy);
    last_source_line_ = -1;
}

CFIRE_CG_INLINE void Emitter::emit_match_arm_body(std::ostringstream& sb, const std::vector<StmtPtr>& body, bool is_expr) {
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
                std::string em = emit_expr(es.expr);
                if (es.expr) {
                    if (const auto* lit = std::get_if<LiteralExpr>(&es.expr->node)) {
                        if ((lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) &&
                            !em.starts_with("std::format(") && !em.starts_with("std::string(")) {
                            em = std::format("std::string({})", em);
                        }
                    }
                }
                for (int j = 0; j < indent_; ++j) sb << "    ";
                sb << "return " << em << ";\n";
                continue;
            }
        }
        Emitter sub = make_sub_emitter();
        sub.emit_stmt(body[i], false);
        sb << sub.buf_.str();
        local_vars_ = sub.local_vars_;
        ptr_vars_ = sub.ptr_vars_;
        optional_vars_ = sub.optional_vars_;
        map_vars_ = sub.map_vars_;
        fixed_array_vars_ = sub.fixed_array_vars_;
    }
}

[[nodiscard]] CFIRE_CG_INLINE auto Emitter::emit_match_expr(const MatchExpr& me) -> std::string {
    std::ostringstream sb;
    std::string saved = std::exchange(match_expr_result_type_, me.result_type);
    TypeId saved_tid = std::exchange(match_expr_result_tid_, TypeId{me.resolved_type_id});
    emit_match_common(me.target, me.is_move, me.arms, true, false, sb);
    match_expr_result_type_ = saved;
    match_expr_result_tid_ = saved_tid;
    last_source_line_ = -1;
    return sb.str();
}


} // namespace cfire::frontend
