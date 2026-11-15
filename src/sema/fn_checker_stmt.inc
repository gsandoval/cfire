// Generated shard for SemaFnChecker (stmt)



    CFIRE_SEMA_INLINE void SemaFnChecker::save_var_before_redecl(const std::string& name) {
        if (scope_stack_.empty() || name.empty() || name == "_") return;
        auto* top = scope_stack_.back();
        if (top->contains(name)) return;
        SavedVarDeclState st;
        if (auto it = var_scope_depth_.find(name); it != var_scope_depth_.end()) st.scope_depth = it->second;
        st.was_local_defined = local_defined_vars_.contains(name);
        st.was_mut = mut_vars_.contains(name);
        st.was_imm = imm_vars_.contains(name);
        if (auto it = var_types_.find(name); it != var_types_.end()) st.var_type = it->second;
        st.was_optional = optional_vars_.contains(name);
        st.was_fallible = fallible_vars_.contains(name);
        st.was_reported_unhandled = reported_unhandled_vars_.contains(name);
        st.was_scalar_primitive = scalar_primitive_vars_.contains(name);
        st.was_ptr_or_smart = ptr_or_smart_vars_.contains(name);
        st.was_raw_or_smart_ptr = raw_or_smart_ptr_vars_.contains(name);
        if (auto it = ptr_null_state_.find(name); it != ptr_null_state_.end()) st.null_state = it->second;
        if (auto it = fixed_array_lens_.find(name); it != fixed_array_lens_.end()) st.fixed_array_len = it->second;
        st.was_map = map_vars_.contains(name);
        if (auto it = borrow_origin_.find(name); it != borrow_origin_.end()) st.borrow_origin = it->second;
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) st.moved_at_line = it->second;
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) st.invalidated_loan = it->second;
        top->emplace(name, std::move(st));
    }


    CFIRE_SEMA_INLINE void SemaFnChecker::bind_scoped_var(const std::string& name, bool is_mut) {
        if (name.empty() || name == "_") return;
        save_var_before_redecl(name);
        var_scope_depth_[name] = scope_depth_;
        local_defined_vars_.insert(name);
        moved_at_line_.erase(name);
        invalidated_loans_.erase(name);
        borrow_origin_.erase(name);
        if (is_mut) {
            imm_vars_.erase(name);
            mut_vars_.insert(name);
        } else {
            mut_vars_.erase(name);
            imm_vars_.insert(name);
        }
    }


    CFIRE_SEMA_INLINE void SemaFnChecker::bind_match_arm_vars(const MatchArm& arm, const std::string& target_t) {
        for (const auto& b : arm.bindings) {
            bind_scoped_var(b, false);
        }
        if (!arm.bindings.empty()) {
            if (arm.is_some || arm.variant_name == "Some") {
                if (target_t.ends_with('?')) {
                    var_types_[arm.bindings[0]] = target_t.substr(0, target_t.size() - 1);
                }
            } else if (arm.is_ok || arm.variant_name == "Ok") {
                if (target_t.ends_with('!')) {
                    var_types_[arm.bindings[0]] = target_t.substr(0, target_t.size() - 1);
                }
            } else if (arm.is_err || arm.variant_name == "Err") {
                var_types_[arm.bindings[0]] = "Error";
            }
        }
        if (arm.is_slice_pattern) {
            for (const auto& h : arm.slice_head) bind_scoped_var(h, false);
            if (!arm.slice_rest.empty()) bind_scoped_var(arm.slice_rest, false);
            for (const auto& t : arm.slice_tail) bind_scoped_var(t, false);
        }
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_block_stmts(const std::vector<StmtPtr>& stmts, bool tail_is_value) -> std::expected<void, std::string> {
        for (std::size_t i = 0; i < stmts.size(); ++i) {
            if (tail_is_value && i + 1 == stmts.size() && stmts[i]) {
                if (const auto* es = std::get_if<ExprStmt>(&stmts[i]->node)) {
                    if (auto r = check_expr(es->expr); !r) return r;
                    continue;
                }
            }
            if (auto r = check_stmt(stmts[i]); !r) return r;
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_stmt(const StmtPtr& s) -> std::expected<void, std::string> {
        if (!s) return {};
        if (std::holds_alternative<CppBlockStmt>(s->node)) {
            seen_cpp_block_ = true;
            // CF-124 / F9: Foreign C++ blocks can arbitrarily mutate pointees and alias memory.
            // Invalidate all active loans and demote NonNull facts to MaybeNull.
            for (const auto& [borrower, orig] : borrow_origin_) {
                invalidated_loans_[borrower] = InvalidatedLoan{"loan invalidated by foreign cpp block", s ? s->line : 1};
            }
            for (auto& [var, state] : ptr_null_state_) {
                if (state == NullState::NonNull) {
                    state = NullState::MaybeNull;
                }
            }
            return {};
        }
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            return check_var_decl_stmt(*vd);
        }
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            return check_destructure_var_decl_stmt(*dvs);
        }
        if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            return check_assign_stmt(*as);
        }
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            return check_destructure_assign_stmt(*das);
        }
        if (const auto* df = std::get_if<DeferStmt>(&s->node)) {
            return check_defer_stmt(*df);
        }
        if (const auto* rt = std::get_if<ReturnStmt>(&s->node)) {
            return check_return_stmt(*rt);
        }
        if (const auto* bs = std::get_if<BreakStmt>(&s->node)) {
            if (loop_depth_ == 0) {
                return emit_error(diag_at(bs->line, 0, "cfire/BREAK-OUTSIDE-LOOP", std::format("'break' cannot be used outside of a 'for' loop")));
            }
            return {};
        }
        if (const auto* cs = std::get_if<ContinueStmt>(&s->node)) {
            if (loop_depth_ == 0) {
                return emit_error(diag_at(cs->line, 0, "cfire/BREAK-OUTSIDE-LOOP", std::format("'continue' cannot be used outside of a 'for' loop")));
            }
            return {};
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            return check_if_stmt(*ifs);
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            return check_for_stmt(*fs);
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            return check_match_common(ms->target, ms->is_move, ms->arms, ms->line, ms->col, false);
        }
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) {
            return check_async_scope_stmt(*sc);
        }
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* or_ex = std::get_if<OrExpr>(&es->expr->node); or_ex && !or_ex->handler_body.empty()) {
                    discarded_or_exprs_.insert(es->expr);
                }
            }
            std::size_t errors_before = errors_.size();
            if (auto r = check_expr(es->expr); !r) return r;
            if (errors_.size() == errors_before && es->expr && !is_effectful_stmt_expr(es->expr)) {
                auto [ln, col] = expr_start_loc(es->expr);
                if (ln <= 0) ln = s->line;
                if (const auto* bin = std::get_if<BinaryExpr>(&es->expr->node); bin && bin->op == "==") {
                    return emit_error(diag_at(ln, col, "cfire/NO-EFFECT-STMT", std::format("equality comparison '==' statement has no effect; did you mean '=' assignment?")));
                }
                return emit_error(diag_at(ln, col, "cfire/NO-EFFECT-STMT", std::format("expression statement has no effect; assign the result or call a function")));
            }
            return {};
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_var_decl_stmt(const VarDeclStmt& vd) -> std::expected<void, std::string> {
        if (vd.is_mut && !vd.type && vd.init) {
            TypeId it = infer_expr_type_id_sema(vd.init);
            if (it == TypeId::Str() || it == type_id_of("&str")) cf_.owned_text_decls.insert(&vd);
            if (cf_.type_table.get(it).kind == TypeKind::Slice) cf_.owned_slice_decls.insert(&vd);
        }
        if (!vd.type && vd.init) {
            if (const auto* me = std::get_if<MatchExpr>(&vd.init->node); me && !me->arms.empty()) {
                const bool all_const = std::all_of(me->arms.begin(), me->arms.end(), [](const MatchArm& arm) {
                    const ExprStmt* es = arm.body.empty() || !arm.body.back() ? nullptr : std::get_if<ExprStmt>(&arm.body.back()->node);
                    return es && const_int_value(es->expr).has_value();
                });
                if (all_const) cf_.int64_decls.insert(&vd);
            }
        }
        const std::size_t errors_before_type = errors_.size();
        if (vd.type) {
            if (auto r = check_type_ref_separators(*vd.type, vd.line, vd.col, true); !r) return r;
            if (!vd.type->is_slice && !vd.type->is_array && !vd.type->is_optional && !vd.type->is_ptr) {
                if (auto r = check_int_literal_fits(vd.init, vd.type->name, vd.line, vd.col); !r) return r;
            }
            if (auto iface = find_interface_in_slice(*vd.type, cf_.interfaces)) {
                if (auto r = emit_error(diag_at(vd.line, vd.col, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface))); !r) return r;
            }
        }
        const bool decl_type_reported = errors_.size() != errors_before_type;
        if (vd.init && (vd.name == "_" || (vd.type && vd.type->is_fallible))) {
            unwrapped_exprs_.insert(vd.init);
        }
        // Check initializer expression before binding the new variable so `x := x + 1` reads outer `x` first
        if (auto r = check_expr(vd.init); !r) return r;
        if (vd.init && vd.name != "_" && (!vd.type || (!vd.type->is_borrow && !vd.type->is_mut_borrow))) {
            if (auto r = check_move_only_copy(vd.init, std::format("variable '{}'", vd.name), vd.line, vd.col); !r) return r;
        }

        bool is_byte_slice = (vd.type && (vd.type->is_slice || vd.type->is_array) &&
                              (vd.type->name == "byte" || (vd.type->elem_type && vd.type->elem_type->name == "byte"))) ||
                             (!vd.type && is_byte_slice_expr(vd.init));
        if (is_byte_slice && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                for (const auto& el : arr_lit->elements) {
                    if (is_non_byte_int_or_u8_expr(el)) {
                        if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("a '[]byte' literal element must be 0..=255 or a 'byte'; convert wider integers with 'byte(...)'"))); !r) return r;
                    }
                }
            }
        }

        // CF-116: Type compatibility check for declared type vs initializer (skipped when the declared type itself
        // was just reported, so a bad stdlib type name is one error, not two).
        if (vd.type && vd.init && !decl_type_reported) {
            TypeId dest_t = type_id_of(*vd.type);
            TypeId src_t = infer_expr_type_id_sema(vd.init);
            // An array literal adapts to the declared element type, like a bare literal does (`xs: []i32 = [1, 2]`);
            // its elements are checked individually instead of as an inferred `[]i64` (CF-191). A bare integer
            // literal was range-checked above.
            if (!is_numeric_literal_expr(vd.init) && !is_byte_slice) {
                bool handled = false;
                if (auto r = check_value_for(dest_t, vd.init, handled); !r) return r;
                if (handled) src_t = TypeId::Unknown();
            }
            if (std::holds_alternative<ArrayLitExpr>(vd.init->node)) src_t = TypeId::Unknown();
            if (dest_t.is_valid() && src_t.is_valid() && !dest_t.is_unknown() && !src_t.is_unknown()) {
                std::string dest_str = cf_.type_table.to_string(dest_t);
                std::string src_str = cf_.type_table.to_string(src_t);
                if (dest_str.find("cpp::") == std::string::npos &&
                    src_str.find("cpp::") == std::string::npos &&
                    dest_str.find("cpp.") == std::string::npos &&
                    src_str.find("cpp.") == std::string::npos &&
                    !cf_.type_table.is_assignable(dest_t, src_t)) {
                    if (!is_byte_slice) {
                        return emit_error(diag_at(vd.line, vd.col, "cfire/TYPE-MISMATCH", std::format("cannot initialize variable '{}' of type '{}' with expression of type '{}'{}", vd.name, dest_str, src_str, nominal_mismatch_hint_sema(dest_t, src_t))));
                    }
                }
            }
        }
        if (!vd.type && vd.name != "_" && vd.init) {
            TypeId src_t = infer_expr_type_id_sema(vd.init);
            if (src_t.is_unknown() && !std::holds_alternative<LiteralExpr>(vd.init->node)) {
                bool is_foreign = false;
                auto check_foreign_expr = [&](auto& self, const ExprPtr& ex) -> bool {
                    if (!ex) return false;
                    if (const auto* call = std::get_if<CallExpr>(&ex->node)) {
                        if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                            if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr) {
                                if (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name)) return true;
                            }
                        } else if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                            if (cid->name.starts_with("cpp.") || is_vendor_alias_sema(cid->name) || is_cpp_qualifier_sema(cid->name)) return true;
                        }
                    } else if (const auto* slit = std::get_if<StructLitExpr>(&ex->node)) {
                        auto dot = slit->type_name.find('.');
                        if (dot != std::string::npos) {
                            std::string qual = slit->type_name.substr(0, dot);
                            if (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual)) return true;
                        }
                    } else if (const auto* mem = std::get_if<MemberExpr>(&ex->node)) {
                        if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr) {
                            if (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name) || foreign_vars_.contains(oid->name)) return true;
                        }
                    } else if (const auto* id = std::get_if<IdentExpr>(&ex->node)) {
                        if (foreign_vars_.contains(id->name)) return true;
                    } else if (const auto* bin = std::get_if<BinaryExpr>(&ex->node)) {
                        if (self(self, bin->left) || self(self, bin->right)) return true;
                    } else if (const auto* un = std::get_if<UnaryExpr>(&ex->node)) {
                        if (self(self, un->operand)) return true;
                    } else if (const auto* or_ex = std::get_if<OrExpr>(&ex->node)) {
                        if (self(self, or_ex->inner)) return true;
                    }
                    return false;
                };
                is_foreign = check_foreign_expr(check_foreign_expr, vd.init);
                if (is_foreign) {
                    foreign_vars_.insert(vd.name);
                } else {
                    emit_warning(diag_at(vd.line, vd.col, "cfire/UNKNOWN-TYPE", std::format("could not infer concrete type for variable '{}'; expression has unknown type", vd.name)));
                }
            }
        }

        if (auto r = check_view_of_temporary(vd.init, std::format("'{}'", vd.name), vd.line, vd.col); !r) return r;
        if (vd.init && is_constant_string_expr(vd.init)) const_string_vars_.insert(vd.name);
        else const_string_vars_.erase(vd.name);
        disjoint_slot_ptrs_.erase(vd.name);
        if (vd.init) {
            if (const auto* un = std::get_if<UnaryExpr>(&vd.init->node); un && (un->op == "&mut" || un->op == "&")) {
                if (const auto* ix = un->operand ? std::get_if<IndexExpr>(&un->operand->node) : nullptr) {
                    if (const auto* iv = ix->index ? std::get_if<IdentExpr>(&ix->index->node) : nullptr;
                        iv && active_loop_index_vars_.contains(iv->name)) {
                        disjoint_slot_ptrs_.insert(vd.name);
                    }
                }
            }
        }
        std::optional<std::string> src = extract_borrow_source(vd.init);
        if (!src) {
            if (auto owners = extract_borrow_sources(vd.init); !owners.empty()) src = owners.front();
        }
        record_borrow_origins(vd.name, vd.init);
        std::string inferred_t;
        if (vd.type && !vd.type->smart_wrap.empty() && vd.type->elem_type && !vd.type->elem_type->name.empty()) {
            inferred_t = vd.type->elem_type->name;
        } else if (vd.type) {
            inferred_t = format_type_ref_sema(*vd.type);
        } else {
            inferred_t = type_name_of(vd.init);
        }
        auto dot = inferred_t.find('.');
        if (dot != std::string::npos) {
            std::string qual = inferred_t.substr(0, dot);
            if (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual)) {
                foreign_vars_.insert(vd.name);
            }
        }
        bool is_opt_var = (vd.type && vd.type->is_optional) || (!vd.type && expr_returns_optional_sema(vd.init));
        bool is_fallible_var = (vd.type && vd.type->is_fallible);
        bool init_was_unhandled_fallible = (!vd.type && vd.name != "_" && expr_returns_fallible_sema(vd.init));
        bool is_smart = (vd.type && !vd.type->smart_wrap.empty());
        if (!is_smart && vd.init) {
            const Expr* cur_init = vd.init;
            if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
            else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
            if (cur_init) {
                if (const auto* id = std::get_if<IdentExpr>(&cur_init->node)) {
                    if (smart_ptr_vars_.contains(id->name)) is_smart = true;
                } else if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                    if (const auto* fmem = std::get_if<MemberExpr>(&call_ex->callee->node);
                        fmem && fmem->field == "from_raw" && !own_type_prefix(fmem->object).empty()) {
                        is_smart = true;  // CF-181: `own[T].from_raw(p)`
                    }
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                        if (cid->name == "make" && call_ex->type_args.size() == 1 && !call_ex->type_args[0].smart_wrap.empty()) {
                            is_smart = true;
                        } else if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end() && !fit->second.return_type.smart_wrap.empty()) {
                            is_smart = true;
                        }
                    }
                }
            }
        }
        bool is_raw_or_smart_ptr = (vd.type && (vd.type->is_ptr || vd.type->is_mut_ptr || !vd.type->smart_wrap.empty())) ||
                                   is_ptr_or_smart_init(vd.init);
        bool is_ptr_or_smart = is_raw_or_smart_ptr || is_opt_var;
        bool is_scalar_prim = false;
        if (vd.type) {
            is_scalar_prim = is_scalar_primitive_typeref(*vd.type);
        } else if (!is_opt_var && !is_fallible_var && !init_was_unhandled_fallible && !is_raw_or_smart_ptr && !is_byte_slice && vd.init) {
            if (const auto* lit = std::get_if<LiteralExpr>(&vd.init->node)) {
                is_scalar_prim = (lit->kind == TokenKind::Int || lit->kind == TokenKind::Float || lit->kind == TokenKind::Char ||
                                  lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse);
            } else if (std::holds_alternative<BinaryExpr>(vd.init->node) || std::holds_alternative<UnaryExpr>(vd.init->node)) {
                is_scalar_prim = is_scalar_primitive_type_name(inferred_t);
            } else {
                const Expr* cur_init = vd.init;
                if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
                else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
                if (cur_init) {
                    if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                        if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                            if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end()) {
                                TypeRef rt = fit->second.return_type;
                                rt.is_fallible = false;
                                rt.is_optional = false;
                                is_scalar_prim = is_scalar_primitive_typeref(rt);
                            } else if (is_scalar_primitive_type_name(cid->name)) {
                                is_scalar_prim = true;
                            }
                        } else if (const auto* mem = std::get_if<MemberExpr>(&call_ex->callee->node)) {
                            if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                                    is_scalar_prim = is_scalar_primitive_type_name(std_sig->unwrapped_return_type);
                                }
                            }
                        }
                    }
                }
            }
        }
        auto init_ns = infer_expr_null_state(vd.init);

        bind_scoped_var(vd.name, vd.is_mut);
        map_vars_.erase(vd.name);
        fixed_array_lens_.erase(vd.name);
        if (is_byte_slice) byte_slice_vars_.insert(vd.name);
        else byte_slice_vars_.erase(vd.name);
        if (is_smart) smart_ptr_vars_.insert(vd.name);
        else smart_ptr_vars_.erase(vd.name);
        {
            // CF-181: remember which smart pointers are `shared[T]` (no `release`).
            bool is_shared = vd.type && vd.type->smart_wrap == "shared";
            if (!is_shared && vd.init) {
                if (const auto* call_ex = std::get_if<CallExpr>(&vd.init->node)) {
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                        cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].smart_wrap == "shared") {
                        is_shared = true;
                    }
                } else if (const auto* id = std::get_if<IdentExpr>(&vd.init->node)) {
                    is_shared = shared_ptr_var_names_.contains(id->name);
                }
            }
            if (is_smart && is_shared) shared_ptr_var_names_.insert(vd.name);
            else shared_ptr_var_names_.erase(vd.name);
        }
        if (is_fallible_var) fallible_vars_.insert(vd.name);
        else fallible_vars_.erase(vd.name);
        if (init_was_unhandled_fallible) reported_unhandled_vars_.insert(vd.name);
        else reported_unhandled_vars_.erase(vd.name);
        if (is_raw_or_smart_ptr) raw_or_smart_ptr_vars_.insert(vd.name);
        else raw_or_smart_ptr_vars_.erase(vd.name);
        if (is_scalar_prim) scalar_primitive_vars_.insert(vd.name);
        else scalar_primitive_vars_.erase(vd.name);
        if (vd.type && vd.type->is_fn && vd.type->fn_ret) {
            if (vd.type->fn_ret->is_fallible) fn_ret_fallible_vars_.insert(vd.name);
            else fn_ret_fallible_vars_.erase(vd.name);
            if (vd.type->fn_ret->is_optional) fn_ret_optional_vars_.insert(vd.name);
            else fn_ret_optional_vars_.erase(vd.name);
        } else {
            fn_ret_fallible_vars_.erase(vd.name);
            fn_ret_optional_vars_.erase(vd.name);
        }

        if (vd.type) {
            if (vd.type->is_map) {
                map_vars_.insert(vd.name);
            }
            if (vd.type->is_array && !vd.type->array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(vd.type->array_len)) {
                    fixed_array_lens_[vd.name] = *n;
                }
            }
        } else if (!vd.is_mut && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                fixed_array_lens_[vd.name] = arr_lit->elements.size();
            }
        }
        if (vd.init) {
            if (const auto* call_ex = std::get_if<CallExpr>(&vd.init->node)) {
                if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                    cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].is_map) {
                    map_vars_.insert(vd.name);
                }
            }
        }

        if (src) {
            borrow_origin_[vd.name] = *src;
        }
        if (!inferred_t.empty()) {
            var_types_[vd.name] = std::move(inferred_t);
        } else {
            var_types_.erase(vd.name);
        }
        if (is_opt_var) {
            optional_vars_.insert(vd.name);
        } else {
            optional_vars_.erase(vd.name);
        }
        if (is_ptr_or_smart) {
            ptr_or_smart_vars_.insert(vd.name);
        } else {
            ptr_or_smart_vars_.erase(vd.name);
        }
        if (init_ns) {
            ptr_null_state_[vd.name] = *init_ns;
        } else if (!vd.init && is_ptr_or_smart) {
            ptr_null_state_[vd.name] = NullState::Null;
        } else if (is_opt_var) {
            if (vd.init && (std::holds_alternative<StructLitExpr>(vd.init->node) ||
                            (std::holds_alternative<LiteralExpr>(vd.init->node) &&
                             std::get<LiteralExpr>(vd.init->node).kind != TokenKind::KwNone))) {
                ptr_null_state_[vd.name] = NullState::NonNull;
            } else {
                ptr_null_state_[vd.name] = NullState::MaybeNull;
            }
        } else if (vd.type && (vd.type->is_ptr || vd.type->is_mut_ptr || !vd.type->smart_wrap.empty())) {
            ptr_null_state_[vd.name] = vd.type->is_optional ? NullState::MaybeNull : NullState::NonNull;
        } else {
            ptr_null_state_.erase(vd.name);
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_destructure_var_decl_stmt(const DestructureVarDeclStmt& dvs) -> std::expected<void, std::string> {
        if (auto r = check_expr(dvs.init); !r) return r;
        std::string elem_type;
        if (dvs.init) {
            const Expr* inner = dvs.init;
            if (const auto* tr = std::get_if<TryExpr>(&inner->node)) inner = tr->inner;
            else if (const auto* orex = std::get_if<OrExpr>(&inner->node)) inner = orex->inner;
            if (inner) {
                if (const auto* call = std::get_if<CallExpr>(&inner->node)) {
                    if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                        if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                            if (obj_id->name == "os" && mem->field == "pipe") elem_type = "File";
                            else if (obj_id->name == "net" && mem->field == "socketpair") elem_type = "TcpConn";
                            else if (obj_id->name == "websocket" && mem->field == "pair") elem_type = "websocket.Conn";
                        }
                    }
                }
            }
        }
        for (const auto& [name, is_mut] : dvs.bindings) {
            bind_scoped_var(name, is_mut);
            if (!elem_type.empty()) {
                var_types_[name] = elem_type;
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::get_root_variable_name(const ExprPtr& e) const -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return get_root_variable_name(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return get_root_variable_name(idx->object);
        return "";
    }


    // CF-164 (G1): inside a `scope.spawn` closure, writing through a captured outer pointer (`*p = v`,
    // `p.field = v` via auto-deref) races with every other task holding the same pointer. The one provably
    // disjoint form is allowed: a pointer created inside a `for` body as `&mut xs[i]` from that loop's own
    // index variable, so each task writes a different slot (examples/04-async-workers).
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_spawn_pointer_write(const ExprPtr& target, int line, bool* reported) -> std::expected<void, std::string> {
        const Expr* e = target;
        bool through_deref = false;
        while (e) {
            if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "*") { through_deref = true; e = un->operand; continue; }
            if (const auto* m = std::get_if<MemberExpr>(&e->node)) { e = m->object; continue; }
            if (const auto* ix = std::get_if<IndexExpr>(&e->node)) { e = ix->object; continue; }
            break;
        }
        const auto* id = e ? std::get_if<IdentExpr>(&e->node) : nullptr;
        if (!id) return {};
        auto sit = var_scope_depth_.find(id->name);
        if (sit == var_scope_depth_.end() || sit->second > spawn_entry_scope_) return {};  // task-local
        std::string ty;
        if (auto tit = var_types_.find(id->name); tit != var_types_.end()) ty = tit->second;
        bool is_pointer = raw_or_smart_ptr_vars_.contains(id->name) || ptr_or_smart_vars_.contains(id->name) ||
                          ty.starts_with("*") || ty.starts_with("&mut") || ty.starts_with("shared[") || ty.starts_with("own[");
        if (!is_pointer || (!through_deref && !ty.starts_with("shared[") && !ty.starts_with("own[") &&
                            !raw_or_smart_ptr_vars_.contains(id->name) && !ptr_or_smart_vars_.contains(id->name))) {
            return {};
        }
        if (disjoint_slot_ptrs_.contains(id->name)) return {};
        if (reported) *reported = true;
        return emit_error(diag_at(line, 0, "cfire/SPAWN-SHARED-MUT", std::format("spawned task writes through captured pointer '{}', which other tasks can also reach (data race); protect the target with 'sync.Mutex[T]'/'sync.Atomic[T]', send results over a 'sync.Channel[T]', or give each task its own slot with 'slot := &mut xs[i]' inside the spawning loop", id->name)));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_concurrency_safe_type(const std::string& ty) const -> bool {
        if (ty.starts_with("sync.Mutex") || ty.starts_with("cfire::sync::Mutex")) return true;
        if (ty.starts_with("sync.Atomic") || ty.starts_with("cfire::sync::Atomic")) return true;
        if (ty.starts_with("sync.Channel") || ty.starts_with("cfire::sync::Channel")) return true;
        if (ty.starts_with("sync.WaitGroup") || ty.starts_with("sync.RwLock")) return true;
        // shared[T] shares ownership, not access: its pointee is unsynchronized (CF-164).
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_assign_stmt(const AssignStmt& as) -> std::expected<void, std::string> {
        if (as.value && as.target) {
            if (const auto* id = std::get_if<IdentExpr>(&as.target->node)) {
                if (id->name == "_" || fallible_vars_.contains(id->name)) {
                    unwrapped_exprs_.insert(as.value);
                }
            }
        }
        if (auto r = check_expr(as.value); !r) return r;
        if (as.op == "<<=" || as.op == ">>=") {
            if (auto r = check_const_shift_count(as.target, as.value, as.line, 1); !r) return r;
        }
        if ((as.op == "/=" || as.op == "%=") && is_const_zero_expr(as.value)) {
            if (auto r = emit_error(diag_at(as.line, 0, "cfire/DIVIDE-BY-ZERO", std::format("division or modulo assignment by constant zero"))); !r) return r;
        }
        if (as.op != "=") {
            if (auto r = check_expr(as.target); !r) return r;
        }
        if (const auto* tid = as.target ? std::get_if<IdentExpr>(&as.target->node) : nullptr) {
            bool stays_constant = as.op == "=" ? is_constant_string_expr(as.value)
                                : (as.op == "+=" && const_string_vars_.contains(tid->name) && is_constant_string_expr(as.value));
            if (stays_constant) const_string_vars_.insert(tid->name);
            else const_string_vars_.erase(tid->name);
        }
        bool spawn_ptr_reported = false;
        if (in_spawn_closure_depth_ > 0 && as.target) {
            if (auto r = check_spawn_pointer_write(as.target, as.line, &spawn_ptr_reported); !r) return r;
        }
        if (in_escaping_closure_depth_ > 0 && as.target && !spawn_ptr_reported) {
            std::string root = get_root_variable_name(as.target);
            if (!root.empty() && root != "_" && !active_slot_captures_.contains(root)) {
                auto sit = var_scope_depth_.find(root);
                if (sit != var_scope_depth_.end() && sit->second <= escaping_closure_entry_scope_) {
                    std::string r_ty;
                    if (auto tit = var_types_.find(root); tit != var_types_.end()) r_ty = tit->second;
                    if (!is_concurrency_safe_type(r_ty)) {
                        return emit_error(diag_at(as.line, 0, "cfire/ESCAPING-CAPTURE-MUT", std::format("cannot mutate captured outer variable '{}' inside escaping closure; wrap shared mutable state in 'sync.Mutex[T]', 'sync.Atomic[T]', or communicate via 'sync.Channel[T]'", root)));
                    }
                }
            }
        }
        if (as.op == "=" && as.target && as.value) {
            if (const auto* tid_mv = std::get_if<IdentExpr>(&as.target->node); !tid_mv || tid_mv->name != "_") {
                std::string tdesc = (tid_mv && !tid_mv->name.empty()) ? std::format("'{}'", tid_mv->name) : std::string("assignment target");
                if (auto r = check_move_only_copy(as.value, tdesc, as.line, 1); !r) return r;
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&as.target->node)) {
            if (id->name == "_") return {};
            if (byte_slice_vars_.contains(id->name) && as.value) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&as.value->node)) {
                    for (const auto& el : arr_lit->elements) {
                        if (is_non_byte_int_or_u8_expr(el)) {
                            if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("a '[]byte' literal element must be 0..=255 or a 'byte'; convert wider integers with 'byte(...)'"))); !r) return r;
                        }
                    }
                }
            }
            if (imm_vars_.contains(id->name)) {
                if (auto r = emit_error(diag_at(as.line, 1, "cfire/ASSIGN-IMMUTABLE", std::format("cannot assign to immutable variable '{}'", id->name))); !r) return r;
            }
            if (auto r = check_not_iterated_collection(id->name, as.line, 1); !r) return r;
            moved_at_line_.erase(id->name);
            if (as.op == "=") {
                invalidate_loans_for_owner(
                    id->name,
                    std::format("underlying owner '{}' was reassigned at line {}", id->name, as.line),
                    as.line);
                if (auto r = check_view_of_temporary(as.value, std::format("'{}'", id->name), as.line, 1); !r) return r;
                std::optional<std::string> src = extract_borrow_source(as.value);
                if (!src) {
                    if (auto owners = extract_borrow_sources(as.value); !owners.empty()) src = owners.front();
                }
                record_borrow_origins(id->name, as.value);
                if (src) {
                    auto d_target = var_scope_depth_.find(id->name);
                    auto d_src = var_scope_depth_.find(*src);
                    if (d_target != var_scope_depth_.end() && d_src != var_scope_depth_.end()) {
                        if (d_src->second > d_target->second) {
                            if (auto r = emit_error(diag_at(as.line, 0, "cfire/DANGLING-POINTER", std::format("pointer/view '{}' cannot borrow '{}' from an inner scope", id->name, *src))); !r) return r;
                        }
                    }
                    borrow_origin_[id->name] = *src;
                    invalidated_loans_.erase(id->name);
                } else {
                    borrow_origin_.erase(id->name);
                    invalidated_loans_.erase(id->name);
                }
                if (is_ptr_or_smart_init(as.value)) {
                    ptr_or_smart_vars_.insert(id->name);
                }
                if (auto ns = infer_expr_null_state(as.value)) {
                    ptr_null_state_[id->name] = *ns;
                } else if (ptr_null_state_.contains(id->name)) {
                    ptr_null_state_[id->name] = NullState::NonNull;
                }
            }
        } else {
            if (const auto* idx = std::get_if<IndexExpr>(&as.target->node)) {
                if (is_byte_slice_expr(idx->object) && is_non_byte_int_or_u8_expr(as.value)) {
                    if (auto r = emit_error(diag_at(as.line, 0, "cfire/TYPE-MISMATCH", std::format("cannot assign integer/u8 value to '[]byte' element without explicit 'byte(...)' conversion"))); !r) return r;
                }
            }
            if (as.op == "=") {
                if (auto r = check_expr(as.target); !r) return r;
            }
        }
        // CF-196: a compound assignment (`sum += i`) stores into the target's type, so the value must widen to it.
        if (as.target && as.value && as.op != "=" && as.op != "<<=" && as.op != ">>=") {
            TypeId dest_t = infer_expr_type_id_sema(as.target);
            if (cf_.type_table.is_integer(dest_t) && !const_int_value(as.value)) {
                bool handled = false;
                if (auto r = check_value_for(dest_t, as.value, handled); !r) return r;
            }
        }
        // CF-116: Type compatibility check for assignment
        if (as.target && as.value && as.op == "=") {
            TypeId dest_t = infer_expr_type_id_sema(as.target);
            TypeId src_t = infer_expr_type_id_sema(as.value);
            {
                bool handled = false;
                if (auto r = check_value_for(dest_t, as.value, handled); !r) return r;
                if (handled) src_t = TypeId::Unknown();
            }
            if (std::holds_alternative<ArrayLitExpr>(as.value->node)) src_t = TypeId::Unknown();  // literal adapts (CF-148)
            if (dest_t.is_valid() && src_t.is_valid() && !dest_t.is_unknown() && !src_t.is_unknown()) {
                std::string dest_str = cf_.type_table.to_string(dest_t);
                std::string src_str = cf_.type_table.to_string(src_t);
                if (dest_str.find("cpp::") == std::string::npos &&
                    src_str.find("cpp::") == std::string::npos &&
                    dest_str.find("cpp.") == std::string::npos &&
                    src_str.find("cpp.") == std::string::npos &&
                    !cf_.type_table.is_assignable(dest_t, src_t)) {
                    return emit_error(diag_at(as.line, 0, "cfire/TYPE-MISMATCH", std::format("cannot assign expression of type '{}' to target of type '{}'{}", src_str, dest_str, nominal_mismatch_hint_sema(dest_t, src_t))));
                }
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_destructure_assign_stmt(const DestructureAssignStmt& das) -> std::expected<void, std::string> {
        if (auto r = check_expr(das.value); !r) return r;
        for (const auto& targ : das.targets) {
            if (const auto* id = std::get_if<IdentExpr>(&targ->node)) {
                if (imm_vars_.contains(id->name)) {
                    if (auto r = emit_error(diag_at(das.line, 1, "cfire/ASSIGN-IMMUTABLE", std::format("cannot assign to immutable variable '{}'", id->name))); !r) return r;
                }
                moved_at_line_.erase(id->name);
            } else {
                if (auto r = check_expr(targ); !r) return r;
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_defer_stmt(const DeferStmt& df) -> std::expected<void, std::string> {
        if (df.call) {
            unwrapped_exprs_.insert(df.call);
            return check_expr(df.call);
        }
        ++in_defer_depth_;
        int saved_loop_depth = loop_depth_;
        loop_depth_ = 0;
        auto defer_guard = cfire::scope_exit([this, saved_loop_depth] {
            --in_defer_depth_;
            loop_depth_ = saved_loop_depth;
        });
        ScopeGuard sg(*this);
        return check_block_stmts(df.body);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_return_stmt(const ReturnStmt& rt) -> std::expected<void, std::string> {
        if (in_defer_depth_ > 0) {
            if (auto r = emit_error(diag_at(rt.line, 0, "cfire/RETURN-IN-DEFER", std::format("cannot use 'return' inside a 'defer {{ ... }}' block"))); !r) return r;
        }
        if (rt.value) {
            if (auto* cl = std::get_if<ClosureExpr>(&rt.value->node)) {
                cl->is_escaping = true;
            }
        }
        if (rt.value && (fn_is_fallible_ || in_closure_depth_ > 0)) {
            unwrapped_exprs_.insert(rt.value);
        }
        if (auto r = check_expr(rt.value); !r) return r;
        if (rt.value && !closure_ret_stack_.empty() && closure_ret_stack_.back().second == in_closure_depth_) {
            if (TypeId t = infer_expr_type_id_sema(rt.value); t.is_valid() && !t.is_unknown()) {
                closure_ret_types_.try_emplace(closure_ret_stack_.back().first, cf_.type_table.to_string(t));
            }
        }
        if (rt.value) {
            bool is_direct = false;
            if (auto r = check_view_of_temporary(rt.value, "returned value", rt.line, 1); !r) return r;
            if (!extract_borrow_source(rt.value)) {
                // CF-144: a call returning a view of a local (e.g. `return head(local)`) dangles too.
                for (const auto& owner : extract_borrow_sources(rt.value)) {
                    if (local_defined_vars_.contains(owner) || move_params_.contains(owner)) {
                        if (auto r = emit_error(diag_at(rt.line, 0, "cfire/DANGLING-RETURN", std::format("cannot return pointer/view borrowing local variable '{}'", owner))); !r) return r;
                        break;
                    }
                }
            }
            if (auto src = extract_borrow_source(rt.value, &is_direct)) {
                if (local_defined_vars_.contains(*src) || move_params_.contains(*src)) {
                    if (auto r = emit_error(diag_at(rt.line, 0, "cfire/DANGLING-RETURN", std::format("cannot return pointer/view borrowing local variable '{}'", *src))); !r) return r;
                }
            }
        }
        // CF-116: Type compatibility check for return statement
        if (rt.value && !fn_ret_str_.empty() && fn_ret_str_ != "void" && in_closure_depth_ == 0) {
            TypeId ret_t = type_id_of(fn_ret_str_);
            TypeId val_t = infer_expr_type_id_sema(rt.value);
            {
                bool handled = false;
                if (auto r = check_value_for(fn_is_fallible_ ? cf_.type_table.unwrap_fallible(ret_t) : ret_t, rt.value, handled); !r) return r;
                if (handled) val_t = TypeId::Unknown();
            }
            if (std::holds_alternative<ArrayLitExpr>(rt.value->node)) val_t = TypeId::Unknown();  // literal adapts (CF-148)
            if (ret_t.is_valid() && val_t.is_valid() && !ret_t.is_unknown() && !val_t.is_unknown()) {
                std::string ret_str = cf_.type_table.to_string(ret_t);
                std::string val_str = cf_.type_table.to_string(val_t);
                if (fn_ret_str_.find("cpp::") == std::string::npos &&
                    ret_str.find("cpp::") == std::string::npos &&
                    val_str.find("cpp::") == std::string::npos &&
                    fn_ret_str_.find("cpp.") == std::string::npos &&
                    ret_str.find("cpp.") == std::string::npos &&
                    val_str.find("cpp.") == std::string::npos) {
                    bool ok = cf_.type_table.is_assignable(ret_t, val_t);
                    if (!ok && fn_is_fallible_) {
                        ok = cf_.type_table.is_assignable(cf_.type_table.unwrap_fallible(ret_t), val_t);
                    }
                    if (!ok) {
                        return emit_error(diag_at(rt.line, 0, "cfire/TYPE-MISMATCH", std::format("return type mismatch: function '{}' expects '{}', got '{}'{}", fn_name_, ret_str, val_str, nominal_mismatch_hint_sema(ret_t, val_t))));
                    }
                }
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_if_stmt(const IfStmt& ifs) -> std::expected<void, std::string> {
        std::string unwrap_elem_ty;
        if (ifs.unwrap_init) {
            if (auto r = check_expr(ifs.unwrap_init); !r) return r;
            std::string init_ty = type_name_of(ifs.unwrap_init);
            if (init_ty.ends_with('?')) {
                unwrap_elem_ty = init_ty.substr(0, init_ty.size() - 1);
            } else if (init_ty.ends_with('!')) {
                unwrap_elem_ty = init_ty.substr(0, init_ty.size() - 1);
            } else if (init_ty.starts_with("std::optional<") && init_ty.ends_with('>')) {
                unwrap_elem_ty = init_ty.substr(14, init_ty.size() - 15);
            } else if (init_ty.starts_with("cfire::Result<") && init_ty.ends_with('>')) {
                unwrap_elem_ty = init_ty.substr(14, init_ty.size() - 15);
            } else {
                unwrap_elem_ty = "auto";
            }
        }
        if (ifs.cond) {
            ScopeGuard sg_cond(*this);
            if (ifs.unwrap_init && !ifs.unwrap_var.empty()) {
                bind_scoped_var(ifs.unwrap_var, ifs.unwrap_mut);
                var_types_[ifs.unwrap_var] = unwrap_elem_ty;
            }
            if (auto r = check_expr(ifs.cond); !r) return r;
            if (!ifs.unwrap_init) {
                if (auto r = check_bool_condition(ifs.cond, "'if' condition"); !r) return r;
            }
        }
        auto initial_moves = moved_at_line_;
        auto initial_loans = invalidated_loans_;
        auto initial_null = ptr_null_state_;
        auto nc = ifs.cond ? extract_null_check(ifs.cond) : std::nullopt;

        if (nc) {
            ptr_null_state_[nc->first] = nc->second ? NullState::NonNull : NullState::Null;
        }
        {
            ScopeGuard sg(*this);
            if (ifs.unwrap_init && !ifs.unwrap_var.empty()) {
                bind_scoped_var(ifs.unwrap_var, ifs.unwrap_mut);
                var_types_[ifs.unwrap_var] = unwrap_elem_ty;
            }
            if (auto r = check_block_stmts(ifs.then_body); !r) return r;
        }
        auto then_moves = moved_at_line_;
        auto then_loans = invalidated_loans_;
        auto then_null = ptr_null_state_;

        moved_at_line_ = initial_moves;
        invalidated_loans_ = initial_loans;
        ptr_null_state_ = initial_null;
        if (nc) {
            ptr_null_state_[nc->first] = nc->second ? NullState::Null : NullState::NonNull;
        }
        {
            ScopeGuard sg(*this);
            if (auto r = check_block_stmts(ifs.else_body); !r) return r;
        }
        auto else_moves = moved_at_line_;
        auto else_loans = invalidated_loans_;
        auto else_null = ptr_null_state_;

        bool then_div = block_diverges(ifs.then_body);
        bool else_div = !ifs.else_body.empty() && block_diverges(ifs.else_body);

        if (ifs.else_body.empty()) {
            moved_at_line_ = initial_moves;
            if (then_div) {
                invalidated_loans_ = else_loans;
                ptr_null_state_ = else_null;
            } else {
                invalidated_loans_ = then_loans;
                for (const auto& [k, v] : initial_loans) {
                    invalidated_loans_.try_emplace(k, v);
                }
                std::unordered_map<std::string, NullState> merged_null = initial_null;
                for (const auto& [var, st] : then_null) {
                    if (nc && var == nc->first) {
                        continue;
                    }
                    auto it = initial_null.find(var);
                    if (it == initial_null.end() || it->second == st) {
                        merged_null[var] = st;
                    } else {
                        merged_null[var] = NullState::MaybeNull;
                    }
                }
                ptr_null_state_ = std::move(merged_null);
            }
        } else {
            if (then_div) {
                moved_at_line_ = else_moves;
                invalidated_loans_ = else_loans;
                ptr_null_state_ = else_null;
            } else if (else_div) {
                moved_at_line_ = then_moves;
                invalidated_loans_ = then_loans;
                ptr_null_state_ = then_null;
            } else {
                std::unordered_map<std::string, int> post_moves = initial_moves;
                for (const auto& [var, line] : then_moves) {
                    if (else_moves.contains(var)) {
                        post_moves[var] = std::max(line, else_moves[var]);
                    }
                }
                moved_at_line_ = std::move(post_moves);

                std::unordered_map<std::string, InvalidatedLoan> post_loans = then_loans;
                for (const auto& [var, loan] : else_loans) {
                    post_loans.try_emplace(var, loan);
                }
                invalidated_loans_ = std::move(post_loans);

                std::unordered_map<std::string, NullState> merged_null = initial_null;
                for (const auto& [var, st_then] : then_null) {
                    auto it_else = else_null.find(var);
                    if (it_else != else_null.end() && it_else->second == st_then) {
                        merged_null[var] = st_then;
                    } else {
                        merged_null[var] = NullState::MaybeNull;
                    }
                }
                ptr_null_state_ = std::move(merged_null);
            }
        }
        return {};
    }


    // CF-146: conditions and logical operands must be `bool`; no implicit truthiness for numbers, strings,
    // optionals or pointers. Only reported when the type is known, so inference gaps never produce errors.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_bool_condition(const ExprPtr& e, std::string_view what) -> std::expected<void, std::string> {
        if (!e) return {};
        std::string fix;
        if (expr_returns_optional_sema(e)) {
            fix = "compare explicitly: 'x != none'";
        } else {
            TypeId t = infer_expr_type_id_sema(e);
            if (t.is_unknown() || t == TypeId::Bool()) return {};
            const auto& d = cf_.type_table.get(t);
            if (cf_.type_table.is_integer(t) || cf_.type_table.is_float(t)) fix = "compare explicitly: 'x != 0'";
            else if (t == TypeId::Str()) fix = "compare explicitly: 's.len() > 0'";
            else if (d.kind == TypeKind::RawPtr || d.kind == TypeKind::MutPtr || d.kind == TypeKind::Optional) fix = "compare explicitly: 'p != none'";
            else if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array || d.kind == TypeKind::Map) fix = "compare explicitly: 'xs.len() > 0'";
            else return {};
        }
        return emit_error(diag_at(e->line, e->col, "cfire/NON-BOOL-CONDITION", std::format("{} must be 'bool', got '{}'; {}", what, type_name_of(e).empty() ? "optional" : type_name_of(e), fix)));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_for_stmt(const ForStmt& fs) -> std::expected<void, std::string> {
        if (fs.is_unwrap) {
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            std::string init_ty = type_name_of(fs.iter_expr);
            std::string elem_ty;
            if (init_ty.ends_with('?')) elem_ty = init_ty.substr(0, init_ty.size() - 1);
            else if (init_ty.ends_with('!')) elem_ty = init_ty.substr(0, init_ty.size() - 1);
            else if (init_ty.starts_with("sync.Channel[") && init_ty.ends_with(']')) elem_ty = init_ty.substr(13, init_ty.size() - 14);
            else elem_ty = "auto";

            ++loop_depth_;
            auto loop_guard = cfire::scope_exit([this] { --loop_depth_; });
            ScopeGuard sg(*this);
            bind_scoped_var(fs.item_var, fs.unwrap_mut);
            var_types_[fs.item_var] = elem_ty;
            return check_block_stmts(fs.body);
        }
        if (fs.item_var.empty()) {
            ++in_invalid_unwrap_context_;
            auto guard = cfire::scope_exit([this] { --in_invalid_unwrap_context_; });
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            if (auto r = check_bool_condition(fs.iter_expr, "'for' condition"); !r) return r;
        } else {
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            if (fs.range_end) {
                if (auto r = check_expr(fs.range_end); !r) return r;
            }
        }
        auto pre_loop_moves = moved_at_line_;
        auto pre_loop_loans = invalidated_loans_;
        std::string locked_col;
        if (!fs.item_var.empty() && !fs.range_end) {
            if (auto pi = extract_path_info(fs.iter_expr); pi && pi->path == pi->root) {
                if (!active_loop_collections_.contains(pi->root)) {
                    locked_col = pi->root;
                    active_loop_collections_.insert(locked_col);
                }
            }
        }
        auto unlock_guard = cfire::scope_exit([this, &locked_col] {
            if (!locked_col.empty()) active_loop_collections_.erase(locked_col);
        });
        std::string loop_index = fs.range_end ? fs.item_var : fs.index_var;
        if (!loop_index.empty()) ++active_loop_index_vars_[loop_index];
        auto index_guard = cfire::scope_exit([this, loop_index] {
            if (!loop_index.empty() && --active_loop_index_vars_[loop_index] <= 0) active_loop_index_vars_.erase(loop_index);
        });
        {
            ++loop_depth_;
            auto loop_guard = cfire::scope_exit([this] { --loop_depth_; });
            ScopeGuard sg(*this);
            if (fs.range_end) {
                bind_scoped_var(fs.item_var, false);
                // CF-196: the typed end decides the index type, as in codegen (`0..xs.len()` is `usize`); two
                // literals are `i64`. A literal end must fit the other end's type.
                std::string idx_ty = "i64";
                const bool lit_start = is_numeric_literal_expr(fs.iter_expr);
                const bool lit_end = is_numeric_literal_expr(fs.range_end);
                if (lit_start != lit_end) {
                    const ExprPtr& typed = lit_start ? fs.range_end : fs.iter_expr;
                    const ExprPtr& lit = lit_start ? fs.iter_expr : fs.range_end;
                    TypeId tt = infer_expr_type_id_sema(typed);
                    if (cf_.type_table.is_integer(tt)) {
                        idx_ty = cf_.type_table.to_string(tt);
                        if (auto r = check_int_literal_fits(lit, idx_ty, lit->line, lit->col); !r) return r;
                    }
                } else if (!lit_start) {
                    TypeId a = infer_expr_type_id_sema(fs.iter_expr);
                    TypeId b = infer_expr_type_id_sema(fs.range_end);
                    if (cf_.type_table.is_integer(a) && cf_.type_table.is_integer(b)) {
                        idx_ty = cf_.type_table.to_string(cf_.type_table.int_widens(a, b) ? a : b);
                    }
                }
                var_types_[fs.item_var] = idx_ty;
            } else if (!fs.item_var.empty()) {
                std::string col_ty = type_name_of(fs.iter_expr);
                std::string idx_ty = "usize";
                std::string item_ty = "auto";
                if (fs.is_unwrap) {
                    if (col_ty.ends_with('?')) {
                        item_ty = col_ty.substr(0, col_ty.size() - 1);
                    } else if (col_ty.ends_with('!')) {
                        item_ty = col_ty.substr(0, col_ty.size() - 1);
                    } else if (col_ty.starts_with("std::optional<") && col_ty.ends_with('>')) {
                        item_ty = col_ty.substr(14, col_ty.size() - 15);
                    } else if (col_ty.starts_with("cfire::Result<") && col_ty.ends_with('>')) {
                        item_ty = col_ty.substr(14, col_ty.size() - 15);
                    }
                } else if (col_ty == "str" || col_ty == "string") {
                    idx_ty = "usize";
                    item_ty = "u8";
                } else if (col_ty.starts_with("map[") && col_ty.find(']') != std::string::npos) {
                    auto close_b = col_ty.find(']');
                    idx_ty = col_ty.substr(4, close_b - 4);
                    item_ty = col_ty.substr(close_b + 1);
                } else if (col_ty.starts_with("collections.BTreeMap[") && col_ty.ends_with(']')) {
                    auto inner = col_ty.substr(21, col_ty.size() - 22);
                    auto comma = inner.find(',');
                    if (comma != std::string::npos) {
                        idx_ty = trim_str(inner.substr(0, comma));
                        item_ty = trim_str(inner.substr(comma + 1));
                    }
                } else if (col_ty.starts_with("[]")) {
                    idx_ty = "usize";
                    item_ty = col_ty.substr(2);
                } else if (col_ty.starts_with("collections.Set[") && col_ty.ends_with(']')) {
                    item_ty = col_ty.substr(16, col_ty.size() - 17);
                } else if (col_ty.starts_with("collections.Deque[") && col_ty.ends_with(']')) {
                    idx_ty = "usize";
                    item_ty = col_ty.substr(18, col_ty.size() - 19);
                } else if (col_ty.starts_with("sync.Channel[") && col_ty.ends_with(']')) {
                    item_ty = col_ty.substr(13, col_ty.size() - 14);
                }
                if (!fs.index_var.empty()) {
                    bind_scoped_var(fs.index_var, false);
                    var_types_[fs.index_var] = idx_ty;
                }
                bind_scoped_var(fs.item_var, false);
                var_types_[fs.item_var] = item_ty;
            }
            if (auto r = check_block_stmts(fs.body); !r) return r;
        }

        for (const auto& [var, line] : moved_at_line_) {
            if (!pre_loop_moves.contains(var)) {
                if (auto r = emit_error(diag_at(line, 1, "cfire/USE-AFTER-MOVE", std::format("variable '{}' moved inside loop body without reassignment before next iteration", var))); !r) return r;
            }
        }
        for (const auto& [var, loan] : invalidated_loans_) {
            if (!pre_loop_loans.contains(var) && var_scope_depth_.contains(var) && var_scope_depth_[var] <= scope_depth_) {
                // Loan on outer pointer invalidated inside loop; remains invalidated after loop
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_async_scope_stmt(const AsyncScopeStmt& sc) -> std::expected<void, std::string> {
        if (auto r = check_expr(sc.workers); !r) return r;
        const bool yields = !sc.result_var.empty() || sc.result_return;
        std::vector<const ClosureExpr*> spawned;
        {
            std::unordered_set<std::string> saved_slot_vars = slot_captured_vars_;
            auto slot_vars_guard = cfire::scope_exit([this, saved_slot_vars] { slot_captured_vars_ = saved_slot_vars; });
            ScopeGuard sg(*this);
            if (!sc.scope_var.empty()) {
                bind_scoped_var(sc.scope_var, false);
            }
            if (yields) value_scope_spawns_[sc.scope_var].clear();
            auto spawns_guard = cfire::scope_exit([&] {
                if (!yields) return;
                if (auto it = value_scope_spawns_.find(sc.scope_var); it != value_scope_spawns_.end()) {
                    spawned = std::move(it->second);
                    value_scope_spawns_.erase(it);
                }
            });
            if (auto r = check_block_stmts(sc.body); !r) return r;
        }
        if (!yields) return {};
        // Q51 (CF-189): the scope's value is `[]T`, `T` being what every spawned task returns.
        std::string elem;
        for (const auto* cl : spawned) {
            std::string t;
            if (cl->return_type) t = format_type_ref_sema(*cl->return_type);
            else if (auto it = closure_ret_types_.find(cl); it != closure_ret_types_.end()) t = it->second;
            if (t.empty() || t == "void") {
                return emit_error(diag_at(sc.line, 0, "cfire/SCOPE-RESULT",
                    "every task spawned in a value-producing 'async.scope' must return a value ('scope.spawn || { return v }')"));
            }
            if (elem.empty()) elem = t;
            else if (type_id_of(elem) != type_id_of(t)) {
                return emit_error(diag_at(sc.line, 0, "cfire/SCOPE-RESULT",
                    std::format("tasks spawned in this 'async.scope' return different types ('{}' and '{}'); its value is one '[]T'", elem, t)));
            }
        }
        if (elem.empty()) {
            return emit_error(diag_at(sc.line, 0, "cfire/SCOPE-RESULT",
                std::format("this 'async.scope' produces a value but spawns no task on '{}'", sc.scope_var)));
        }
        sc.result_elem_type = elem;
        const std::string slice_t = "[]" + elem;
        if (sc.result_return) {
            std::string want = fn_ret_str_;
            if (want.ends_with('!')) want.pop_back();
            if (!want.empty() && type_id_of(want) != type_id_of(slice_t)) {
                return emit_error(diag_at(sc.line, 0, "cfire/TYPE-MISMATCH",
                    std::format("this 'async.scope' returns '{}', but the function returns '{}'", slice_t, want)));
            }
            return {};
        }
        bind_scoped_var(sc.result_var, sc.result_mut);
        var_types_[sc.result_var] = slice_t;
        local_defined_vars_.insert(sc.result_var);
        return {};
    }


    // `scope.spawn |mut xs[i]| { xs[i] = v }`: each task owns one element. The index is the spawning loop's own index
    // variable (so tasks own different elements), and inside the task `xs` appears only as `xs[i]`.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_slot_captures(const ClosureExpr& cl, int line) -> std::expected<void, std::string> {
        if (cl.slot_captures.empty()) return {};
        if (!spawn_closures_.contains(&cl)) {
            return emit_error(diag_at(line, 0, "cfire/SLOT-CAPTURE", "only a task passed to 'scope.spawn' can capture a slot ('|mut xs[i]|')"));
        }
        for (const auto& [name, index] : cl.slot_captures) {
            std::string ty;
            if (auto it = var_types_.find(name); it != var_types_.end()) ty = it->second;
            if (!mut_vars_.contains(name) || !(ty.starts_with("[]") || ty.starts_with("["))) {
                return emit_error(diag_at(line, 0, "cfire/SLOT-CAPTURE", std::format("'|mut {}[...]|' needs '{}' to be a 'mut' slice or array", name, name)));
            }
            const auto* iv = index ? std::get_if<IdentExpr>(&index->node) : nullptr;
            if (!iv || !active_loop_index_vars_.contains(iv->name)) {
                return emit_error(diag_at(line, 0, "cfire/SPAWN-SHARED-MUT", std::format(
                    "every task would write the same element of '{}' (data race); index the slot with the spawning loop's own index variable ('for i, x in {}' ... '|mut {}[i]|')", name, name, name)));
            }
            bool other_use = false;
            visit_exprs(cl.body, [&](const ExprPtr& e) {
                if (const auto* ix = std::get_if<IndexExpr>(&e->node)) {
                    const auto* obj = ix->object ? std::get_if<IdentExpr>(&ix->object->node) : nullptr;
                    const auto* idx = ix->index ? std::get_if<IdentExpr>(&ix->index->node) : nullptr;
                    if (obj && obj->name == name && idx && idx->name == iv->name) return false;  // the owned slot
                }
                if (const auto* id = std::get_if<IdentExpr>(&e->node); id && id->name == name) other_use = true;
                return true;
            });
            if (other_use) {
                return emit_error(diag_at(line, 0, "cfire/SLOT-CAPTURE", std::format(
                    "this task owns only '{0}[{1}]'; other tasks write the rest of '{0}' at the same time, so it cannot use '{0}' any other way", name, iv->name)));
            }
            slot_captured_vars_.insert(name);
        }
        return {};
    }
