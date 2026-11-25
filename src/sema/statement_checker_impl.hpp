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
        if (auto it = var_type_ids_.find(name); it != var_type_ids_.end()) {
            st.had_type_id = true;
            st.prev_type_id = it->second;
        }
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
        auto set_arm_var_type = [&](const std::string& name, const std::string& tstr) {
            if (tstr.empty()) return;
            var_types_[name] = tstr;
            TypeId tid = type_id_of(tstr);
            if (tid.is_valid()) var_type_ids_[name] = tid;
        };
        if (cf_.match_catchall_bindings.contains(&arm) && !arm.variant_name.empty()) {
            bind_scoped_var(arm.variant_name, false);
            set_arm_var_type(arm.variant_name, target_t);
        }
        for (const auto& b : arm.bindings) {
            bind_scoped_var(b, false);
        }
        if (!arm.bindings.empty()) {
            if (arm.is_some || arm.variant_name == "Some") {
                if (target_t.ends_with('?')) {
                    set_arm_var_type(arm.bindings[0], target_t.substr(0, target_t.size() - 1));
                }
            } else if (arm.is_ok || arm.variant_name == "Ok") {
                if (target_t.ends_with('!')) {
                    set_arm_var_type(arm.bindings[0], target_t.substr(0, target_t.size() - 1));
                }
            } else if (arm.is_err || arm.variant_name == "Err") {
                set_arm_var_type(arm.bindings[0], "Error");
            } else {
                const EnumDecl* en = !arm.enum_prefix.empty()
                    ? resolve_enum_by_prefix_sema(arm.enum_prefix)
                    : resolve_enum_by_prefix_sema(target_t);
                if (!en && !arm.variant_name.empty()) {
                    if (auto vit = cf_.variant_to_enum.find(arm.variant_name); vit != cf_.variant_to_enum.end()) {
                        if (auto eit = cf_.enums.find(vit->second); eit != cf_.enums.end()) en = &eit->second;
                    }
                }
                if (en) {
                    for (const auto& v : en->variants) {
                        if (v.name == arm.variant_name) {
                            for (std::size_t k = 0; k < arm.bindings.size() && k < v.payload_types.size(); ++k) {
                                TypeId p_tid = resolve_typeref(v.payload_types[k]);
                                if (p_tid.is_valid()) var_type_ids_[arm.bindings[k]] = p_tid;
                                var_types_[arm.bindings[k]] = format_type_ref_sema(v.payload_types[k]);
                            }
                            break;
                        }
                    }
                }
            }
        }
        if (arm.is_slice_pattern) {
            std::string elem_t;
            std::string rest_t;
            if (target_t.starts_with("[]")) {
                elem_t = target_t.substr(2);
                rest_t = target_t;
            } else if (target_t.starts_with('[')) {
                if (auto rb = target_t.find(']'); rb != std::string::npos) {
                    elem_t = target_t.substr(rb + 1);
                    rest_t = "[]" + elem_t;
                }
            } else if (target_t == "str") {
                elem_t = "u8";
                rest_t = "&str";
            }
            for (const auto& h : arm.slice_head) {
                bind_scoped_var(h, false);
                set_arm_var_type(h, elem_t);
            }
            if (!arm.slice_rest.empty()) {
                bind_scoped_var(arm.slice_rest, false);
                set_arm_var_type(arm.slice_rest, rest_t);
            }
            for (const auto& t : arm.slice_tail) {
                bind_scoped_var(t, false);
                set_arm_var_type(t, elem_t);
            }
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
            cf_.has_cpp_interop = true;
            // Foreign C++ blocks can arbitrarily mutate pointees and alias memory.
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
        TypeId decl_tid = TypeId::Unknown();
        if (vd.type) {
            if (auto r = check_type_ref_separators(*vd.type, vd.line, vd.col, true); !r) return r;
            decl_tid = resolve_typeref(*vd.type);
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
        const std::size_t errors_before_init = errors_.size();
        // Check initializer expression before binding the new variable so `x := x + 1` reads outer `x` first
        if (auto r = check_expr(vd.init, decl_tid); !r) return r;
        const bool init_reported = errors_.size() != errors_before_init;
        if (vd.init && vd.name != "_" && (!vd.type || (!vd.type->is_borrow && !vd.type->is_mut_borrow))) {
            if (auto r = check_move_only_copy(vd.init, std::format("variable '{}'", vd.name), vd.line, vd.col); !r) return r;
        }

        bool is_byte_slice = (vd.type && (vd.type->is_slice || vd.type->is_array) &&
                              (vd.type->name == "byte" || (vd.type->elem_type && vd.type->elem_type->name == "byte"))) ||
                             (!vd.type && is_byte_slice_expr(vd.init));
        bool byte_slice_reported = false;
        if (is_byte_slice && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                for (const auto& el : arr_lit->elements) {
                    if (is_non_byte_int_or_u8_expr(el)) {
                        byte_slice_reported = true;
                        if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("a '[]byte' literal element must be 0..=255 or a 'byte'; convert wider integers with 'byte(...)'"))); !r) return r;
                    }
                }
            }
        }

        // Type compatibility check for declared type vs initializer (skipped when the declared type itself
        // was just reported, so a bad stdlib type name is one error, not two).
        if (vd.type && vd.init && !decl_type_reported && !byte_slice_reported) {
            TypeId src_t = infer_expr_type_id_sema(vd.init);
            if (!(is_numeric_literal_expr(vd.init) && vd.type->is_optional)) {
                if (auto r = check_type_compatible(decl_tid, src_t, vd.init, std::format("variable '{}' of type '{}'", vd.name, cf_.type_table.to_string(decl_tid)), vd.line, vd.col); !r) {
                    return r;
                }
            } else if (decl_tid.is_valid()) {
                bool h = false;
                if (auto r = check_value_for(decl_tid, vd.init, h); !r) return r;
            }
        }
        if (!vd.type && vd.name != "_" && vd.init) {
            TypeId src_t = infer_expr_type_id_sema(vd.init);
            if ((src_t.is_unknown() || cf_.type_table.contains_unknown(src_t)) &&
                !src_t.is_foreign() && !cf_.type_table.contains_foreign(src_t)) {
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
                } else if (!init_reported && !src_t.is_error() && !cf_.type_table.contains_error(src_t)) {
                    if (auto r = emit_error(diag_at(vd.line, vd.col, "cfire/UNKNOWN-TYPE",
                        std::format("could not infer concrete type for variable '{}'; add an explicit type annotation", vd.name))); !r) {
                        return r;
                    }
                }
            }
        }

        const bool explicit_owned_dest = vd.type && !vd.type->is_borrow && !vd.type->is_mut_borrow &&
            !vd.type->is_ptr && !vd.type->is_mut_ptr && !decl_tid.is_foreign();
        if (explicit_owned_dest) {
            if (decl_tid == TypeId::Str()) cf_.owned_text_decls.insert(&vd);
            if (cf_.type_table.get(decl_tid).kind == TypeKind::Slice &&
                vd.init && !std::holds_alternative<ArrayLitExpr>(vd.init->node)) {
                cf_.owned_slice_decls.insert(&vd);
            }
        }
        const bool is_owned_var_decl = explicit_owned_dest || (vd.is_mut && !vd.type);

        if (!is_owned_var_decl) {
            if (auto r = check_view_of_temporary(vd.init, std::format("'{}'", vd.name), vd.line, vd.col); !r) return r;
        }
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
        std::optional<std::string> src;
        if (!is_owned_var_decl) {
            src = extract_borrow_source(vd.init);
            if (!src) {
                if (auto owners = extract_borrow_sources(vd.init); !owners.empty()) src = owners.front();
            }
            record_borrow_origins(vd.name, vd.init);
        } else {
            extra_borrow_origins_.erase(vd.name);
        }
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
        TypeId bound_tid = decl_tid.is_valid() && !decl_tid.is_unknown() ? decl_tid : infer_expr_type_id_sema(vd.init);
        if (!vd.type && vd.is_mut && bound_tid.is_valid()) {
            const auto& bd = cf_.type_table.get(bound_tid);
            if ((bd.kind == TypeKind::Borrow || bd.kind == TypeKind::MutBorrow) && bd.elem_type == TypeId::Str()) {
                bound_tid = TypeId::Str();
                inferred_t = "str";
            }
        }
        if (bound_tid.is_foreign() || cf_.type_table.contains_foreign(bound_tid)) {
            foreign_vars_.insert(vd.name);
        }
        vd.resolved_type_id = bound_tid.index;
        bool is_opt_var = (vd.type && vd.type->is_optional) || cf_.type_table.is_optional(bound_tid) || (!vd.type && expr_returns_optional_sema(vd.init));
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
                        is_smart = true;  // `own[T].from_raw(p)`
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
            // remember which smart pointers are `shared[T]` (no `release`).
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
                if (auto n = parse_int_literal<std::size_t>(vd.type->array_len)) {
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
        if (bound_tid.is_valid() && !bound_tid.is_unknown()) {
            var_type_ids_[vd.name] = bound_tid;
        } else {
            var_type_ids_.erase(vd.name);
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
        TypeId init_t = infer_expr_type_id_sema(dvs.init);
        std::vector<TypeId> elem_tids;
        std::string elem_type;
        if (init_t.is_valid() && !init_t.is_unknown() && !init_t.is_error() && !init_t.is_foreign()) {
            const auto& idesc = cf_.type_table.get(init_t);
            if (idesc.kind == TypeKind::Optional) {
                return emit_error(diag_at(dvs.line, dvs.col, "cfire/UNWRAPPED-OPTIONAL",
                    std::format("cannot destructure optional value of type '{}' without unwrapping with 'or'", cf_.type_table.to_string(init_t))));
            }
            if (idesc.kind == TypeKind::Fallible) {
                return emit_error(diag_at(dvs.line, dvs.col, "cfire/UNHANDLED-RESULT",
                    std::format("cannot destructure fallible value of type '{}' without unwrapping with 'try' or 'or'", cf_.type_table.to_string(init_t))));
            }
            if (idesc.kind == TypeKind::Tuple) {
                if (idesc.param_types.size() != dvs.bindings.size()) {
                    return emit_error(diag_at(dvs.line, dvs.col, "cfire/TYPE-MISMATCH",
                        std::format("tuple destructuring binds {} variables, but expression has type '{}' with {} elements",
                            dvs.bindings.size(), cf_.type_table.to_string(init_t), idesc.param_types.size())));
                }
                elem_tids = idesc.param_types;
            } else {
                return emit_error(diag_at(dvs.line, dvs.col, "cfire/TYPE-MISMATCH",
                    std::format("cannot destructure non-tuple value of type '{}'", cf_.type_table.to_string(init_t))));
            }
        }
        if (elem_tids.empty() && dvs.init) {
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
        dvs.resolved_binding_types.clear();
        dvs.resolved_binding_types.reserve(dvs.bindings.size());
        for (std::size_t i = 0; i < dvs.bindings.size(); ++i) {
            const auto& [name, is_mut] = dvs.bindings[i];
            bind_scoped_var(name, is_mut);
            TypeId bt = i < elem_tids.size() ? elem_tids[i] : (!elem_type.empty() ? type_id_of(elem_type) : TypeId::Unknown());
            dvs.resolved_binding_types.push_back(bt.index);
            if (bt.is_valid() && !bt.is_unknown()) {
                var_type_ids_[name] = bt;
                var_types_[name] = cf_.type_table.to_string(bt);
            } else if (!elem_type.empty()) {
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


    // (G1): inside a `scope.spawn` closure, writing through a captured outer pointer (`*p = v`,
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
        // shared[T] shares ownership, not access: its pointee is unsynchronized.
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
        TypeId expected_rhs = (as.target && as.op == "=") ? infer_expr_type_id_sema(as.target) : TypeId::Unknown();
        if (auto r = check_expr(as.value, expected_rhs); !r) return r;
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
                const bool target_is_view_or_ptr =
                    borrow_origin_.contains(id->name) ||
                    extra_borrow_origins_.contains(id->name) ||
                    raw_or_smart_ptr_vars_.contains(id->name);
                if (target_is_view_or_ptr) {
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
                } else {
                    borrow_origin_.erase(id->name);
                    extra_borrow_origins_.erase(id->name);
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
        // Type & operator compatibility check for assignment and compound assignments.
        if (as.target && as.value) {
            TypeId dest_t = infer_expr_type_id_sema(as.target);
            TypeId src_t = infer_expr_type_id_sema(as.value);
            if (as.op != "=") {
                if (!dest_t.is_unknown() && !dest_t.is_error() && !dest_t.is_foreign() &&
                    !src_t.is_unknown() && !src_t.is_error() && !src_t.is_foreign()) {
                    if (cf_.type_table.is_optional(dest_t) || cf_.type_table.is_optional(src_t)) {
                        return emit_error(diag_at(as.line, 1, "cfire/UNWRAPPED-OPTIONAL",
                            std::format("compound assignment '{}' cannot operate on optional values; unwrap with 'or' first", as.op)));
                    }
                    if (cf_.type_table.is_fallible(src_t)) {
                        return emit_error(diag_at(as.line, 1, "cfire/UNHANDLED-RESULT",
                            std::format("compound assignment '{}' cannot use unhandled fallible value '{}'", as.op, cf_.type_table.to_string(src_t))));
                    }
                    const TypeId str_view = type_id_of("&str");
                    const bool dest_str = dest_t == TypeId::Str();
                    const bool src_str = src_t == TypeId::Str() || (str_view.is_valid() && src_t == str_view);
                    if (as.op == "+=") {
                        if (dest_str) {
                            if (!src_str) {
                                return emit_error(diag_at(as.line, 1, "cfire/TYPE-MISMATCH",
                                    std::format("cannot append '{}' to 'str' with '+='", cf_.type_table.to_string(src_t))));
                            }
                        } else if (!cf_.type_table.is_numeric_or_constrained(dest_t) ||
                                   !cf_.type_table.is_numeric_or_constrained(src_t)) {
                            return emit_error(diag_at(as.line, 1, "cfire/TYPE-MISMATCH",
                                std::format("compound assignment '+=' cannot be applied to '{}' and '{}'",
                                    cf_.type_table.to_string(dest_t), cf_.type_table.to_string(src_t))));
                        }
                    } else if (as.op == "-=" || as.op == "*=" || as.op == "/=") {
                        if (!cf_.type_table.is_numeric_or_constrained(dest_t) ||
                            !cf_.type_table.is_numeric_or_constrained(src_t)) {
                            return emit_error(diag_at(as.line, 1, "cfire/TYPE-MISMATCH",
                                std::format("compound assignment '{}' cannot be applied to '{}' and '{}'",
                                    as.op, cf_.type_table.to_string(dest_t), cf_.type_table.to_string(src_t))));
                        }
                    } else if (as.op == "%=" || as.op == "&=" || as.op == "|=" ||
                               as.op == "^=" || as.op == "<<=" || as.op == ">>=") {
                        const bool float_mod_assign = (as.op == "%=") &&
                            cf_.type_table.is_float(dest_t) &&
                            (cf_.type_table.is_float(src_t) || is_numeric_literal_expr(as.value));
                        const bool enum_flags = (as.op == "|=" || as.op == "&=") &&
                            cf_.type_table.get(dest_t).kind == TypeKind::NominalEnum &&
                            (dest_t == src_t || cf_.type_table.is_integer(src_t));
                        if (!float_mod_assign && !enum_flags &&
                            (!cf_.type_table.is_integer_or_constrained(dest_t) ||
                             !cf_.type_table.is_integer_or_constrained(src_t))) {
                            return emit_error(diag_at(as.line, 1, "cfire/TYPE-MISMATCH",
                                std::format("compound assignment '{}' requires integer operands, got '{}' and '{}'",
                                    as.op, cf_.type_table.to_string(dest_t), cf_.type_table.to_string(src_t))));
                        }
                    }
                }
                if (as.op != "<<=" && as.op != ">>=" && cf_.type_table.is_integer(dest_t) && !const_int_value(as.value)) {
                    bool handled = false;
                    if (auto r = check_value_for(dest_t, as.value, handled); !r) return r;
                }
            } else {
                if (auto r = check_type_compatible(dest_t, src_t, as.value, "assignment target", as.line, 1); !r) return r;
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
        TypeId expected_ret = TypeId::Unknown();
        if (fn_ret_tid_.is_valid() && !fn_ret_tid_.is_void() && !fn_ret_tid_.is_unknown() && in_closure_depth_ == 0) {
            expected_ret = fn_ret_tid_;
            if (fn_is_fallible_ && cf_.type_table.is_fallible(expected_ret)) {
                expected_ret = cf_.type_table.unwrap_fallible(expected_ret);
            }
        }
        if (auto r = check_expr(rt.value, expected_ret); !r) return r;
        const bool returning_error = rt.value && type_name_of(rt.value) == "Error";
        if (rt.value && !returning_error && !closure_ret_stack_.empty() && closure_ret_stack_.back().second == in_closure_depth_) {
            if (TypeId t = infer_expr_type_id_sema(rt.value); t.is_valid() && !t.is_unknown()) {
                closure_ret_types_.try_emplace(closure_ret_stack_.back().first, cf_.type_table.to_string(t));
            }
        }
        if (rt.value) {
            bool returns_borrow_or_ptr = false;
            if (fn_ret_tid_.is_valid() && !fn_ret_tid_.is_unknown() && in_closure_depth_ == 0) {
                TypeId want_t = fn_is_fallible_ ? cf_.type_table.unwrap_fallible(fn_ret_tid_) : fn_ret_tid_;
                const TypeDesc& wd = cf_.type_table.get(want_t);
                returns_borrow_or_ptr = (wd.kind == TypeKind::Borrow || wd.kind == TypeKind::MutBorrow ||
                                         wd.kind == TypeKind::RawPtr || wd.kind == TypeKind::MutPtr ||
                                         want_t.is_foreign());
            }
            if (returns_borrow_or_ptr || !fn_ret_tid_.is_valid() || fn_ret_tid_.is_unknown()) {
                bool is_direct = false;
                if (auto r = check_view_of_temporary(rt.value, "returned value", rt.line, 1); !r) return r;
                if (!extract_borrow_source(rt.value)) {
                    // a call returning a view of a local (e.g. `return head(local)`) dangles too.
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
        }
        if (returning_error && in_closure_depth_ == 0) {
            if (!fn_is_fallible_ && fn_name_ != "main" && !fn_name_.starts_with("__cfire_test_")) {
                return emit_error(diag_at(rt.line, 0, "cfire/TYPE-MISMATCH", std::format("cannot return 'Error' from non-fallible function '{}'", fn_name_)));
            }
            return {};
        }
        // Type compatibility check for return statement
        if (rt.value && fn_ret_tid_.is_valid() && !fn_ret_tid_.is_void() && !fn_ret_tid_.is_unknown() && in_closure_depth_ == 0) {
            TypeId ret_t = fn_ret_tid_;
            TypeId want_t = fn_is_fallible_ ? cf_.type_table.unwrap_fallible(ret_t) : ret_t;
            TypeId val_t = infer_expr_type_id_sema(rt.value);
            if (want_t.is_void() && !val_t.is_unknown() && !val_t.is_void() && !val_t.is_never() && !val_t.is_error()) {
                if (!fn_is_fallible_ || (type_name_of(rt.value) != "Error" && !cf_.type_table.is_fallible(val_t))) {
                    return emit_error(diag_at(rt.line, 0, "cfire/TYPE-MISMATCH",
                        std::format("return type mismatch: function '{}' returns '{}', got '{}'", fn_name_, cf_.type_table.to_string(ret_t), cf_.type_table.to_string(val_t))));
                }
            }
            if (auto r = check_type_compatible(want_t, val_t, rt.value, std::format("return value of function '{}'", fn_name_), rt.line, rt.value->col); !r) {
                return r;
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_if_stmt(const IfStmt& ifs) -> std::expected<void, std::string> {
        std::string unwrap_elem_ty;
        TypeId unwrap_elem_tid = TypeId::Unknown();
        if (ifs.unwrap_init) {
            unwrapped_exprs_.insert(ifs.unwrap_init);
            if (auto r = check_expr(ifs.unwrap_init); !r) return r;
            TypeId init_tid = infer_expr_type_id_sema(ifs.unwrap_init);
            if (init_tid.is_valid() && !init_tid.is_unknown() && !init_tid.is_error() && !init_tid.is_foreign()) {
                const auto& d = cf_.type_table.get(init_tid);
                if (d.kind == TypeKind::Optional) {
                    unwrap_elem_tid = d.elem_type;
                } else if (d.kind == TypeKind::Fallible) {
                    unwrap_elem_tid = d.elem_type;
                } else {
                    if (auto r = emit_error(diag_at(ifs.unwrap_init->line, ifs.unwrap_init->col, "cfire/TYPE-MISMATCH",
                        std::format("conditional unwrap 'if {} :=' requires an optional value, got '{}'",
                            ifs.unwrap_var, cf_.type_table.to_string(init_tid)))); !r) return r;
                }
            }
            if (unwrap_elem_tid.is_valid() && !unwrap_elem_tid.is_unknown()) {
                unwrap_elem_ty = cf_.type_table.to_string(unwrap_elem_tid);
                ifs.resolved_unwrap_type_id = unwrap_elem_tid.index;
            } else {
                unwrap_elem_ty = "auto";
            }
        }
        if (ifs.cond) {
            ScopeGuard sg_cond(*this);
            if (ifs.unwrap_init && !ifs.unwrap_var.empty()) {
                bind_scoped_var(ifs.unwrap_var, ifs.unwrap_mut);
                var_types_[ifs.unwrap_var] = unwrap_elem_ty;
                if (unwrap_elem_tid.is_valid()) var_type_ids_[ifs.unwrap_var] = unwrap_elem_tid;
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
                if (unwrap_elem_tid.is_valid()) var_type_ids_[ifs.unwrap_var] = unwrap_elem_tid;
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


    // conditions and logical operands must be `bool`; no implicit truthiness for numbers, strings,
    // optionals or pointers. Only reported when the type is known, so inference gaps never produce errors.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_bool_condition(const ExprPtr& e, std::string_view what) -> std::expected<void, std::string> {
        if (!e) return {};
        TypeId t = infer_expr_type_id_sema(e);
        if (t == TypeId::Bool() || t.is_never() || t.is_error() || t.is_foreign() ||
            cf_.type_table.contains_foreign(t) || cf_.type_table.contains_error(t)) {
            return {};
        }
        std::string fix;
        if (t.is_unknown()) {
            if (!expr_returns_optional_sema(e)) return {};
            fix = "compare explicitly: 'x != none'";
        } else {
            const auto& d = cf_.type_table.get(t);
            if (cf_.type_table.is_integer_or_constrained(t) || cf_.type_table.is_float(t) || cf_.type_table.is_numeric_or_constrained(t)) {
                fix = "compare explicitly: 'x != 0'";
            } else if (t == TypeId::Str() || (d.kind == TypeKind::Borrow && d.elem_type == TypeId::Str())) {
                fix = "compare explicitly: 's.len() > 0'";
            } else if (d.kind == TypeKind::RawPtr || d.kind == TypeKind::MutPtr || d.kind == TypeKind::Optional) {
                fix = "compare explicitly: 'p != none'";
            } else if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array || d.kind == TypeKind::Map) {
                fix = "compare explicitly: 'xs.len() > 0'";
            } else {
                fix = "use a boolean expression";
            }
        }
        std::string tname = type_name_of(e);
        return emit_error(diag_at(e->line, e->col, "cfire/NON-BOOL-CONDITION",
            std::format("{} must be 'bool', got '{}'; {}", what, tname.empty() ? "optional" : tname, fix)));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_for_stmt(const ForStmt& fs) -> std::expected<void, std::string> {
        if (fs.is_unwrap) {
            if (fs.iter_expr) unwrapped_exprs_.insert(fs.iter_expr);
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            TypeId init_t = infer_expr_type_id_sema(fs.iter_expr);
            TypeId elem_tid = TypeId::Unknown();
            if (init_t.is_valid() && !init_t.is_unknown() && !init_t.is_error() && !init_t.is_foreign()) {
                const auto& d = cf_.type_table.get(init_t);
                if (d.kind == TypeKind::Optional || d.kind == TypeKind::Fallible) {
                    elem_tid = d.elem_type;
                } else if (d.kind == TypeKind::GenericInst &&
                           (d.base_name == "sync.Channel" || d.base_name == "Channel") &&
                           !d.type_args.empty()) {
                    elem_tid = d.type_args[0];
                } else {
                    if (auto r = emit_error(diag_at(fs.iter_expr->line, fs.iter_expr->col, "cfire/TYPE-MISMATCH",
                        std::format("unwrap loop 'for {} :=' requires an optional value or channel, got '{}'",
                            fs.item_var, cf_.type_table.to_string(init_t)))); !r) return r;
                }
            }
            ++loop_depth_;
            auto loop_guard = cfire::scope_exit([this] { --loop_depth_; });
            ScopeGuard sg(*this);
            bind_scoped_var(fs.item_var, fs.unwrap_mut);
            if (elem_tid.is_valid() && !elem_tid.is_unknown()) {
                fs.resolved_unwrap_type_id = elem_tid.index;
                var_type_ids_[fs.item_var] = elem_tid;
                var_types_[fs.item_var] = cf_.type_table.to_string(elem_tid);
            } else {
                var_types_[fs.item_var] = "auto";
            }
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
                TypeId a = infer_expr_type_id_sema(fs.iter_expr);
                TypeId b = infer_expr_type_id_sema(fs.range_end);
                auto validate_bound = [&](const ExprPtr& bound_e, TypeId bt, std::string_view side) -> std::expected<bool, std::string> {
                    if (!bound_e || bt.is_unknown() || bt.is_error() || bt.is_foreign() || bt.is_never()) return true;
                    if (cf_.type_table.is_optional(bt)) {
                        TypeId inner = cf_.type_table.unwrap_optional(bt);
                        if (cf_.type_table.is_integer_or_constrained(inner)) {
                            if (auto r = emit_error(diag_at(bound_e->line, bound_e->col, "cfire/UNWRAPPED-OPTIONAL",
                                std::format("range {} bound has optional type '{}' and must be unwrapped before looping; use 'or' (e.g. 'val or 0')",
                                    side, cf_.type_table.to_string(bt)))); !r) return std::unexpected(r.error());
                            return false;
                        }
                    }
                    if (cf_.type_table.is_fallible(bt)) {
                        if (auto r = emit_error(diag_at(bound_e->line, bound_e->col, "cfire/UNHANDLED-RESULT",
                            std::format("range {} bound has fallible type '{}' and must be handled with 'try' or 'or'",
                                side, cf_.type_table.to_string(bt)))); !r) return std::unexpected(r.error());
                        return false;
                    }
                    if (!cf_.type_table.is_integer_or_constrained(bt)) {
                        if (auto r = emit_error(diag_at(bound_e->line, bound_e->col, "cfire/TYPE-MISMATCH",
                            std::format("range {} bound must be an integer type, got '{}'",
                                side, cf_.type_table.to_string(bt)))); !r) return std::unexpected(r.error());
                        return false;
                    }
                    return true;
                };
                auto a_ok = validate_bound(fs.iter_expr, a, "start");
                if (!a_ok) return std::unexpected(a_ok.error());
                auto b_ok = validate_bound(fs.range_end, b, "end");
                if (!b_ok) return std::unexpected(b_ok.error());

                TypeId idx_tid = TypeId::Int64();
                const bool lit_start = is_numeric_literal_expr(fs.iter_expr);
                const bool lit_end = is_numeric_literal_expr(fs.range_end);
                if (*a_ok && *b_ok) {
                    if (lit_start != lit_end) {
                        const ExprPtr& typed = lit_start ? fs.range_end : fs.iter_expr;
                        const ExprPtr& lit = lit_start ? fs.iter_expr : fs.range_end;
                        TypeId tt = infer_expr_type_id_sema(typed);
                        if (cf_.type_table.is_integer(tt)) {
                            idx_tid = tt;
                            if (auto r = check_int_literal_fits(lit, cf_.type_table.to_string(tt), lit->line, lit->col); !r) return r;
                        } else if (cf_.type_table.has_constraint(tt, "Integer")) {
                            idx_tid = tt;
                        }
                    } else if (!lit_start) {
                        if (cf_.type_table.is_integer(a) && cf_.type_table.is_integer(b)) {
                            if (!cf_.type_table.int_widens(a, b) && !cf_.type_table.int_widens(b, a)) {
                                if (auto r = emit_error(diag_at(fs.line, 1, "cfire/TYPE-MISMATCH",
                                    std::format("incompatible range bound types '{}' and '{}'",
                                        cf_.type_table.to_string(a), cf_.type_table.to_string(b)))); !r) return r;
                            } else {
                                idx_tid = cf_.type_table.int_widens(a, b) ? a : b;
                            }
                        } else if (a.is_valid() && b.is_valid() &&
                                   cf_.type_table.is_integer_or_constrained(a) &&
                                   cf_.type_table.is_integer_or_constrained(b)) {
                            idx_tid = cf_.type_table.has_constraint(a, "Integer") ? a : b;
                        }
                    }
                }
                fs.resolved_index_type_id = idx_tid.index;
                var_type_ids_[fs.item_var] = idx_tid;
                var_types_[fs.item_var] = cf_.type_table.to_string(idx_tid);
            } else if (!fs.item_var.empty()) {
                TypeId raw_ct = infer_expr_type_id_sema(fs.iter_expr);
                TypeId idx_tid = TypeId::Usize();
                TypeId item_tid = TypeId::Unknown();
                if (raw_ct.is_valid() && !raw_ct.is_unknown() && !raw_ct.is_error() && !raw_ct.is_foreign()) {
                    if (cf_.type_table.is_optional(raw_ct)) {
                        if (auto r = emit_error(diag_at(fs.iter_expr->line, fs.iter_expr->col, "cfire/UNWRAPPED-OPTIONAL",
                            std::format("cannot iterate over optional value '{}' without unwrapping; use 'or' or 'if v :='",
                                cf_.type_table.to_string(raw_ct)))); !r) return r;
                    } else if (cf_.type_table.is_fallible(raw_ct)) {
                        if (auto r = emit_error(diag_at(fs.iter_expr->line, fs.iter_expr->col, "cfire/UNHANDLED-RESULT",
                            std::format("cannot iterate over fallible result '{}' without unwrapping with 'try' or 'or'",
                                cf_.type_table.to_string(raw_ct)))); !r) return r;
                    } else {
                        TypeId ct = unwrap_receiver_type(raw_ct, false);
                        const auto& cd = cf_.type_table.get(ct);
                        if (ct == TypeId::Str()) {
                            idx_tid = TypeId::Usize();
                            item_tid = TypeId::Byte();
                        } else if (cd.kind == TypeKind::Slice || cd.kind == TypeKind::Array) {
                            idx_tid = TypeId::Usize();
                            item_tid = cd.elem_type;
                        } else if (cd.kind == TypeKind::Map) {
                            idx_tid = cd.key_type;
                            item_tid = cd.elem_type;
                        } else if (cd.kind == TypeKind::GenericInst) {
                            if (cd.base_name.ends_with("BTreeMap") && cd.type_args.size() == 2) {
                                idx_tid = cd.type_args[0];
                                item_tid = cd.type_args[1];
                            } else if ((cd.base_name.ends_with("Set") || cd.base_name.ends_with("Deque") ||
                                        cd.base_name.ends_with("Channel") || cd.base_name.ends_with("Vector")) &&
                                       !cd.type_args.empty()) {
                                idx_tid = TypeId::Usize();
                                item_tid = cd.type_args[0];
                            } else if (cd.base_name.ends_with("BitSet")) {
                                idx_tid = TypeId::Usize();
                                item_tid = TypeId::Usize();
                            } else {
                                if (auto r = emit_error(diag_at(fs.iter_expr->line, fs.iter_expr->col, "cfire/TYPE-MISMATCH",
                                    std::format("cannot iterate over value of type '{}'", cf_.type_table.to_string(raw_ct)))); !r) return r;
                            }
                        } else if (!ct.is_unknown() && !ct.is_foreign() && !ct.is_error()) {
                            if (auto r = emit_error(diag_at(fs.iter_expr->line, fs.iter_expr->col, "cfire/TYPE-MISMATCH",
                                std::format("cannot iterate over non-iterable type '{}'", cf_.type_table.to_string(raw_ct)))); !r) return r;
                        }
                    }
                }
                if (!fs.index_var.empty()) {
                    bind_scoped_var(fs.index_var, false);
                    fs.resolved_index_type_id = idx_tid.index;
                    var_type_ids_[fs.index_var] = idx_tid;
                    var_types_[fs.index_var] = cf_.type_table.to_string(idx_tid);
                }
                bind_scoped_var(fs.item_var, false);
                if (item_tid.is_valid() && !item_tid.is_unknown()) {
                    fs.resolved_item_type_id = item_tid.index;
                    var_type_ids_[fs.item_var] = item_tid;
                    var_types_[fs.item_var] = cf_.type_table.to_string(item_tid);
                } else {
                    var_types_[fs.item_var] = "auto";
                }
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
        // the scope's value is `[]T`, `T` being what every spawned task returns.
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
        TypeId elem_tid = type_id_of(elem);
        if (elem_tid.is_valid() && !elem_tid.is_unknown()) {
            sc.resolved_result_elem_type_id = elem_tid.index;
        }
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
        if (elem_tid.is_valid() && !elem_tid.is_unknown()) {
            var_type_ids_[sc.result_var] = cf_.type_table.make_slice(elem_tid);
        }
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
