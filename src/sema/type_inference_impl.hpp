// Generated shard for SemaFnChecker (infer)

// why `<ns>.<name>` in a type position is not a stdlib type, or "" when it is one. `ns` is the module's
// canonical namespace (`http`, `sqlite`); `in_build_manifest` is true in a `build.cf` file, the only place where
// the `build.*` types exist. Without this check clang rejected the generated C++ (cfire/TYPE-ERROR).
[[nodiscard]] CFIRE_SEMA_INLINE auto stdlib_type_name_problem(std::string_view ns, std::string_view name, bool in_build_manifest) -> std::string {
    if (auto br = name.find('['); br != std::string_view::npos) name = name.substr(0, br);
    if (name.empty() || name.contains('.') || name.contains(':')) return "";
    const auto& idx = StdlibCatalogIndex::instance();
    const std::string import_path = idx.import_path_for(ns);
    if (import_path.empty()) return "";
    if (ns == "build" && !in_build_manifest) {
        return std::format("'build.{}' exists only in a build.cf manifest; a program cannot use the build module's types", name);
    }
    if (!idx.find_ns_const_type(ns, name).empty()) {
        return std::format("'{}.{}' is a constant in '{}', not a type", ns, name, import_path);
    }
    auto it = idx.ns_types_and_consts.find(std::string(ns));
    if (it != idx.ns_types_and_consts.end() && it->second.contains(std::string(name))) return "";
    if (find_stdlib_struct(ns, name) != nullptr) return "";
    if (const auto* removed = find_removed_stdlib_type(ns, name); removed && !removed->use.empty()) {
        return std::format("'{}.{}' is not a type in '{}'; use {}", ns, name, import_path, removed->use);
    }
    std::vector<std::string> known;
    if (it != idx.ns_types_and_consts.end()) {
        for (const auto& t : it->second) {
            if (!t.empty() && std::isupper(static_cast<unsigned char>(t[0])) && !std::ranges::all_of(t, [](char c) { return std::isupper(static_cast<unsigned char>(c)) || c == '_'; }))
                known.push_back(t);
        }
    }
    std::ranges::sort(known);
    std::string list;
    for (const auto& k : known) list += (list.empty() ? "" : ", ") + std::format("{}.{}", ns, k);
    return std::format("'{}.{}' is not a type in '{}'{}", ns, name, import_path,
                       list.empty() ? std::string("; the module exports no types") : std::format("; its types are {}", list));
}


// why a bare type name (no module qualifier) does not resolve, or "" when it does. It resolves to a
// builtin, a struct / enum / interface of the package, or a type parameter in scope.
[[nodiscard]] CFIRE_SEMA_INLINE auto bare_type_name_problem(std::string_view name, const std::unordered_set<std::string>& declared,
                                                 const std::unordered_set<std::string>& type_params) -> std::string {
    static const std::unordered_set<std::string_view> kBuiltin = {
        "void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "usize", "int", "uint",
        "f32", "f64", "str", "byte", "path", "char", "c_char", "own", "shared", "Self",
        // Bare stdlib names codegen has always accepted (`lower_owning_type`); qualified spellings are preferred.
        "Request", "Response", "Router", "HttpServer", "SseEvent", "Conn", "WebSocket", "Context", "Url",
        "ServerContext", "CertKeyPair", "TcpConn", "TcpListener", "File", "App", "Channel"};
    if (name.empty() || name.contains('.') || name.contains(':') || name.contains('[') || name.contains('(') ||
        kBuiltin.contains(name) || declared.contains(std::string(name)) || type_params.contains(std::string(name))) {
        return "";
    }
    std::string best;
    std::size_t best_d = 3;
    auto lower = [](std::string_view v) {
        std::string out(v);
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::string lname = lower(name);
    auto consider = [&](std::string_view cand) {
        const std::size_t d = StdlibCatalogIndex::edit_distance(lname, lower(cand));
        if (d < best_d) { best_d = d; best = std::string(cand); }
    };
    for (auto b : kBuiltin) consider(b);
    for (const auto& d : declared) consider(d);
    for (const auto& t : type_params) consider(t);
    if (!best.empty()) return std::format("unknown type '{}'; did you mean '{}'?", name, best);
    // A stdlib type written without its module (`Request` for `http.Request`).
    std::vector<std::string> mods;
    for (const auto& [ns, names] : StdlibCatalogIndex::instance().ns_types_and_consts) {
        if (names.contains(std::string(name))) mods.push_back(std::format("{}.{}", ns, name));
    }
    std::ranges::sort(mods);
    if (!mods.empty()) {
        std::string list;
        for (const auto& m : mods) list += (list.empty() ? "'" : "', '") + m;
        return std::format("unknown type '{}'; stdlib types are written with their module: {}'", name, list);
    }
    return std::format("unknown type '{}': not a builtin type, a type declared in this package, or a type parameter", name);
}


    CFIRE_SEMA_INLINE SemaFnChecker::SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf,
                  std::vector<std::string>& warnings, std::vector<std::string>& errors,
                  std::vector<cfire::diag::StructuredDiagnostic>& diagnostics, bool resilient)
        : path_(path), pkg_name_(fn.package_name), fn_name_(fn.name), fn_ret_str_(format_type_ref_sema(fn.return_type)),
          cf_(cf), warnings_(warnings), errors_(errors), diags_(diagnostics), resilient_(resilient),
          fn_is_fallible_(fn.return_type.is_fallible || fn.name == "main" || fn.name.starts_with("__cfire_test_")) {
        for (const auto& imp : cf_.ast.imports) {
            if (imp.is_cpp) {
                has_cpp_imports_ = true;
                break;
            }
        }
        auto register_tp = [&](const std::string& name, const std::vector<std::string>& constraints) {
            if (name.empty()) return;
            fn_type_params_.insert(name);
            if (!constraints.empty()) {
                fn_type_param_bounds_[name] = constraints;
            }
            if (!fn_type_param_ids_.contains(name)) {
                fn_type_param_ids_[name] = cf_.type_table.make_type_param(name, constraints);
            }
        };
        for (const auto& tp : fn.type_params) {
            register_tp(tp.name, tp.constraints);
        }
        if (fn.receiver) {
            for (const auto& st : cf_.ast.structs) {
                if (st.name == fn.receiver->type.name) {
                    for (std::size_t idx = 0; idx < st.type_params.size(); ++idx) {
                        const auto& tp = st.type_params[idx];
                        register_tp(tp.name, tp.constraints);
                        if (idx < fn.receiver->type.type_args.size() && !fn.receiver->type.type_args[idx].name.empty()) {
                            register_tp(fn.receiver->type.type_args[idx].name, tp.constraints);
                        }
                    }
                }
            }
            for (const auto& ta : fn.receiver->type.type_args) register_tp(ta.name, {});  // `fn (b: Box[T])`
        }
        if (fn.return_type.name.empty() && !fn.return_type.is_slice && !fn.return_type.is_array &&
            !fn.return_type.is_map && !fn.return_type.is_tuple && !fn.return_type.is_fn &&
            fn.return_type.smart_wrap.empty()) {
            TypeId base = TypeId::Void();
            if (fn.return_type.is_optional) base = cf_.type_table.make_optional(base);
            if (fn.return_type.is_fallible) base = cf_.type_table.make_fallible(base);
            fn_ret_tid_ = base;
        } else {
            fn_ret_tid_ = resolve_typeref(fn.return_type);
        }
        // Seed in-scope module/file global variables into Sema type tables.
        for (const auto& gv : cf_.ast.global_vars) {
            TypeId gt = TypeId::Unknown();
            if (gv.type) {
                gt = resolve_typeref_in_pkg(*gv.type, gv.package_name);
            } else if (gv.value && gv.value->resolved_type_id != 0) {
                gt = TypeId{gv.value->resolved_type_id};
            }
            if (gt.is_valid() && !gt.is_unknown()) {
                std::string tstr = cf_.type_table.to_string(gt);
                if (gv.package_name.empty() || gv.package_name == pkg_name_) {
                    var_type_ids_[gv.name] = gt;
                    var_types_[gv.name] = tstr;
                }
                if (!gv.package_name.empty()) {
                    var_type_ids_[gv.package_name + "." + gv.name] = gt;
                    var_types_[gv.package_name + "." + gv.name] = tstr;
                }
            }
        }
        if (fn.receiver) {
            (void)check_type_ref_separators(fn.receiver->type, fn.receiver->line, fn.receiver->col);  // recorded by emit_error
            param_names_.insert(fn.receiver->name);
            var_scope_depth_[fn.receiver->name] = 0;
            if (!fn.receiver->type.name.empty()) {
                var_types_[fn.receiver->name] = format_type_ref_sema(fn.receiver->type);
            }
            TypeId recv_tid = resolve_typeref(fn.receiver->type);
            if (recv_tid.is_valid()) var_type_ids_[fn.receiver->name] = recv_tid;
            if (fn.receiver->has_explicit_in) {
                (void)emit_error(diag_at(fn.receiver->line, fn.receiver->col, "cfire/DEPRECATED-IN-PARAM", std::format("explicit 'in' receiver intent is deprecated; omit 'in'")));
            }
            if (fn.receiver->has_deprecated_mut_pos) {
                (void)emit_error(diag_at(fn.receiver->line, fn.receiver->col, "cfire/DEPRECATED-INTENT-POS", std::format("'(s: mut T)' receiver placement is deprecated; use '(mut s: T)' instead")));
            }
            if (fn.receiver->intent == "mut") {
                mut_vars_.insert(fn.receiver->name);
            } else {
                imm_vars_.insert(fn.receiver->name);
            }
            if (fn.receiver->type.is_map) {
                map_vars_.insert(fn.receiver->name);
            }
            if (fn.receiver->type.is_array && !fn.receiver->type.array_len.empty()) {
                if (auto n = parse_int_literal<std::size_t>(fn.receiver->type.array_len)) {
                    fixed_array_lens_[fn.receiver->name] = *n;
                }
            }
        }
        for (const auto& p : fn.params) {
            (void)check_type_ref_separators(p.type, p.line, p.col);  // recorded by emit_error
            param_names_.insert(p.name);
            var_scope_depth_[p.name] = 0;
            TypeId ptid = resolve_typeref(p.type);
            if (ptid.is_valid()) var_type_ids_[p.name] = ptid;
            if (!p.type.smart_wrap.empty() && p.type.elem_type && !p.type.elem_type->name.empty()) {
                var_types_[p.name] = p.type.elem_type->name;
            } else if (!p.type.name.empty() || p.type.is_slice || p.type.is_array || p.type.is_map || p.type.is_tuple || p.type.is_fn) {
                var_types_[p.name] = format_type_ref_sema(p.type);
            }
            if (p.type.is_optional) {
                optional_vars_.insert(p.name);
            }
            if (p.type.is_fallible) {
                fallible_vars_.insert(p.name);
            }
            if (is_scalar_primitive_typeref(p.type)) {
                scalar_primitive_vars_.insert(p.name);
            }
            if (p.intent == "move") {
                move_params_.insert(p.name);
            }
            if (p.has_explicit_in) {
                (void)emit_error(diag_at(p.line, p.col, "cfire/DEPRECATED-IN-PARAM", std::format("explicit 'in' parameter intent is deprecated; omit 'in' (parameters are immutable by default)")));
            }
            if (p.has_deprecated_mut_pos) {
                (void)emit_error(diag_at(p.line, p.col, "cfire/DEPRECATED-INTENT-POS", std::format("'x: mut T' parameter placement is deprecated; use 'mut x: T' instead")));
            }
            if (p.intent == "mut") {
                mut_vars_.insert(p.name);
            } else {
                imm_vars_.insert(p.name);
            }
            if (p.type.is_map) {
                map_vars_.insert(p.name);
            }
            if (p.type.is_array && !p.type.array_len.empty()) {
                if (auto n = parse_int_literal<std::size_t>(p.type.array_len)) {
                    fixed_array_lens_[p.name] = *n;
                }
            }
            if (p.type.is_optional) {
                optional_vars_.insert(p.name);
            }
            if (p.type.is_fallible) {
                fallible_vars_.insert(p.name);
            }
            if (p.type.is_fn && p.type.fn_ret) {
                if (p.type.fn_ret->is_fallible) fn_ret_fallible_vars_.insert(p.name);
                if (p.type.fn_ret->is_optional) fn_ret_optional_vars_.insert(p.name);
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty()) {
                raw_or_smart_ptr_vars_.insert(p.name);
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty() || p.type.is_optional) {
                ptr_or_smart_vars_.insert(p.name);
                ptr_null_state_[p.name] = p.type.is_optional ? NullState::MaybeNull : NullState::NonNull;
            }
            if (!p.type.smart_wrap.empty()) {
                smart_ptr_vars_.insert(p.name);
            }
            if ((p.type.is_slice || p.type.is_array) && p.type.name == "byte") {
                byte_slice_vars_.insert(p.name);
            }
            std::string pt = p.type.raw_name.empty() ? p.type.name : p.type.raw_name;
            auto dot = pt.find('.');
            if (dot != std::string::npos) {
                std::string qual = pt.substr(0, dot);
                if (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual)) {
                    foreign_vars_.insert(p.name);
                }
            }
        }
        if (!fn.return_type.name.empty()) {
            (void)check_type_ref_separators(fn.return_type, fn.line, 0);  // recorded by emit_error
        }
    }


    CFIRE_SEMA_INLINE void SemaFnChecker::invalidate_loans_for_owner(const std::string& owner, std::string_view reason, int line) {
        for (const auto& [borrower, orig] : borrow_origin_) {
            if (orig == owner) {
                invalidated_loans_[borrower] = InvalidatedLoan{std::string(reason), line};
            }
        }
        // a borrower can depend on several owners (a call returning a view of either argument,
        // or a struct literal holding pointers into different containers).
        for (const auto& [borrower, owners] : extra_borrow_origins_) {
            if (std::find(owners.begin(), owners.end(), owner) != owners.end()) {
                invalidated_loans_[borrower] = InvalidatedLoan{std::string(reason), line};
            }
        }
    }


    // Records every owner `e` borrows from onto `name` (the first stays in borrow_origin_ for existing checks).
    CFIRE_SEMA_INLINE void SemaFnChecker::record_borrow_origins(const std::string& name, const ExprPtr& e) {
        auto owners = extract_borrow_sources(e);
        if (owners.size() > 1) {
            extra_borrow_origins_[name] = std::vector<std::string>(owners.begin() + 1, owners.end());
        } else {
            extra_borrow_origins_.erase(name);
        }
    }


    // (A3): does `call` produce a temporary owned value (as opposed to a view/pointer into storage
    // that outlives the expression)? Unknown callees are treated as owning: cfire values are owned unless
    // a signature says `&T` / `*T`.
    CFIRE_SEMA_INLINE auto SemaFnChecker::call_returns_owned_temporary(const CallExpr& call) const -> bool {
        if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
            const FnDecl* sig = nullptr;
            if (!pkg_name_.empty()) {
                if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) sig = &pit->second;
            }
            if (!sig) {
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) sig = &it->second;
            }
            if (sig) return !returns_view_type(sig->return_type);
            return cid->name != "cpp";
        }
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (const auto* sig = resolve_member_call_sig(*mem)) return !returns_view_type(sig->return_type);
            const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
            const auto& idx = StdlibCatalogIndex::instance();
            if (obj_id && idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
                if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                    return !(sfn->return_type.starts_with("&") || sfn->return_type.starts_with("*"));
                }
                return false;  // unknown stdlib member: don't guess
            }
            // Builtin view-returning members borrow from their receiver, not from a temporary;
            // `.bytes()` on an rvalue `str` materializes an owned `[]byte` temporary.
            if (mem->field == "bytes" && call.args.empty()) {
                if (mem->object) {
                    if (const auto* oc = std::get_if<CallExpr>(&mem->object->node)) {
                        return call_returns_owned_temporary(*oc);
                    }
                    if (std::holds_alternative<BinaryExpr>(mem->object->node)) return true;
                }
                return false;
            }
            if (mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr" || mem->field == "byte_ptr_at") return false;
            return false;
        }
        return false;
    }


    // If `e` creates a view or pointer (`x[a..b]`, `&x`, `x.data()`) whose base object is a temporary owned
    // value, the view dangles at the end of the full expression once it is stored (A3).
    CFIRE_SEMA_INLINE auto SemaFnChecker::view_of_temporary(const ExprPtr& e) const -> bool {
        if (!e) return false;
        ExprPtr base = nullptr;
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            base = sl->object;
        } else if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && (un->op == "&" || un->op == "&mut")) {
            base = un->operand;
        } else if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node);
                mem && (mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr" || mem->field == "byte_ptr_at")) {
                base = mem->object;
            }
        }
        while (base) {
            if (const auto* m = std::get_if<MemberExpr>(&base->node); m && !m->is_namespace) { base = m->object; continue; }
            if (const auto* ix = std::get_if<IndexExpr>(&base->node)) { base = ix->object; continue; }
            if (const auto* sl = std::get_if<SliceExpr>(&base->node)) { base = sl->object; continue; }
            break;
        }
        if (!base) return false;
        if (const auto* call = std::get_if<CallExpr>(&base->node)) return call_returns_owned_temporary(*call);
        return std::holds_alternative<StructLitExpr>(base->node) || std::holds_alternative<ArrayLitExpr>(base->node);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_view_of_temporary(const ExprPtr& e, std::string_view target, int line, int col)
        -> std::expected<void, std::string> {
        if (!view_of_temporary(e)) return {};
        return emit_error(diag_at(line, col, "cfire/DANGLING-TEMPORARY", std::format("{} borrows from a temporary value that is destroyed at the end of this statement; bind the owner to a variable first (e.g. 'owner := f()' then 'owner[a..b]')", target)));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::returns_view_type(const TypeRef& t) -> bool {
        return t.is_borrow || t.is_mut_borrow || t.is_ptr || t.is_mut_ptr;
    }


    // all owners an expression's value borrows from.
    //  - `&x`, `x[a..b]`, `x.data()`: the root owner of x (existing rule, see extract_borrow_source)
    //  - a call whose declared return type is a view or pointer borrows from its receiver and from every
    //    argument that is (or borrows from) a local path: `h := head(s)` keeps `s` loaned while `h` lives
    //  - a struct literal borrows from every owner its field values borrow from
    CFIRE_SEMA_INLINE auto SemaFnChecker::extract_borrow_sources(const ExprPtr& e) const -> std::vector<std::string> {
        std::vector<std::string> out;
        auto add = [&](const std::string& o) {
            if (!o.empty() && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
        };
        if (!e) return out;
        if (auto direct = extract_borrow_source(e)) {
            add(*direct);
            return out;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            const FnDecl* sig = nullptr;
            ExprPtr receiver = nullptr;
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) sig = &pit->second;
                }
                if (!sig) {
                    if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) sig = &it->second;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                sig = resolve_member_call_sig(*mem);
                receiver = mem->object;
            }
            if (sig && returns_view_type(sig->return_type)) {
                auto from_arg = [&](const ExprPtr& a) {
                    if (auto b = extract_borrow_source(a)) {
                        add(*b);
                    } else if (auto pi = extract_path_info(a)) {
                        add(resolve_root_owner(pi->root));
                    }
                };
                if (receiver && sig->receiver) from_arg(receiver);
                for (const auto& a : call->args) from_arg(a.value);
            }
        } else if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            auto sdit = cf_.struct_defs.find(st->type_name);
            for (const auto& f : st->fields) {
                bool field_is_owned = false;
                if (sdit != cf_.struct_defs.end()) {
                    for (const auto& fd : sdit->second.fields) {
                        if (fd.name == f.name) {
                            if (!fd.type.is_ptr && !fd.type.is_mut_ptr && !fd.type.is_borrow && !fd.type.is_mut_borrow) {
                                field_is_owned = true;
                            }
                            break;
                        }
                    }
                }
                if (!field_is_owned) {
                    for (auto& o : extract_borrow_sources(f.value)) add(o);
                }
            }
        }
        return out;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_not_iterated_collection(const std::string& root, int line, int col)
        -> std::expected<void, std::string> {
        if (active_loop_collections_.contains(root)) {
            return emit_error(diag_at(line, col, "cfire/ITERATOR-INVALIDATION", std::format("cannot structurally mutate collection '{}' while iterating over it", root)));
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::record_var_read(const std::string& name, int line, int col, uint64_t expr_id)
        -> std::expected<void, std::string> {
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) {
            return emit_error(diag_at(line, col, "cfire/USE-AFTER-MOVE", std::format("use of moved variable '{}' (previously moved at line {})", name, it->second)));
        }
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) {
            return emit_error(diag_at(line, col, "cfire/USE-AFTER-FREE", std::format("use of dangling pointer/view '{}' ({})", name, it->second.reason)));
        }
        if (move_params_.contains(name)) {
            if (loop_depth_ == 0) {
                if (!read_in_loop_.contains(name)) {
                    last_read_expr_id[name] = expr_id;
                }
            } else {
                read_in_loop_.insert(name);
                last_read_expr_id.erase(name);
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::extract_path_info(const ExprPtr& e) -> std::optional<PathInfo> {
        if (!e) return std::nullopt;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return PathInfo{id->name, id->name};
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->is_namespace) return std::nullopt;
            auto inner = extract_path_info(mem->object);
            if (!inner) return std::nullopt;
            return PathInfo{inner->root, inner->path + "." + mem->field};
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            auto inner = extract_path_info(idx->object);
            if (!inner) return std::nullopt;
            return PathInfo{inner->root, inner->path + "[*]"};
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut" || un->op == "*") {
                return extract_path_info(un->operand);
            }
        }
        return std::nullopt;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_root_owner(const std::string& var) const -> std::string {
        if (auto it = borrow_origin_.find(var); it != borrow_origin_.end()) {
            return it->second;
        }
        return var;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::extract_borrow_source(const ExprPtr& e, bool* is_direct_expr) const -> std::optional<std::string> {
        if (is_direct_expr) *is_direct_expr = false;
        if (!e) return std::nullopt;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut") {
                if (auto pi = extract_path_info(un->operand)) {
                    if (is_direct_expr) *is_direct_expr = true;
                    return resolve_root_owner(pi->root);
                }
            }
            return std::nullopt;
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto pi = extract_path_info(sl->object)) {
                if (is_direct_expr) *is_direct_expr = true;
                return resolve_root_owner(pi->root);
            }
            return std::nullopt;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if ((mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr") && call->args.empty()) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                if (mem->field == "byte_ptr_at" && call->args.size() == 1) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                // view-returning builtin methods (`s.trim`, `s.strip_prefix(p)`) borrow from their receiver.
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && bm->borrows) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                if (const auto* mod_id = std::get_if<IdentExpr>(&mem->object->node);
                    mod_id && mod_id->name == "str" && !call->args.empty() &&
                    (mem->field == "byte_ptr" || mem->field == "byte_ptr_at")) {
                    if (auto pi = extract_path_info(call->args[0].value)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
            }
            return std::nullopt;
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = borrow_origin_.find(id->name); it != borrow_origin_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_ptr_or_smart_init(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            return un->op == "&" || un->op == "&mut";
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "make" && call->type_args.size() == 1 && !call->type_args[0].smart_wrap.empty()) {
                    return true;
                }
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->field == "data" || mem->field == "data_ptr" ||
                    mem->field == "byte_ptr" || mem->field == "byte_ptr_at") {
                    return true;
                }
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return ptr_or_smart_vars_.contains(id->name) || smart_ptr_vars_.contains(id->name) || raw_or_smart_ptr_vars_.contains(id->name);
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_const_zero_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "-" || un->op == "+") {
                return is_const_zero_expr(un->operand);
            }
            return false;
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::Int) {
                if (auto v = parse_int_literal<std::int64_t>(lit->value)) {
                    return *v == 0;
                }
            }
            // Float division by zero is IEEE-defined (±inf / NaN), so only integer zero counts (corpus).
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_dynamic_string_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String && lit->value.contains('{') && lit->value.contains('}')) {
                return true;
            }
            return false;
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->op == "+") {
                bool left_static = bin->left && std::holds_alternative<LiteralExpr>(bin->left->node) && !is_dynamic_string_expr(bin->left);
                bool right_static = bin->right && std::holds_alternative<LiteralExpr>(bin->right->node) && !is_dynamic_string_expr(bin->right);
                return !(left_static && right_static);
            }
        }
        return false;
    }


    // SQL statements reach `.exec` / `.query` on SQLite connections, statements and transactions.
    CFIRE_SEMA_INLINE auto SemaFnChecker::is_sql_receiver(const ExprPtr& obj) const -> bool {
        std::string t = type_name_of(obj);
        while (!t.empty() && (t.front() == '&' || t.front() == '*')) t.erase(t.begin());
        if (t.starts_with("mut ")) t.erase(0, 4);
        if (t.starts_with("sqlite.") || t.starts_with("sqlite::") || t.starts_with("cfire::sqlite")) return true;
        if (t.empty() || t == "Conn" || t == "Db" || t == "Tx" || t == "Stmt") return is_stdlib_ns_imported("sqlite");
        return false;
    }


    // is `e` a string the program fully controls? Literals without interpolation, concatenations of
    // constants, and locals whose every assignment was constant. Anything else (parameters, call results,
    // interpolated or concatenated runtime data) may carry external input into a shell or SQL statement.
    CFIRE_SEMA_INLINE auto SemaFnChecker::is_constant_string_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::String && !is_dynamic_string_expr(e);
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node); bin && bin->op == "+") {
            return is_constant_string_expr(bin->left) && is_constant_string_expr(bin->right);
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return const_string_vars_.contains(id->name);
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_cpp_qualifier_sema(std::string_view name) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(std::string(name))) return false;
        if (name == "cpp") return true;
        return cf_.cpp_qualifiers.contains(std::string(name));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_vendor_alias_sema(std::string_view name) const -> bool {
        if (cf_.vendor_aliases.contains(std::string(name))) return true;
        for (const auto& imp : cf_.ast.imports) {
            if (imp.alias == name && imp.path.starts_with("vendor:")) return true;
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_unresolved_local_pkg_alias_sema(std::string_view name) const -> bool {
        if (name.empty()) return false;
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.path.starts_with("./") && !imp.path.starts_with("../")) continue;
            auto slash = imp.path.rfind('/');
            std::string_view tail = (slash == std::string_view::npos)
                ? std::string_view(imp.path)
                : std::string_view(imp.path).substr(slash + 1);
            std::string_view eff_alias = imp.alias.empty() ? tail : std::string_view(imp.alias);
            if (eff_alias != name) continue;
            bool has_any_decl = false;
            for (const auto& fn : cf_.ast.functions) if (fn.package_name == tail) { has_any_decl = true; break; }
            if (!has_any_decl) for (const auto& st : cf_.ast.structs) if (st.package_name == tail) { has_any_decl = true; break; }
            if (!has_any_decl) for (const auto& en : cf_.ast.enums) if (en.package_name == tail) { has_any_decl = true; break; }
            if (!has_any_decl) for (const auto& iface : cf_.ast.interfaces) if (iface.package_name == tail) { has_any_decl = true; break; }
            if (!has_any_decl) for (const auto& gv : cf_.ast.global_vars) if (gv.package_name == tail) { has_any_decl = true; break; }
            if (!has_any_decl) return true;
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_stdlib_ns_sema(std::string_view qualifier) const -> std::string {
        if (cf_.shadowed_default_qualifiers.contains(std::string(qualifier))) return "";
        if (auto it = cf_.core_aliases.find(std::string(qualifier)); it != cf_.core_aliases.end()) {
            return it->second;
        }
        if (StdlibCatalogIndex::instance().has_namespace(qualifier)) {
            return std::string(qualifier);
        }
        return "";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_namespace_ident_sema(std::string_view name) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(std::string(name))) return false;
        if (name == "cpp") return true;
        if (is_cpp_qualifier_sema(name)) return true;
        if (is_vendor_alias_sema(name)) return true;
        if (cf_.core_aliases.contains(std::string(name))) return true;
        if (StdlibCatalogIndex::instance().has_namespace(name)) return true;
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.alias.empty() && imp.alias == name) return true;
            auto slash = imp.path.rfind('/');
            std::string_view tail = (slash == std::string::npos) ? std::string_view(imp.path) : std::string_view(imp.path).substr(slash + 1);
            if (tail == name) return true;
        }
        for (const auto& fn : cf_.ast.functions) {
            if (!fn.package_name.empty() && fn.package_name == name) return true;
        }
        for (const auto& st : cf_.ast.structs) {
            if (!st.package_name.empty() && st.package_name == name) return true;
        }
        for (const auto& en : cf_.ast.enums) {
            if (!en.package_name.empty() && en.package_name == name) return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (!gv.package_name.empty() && gv.package_name == name) return true;
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_common_stdlib_method_sema(std::string_view name) -> bool {
        return name == "open" || name == "close" || name == "fd" || name == "is_open" || name == "is_tty" ||
               name == "poll" || name == "read" || name == "read_into" || name == "read_bytes" ||
               name == "read_exact" || name == "read_text" || name == "read_bytes_until" ||
               name == "read_text_until" || name == "read_str" || name == "read_line" ||
               name == "read_lines" || name == "read_all" || name == "write" ||
               name == "write_text" || name == "write_str" || name == "write_bytes" ||
               name == "flush" || name == "seek" ||
               name == "tell" || name == "size" || name == "len" || name == "empty" ||
               name == "clear" || name == "push" || name == "pop" || name == "get" || name == "set" ||
               name == "contains" || name == "keys" || name == "values" || name == "erase" ||
               name == "remove" || name == "send" || name == "recv" || name == "try_recv" ||
               name == "recv_timeout" || name == "bind" || name == "step" || name == "exec" ||
               name == "query" || name == "prepare" || name == "begin" || name == "commit" ||
               name == "rollback" || name == "join" || name == "detach" || name == "lock" ||
               name == "unlock" || name == "reset" || name == "or_error" || name == "ignore_error";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_member_call_sig(const MemberExpr& mem) const -> const FnDecl* {
        const auto* obj_id = mem.object ? std::get_if<IdentExpr>(&mem.object->node) : nullptr;
        if (obj_id && is_namespace_ident_sema(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
            if (auto it = cf_.fn_sigs.find(obj_id->name + "." + mem.field); it != cf_.fn_sigs.end()) {
                return &it->second;
            }
            return nullptr;
        }
        if (mem.object) {
            TypeId recv_t = unwrap_receiver_type(infer_expr_type_id_sema(mem.object), /*unwrap_opt=*/true);
            if (recv_t.is_valid() && !recv_t.is_unknown() && !recv_t.is_error()) {
                const auto& rd = cf_.type_table.get(recv_t);
                std::string owner = (rd.kind == TypeKind::GenericInst) ? rd.base_name : rd.name;
                if (auto lb = owner.find('['); lb != std::string::npos) owner = owner.substr(0, lb);
                auto try_owner = [&](const std::string& o) -> const FnDecl* {
                    if (o.empty()) return nullptr;
                    if (auto mit = cf_.methods.find(o + "::" + mem.field); mit != cf_.methods.end()) {
                        return &mit->second;
                    }
                    if (auto dot = o.find('.'); dot != std::string::npos) {
                        std::string mangled = std::format("cfire_pkg_{}__{}", o.substr(0, dot), o.substr(dot + 1));
                        if (auto mit = cf_.methods.find(mangled + "::" + mem.field); mit != cf_.methods.end()) {
                            return &mit->second;
                        }
                        if (auto mit = cf_.methods.find(o.substr(dot + 1) + "::" + mem.field); mit != cf_.methods.end()) {
                            return &mit->second;
                        }
                    }
                    return nullptr;
                };
                if (const auto* sig = try_owner(owner)) return sig;
                std::string canon = canonical_nominal_path_sema(owner);
                if (canon != owner) {
                    if (const auto* sig = try_owner(canon)) return sig;
                }
            }
        }
        if (obj_id) {
            if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                std::string o = vt->second;
                if (auto lb = o.find('['); lb != std::string::npos) o = o.substr(0, lb);
                if (auto mit = cf_.methods.find(o + "::" + mem.field); mit != cf_.methods.end()) {
                    return &mit->second;
                }
            }
        }
        if (auto owner_it = cf_.method_owner.find(mem.field); owner_it != cf_.method_owner.end()) {
            auto owners_it = cf_.method_owners.find(mem.field);
            if (owners_it != cf_.method_owners.end() && owners_it->second.size() == 1 &&
                !is_common_stdlib_method_sema(mem.field)) {
                if (auto mit = cf_.methods.find(owner_it->second + "::" + mem.field); mit != cf_.methods.end()) {
                    return &mit->second;
                }
            }
        }
        return nullptr;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::expr_returns_optional_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return optional_vars_.contains(id->name);
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::KwNone;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return expr_returns_optional_sema(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return expr_returns_optional_sema(or_ex->fallback_val);
            }
            return false;
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    for (const auto& st : cf_.ast.structs) {
                        if (st.name == owner_t) {
                            for (const auto& fld : st.fields) {
                                if (fld.name == mem->field) {
                                    return fld.type.is_optional && !fld.type.is_fallible;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->object && (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string")) {
                    if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(id->name)) return true;
                    } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(sub_mem->field)) return true;
                    }
                }
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "some" || cid->name == "readln" || cid->name == "input") {
                    return true;
                }
                if (cid->name.ends_with("::from_i32") || cid->name.ends_with("::from_i64") || cid->name.ends_with("::from_string")) {
                    return true;
                }
                if (fn_ret_optional_vars_.contains(cid->name)) {
                    return true;
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        return pit->second.return_type.is_optional && !pit->second.return_type.is_fallible;
                    }
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    return it->second.return_type.is_optional && !it->second.return_type.is_fallible;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && !bm->ret.empty()) return bm->ret.ends_with('?');  // builtin method table
                if (mem->field == "try_as" && call->type_args.size() == 1) return true;  // x.try_as[T]() -> T?
                if (mem->field == "checked_shl" || mem->field == "checked_shr") return true;
                if (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string") {
                    if (mem->object) {
                        if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                            if (cf_.enums.contains(id->name)) return true;
                        } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                            if (cf_.enums.contains(sub_mem->field)) return true;
                        }
                    }
                }
                const auto& idx = StdlibCatalogIndex::instance();
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if ((is_namespace_ident_sema(recv_id->name) || idx.has_namespace(recv_id->name)) &&
                        !is_ident_in_scope_sema(recv_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(recv_id->name, mem->field)) {
                            if (sfn->is_optional) return true;
                        }
                        if (recv_id->name == "os" && mem->field == "getenv" && call->args.size() <= 1) {
                            return true;
                        }
                        if (recv_id->name == "str" && mem->field == "find") {
                            return true;
                        }
                        if (recv_id->name == "cli" &&
                            (mem->field == "prompt" || mem->field == "prompt_password")) {
                            return true;
                        }
                        if (const auto* sig = resolve_member_call_sig(*mem)) {
                            return sig->return_type.is_optional && !sig->return_type.is_fallible;
                        }
                        return false;
                    }
                }
                if (mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "read_line" ||
                    (mem->field == "ignore_error" && call->args.empty())) {
                    return true;
                }
                if (mem->field == "get" && call->args.size() == 1 && !call->block_arg) {
                    return true;
                }
                if (mem->field == "recv" && call->args.empty()) {
                    return true;
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_optional && !sig->return_type.is_fallible;
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem)) {
                    return im->return_type.is_optional && !im->return_type.is_fallible;
                }
            }
        }
        return false;
    }


    // the former string-based inference, now producing TypeIds. Only reached as the fallback
    // of infer_expr_type_id_sema; names for messages come from type_name_of().
    CFIRE_SEMA_INLINE auto SemaFnChecker::infer_named_type_id_(const ExprPtr& e) const -> TypeId {
        if (!e) return TypeId::Unknown();
        if (e->resolved_type_id != 0) return TypeId{e->resolved_type_id};
        auto resolve_fn_ret = [&](const TypeRef& rt, std::string_view owner_pkg) -> TypeId {
            if (rt.name.empty() && !rt.is_slice && !rt.is_array && !rt.is_map &&
                !rt.is_tuple && !rt.is_fn && rt.smart_wrap.empty() &&
                !rt.is_borrow && !rt.is_mut_borrow && !rt.is_ptr && !rt.is_mut_ptr) {
                TypeId v = TypeId::Void();
                if (rt.is_optional) v = cf_.type_table.make_optional(v);
                if (rt.is_fallible) v = cf_.type_table.make_fallible(v);
                return v;
            }
            return resolve_typeref_in_pkg(rt, owner_pkg);
        };
        auto lookup_struct_def = [&](std::string_view name) -> const StructDecl* {
            if (auto it = cf_.struct_defs.find(std::string(name)); it != cf_.struct_defs.end()) return &it->second;
            std::string canon = canonical_nominal_path_sema(name);
            if (auto it = cf_.struct_defs.find(canon); it != cf_.struct_defs.end()) return &it->second;
            if (auto dot = canon.find('.'); dot != std::string::npos) {
                std::string mangled = std::format("cfire_pkg_{}__{}", canon.substr(0, dot), canon.substr(dot + 1));
                if (auto it = cf_.struct_defs.find(mangled); it != cf_.struct_defs.end()) return &it->second;
                if (auto it = cf_.struct_defs.find(canon.substr(dot + 1)); it != cf_.struct_defs.end()) return &it->second;
            }
            return nullptr;
        };
        auto wrap_stdlib_ret = [&](const StdFnSigInfo& sig, TypeId base) -> TypeId {
            if (base.is_unknown() && (sig.unwrapped_return_type.empty() || sig.unwrapped_return_type == "void")) {
                base = TypeId::Void();
            }
            if (sig.is_optional && !cf_.type_table.is_optional(base)) base = cf_.type_table.make_optional(base);
            if (sig.is_fallible && !cf_.type_table.is_fallible(base)) base = cf_.type_table.make_fallible(base);
            return base;
        };
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return TypeId::Str();
            if (lit->kind == TokenKind::Int) return TypeId::Int64();
            if (lit->kind == TokenKind::Float) return TypeId::Float64();
            if (lit->kind == TokenKind::Char) return TypeId::Byte();
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return TypeId::Bool();
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            if (!st->type_args.empty()) {
                std::vector<TypeId> targs;
                targs.reserve(st->type_args.size());
                for (const auto& ta : st->type_args) {
                    targs.push_back(resolve_typeref(ta));
                }
                return cf_.type_table.make_generic_inst(canonical_nominal_path_sema(st->type_name), std::move(targs));
            }
            return type_id_of(st->type_name);
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            TypeId inner_t = infer_expr_type_id_sema(tr->inner);
            if (cf_.type_table.get(inner_t).kind == TypeKind::Fallible) {
                return cf_.type_table.unwrap_fallible(inner_t);
            }
            return inner_t;
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->resolved_type_id != 0) return TypeId{or_ex->resolved_type_id};
            TypeId inner_t = infer_expr_type_id_sema(or_ex->inner);
            if (or_ex->fallback_val) {
                TypeId fb_t = infer_expr_type_id_sema(or_ex->fallback_val);
                if ((cf_.type_table.is_fallible(inner_t) && cf_.type_table.is_fallible(fb_t)) ||
                    (cf_.type_table.is_optional(inner_t) && cf_.type_table.is_optional(fb_t))) {
                    return fb_t;
                }
            }
            if (cf_.type_table.is_fallible(inner_t)) return cf_.type_table.unwrap_fallible(inner_t);
            if (cf_.type_table.is_optional(inner_t)) return cf_.type_table.unwrap_optional(inner_t);
            return inner_t;
        }
        if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
            std::vector<TypeId> elem_ids;
            elem_ids.reserve(tup->elements.size());
            for (const auto& el : tup->elements) {
                elem_ids.push_back(infer_expr_type_id_sema(el));
            }
            return cf_.type_table.make_tuple(std::move(elem_ids));
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = var_type_ids_.find(id->name); it != var_type_ids_.end() && it->second.is_valid()) {
                return it->second;
            }
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                return type_id_of(it->second);
            }
            if (foreign_vars_.contains(id->name) || is_cpp_qualifier_sema(id->name)) {
                return TypeId::Foreign();
            }
            if (auto vit = cf_.variant_to_enum.find(id->name); vit != cf_.variant_to_enum.end()) {
                return type_id_of(vit->second);
            }
            if (auto pos = id->name.find("::"); pos != std::string::npos) {
                std::string prefix = id->name.substr(0, pos);
                if (cf_.enums.contains(prefix)) return type_id_of(prefix);
            }
            if (auto pos = id->name.find('.'); pos != std::string::npos) {
                std::string prefix = id->name.substr(0, pos);
                if (cf_.enums.contains(prefix)) return type_id_of(prefix);
            }
            if (!is_namespace_ident_sema(id->name) && !is_builtin_ident_sema(id->name)) {
                const FnDecl* fdecl = nullptr;
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + id->name); pit != cf_.fn_sigs.end()) {
                        fdecl = &pit->second;
                    }
                }
                if (!fdecl) {
                    if (auto it = cf_.fn_sigs.find(id->name); it != cf_.fn_sigs.end()) fdecl = &it->second;
                }
                if (fdecl && fdecl->type_params.empty()) {
                    std::vector<TypeId> ptypes;
                    ptypes.reserve(fdecl->params.size());
                    for (const auto& p : fdecl->params) ptypes.push_back(resolve_typeref_in_pkg(p.type, fdecl->package_name));
                    return cf_.type_table.make_function(std::move(ptypes), resolve_fn_ret(fdecl->return_type, fdecl->package_name));
                }
            }
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->object) {
                if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (cf_.enums.contains(id->name)) return type_id_of(id->name);
                    if (is_cpp_qualifier_sema(id->name) || foreign_vars_.contains(id->name)) {
                        return TypeId::Foreign();
                    }
                } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                    if (const auto* sid = sub_mem->object ? std::get_if<IdentExpr>(&sub_mem->object->node) : nullptr) {
                        if (const auto* en = resolve_enum_by_prefix_sema(sid->name + "." + sub_mem->field)) {
                            std::string ename = en->package_name.empty() ? en->name : (en->package_name + "." + en->name);
                            return type_id_of(ename);
                        }
                        if (is_cpp_qualifier_sema(sid->name)) return TypeId::Foreign();
                    }
                    if (cf_.enums.contains(sub_mem->field)) return type_id_of(sub_mem->field);
                }
            }
            if (!mem->is_namespace && mem->object) {
                TypeId raw_obj_t = infer_expr_type_id_sema(mem->object);
                if (raw_obj_t.is_foreign() || cf_.type_table.contains_foreign(raw_obj_t)) return TypeId::Foreign();
                TypeId obj_t = unwrap_receiver_type(raw_obj_t, /*unwrap_opt=*/true);
                const auto& od = cf_.type_table.get(obj_t);
                if (od.kind == TypeKind::GenericInst) {
                    if (const StructDecl* sdef = lookup_struct_def(od.base_name)) {
                        std::unordered_map<std::string, TypeId> env;
                        for (std::size_t i = 0; i < sdef->type_params.size() && i < od.type_args.size(); ++i) {
                            env[sdef->type_params[i].name] = od.type_args[i];
                        }
                        for (const auto& f : sdef->fields) {
                            if (f.name == mem->field) {
                                TypeRef ftr = canonical_typeref_in_pkg_sema(f.type, sdef->package_name);
                                return cf_.type_table.from_typeref(ftr, &env);
                            }
                        }
                    }
                } else if (od.kind == TypeKind::NominalStruct && !od.name.empty()) {
                    if (const StructDecl* sdef = lookup_struct_def(od.name)) {
                        for (const auto& f : sdef->fields) {
                            if (f.name == mem->field) return resolve_typeref_in_pkg(f.type, sdef->package_name);
                        }
                    }
                    if (auto sit = cf_.struct_tags.find(od.name); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return resolve_typeref(f.field_type);
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            auto infer_fn_decl_ret = [&](const FnDecl& decl) -> TypeId {
                if (!decl.type_params.empty()) {
                    std::unordered_map<std::string, TypeId> pat_env;
                    std::unordered_map<std::string, TypeId> inst_env;
                    for (std::size_t idx = 0; idx < decl.type_params.size(); ++idx) {
                        const auto& tp = decl.type_params[idx];
                        pat_env[tp.name] = cf_.type_table.make_type_param(tp.name, tp.constraints);
                        if (idx < call->type_args.size()) {
                            TypeId ta = resolve_typeref(call->type_args[idx]);
                            if (ta.is_valid()) inst_env[tp.name] = ta;
                        }
                    }
                    for (std::size_t idx = 0; idx < call->args.size() && idx < decl.params.size(); ++idx) {
                        const ParamDecl* param = &decl.params[idx];
                        if (!call->args[idx].label.empty()) {
                            auto lit = std::find_if(decl.params.begin(), decl.params.end(), [&](const ParamDecl& pd) { return pd.name == call->args[idx].label; });
                            if (lit != decl.params.end()) param = &*lit;
                        }
                        TypeId pat = cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(param->type, decl.package_name), &pat_env);
                        TypeId got = infer_expr_type_id_sema(call->args[idx].value);
                        (void)cf_.type_table.unify(pat, got, inst_env);
                    }
                    TypeRef ret_tr = canonical_typeref_in_pkg_sema(decl.return_type, decl.package_name);
                    if (ret_tr.name.empty() && !ret_tr.is_slice && !ret_tr.is_array && !ret_tr.is_map &&
                        !ret_tr.is_tuple && !ret_tr.is_fn && ret_tr.smart_wrap.empty()) {
                        return resolve_fn_ret(decl.return_type, decl.package_name);
                    }
                    return cf_.type_table.from_typeref(ret_tr, &inst_env);
                }
                return resolve_fn_ret(decl.return_type, decl.package_name);
            };
            if (const auto* cm = std::get_if<MemberExpr>(&call->callee->node);
                cm && call->type_args.size() == 1 &&
                (cm->field == "wrapping_as" || cm->field == "saturating_as" || cm->field == "try_as")) {
                TypeId target = resolve_typeref(call->type_args[0]);
                return cm->field == "try_as" ? cf_.type_table.make_optional(target) : target;
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "some" && call->args.size() == 1 && call->args[0].value) {
                    return cf_.type_table.make_optional(infer_expr_type_id_sema(call->args[0].value));
                }
                if (cid->name == "readln" || cid->name == "input") {
                    return cf_.type_table.make_optional(TypeId::Str());
                }
                if (cid->name == "println" || cid->name == "print" || cid->name == "eprintln" ||
                    cid->name == "assert" || cid->name == "assert_eq" || cid->name == "assert_ne" ||
                    cid->name == "assert_true" || cid->name == "assert_false" ||
                    cid->name == "assert_ok" || cid->name == "assert_err" ||
                    cid->name == "delete" || cid->name == "close") {
                    return TypeId::Void();
                }
                if (cid->name == "panic" || cid->name == "exit") {
                    return TypeId::Never();
                }
                if (cid->name == "format") {
                    return TypeId::Str();
                }
                if (cid->name == "error") {
                    return type_id_of("Error");
                }
                if (cid->name == "len" || cid->name == "cap") {
                    return TypeId::Usize();
                }
                if (auto pos = cid->name.find("::"); pos != std::string::npos) {
                    std::string prefix = cid->name.substr(0, pos);
                    std::string tail = cid->name.substr(pos + 2);
                    if (cf_.enums.contains(prefix)) {
                        TypeId et = type_id_of(prefix);
                        if (tail == "from_i32" || tail == "from_i64" || tail == "from_string") {
                            return cf_.type_table.make_optional(et);
                        }
                        return et;
                    }
                }
                if (auto pos = cid->name.find('.'); pos != std::string::npos) {
                    std::string prefix = cid->name.substr(0, pos);
                    std::string tail = cid->name.substr(pos + 1);
                    if (cf_.enums.contains(prefix)) {
                        TypeId et = type_id_of(prefix);
                        if (tail == "from_i32" || tail == "from_i64" || tail == "from_string") {
                            return cf_.type_table.make_optional(et);
                        }
                        return et;
                    }
                }
                if (auto vit = cf_.variant_to_enum.find(cid->name); vit != cf_.variant_to_enum.end()) {
                    return type_id_of(vit->second);
                }
                if (cid->name == "make" && call->type_args.size() == 1) {
                    if (const auto* sc = find_stdlib_ctor(call->type_args[0].name); sc && !sc->generic) {
                        if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                            TypeId base = (!sig->unwrapped_return_type.empty() && sig->unwrapped_return_type != "void")
                                ? type_id_of(qualify_stdlib_type_sema(*sig, std::string(sc->ns)))
                                : TypeId::Void();
                            return wrap_stdlib_ret(*sig, base);
                        }
                    }
                    TypeId targ_tid = resolve_typeref(call->type_args[0]);
                    if (targ_tid.is_valid()) return targ_tid;
                }
                if (cid->name == "sync.Mutex" || cid->name == "sync.make_mutex") {
                    if (!call->type_args.empty()) return cf_.type_table.make_generic_inst("sync.Mutex", {resolve_typeref(call->type_args[0])});
                    if (!call->args.empty() && call->args[0].value) return cf_.type_table.make_generic_inst("sync.Mutex", {infer_expr_type_id_sema(call->args[0].value)});
                    return type_id_of("sync.Mutex[void]");
                }
                if (cid->name == "sync.Atomic" || cid->name == "sync.make_atomic") {
                    if (!call->type_args.empty()) return cf_.type_table.make_generic_inst("sync.Atomic", {resolve_typeref(call->type_args[0])});
                    if (!call->args.empty() && call->args[0].value) return cf_.type_table.make_generic_inst("sync.Atomic", {infer_expr_type_id_sema(call->args[0].value)});
                    return type_id_of("sync.Atomic[i64]");
                }
                if (cid->name == "[]byte" || cid->name == "[]u8" || cid->name == "str" ||
                    cid->name == "i8" || cid->name == "i16" || cid->name == "i32" || cid->name == "i64" || cid->name == "int" ||
                    cid->name == "u8" || cid->name == "byte" || cid->name == "u16" || cid->name == "u32" ||
                    cid->name == "u64" || cid->name == "usize" || cid->name == "isize" || cid->name == "uint" ||
                    cid->name == "f32" || cid->name == "f64" || cid->name == "bool" || cid->name == "char") {
                    if (cid->name == "char") return TypeId::Byte();
                    return type_id_of(cid->name);
                }
                const FnDecl* fdecl = nullptr;
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        fdecl = &pit->second;
                    }
                }
                if (!fdecl) {
                    if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) fdecl = &it->second;
                }
                if (fdecl) {
                    return infer_fn_decl_ret(*fdecl);
                }
                if (auto rtit = cf_.cpp_fn_returns.find(cid->name); rtit != cf_.cpp_fn_returns.end()) {
                    return map_cpp_qualtype_to_type_id(rtit->second);
                }
                if (cf_.cpp_fn_params.contains(cid->name) || is_cpp_qualifier_sema(cid->name) || foreign_vars_.contains(cid->name)) {
                    return TypeId::Foreign();
                }
                if (auto vit = var_type_ids_.find(cid->name); vit != var_type_ids_.end()) {
                    const auto& vd = cf_.type_table.get(vit->second);
                    if (vd.kind == TypeKind::Function) return vd.ret_type;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->object) {
                    if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(id->name)) {
                            TypeId et = type_id_of(id->name);
                            if (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string") {
                                return cf_.type_table.make_optional(et);
                            }
                            return et;
                        }
                    } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sid = sub_mem->object ? std::get_if<IdentExpr>(&sub_mem->object->node) : nullptr) {
                            if (const auto* en = resolve_enum_by_prefix_sema(sid->name + "." + sub_mem->field)) {
                                std::string ename = en->package_name.empty() ? en->name : (en->package_name + "." + en->name);
                                TypeId et = type_id_of(ename);
                                if (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string") {
                                    return cf_.type_table.make_optional(et);
                                }
                                return et;
                            }
                        }
                        if (cf_.enums.contains(sub_mem->field)) {
                            TypeId et = type_id_of(sub_mem->field);
                            if (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string") {
                                return cf_.type_table.make_optional(et);
                            }
                            return et;
                        }
                    }
                }
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (is_cpp_qualifier_sema(recv_id->name) && !is_ident_in_scope_sema(recv_id->name)) {
                        if (auto rtit = cf_.cpp_fn_returns.find(recv_id->name + "." + mem->field); rtit != cf_.cpp_fn_returns.end()) {
                            return map_cpp_qualtype_to_type_id(rtit->second);
                        }
                        if (auto rtit = cf_.cpp_fn_returns.find(mem->field); rtit != cf_.cpp_fn_returns.end()) {
                            return map_cpp_qualtype_to_type_id(rtit->second);
                        }
                        return TypeId::Foreign();
                    }
                    std::string ns_name = resolve_stdlib_ns_sema(recv_id->name);
                    if (ns_name.empty()) ns_name = recv_id->name;
                    if (!is_ident_in_scope_sema(recv_id->name)) {
                        if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(ns_name, mem->field)) {
                            const std::string& ur = std_sig->unwrapped_return_type;
                            if (!call->type_args.empty() && (ur == "T" || ur == "[]T" || ur == "T?")) {
                                TypeId ta = resolve_typeref(call->type_args[0]);
                                TypeId base = (ur == "[]T") ? cf_.type_table.make_slice(ta)
                                    : (ur == "T?") ? cf_.type_table.make_optional(ta) : ta;
                                return wrap_stdlib_ret(*std_sig, base);
                            }
                            if (ur.empty() || ur == "void") {
                                return wrap_stdlib_ret(*std_sig, TypeId::Void());
                            }
                            if (ur != "T") {
                                std::string ret_type = qualify_stdlib_type_sema(*std_sig, recv_id->name);
                                if (!call->type_args.empty()) {
                                    std::string targ = cf_.type_table.to_string(resolve_typeref(call->type_args[0]));
                                    if (auto p = ret_type.find("[T]"); p != std::string::npos && !targ.empty()) {
                                        ret_type.replace(p, 3, "[" + targ + "]");
                                    }
                                }
                                return wrap_stdlib_ret(*std_sig, type_id_of(ret_type));
                            }
                        }
                    }
                    if (!is_ident_in_scope_sema(recv_id->name) && is_unresolved_local_pkg_alias_sema(recv_id->name)) {
                        return TypeId::Error();
                    }
                    if (!is_ident_in_scope_sema(recv_id->name)) {
                        const FnDecl* fdecl = nullptr;
                        if (auto pit = cf_.fn_sigs.find(recv_id->name + "." + mem->field); pit != cf_.fn_sigs.end()) {
                            fdecl = &pit->second;
                        } else if (auto fit = cf_.fn_sigs.find(mem->field); fit != cf_.fn_sigs.end()) {
                            fdecl = &fit->second;
                        }
                        if (fdecl) {
                            return infer_fn_decl_ret(*fdecl);
                        }
                    }
                } else if (mem->object) {
                    if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                            std::string ns_name = resolve_stdlib_ns_sema(sub_id->name);
                            if (ns_name.empty()) ns_name = sub_id->name;
                            if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(ns_name, sub_mem->field + "." + mem->field)) {
                                TypeId base = (!std_sig->unwrapped_return_type.empty() && std_sig->unwrapped_return_type != "void")
                                    ? type_id_of(qualify_stdlib_type_sema(*std_sig, sub_id->name))
                                    : TypeId::Void();
                                return wrap_stdlib_ret(*std_sig, base);
                            }
                        }
                    }
                }
                if (mem->field == "bytes" && call->args.empty()) {
                    return type_id_of("[]byte");
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    std::unordered_map<std::string, TypeId> env;
                    if (mem->object) {
                        TypeId raw_rt = infer_expr_type_id_sema(mem->object);
                        TypeId unwrapped_rt = unwrap_receiver_type(raw_rt, /*unwrap_opt=*/true);
                        const auto& rd = cf_.type_table.get(unwrapped_rt);
                        if (rd.kind == TypeKind::GenericInst) {
                            if (sig->receiver) {
                                for (std::size_t i = 0; i < sig->receiver->type.type_args.size() && i < rd.type_args.size(); ++i) {
                                    if (!sig->receiver->type.type_args[i].name.empty()) {
                                        env[sig->receiver->type.type_args[i].name] = rd.type_args[i];
                                    }
                                }
                            }
                            if (const StructDecl* sdef = lookup_struct_def(rd.base_name)) {
                                for (std::size_t i = 0; i < sdef->type_params.size() && i < rd.type_args.size(); ++i) {
                                    env[sdef->type_params[i].name] = rd.type_args[i];
                                }
                            }
                        }
                    }
                    if (!sig->type_params.empty()) {
                        std::unordered_map<std::string, TypeId> pat_env = env;
                        for (std::size_t idx = 0; idx < sig->type_params.size(); ++idx) {
                            const auto& tp = sig->type_params[idx];
                            pat_env[tp.name] = cf_.type_table.make_type_param(tp.name, tp.constraints);
                            if (idx < call->type_args.size()) {
                                TypeId ta = resolve_typeref(call->type_args[idx]);
                                if (ta.is_valid()) env[tp.name] = ta;
                            }
                        }
                        for (std::size_t idx = 0; idx < call->args.size() && idx < sig->params.size(); ++idx) {
                            TypeId pat = cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(sig->params[idx].type, sig->package_name), &pat_env);
                            TypeId got = infer_expr_type_id_sema(call->args[idx].value);
                            (void)cf_.type_table.unify(pat, got, env);
                        }
                    }
                    TypeRef ret_tr = canonical_typeref_in_pkg_sema(sig->return_type, sig->package_name);
                    if (ret_tr.name.empty() && !ret_tr.is_slice && !ret_tr.is_array && !ret_tr.is_map &&
                        !ret_tr.is_tuple && !ret_tr.is_fn && ret_tr.smart_wrap.empty()) {
                        return resolve_fn_ret(sig->return_type, sig->package_name);
                    }
                    if (!env.empty()) {
                        return cf_.type_table.from_typeref(ret_tr, &env);
                    }
                    return resolve_typeref_in_pkg(ret_tr, sig->package_name);
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem)) {
                    return resolve_fn_ret(im->return_type, pkg_name_);
                }
                if (mem->object) {
                    TypeId raw_recv = infer_expr_type_id_sema(mem->object);
                    if (raw_recv.is_foreign() || cf_.type_table.contains_foreign(raw_recv)) {
                        return TypeId::Foreign();
                    }
                    TypeId un_recv = unwrap_receiver_type(raw_recv, /*unwrap_opt=*/true);
                    if (mem->field == "spawn" &&
                        (call->block_arg || (!call->args.empty() && call->args[0].value && std::holds_alternative<ClosureExpr>(call->args[0].value->node))) &&
                        !resolve_member_call_sig(*mem)) {
                        std::string base_check = un_recv.is_valid() && !un_recv.is_unknown() ? cf_.type_table.to_string(un_recv) : "";
                        if (base_check.empty() || base_check == "async.Scope" || base_check.ends_with(".Scope") || base_check == "Scope") {
                            std::string ret_t = "void";
                            if (!call->args.empty() && call->args[0].value) {
                                if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                    ret_t = cf_.type_table.to_string(resolve_typeref(*cl->return_type));
                                }
                            }
                            if (call->block_arg && call->block_arg->return_type) {
                                ret_t = cf_.type_table.to_string(resolve_typeref(*call->block_arg->return_type));
                            }
                            return type_id_of("async.Task[" + ret_t + "]");
                        }
                    }
                    std::string recv_t = un_recv.is_valid() && !un_recv.is_unknown() ? cf_.type_table.to_string(un_recv) : "";
                    if (!recv_t.empty()) {
                        const auto& idx = StdlibCatalogIndex::instance();
                        std::string base_recv = recv_t;
                        std::string generic_arg;
                        if (auto br = base_recv.find('['); br != std::string::npos && base_recv.ends_with(']')) {
                            generic_arg = base_recv.substr(br + 1, base_recv.size() - br - 2);
                            base_recv = base_recv.substr(0, br);
                        }
                        std::string unqual_recv = base_recv;
                        if (auto dot = unqual_recv.rfind('.'); dot != std::string::npos) {
                            unqual_recv = unqual_recv.substr(dot + 1);
                        }
                        if (auto dcol = unqual_recv.rfind("::"); dcol != std::string::npos) {
                            unqual_recv = unqual_recv.substr(dcol + 2);
                        }

                        auto find_ret_id = [&](const std::string& key) -> TypeId {
                            if (auto mit = idx.receiver_methods.find(key); mit != idx.receiver_methods.end()) {
                                const std::string& uret = mit->second.unwrapped_return_type;
                                if (!generic_arg.empty() && uret == "T") {
                                    return wrap_stdlib_ret(mit->second, type_id_of(generic_arg));
                                }
                                if (!generic_arg.empty() && uret == "[]T") {
                                    return wrap_stdlib_ret(mit->second, type_id_of("[]" + generic_arg));
                                }
                                if (uret.empty() || uret == "void") {
                                    return wrap_stdlib_ret(mit->second, TypeId::Void());
                                }
                                const auto dotq = recv_t.rfind('.');
                                std::string qual = (dotq == std::string::npos)
                                    ? local_qualifier_for_module_sema(mit->second.module_import, mit->second.ns_alias)
                                    : recv_t.substr(0, dotq);
                                std::string qtype = qualify_stdlib_type_sema(mit->second, qual);
                                return wrap_stdlib_ret(mit->second, type_id_of(qtype));
                            }
                            return TypeId::Unknown();
                        };

                        if (base_recv == "Error") {
                            if (mem->field == "code") return TypeId::Int32();
                            if (mem->field == "message") return TypeId::Str();
                        }

                        if (base_recv == "async.Task" && mem->field == "join") {
                            return generic_arg.empty() ? TypeId::Void() : type_id_of(generic_arg);
                        }
                        if (base_recv == "sync.Mutex") {
                            if (mem->field == "lock") return type_id_of("sync.MutexGuard[" + generic_arg + "]");
                            if (mem->field == "get") return type_id_of(generic_arg);
                            if (mem->field == "with_lock") {
                                if (call->block_arg && call->block_arg->return_type) return resolve_typeref(*call->block_arg->return_type);
                                if (!call->args.empty() && call->args[0].value) {
                                    if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                        return resolve_typeref(*cl->return_type);
                                    }
                                }
                                return generic_arg.empty() ? TypeId::Unknown() : type_id_of(generic_arg);
                            }
                        }
                        if (base_recv == "sync.MutexGuard" && mem->field == "get") {
                            return type_id_of(generic_arg);
                        }
                        if ((unqual_recv == "PriorityQueue" || unqual_recv == "MinQueue") && !generic_arg.empty()) {
                            if (mem->field == "pop" || mem->field == "peek") return type_id_of(generic_arg + "?");
                            if (mem->field == "top") return type_id_of(generic_arg);
                            if (mem->field == "empty") return TypeId::Bool();
                        }
                        if (base_recv == "sync.Atomic") {
                            if (mem->field == "load" || mem->field == "add" || mem->field == "sub" ||
                                mem->field == "fetch_add" || mem->field == "fetch_sub") {
                                return type_id_of(generic_arg.empty() ? "i64" : generic_arg);
                            }
                            if (mem->field == "cas") return TypeId::Bool();
                        }
                        if (mem->field == "spawn") {
                            std::string ret_t = "void";
                            if (!call->args.empty() && call->args[0].value) {
                                if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                    ret_t = cf_.type_table.to_string(resolve_typeref(*cl->return_type));
                                }
                            }
                            if (call->block_arg && call->block_arg->return_type) ret_t = cf_.type_table.to_string(resolve_typeref(*call->block_arg->return_type));
                            return type_id_of("async.Task[" + ret_t + "]");
                        }
                        if (mem->field == "len") {
                            if (unqual_recv == "Set" || unqual_recv == "Deque" || unqual_recv == "PriorityQueue" ||
                                unqual_recv == "MinQueue" || unqual_recv == "BTreeMap" || unqual_recv == "BitSet") {
                                return TypeId::Int64();
                            }
                        }

                        if (TypeId r = find_ret_id(recv_t + "." + mem->field); r.is_valid()) return r;
                        if (TypeId r = find_ret_id(base_recv + "." + mem->field); r.is_valid()) return r;
                        if (auto dot = base_recv.find('.'); dot != std::string::npos) {
                            if (std::string cns = resolve_stdlib_ns_sema(base_recv.substr(0, dot)); !cns.empty()) {
                                if (TypeId r = find_ret_id(cns + base_recv.substr(dot) + "." + mem->field); r.is_valid()) return r;
                            }
                        }
                        if (TypeId r = find_ret_id(unqual_recv + "." + mem->field); r.is_valid()) return r;
                    }
                }
            }
            if (call->callee) {
                TypeId ct = infer_expr_type_id_sema(call->callee);
                const auto& cd = cf_.type_table.get(ct);
                if (cd.kind == TypeKind::Function) return cd.ret_type;
                if (ct.is_foreign()) return TypeId::Foreign();
            }
        }
        // Receiver of unknown type (e.g. an untyped closure parameter `|req|`): if exactly one stdlib
        // receiver method has this name, its return type is unambiguous.
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->object && !mem->is_namespace) {
                if (infer_expr_type_id_sema(mem->object).is_unknown()) {
                    TypeId unique_ret = TypeId::Unknown();
                    bool ambiguous = false;
                    const std::string suffix = "." + mem->field;
                    for (const auto& [key, sig] : StdlibCatalogIndex::instance().receiver_methods) {
                        if (!key.ends_with(suffix) || key.find('.') != key.size() - suffix.size()) continue;
                        const std::string& r = sig.unwrapped_return_type;
                        if (r.empty() || r == "void" || r == "T" || r.find("T]") != std::string::npos) { ambiguous = true; break; }
                        TypeId cand = wrap_stdlib_ret(sig, type_id_of(r));
                        if (unique_ret.is_unknown()) unique_ret = cand;
                        else if (unique_ret != cand) { ambiguous = true; break; }
                    }
                    if (!ambiguous && !unique_ret.is_unknown()) return unique_ret;
                }
            }
        }
        return TypeId::Unknown();
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::infer_expr_type_id_sema(const ExprPtr& e) const -> TypeId {
        if (!e) return TypeId::Unknown();
        if (e->resolved_type_id != 0) return TypeId{e->resolved_type_id};
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return TypeId::Str();
            if (lit->kind == TokenKind::Char) return TypeId::Byte();
            if (lit->kind == TokenKind::Int) return TypeId::Int64();
            if (lit->kind == TokenKind::Float) return TypeId::Float64();
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return TypeId::Bool();
            return TypeId::Unknown();
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            TypeId op_t = infer_expr_type_id_sema(un->operand);
            if (un->op == "!" || un->op == "not") return TypeId::Bool();
            if (un->op == "&") return cf_.type_table.make_borrow(op_t);
            if (un->op == "&mut") return cf_.type_table.make_mut_borrow(op_t);
            if (un->op == "*") {
                TypeId ptr_t = cf_.type_table.unwrap_optional(op_t);
                const auto& d = cf_.type_table.get(ptr_t);
                if (d.elem_type.is_valid()) return d.elem_type;
            }
            return op_t;
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
                bin->op == ">" || bin->op == ">=" || bin->op == "&&" || bin->op == "||") {
                return TypeId::Bool();
            }
            TypeId lt = infer_expr_type_id_sema(bin->left);
            TypeId rt = infer_expr_type_id_sema(bin->right);
            if (lt.is_foreign() || rt.is_foreign()) return TypeId::Foreign();
            if (bin->op == "+" && (lt == TypeId::Str() || rt == TypeId::Str())) {
                return TypeId::Str();
            }
            if (bin->op == "+") {
                const TypeId str_borrow = type_id_of("&str");
                if (lt == str_borrow || rt == str_borrow) return TypeId::Str();
            }
            if (cf_.type_table.is_type_param(lt) && is_numeric_literal_expr(bin->right)) return lt;
            if (cf_.type_table.is_type_param(rt) && is_numeric_literal_expr(bin->left)) return rt;
            // an integer literal takes the other operand's type (`100 + f` and `f + 100` keep `f`'s type, as in C++);
            // an expression of literals only is `i64`. A shift has its left operand's type.
            if (bin->op != "<<" && bin->op != ">>") {
                if (cf_.type_table.is_integer(rt) && const_int_value(bin->left) && !const_int_value(bin->right)) return rt;
                if (cf_.type_table.is_integer(lt) && !const_int_value(bin->left) && const_int_value(bin->right)) return lt;
            }
            if (cf_.type_table.is_float(rt) && is_numeric_literal_expr(bin->left) && !is_numeric_literal_expr(bin->right)) return rt;
            if (cf_.type_table.is_float(lt) && !is_numeric_literal_expr(bin->left) && is_numeric_literal_expr(bin->right)) return lt;
            if (cf_.type_table.is_integer(lt) && cf_.type_table.is_integer(rt)) {
                return cf_.type_table.int_widens(lt, rt) ? lt : (cf_.type_table.int_widens(rt, lt) ? rt : lt);
            }
            if (lt.is_valid() && !lt.is_unknown()) return lt;
            if (rt.is_valid() && !rt.is_unknown()) return rt;
            return TypeId::Unknown();
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            if (arr->resolved_elem_type_id != 0) {
                return cf_.type_table.make_slice(TypeId{arr->resolved_elem_type_id});
            }
            // `[18, 26, 22]` is a slice of its first non-never element's type (no UNKNOWN-TYPE for literals).
            for (const auto& el : arr->elements) {
                TypeId elem = infer_expr_type_id_sema(el);
                if (elem.is_valid() && !elem.is_unknown() && !elem.is_never()) {
                    return cf_.type_table.make_slice(elem);
                }
            }
            return TypeId::Unknown();
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            TypeId obj_t = unwrap_receiver_type(infer_expr_type_id_sema(sl->object), /*unwrap_opt=*/false);
            if (obj_t == TypeId::Str()) return type_id_of("&str");
            if (obj_t.is_foreign()) return TypeId::Foreign();
            const auto& d = cf_.type_table.get(obj_t);
            if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array) {
                return cf_.type_table.make_slice(d.elem_type);
            }
            return TypeId::Unknown();
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            TypeId obj_t = infer_expr_type_id_sema(idx->object);
            const auto& raw_d = cf_.type_table.get(obj_t);
            if (raw_d.kind == TypeKind::RawPtr || raw_d.kind == TypeKind::MutPtr) return raw_d.elem_type;
            TypeId un_t = unwrap_receiver_type(obj_t, /*unwrap_opt=*/false);
            if (un_t.is_foreign()) return TypeId::Foreign();
            if (un_t == TypeId::Str()) return TypeId::Byte();
            const auto& d = cf_.type_table.get(un_t);
            if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array || d.kind == TypeKind::Map) {
                return d.elem_type;
            }
            if (d.kind == TypeKind::GenericInst) {
                if (d.base_name.ends_with("BTreeMap") && d.type_args.size() == 2) return d.type_args[1];
                if ((d.base_name.ends_with("Deque") || d.base_name.ends_with("Vector")) && !d.type_args.empty()) return d.type_args[0];
                if (d.base_name.ends_with("BitSet")) return TypeId::Bool();
            }
            if (d.kind == TypeKind::NominalStruct) {
                if (d.name == "json.Value") return un_t;
                if (auto dot = d.name.find('.'); dot != std::string::npos) {
                    if (resolve_stdlib_ns_sema(std::string_view(d.name.data(), dot)) == "json" &&
                        std::string_view(d.name).substr(dot + 1) == "Value") {
                        return un_t;
                    }
                }
            }
            return TypeId::Unknown();
        }
        if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) {
            std::vector<TypeId> ptypes;
            ptypes.reserve(cl->params.size());
            for (std::size_t i = 0; i < cl->params.size(); ++i) {
                if (i < cl->resolved_param_types.size() && cl->resolved_param_types[i] != 0) {
                    ptypes.push_back(TypeId{cl->resolved_param_types[i]});
                } else if (i < cl->param_types.size() && cl->param_types[i].has_value()) {
                    ptypes.push_back(resolve_typeref(*cl->param_types[i]));
                } else if (auto h = closure_param_hints_.find(cl); h != closure_param_hints_.end() && i < h->second.size() && !h->second[i].empty()) {
                    ptypes.push_back(type_id_of(h->second[i]));
                } else {
                    ptypes.push_back(TypeId::Unknown());
                }
            }
            TypeId ret = TypeId::Unknown();
            if (cl->resolved_return_type_id != 0) {
                ret = TypeId{cl->resolved_return_type_id};
            } else if (cl->return_type.has_value()) {
                if (cl->return_type->name == "void" || cl->return_type->name.empty()) {
                    ret = cl->return_type->is_fallible ? cf_.type_table.make_fallible(TypeId::Void())
                        : cl->return_type->is_optional ? cf_.type_table.make_optional(TypeId::Void())
                        : TypeId::Void();
                } else {
                    ret = resolve_typeref(*cl->return_type);
                }
            } else if (auto it = closure_ret_types_.find(cl); it != closure_ret_types_.end() && !it->second.empty()) {
                ret = type_id_of(it->second);
            } else {
                ret = TypeId::Void();
                for (const auto& s : cl->body) {
                    if (!s) continue;
                    if (const auto* rs = std::get_if<ReturnStmt>(&s->node); rs && rs->value) {
                        TypeId rt = infer_expr_type_id_sema(rs->value);
                        if (rt.is_valid() && !rt.is_unknown() && !rt.is_never()) {
                            ret = rt;
                            break;
                        }
                    }
                }
                if (ret.is_void() && !cl->body.empty() && cl->body.back()) {
                    if (const auto* es = std::get_if<ExprStmt>(&cl->body.back()->node); es && es->expr) {
                        TypeId rt = infer_expr_type_id_sema(es->expr);
                        if (rt.is_valid() && !rt.is_unknown() && !rt.is_never()) ret = rt;
                    }
                }
            }
            return cf_.type_table.make_function(std::move(ptypes), ret);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->resolved_type_id != 0) return TypeId{or_ex->resolved_type_id};
            TypeId inner_t = infer_expr_type_id_sema(or_ex->inner);
            if (or_ex->fallback_val) {
                TypeId fb_t = infer_expr_type_id_sema(or_ex->fallback_val);
                if ((cf_.type_table.is_fallible(inner_t) && cf_.type_table.is_fallible(fb_t)) ||
                    (cf_.type_table.is_optional(inner_t) && cf_.type_table.is_optional(fb_t))) {
                    return fb_t;
                }
            }
            if (inner_t.is_valid() && !inner_t.is_unknown()) {
                const auto& d = cf_.type_table.get(inner_t);
                if (d.kind == TypeKind::Fallible) return cf_.type_table.unwrap_fallible(inner_t);
                if (d.kind == TypeKind::Optional) return cf_.type_table.unwrap_optional(inner_t);
                return inner_t;
            }
            if (or_ex->fallback_val) {
                TypeId fb_t = infer_expr_type_id_sema(or_ex->fallback_val);
                if (fb_t.is_valid() && !fb_t.is_unknown()) return fb_t;
            }
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            TypeId inner_t = infer_expr_type_id_sema(tr->inner);
            if (inner_t.is_valid() && !inner_t.is_unknown()) {
                const auto& d = cf_.type_table.get(inner_t);
                if (d.kind == TypeKind::Fallible) return cf_.type_table.unwrap_fallible(inner_t);
                if (d.kind == TypeKind::Optional) return cf_.type_table.unwrap_optional(inner_t);
                return inner_t;
            }
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            if (me->resolved_type_id != 0) return TypeId{me->resolved_type_id};
            TypeId acc = TypeId::Unknown();
            for (const auto& arm : me->arms) {
                if (arm.body.empty() || !arm.body.back()) continue;
                if (const auto* expr_st = std::get_if<ExprStmt>(&arm.body.back()->node)) {
                    TypeId t = infer_expr_type_id_sema(expr_st->expr);
                    if (t.is_unknown() || t.is_never()) continue;
                    if (acc.is_unknown()) acc = t;
                    else if (cf_.type_table.is_integer(acc) && cf_.type_table.is_integer(t) &&
                             cf_.type_table.int_widens(t, acc)) {
                        acc = t;
                    } else if (cf_.type_table.get(t).kind == TypeKind::Optional &&
                               cf_.type_table.unwrap_optional(t) == acc) {
                        acc = t;
                    }
                }
            }
            if (!acc.is_unknown()) return acc;
            bool all_diverge = !me->arms.empty();
            for (const auto& arm : me->arms) {
                if (!block_diverges(arm.body, &cf_)) { all_diverge = false; break; }
            }
            if (all_diverge) return TypeId::Never();
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) {
                if (mem->field == "BITS") return TypeId::Uint32();
                if (find_builtin_assoc_const(primitive_bits(prim), mem->field)) return prim;
            }
            if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                oid && !is_ident_in_scope_sema(oid->name)) {
                if (oid->name == "math" && is_math_constant(mem->field)) {
                    return TypeId::Float64();
                }
                if (is_cpp_qualifier_sema(oid->name)) return TypeId::Foreign();
                if (is_unresolved_local_pkg_alias_sema(oid->name)) return TypeId::Error();
                std::string canon = resolve_stdlib_ns_sema(oid->name);
                if (!canon.empty()) {
                    if (auto cty = StdlibCatalogIndex::instance().find_ns_const_type(canon, mem->field); !cty.empty()) {
                        return type_id_of(cty);
                    }
                }
            }
            // fields of stdlib structs (`req.body: str`, `res.exit_code: i32`), from the catalog's type docs.
            if (mem->object) {
                TypeId raw_ot = infer_expr_type_id_sema(mem->object);
                if (raw_ot.is_foreign() || cf_.type_table.contains_foreign(raw_ot)) return TypeId::Foreign();
                TypeId un_ot = unwrap_receiver_type(raw_ot, /*unwrap_opt=*/true);
                const auto& uod = cf_.type_table.get(un_ot);
                std::string ot = cf_.type_table.to_string(un_ot);
                if (mem->field == "value") {
                    if (uod.kind == TypeKind::GenericInst &&
                        (uod.base_name == "sync.MutexGuard" || uod.base_name == "MutexGuard") &&
                        uod.type_args.size() == 1) {
                        return uod.type_args[0];
                    }
                    for (std::string_view pre : {"sync.MutexGuard[", "MutexGuard["}) {
                        if (ot.starts_with(pre) && ot.ends_with(']')) return type_id_of(ot.substr(pre.size(), ot.size() - pre.size() - 1));
                    }
                }
                if (auto dot = ot.find('.'); dot != std::string::npos && ot.find('.', dot + 1) == std::string::npos) {
                    const std::string raw_ns = ot.substr(0, dot);
                    std::string ns = resolve_stdlib_ns_sema(raw_ns);
                    if (ns.empty()) ns = raw_ns;
                    std::string tname = ot.substr(dot + 1);
                    if (auto lb = tname.find('['); lb != std::string::npos) tname = tname.substr(0, lb);
                    for (const auto& mod : cfire::doc::stdlib_catalog()) {
                        if (mod.ns_alias != ns) continue;
                        for (const auto& td : mod.type_docs) {
                            if (td.bare_name() != tname) continue;
                            for (const auto& f : td.fields) {
                                if (f.name != mem->field) continue;
                                std::string ft = f.type;
                                for (const auto& own : mod.types) {
                                    if (ft == "[]" + own) ft = "[]" + raw_ns + "." + own;
                                }
                                if (TypeId t = type_id_of(ft); t.is_valid() && !t.is_unknown()) return t;
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->field == "release" && smart_wrap_of(mem->object) == "own") {
                TypeId ot = infer_expr_type_id_sema(mem->object);
                if (ot.is_valid() && cf_.type_table.get(ot).kind == TypeKind::Own) return cf_.type_table.make_mut_ptr(cf_.type_table.get(ot).elem_type);
                return cf_.type_table.make_mut_ptr(type_id_of(type_name_of(mem->object)));
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->field == "from_raw") {
                if (std::string own_t = own_type_prefix(mem->object); !own_t.empty()) return type_id_of(own_t);
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->field == "parse") {
                if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) return cf_.type_table.make_fallible(prim);
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && !bm->ret.empty()) {
                    TypeId self_tid = builtin_self_type(mem->object);
                    if (bm->name == "ignore_error" && self_tid.is_valid() &&
                        cf_.type_table.get(self_tid).kind == TypeKind::Fallible &&
                        cf_.type_table.get(self_tid).elem_type == TypeId::Void()) {
                        return TypeId::Unknown();
                    }
                    TypeId rt = resolve_builtin_type(bm->ret, self_tid);
                    if (rt.is_valid()) return rt;
                }
                if (mem->field == "len") return TypeId::Usize();
                if (mem->field == "bytes" && call->args.empty()) return type_id_of("[]byte");
                if ((mem->field == "byte_ptr" || mem->field == "byte_ptr_at") && mem->object) {
                    TypeId recv = unwrap_receiver_type(infer_expr_type_id_sema(mem->object), /*unwrap_opt=*/true);
                    const auto& rd = cf_.type_table.get(recv);
                    if ((rd.kind == TypeKind::Slice || rd.kind == TypeKind::Array) && !rd.elem_type.is_unknown()) {
                        return cf_.type_table.make_raw_ptr(rd.elem_type);
                    }
                    return type_id_of("*c_char");
                }
                if ((mem->field == "data_ptr" || mem->field == "data") && call->args.empty() && mem->object) {
                    TypeId recv = unwrap_receiver_type(infer_expr_type_id_sema(mem->object), /*unwrap_opt=*/true);
                    if (recv == TypeId::Str()) return type_id_of("*c_char");
                    const auto& rd = cf_.type_table.get(recv);
                    if ((rd.kind == TypeKind::Slice || rd.kind == TypeKind::Array) && !rd.elem_type.is_unknown()) {
                        return cf_.type_table.make_raw_ptr(rd.elem_type);
                    }
                }
            }
        }
        return infer_named_type_id_(e);
    }


    // Display / lookup name of an expression's type, derived from its TypeId ("" when unknown).
    CFIRE_SEMA_INLINE auto SemaFnChecker::type_name_of(const ExprPtr& e) const -> std::string {
        TypeId id = infer_expr_type_id_sema(e);
        if (id.is_unknown()) return "";
        return cf_.type_table.to_string(id);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_typeref(const TypeRef& tr) const -> TypeId {
        return resolve_typeref_in_pkg(tr, pkg_name_);
    }

    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_typeref_in_pkg(const TypeRef& tr, std::string_view owner_pkg) const -> TypeId {
        if (tr.is_fallible) {
            TypeRef inner = tr;
            inner.is_fallible = false;
            return cf_.type_table.make_fallible(resolve_typeref_in_pkg(inner, owner_pkg));
        }
        if (tr.is_optional) {
            TypeRef inner = tr;
            inner.is_optional = false;
            return cf_.type_table.make_optional(resolve_typeref_in_pkg(inner, owner_pkg));
        }
        if (tr.is_fn) {
            std::vector<TypeId> params;
            params.reserve(tr.fn_params.size());
            for (const auto& pt : tr.fn_params) params.push_back(resolve_typeref_in_pkg(pt, owner_pkg));
            TypeId ret = TypeId::Void();
            if (tr.fn_ret) {
                if (tr.fn_ret->name.empty() && !tr.fn_ret->is_slice && !tr.fn_ret->is_array && !tr.fn_ret->is_map &&
                    !tr.fn_ret->is_tuple && !tr.fn_ret->is_fn && tr.fn_ret->smart_wrap.empty()) {
                    ret = TypeId::Void();
                    if (tr.fn_ret->is_optional) ret = cf_.type_table.make_optional(ret);
                    if (tr.fn_ret->is_fallible) ret = cf_.type_table.make_fallible(ret);
                } else {
                    ret = resolve_typeref_in_pkg(*tr.fn_ret, owner_pkg);
                }
            }
            return cf_.type_table.make_function(std::move(params), ret);
        }
        if (tr.is_tuple) {
            std::vector<TypeId> elems;
            elems.reserve(tr.tuple_types.size());
            for (const auto& tt : tr.tuple_types) elems.push_back(resolve_typeref_in_pkg(tt, owner_pkg));
            return cf_.type_table.make_tuple(std::move(elems));
        }
        if (tr.is_slice) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name.starts_with("[]") ? std::string_view(tr.name).substr(2) : std::string_view(tr.name), owner_pkg), &fn_type_param_ids_);
            return cf_.type_table.make_slice(elem);
        }
        if (tr.is_array) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name, owner_pkg), &fn_type_param_ids_);
            std::size_t len = 0;
            if (!tr.array_len.empty()) {
                len = parse_int_literal<std::size_t>(tr.array_len).value_or(0);
            }
            return cf_.type_table.make_array(elem, len);
        }
        if (tr.is_map) {
            TypeId k = tr.key_type ? resolve_typeref_in_pkg(*tr.key_type, owner_pkg) : TypeId::Str();
            TypeId v = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg) : TypeId::Unknown();
            return cf_.type_table.make_map(k, v);
        }
        if (!tr.smart_wrap.empty()) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name, owner_pkg), &fn_type_param_ids_);
            if (tr.smart_wrap == "own") return cf_.type_table.make_own(elem);
            if (tr.smart_wrap == "shared") return cf_.type_table.make_shared(elem);
        }
        if (tr.is_borrow) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name, owner_pkg), &fn_type_param_ids_);
            return cf_.type_table.make_borrow(elem);
        }
        if (tr.is_mut_borrow) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name, owner_pkg), &fn_type_param_ids_);
            return cf_.type_table.make_mut_borrow(elem);
        }
        if (tr.is_ptr) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name, owner_pkg), &fn_type_param_ids_);
            return cf_.type_table.make_raw_ptr(elem);
        }
        if (tr.is_mut_ptr) {
            TypeId elem = tr.elem_type ? resolve_typeref_in_pkg(*tr.elem_type, owner_pkg)
                : cf_.type_table.from_name(canonical_type_name_in_pkg_sema(tr.name, owner_pkg), &fn_type_param_ids_);
            return cf_.type_table.make_mut_ptr(elem);
        }
        if (!tr.type_args.empty()) {
            std::string canon_base = canonical_nominal_path_in_pkg_sema(tr.name, owner_pkg);
            if (canon_base == "cpp" || canon_base.starts_with("cpp.") || canon_base.starts_with("cpp::")) {
                return TypeId::Foreign();
            }
            if (auto dot = canon_base.find('.'); dot != std::string::npos) {
                if (is_cpp_qualifier_sema(std::string_view(canon_base.data(), dot))) {
                    return TypeId::Foreign();
                }
            } else if (auto dcol = canon_base.find("::"); dcol != std::string::npos) {
                if (is_cpp_qualifier_sema(std::string_view(canon_base.data(), dcol))) {
                    return TypeId::Foreign();
                }
            }
            std::vector<TypeId> args;
            args.reserve(tr.type_args.size());
            for (const auto& ta : tr.type_args) args.push_back(resolve_typeref_in_pkg(ta, owner_pkg));
            return cf_.type_table.make_generic_inst(canon_base, std::move(args));
        }
        if (tr.name.empty()) return TypeId::Unknown();
        if (tr.name == "void") return TypeId::Void();
        if (auto tpit = fn_type_param_ids_.find(tr.name); tpit != fn_type_param_ids_.end()) {
            return tpit->second;
        }
        std::string canon_name = canonical_type_name_in_pkg_sema(tr.name, owner_pkg);
        if (cf_.cpp_qualifiers.contains(canon_name) || canon_name.starts_with("cpp.")) {
            return TypeId::Foreign();
        }
        if (auto dot = canon_name.find('.'); dot != std::string::npos) {
            if (is_cpp_qualifier_sema(std::string_view(canon_name.data(), dot))) {
                return TypeId::Foreign();
            }
        }
        return cf_.type_table.from_name(canon_name, fn_type_param_ids_.empty() ? nullptr : &fn_type_param_ids_);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::unwrap_receiver_type(TypeId t, bool unwrap_opt) const -> TypeId {
        for (int depth = 0; depth < 8 && t.is_valid(); ++depth) {
            const auto& d = cf_.type_table.get(t);
            if (d.kind == TypeKind::Own || d.kind == TypeKind::Shared ||
                d.kind == TypeKind::Borrow || d.kind == TypeKind::MutBorrow ||
                d.kind == TypeKind::RawPtr || d.kind == TypeKind::MutPtr) {
                t = d.elem_type;
                continue;
            }
            if (unwrap_opt && d.kind == TypeKind::Optional) {
                t = d.elem_type;
                continue;
            }
            break;
        }
        return t;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::map_cpp_qualtype_to_type_id(std::string_view qtype) const -> TypeId {
        auto trim = [](std::string_view sv) -> std::string_view {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
            return sv;
        };
        std::string_view s = trim(qtype);
        if (s.empty()) return TypeId::Foreign();
        while (s.ends_with('&')) s = trim(s.substr(0, s.size() - 1));
        if (s.ends_with('*')) {
            std::string_view pointee = trim(s.substr(0, s.size() - 1));
            bool is_const = false;
            if (pointee.starts_with("const ")) {
                is_const = true;
                pointee = trim(pointee.substr(6));
            } else if (pointee.ends_with(" const")) {
                is_const = true;
                pointee = trim(pointee.substr(0, pointee.size() - 6));
            }
            TypeId pe = TypeId::Unknown();
            if (pointee == "char") pe = cf_.type_table.from_name("c_char");
            else if (pointee == "void") pe = cf_.type_table.from_name("c_void");
            else pe = map_cpp_qualtype_to_type_id(pointee);
            return is_const ? cf_.type_table.make_raw_ptr(pe) : cf_.type_table.make_mut_ptr(pe);
        }
        if (s.starts_with("const ")) s = trim(s.substr(6));
        if (s.starts_with("volatile ")) s = trim(s.substr(9));
        if (s.ends_with(" const")) s = trim(s.substr(0, s.size() - 6));
        if (s == "void") return TypeId::Void();
        if (s == "bool" || s == "_Bool") return TypeId::Bool();
        if (s == "int8_t" || s == "std::int8_t" || s == "signed char" || s == "char") return TypeId::Int8();
        if (s == "uint8_t" || s == "std::uint8_t" || s == "unsigned char") return TypeId::Uint8();
        if (s == "int16_t" || s == "std::int16_t" || s == "short" || s == "short int" || s == "signed short") return TypeId::Int16();
        if (s == "uint16_t" || s == "std::uint16_t" || s == "unsigned short" || s == "unsigned short int") return TypeId::Uint16();
        if (s == "int32_t" || s == "std::int32_t" || s == "int" || s == "signed int" || s == "signed") return TypeId::Int32();
        if (s == "uint32_t" || s == "std::uint32_t" || s == "unsigned int" || s == "unsigned") return TypeId::Uint32();
        if (s == "int64_t" || s == "std::int64_t" || s == "long long" || s == "long long int" || s == "signed long long") return TypeId::Int64();
        if (s == "uint64_t" || s == "std::uint64_t" || s == "unsigned long long" || s == "unsigned long long int") return TypeId::Uint64();
        if (s == "long" || s == "long int" || s == "ssize_t" || s == "ptrdiff_t" || s == "std::ptrdiff_t" || s == "intptr_t") return TypeId::Isize();
        if (s == "unsigned long" || s == "unsigned long int" || s == "size_t" || s == "std::size_t" || s == "uintptr_t") return TypeId::Usize();
        if (s == "float") return TypeId::Float32();
        if (s == "double") return TypeId::Float64();
        if (s == "std::string" || s == "std::basic_string<char>") return TypeId::Str();
        if (s == "std::string_view" || s == "std::basic_string_view<char>") return type_id_of("&str");
        return TypeId::Foreign();
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_type_comparable_sema(TypeId t) const -> bool {
        std::unordered_set<uint32_t> visited;
        auto check = [&](auto& self, TypeId cur) -> bool {
            if (cur.is_unknown() || cur.is_error() || cur.is_foreign() || cur.is_never()) return true;
            if (cur == TypeId::Bool() || cur == TypeId::Str() || cf_.type_table.is_numeric(cur)) return true;
            if (!visited.insert(cur.index).second) return true;
            const auto& d = cf_.type_table.get(cur);
            switch (d.kind) {
                case TypeKind::NominalEnum:
                case TypeKind::RawPtr:
                case TypeKind::MutPtr:
                    return true;
                case TypeKind::Borrow:
                case TypeKind::MutBorrow:
                case TypeKind::Optional:
                case TypeKind::Slice:
                case TypeKind::Array:
                    return self(self, d.elem_type);
                case TypeKind::Map:
                    return self(self, d.key_type) && self(self, d.elem_type);
                case TypeKind::Tuple:
                    for (TypeId p : d.param_types) {
                        if (!self(self, p)) return false;
                    }
                    return true;
                case TypeKind::NominalStruct: {
                    auto sit = cf_.struct_defs.find(d.name);
                    if (sit == cf_.struct_defs.end()) {
                        std::string canon = d.name;
                        if (auto dot = d.name.find('.'); dot != std::string::npos) {
                            if (std::string ns = resolve_stdlib_ns_sema(std::string_view(d.name.data(), dot)); !ns.empty()) {
                                canon = ns + d.name.substr(dot);
                            }
                        }
                        return canon == "time.Time" || canon == "time.Duration" ||
                               canon == "fs.FileInfo" || canon == "ipc.PeerCred";
                    }
                    for (const auto& f : sit->second.fields) {
                        if (!self(self, resolve_typeref_in_pkg(f.type, sit->second.package_name))) return false;
                    }
                    return true;
                }
                case TypeKind::GenericInst: {
                    auto sit = cf_.struct_defs.find(d.base_name);
                    if (sit == cf_.struct_defs.end()) return false;
                    std::unordered_map<std::string, TypeId> env;
                    for (std::size_t i = 0; i < sit->second.type_params.size() && i < d.type_args.size(); ++i) {
                        env[sit->second.type_params[i].name] = d.type_args[i];
                    }
                    for (const auto& f : sit->second.fields) {
                        if (!self(self, cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(f.type, sit->second.package_name), &env))) return false;
                    }
                    return true;
                }
                case TypeKind::TypeParam:
                    return cf_.type_table.has_constraint(cur, "Comparable");
                default:
                    return false;
            }
        };
        return check(check, t);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_type_ordered_sema(TypeId t) const -> bool {
        std::unordered_set<uint32_t> visited;
        auto check = [&](auto& self, TypeId cur) -> bool {
            if (cur.is_unknown() || cur.is_error() || cur.is_foreign() || cur.is_never()) return true;
            if (cur == TypeId::Str() || cf_.type_table.is_numeric(cur)) return true;
            if (!visited.insert(cur.index).second) return true;
            const auto& d = cf_.type_table.get(cur);
            switch (d.kind) {
                case TypeKind::Borrow:
                case TypeKind::MutBorrow:
                    return d.elem_type == TypeId::Str();
                case TypeKind::Tuple:
                    for (TypeId p : d.param_types) {
                        if (!self(self, p)) return false;
                    }
                    return true;
                case TypeKind::NominalStruct: {
                    auto sit = cf_.struct_defs.find(d.name);
                    if (sit == cf_.struct_defs.end()) {
                        std::string canon = d.name;
                        if (auto dot = d.name.find('.'); dot != std::string::npos) {
                            if (std::string ns = resolve_stdlib_ns_sema(std::string_view(d.name.data(), dot)); !ns.empty()) {
                                canon = ns + d.name.substr(dot);
                            }
                        }
                        return canon == "time.Time" || canon == "time.Duration";
                    }
                    for (const auto& f : sit->second.fields) {
                        if (!self(self, resolve_typeref_in_pkg(f.type, sit->second.package_name))) return false;
                    }
                    return true;
                }
                case TypeKind::GenericInst: {
                    auto sit = cf_.struct_defs.find(d.base_name);
                    if (sit == cf_.struct_defs.end()) return false;
                    std::unordered_map<std::string, TypeId> env;
                    for (std::size_t i = 0; i < sit->second.type_params.size() && i < d.type_args.size(); ++i) {
                        env[sit->second.type_params[i].name] = d.type_args[i];
                    }
                    for (const auto& f : sit->second.fields) {
                        if (!self(self, cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(f.type, sit->second.package_name), &env))) return false;
                    }
                    return true;
                }
                case TypeKind::TypeParam:
                    return cf_.type_table.has_constraint(cur, "Ordered");
                default:
                    return false;
            }
        };
        return check(check, t);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::satisfies_constraint_sema(TypeId concrete, std::string_view constraint) const -> bool {
        if (concrete.is_unknown() || concrete.is_error() || concrete.is_foreign() ||
            cf_.type_table.contains_foreign(concrete) || cf_.type_table.contains_error(concrete)) {
            return true;
        }
        if (cf_.type_table.is_type_param(concrete)) {
            if (cf_.type_table.has_constraint(concrete, constraint)) return true;
            const auto& td = cf_.type_table.get(concrete);
            for (const auto& b : td.constraints) {
                if (b == constraint) return true;
            }
            if (auto it = fn_type_param_bounds_.find(td.name); it != fn_type_param_bounds_.end()) {
                for (const auto& b : it->second) {
                    if (b == constraint) return true;
                }
            }
            return false;
        }
        auto iit = cf_.interface_defs.find(std::string(constraint));
        if (iit == cf_.interface_defs.end()) {
            if (constraint == "Comparable" || constraint == "Equatable") return is_type_comparable_sema(concrete);
            if (constraint == "Ordered") return is_type_ordered_sema(concrete);
            if (constraint == "Numeric") return cf_.type_table.is_numeric_or_constrained(concrete);
            if (constraint == "Integer") return cf_.type_table.is_integer_or_constrained(concrete);
            return false;
        }
        TypeId un_t = unwrap_receiver_type(concrete, /*unwrap_opt=*/false);
        const auto& ud = cf_.type_table.get(un_t);
        if (ud.kind == TypeKind::Optional || ud.kind == TypeKind::Fallible) return false;
        std::string lookup_owner = canonical_type_name_sema(ud.kind == TypeKind::GenericInst ? ud.base_name : ud.name);
        if (auto lb = lookup_owner.find('['); lb != std::string::npos) lookup_owner = lookup_owner.substr(0, lb);
        std::string mangled_owner = lookup_owner;
        if (auto dot = lookup_owner.find('.'); dot != std::string::npos) {
            mangled_owner = std::format("cfire_pkg_{}__{}", lookup_owner.substr(0, dot), lookup_owner.substr(dot + 1));
        }
        auto find_impl_method = [&](const std::string& mname) -> const FnDecl* {
            if (auto mit = cf_.methods.find(mangled_owner + "::" + mname); mit != cf_.methods.end()) return &mit->second;
            if (auto mit = cf_.methods.find(lookup_owner + "::" + mname); mit != cf_.methods.end()) return &mit->second;
            if (auto dot = lookup_owner.find('.'); dot != std::string::npos) {
                if (auto mit = cf_.methods.find(lookup_owner.substr(dot + 1) + "::" + mname); mit != cf_.methods.end()) return &mit->second;
            }
            return nullptr;
        };
        for (const auto& m : iit->second.methods) {
            const FnDecl* impl = find_impl_method(m.name);
            if (!impl) return false;
            if (!m.is_mut_receiver && impl->receiver && impl->receiver->intent == "mut") return false;
            if (impl->params.size() != m.param_types.size()) return false;
            for (std::size_t k = 0; k < m.param_types.size(); ++k) {
                std::string exp_intent = (k < m.param_intents.size() && !m.param_intents[k].empty()) ? m.param_intents[k] : std::string("in");
                if (impl->params[k].intent != exp_intent) return false;
                if (canonical_type_name_sema(format_type_ref_sema(impl->params[k].type)) !=
                    canonical_type_name_sema(format_type_ref_sema(m.param_types[k]))) {
                    return false;
                }
            }
            if (canonical_type_name_sema(format_type_ref_sema(impl->return_type)) !=
                canonical_type_name_sema(format_type_ref_sema(m.return_type))) {
                return false;
            }
        }
        return true;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_type_compatible(
        TypeId want,
        TypeId got,
        const ExprPtr& expr,
        std::string_view ctx,
        int line,
        int col
    ) -> std::expected<void, std::string> {
        if (!want.is_valid() || !got.is_valid() || want.is_unknown() || got.is_unknown()) return {};
        if (want.is_error() || got.is_error() || want.is_never() || got.is_never() ||
            want.is_foreign() || got.is_foreign() ||
            cf_.type_table.contains_error(want) || cf_.type_table.contains_error(got) ||
            cf_.type_table.contains_foreign(want) || cf_.type_table.contains_foreign(got)) {
            return {};
        }
        const auto& wd = cf_.type_table.get(want);
        const auto& gd = cf_.type_table.get(got);
        if (expr) {
            if (const auto* lit = std::get_if<LiteralExpr>(&expr->node); lit && lit->kind == TokenKind::KwNone) {
                if (wd.kind == TypeKind::Optional || wd.kind == TypeKind::RawPtr || wd.kind == TypeKind::MutPtr) {
                    return {};
                }
                return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH",
                    std::format("{} expects '{}', got 'none'", ctx, cf_.type_table.to_string(want))));
            }
        }
        if (gd.kind == TypeKind::Optional && wd.kind != TypeKind::Optional) {
            if (cf_.type_table.is_assignable(want, gd.elem_type) ||
                (wd.kind == TypeKind::Interface && satisfies_constraint_sema(gd.elem_type, wd.name))) {
                return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL",
                    std::format("{} expects '{}', got optional '{}' without unwrapping; use 'or' to provide a fallback",
                        ctx, cf_.type_table.to_string(want), cf_.type_table.to_string(got))));
            }
        }
        if (gd.kind == TypeKind::Fallible && wd.kind != TypeKind::Fallible) {
            return emit_error(diag_at(line, col, "cfire/UNHANDLED-RESULT",
                std::format("{} expects '{}', got fallible result '{}' without unwrapping with 'try' or 'or'",
                    ctx, cf_.type_table.to_string(want), cf_.type_table.to_string(got))));
        }
        if (expr) {
            bool handled = false;
            if (auto r = check_value_for(want, expr, handled); !r) return r;
            if (handled) return {};
        }
        if (wd.kind == TypeKind::Interface) {
            if (got == want || satisfies_constraint_sema(got, wd.name)) return {};
        }
        if (wd.kind == TypeKind::Borrow && cf_.type_table.get(wd.elem_type).kind == TypeKind::Interface) {
            const auto& idesc = cf_.type_table.get(wd.elem_type);
            if (got == want || got == wd.elem_type || satisfies_constraint_sema(got, idesc.name)) return {};
        }
        if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got)) return {};
        if (!cf_.type_table.is_assignable(want, got)) {
            return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH",
                std::format("{} expects '{}', got '{}'{}",
                    ctx, cf_.type_table.to_string(want), cf_.type_table.to_string(got),
                    nominal_mismatch_hint_sema(want, got))));
        }
        return {};
    }


    // `byte` is `u8`, so a literal in 0..=255 or a `u8` value goes into a `[]byte` as is; a wider
    // integer needs an explicit `byte(x)`, so a push never truncates silently.
    CFIRE_SEMA_INLINE auto SemaFnChecker::is_non_byte_int_or_u8_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind != TokenKind::Int) return false;
            auto v = parse_int_literal<std::int64_t>(lit->value);
            return !v || *v < 0 || *v > 255;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "-") {
                if (const auto* lit = un->operand ? std::get_if<LiteralExpr>(&un->operand->node) : nullptr; lit && lit->kind == TokenKind::Int) {
                    return lit->value != "0";
                }
            }
            if (un->op == "-" || un->op == "+") {
                return is_non_byte_int_or_u8_expr(un->operand);
            }
        }
        std::string t = type_name_of(e);
        return t == "i8" || t == "i16" || t == "i32" || t == "i64" ||
               t == "u16" || t == "u32" || t == "u64" || t == "usize" || t == "isize" || t == "int";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_byte_slice_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return byte_slice_vars_.contains(id->name);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    if (auto sit = cf_.struct_tags.find(owner_t); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return (f.field_type.is_slice || f.field_type.is_array) && f.field_type.name == "byte";
                            }
                        }
                    }
                }
            }
        }
        return type_name_of(e) == "[]byte";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::infer_expr_null_state(const ExprPtr& e) const -> std::optional<NullState> {
        if (!e) return std::nullopt;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::KwNone) {
                return NullState::Null;
            }
            return std::nullopt;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut") {
                return NullState::NonNull;
            }
            return std::nullopt;
        }
        if (expr_returns_optional_sema(e)) {
            if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
                if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                    return it->second;
                }
            }
            return NullState::MaybeNull;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "make" && call->type_args.size() == 1 && !call->type_args[0].smart_wrap.empty()) {
                    return NullState::NonNull;
                }
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->field == "data" || mem->field == "data_ptr" ||
                    mem->field == "byte_ptr" || mem->field == "byte_ptr_at") {
                    return NullState::NonNull;
                }
            }
            return std::nullopt;
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::extract_null_check(const ExprPtr& cond) -> std::optional<std::pair<std::string, bool>> {
        if (!cond) return std::nullopt;
        if (const auto* bin = std::get_if<BinaryExpr>(&cond->node)) {
            if (bin->op == "!=" || bin->op == "==") {
                bool is_ne = (bin->op == "!=");
                if (bin->left && bin->right) {
                    const auto* l_id = std::get_if<IdentExpr>(&bin->left->node);
                    const auto* r_lit = std::get_if<LiteralExpr>(&bin->right->node);
                    if (l_id && r_lit && r_lit->kind == TokenKind::KwNone) {
                        return std::make_pair(l_id->name, is_ne);
                    }
                    const auto* r_id = std::get_if<IdentExpr>(&bin->right->node);
                    const auto* l_lit = std::get_if<LiteralExpr>(&bin->left->node);
                    if (r_id && l_lit && l_lit->kind == TokenKind::KwNone) {
                        return std::make_pair(r_id->name, is_ne);
                    }
                }
            }
        }
        return std::nullopt;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::paths_conflict(std::string_view a, std::string_view b) -> bool {
        if (a == b) return true;
        if (a.starts_with(b)) {
            char next = a[b.size()];
            if (next == '.' || next == '[') return true;
        }
        if (b.starts_with(a)) {
            char next = b[a.size()];
            if (next == '.' || next == '[') return true;
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::expr_start_loc(const ExprPtr& e) -> std::pair<int, int> {
        if (!e) return {1, 1};
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (call->callee) return expr_start_loc(call->callee);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->object) return expr_start_loc(mem->object);
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            if (idx->object) return expr_start_loc(idx->object);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (sl->object) return expr_start_loc(sl->object);
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->left) return expr_start_loc(bin->left);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->inner) return expr_start_loc(or_ex->inner);
        }
        return {e->line, e->col};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::format_type_ref_sema(const TypeRef& t) -> std::string {
        std::string s;
        if (t.is_borrow) s += "&";
        else if (t.is_mut_borrow) s += "&mut ";
        else if (t.is_ptr) s += "*";
        else if (t.is_mut_ptr) s += "*mut ";
        if (t.is_fn) {
            s += "fn(";
            for (std::size_t i = 0; i < t.fn_params.size(); ++i) {
                if (i > 0) s += ", ";
                s += format_type_ref_sema(t.fn_params[i]);
            }
            s += ")";
            if (t.fn_ret && !t.fn_ret->name.empty() && t.fn_ret->name != "void") {
                s += " -> " + format_type_ref_sema(*t.fn_ret);
            } else if (t.fn_ret && (t.fn_ret->is_fallible || t.fn_ret->is_optional)) {
                s += " -> " + format_type_ref_sema(*t.fn_ret);
            }
            if (t.is_optional) s += "?";
            if (t.is_fallible) s += "!";
            return s;
        }
        if (t.is_slice) {
            s += "[]";
            if (t.elem_type) {
                s += format_type_ref_sema(*t.elem_type);
            } else if (t.name.starts_with("[]")) {
                s += t.name.substr(2);
            } else {
                s += t.name.empty() ? "void" : t.name;
            }
        } else if (t.is_array) {
            s += "[" + t.array_len + "]";
            if (t.elem_type) {
                s += format_type_ref_sema(*t.elem_type);
            } else {
                s += t.name.empty() ? "void" : t.name;
            }
        } else if (t.is_tuple) {
            s += "(";
            for (std::size_t i = 0; i < t.tuple_types.size(); ++i) {
                if (i > 0) s += ", ";
                s += format_type_ref_sema(t.tuple_types[i]);
            }
            s += ")";
        } else if (t.is_map) {
            s += "map[" + (t.key_type ? format_type_ref_sema(*t.key_type) : std::string("str")) + "]" +
                 (t.elem_type ? format_type_ref_sema(*t.elem_type) : std::string("void"));
        } else if (!t.smart_wrap.empty() && t.elem_type) {
            s += t.smart_wrap + "[" + format_type_ref_sema(*t.elem_type) + "]";
        } else if (!t.type_args.empty()) {
            s += t.name.empty() ? "void" : t.name;
            s += "[";
            for (std::size_t i = 0; i < t.type_args.size(); ++i) {
                if (i > 0) s += ", ";
                s += format_type_ref_sema(t.type_args[i]);
            }
            s += "]";
        } else if (t.elem_type && (t.is_ptr || t.is_mut_ptr || t.is_borrow || t.is_mut_borrow)) {
            s += format_type_ref_sema(*t.elem_type);
        } else if (t.elem_type) {
            s += t.name.empty() ? "void" : t.name;
            s += "[" + format_type_ref_sema(*t.elem_type) + "]";
        } else {
            s += t.name.empty() ? "void" : t.name;
        }
        if (t.is_optional) s += "?";
        if (t.is_fallible) s += "!";
        return s;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_stdlib_ns_imported(std::string_view ns) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(std::string(ns))) return false;
        if (ns == "build" || ns == "testing") return true;
        if (ns == "cpp") return !cf_.cpp_qualifiers.empty();
        if (is_cpp_qualifier_sema(ns)) return true;
        if (is_vendor_alias_sema(ns)) return true;
        if (cf_.core_aliases.contains(std::string(ns))) return true;
        const auto& idx = StdlibCatalogIndex::instance();
        auto mod_it = idx.ns_to_import.find(std::string(ns));
        std::string_view expected_mod = (mod_it != idx.ns_to_import.end()) ? std::string_view(mod_it->second) : std::string_view{};
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.alias.empty() && imp.alias == ns) return true;
            if (!expected_mod.empty() && imp.path == expected_mod) return true;
            auto colon = imp.path.rfind(':');
            auto slash = imp.path.rfind('/');
            std::size_t sep = std::string::npos;
            if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
            else if (colon != std::string::npos) sep = colon;
            else sep = slash;
            std::string_view tail = (sep == std::string::npos) ? std::string_view(imp.path) : std::string_view(imp.path).substr(sep + 1);
            if (tail == ns) return true;
        }
        return false;
    }


    // is `t` a type Sema fully understands (so a mismatch is certain, not a gap in inference)?
    CFIRE_SEMA_INLINE auto SemaFnChecker::is_concrete_checkable_type(TypeId t) const -> bool {
        if (t.is_unknown() || t.is_error() || t.is_foreign()) return false;
        if (t == TypeId::Void() || t == TypeId::Never()) return true;
        if (t.index >= TypeId::Bool().index && t.index <= TypeId::Str().index) return true;
        const auto& d = cf_.type_table.get(t);
        switch (d.kind) {
            case TypeKind::Byte:
                return true;
            case TypeKind::Borrow:
            case TypeKind::MutBorrow:
            case TypeKind::RawPtr:
            case TypeKind::MutPtr:
            case TypeKind::Slice:
            case TypeKind::Array:
            case TypeKind::Optional:
            case TypeKind::Fallible:
            case TypeKind::Own:
            case TypeKind::Shared:
                return is_concrete_checkable_type(d.elem_type);
            case TypeKind::Map:
                return is_concrete_checkable_type(d.key_type) && is_concrete_checkable_type(d.elem_type);
            case TypeKind::Tuple:
                for (TypeId p : d.param_types) {
                    if (!is_concrete_checkable_type(p)) return false;
                }
                return true;
            case TypeKind::Function:
                if (!is_concrete_checkable_type(d.ret_type)) return false;
                for (TypeId p : d.param_types) {
                    if (!is_concrete_checkable_type(p)) return false;
                }
                return true;
            case TypeKind::TypeParam:
                return true;
            case TypeKind::GenericInst:
                for (TypeId a : d.type_args) {
                    if (!is_concrete_checkable_type(a)) return false;
                }
                return !d.base_name.empty();
            case TypeKind::NominalStruct:
            case TypeKind::NominalEnum:
            case TypeKind::Interface:
                if (d.name.empty() || d.name == "unknown" || d.name == "any" ||
                    d.name.starts_with("cpp.") || d.name.starts_with("cpp::")) {
                    return false;
                }
                if (auto dot = d.name.find('.'); dot != std::string::npos) {
                    if (is_cpp_qualifier_sema(std::string_view(d.name.data(), dot))) return false;
                }
                return true;
            default:
                return false;
        }
    }


    // a type argument bound to `T: Constraint` (built-in Comparable/Ordered/Numeric/Integer or user interface
    // / compound bounds) must unify consistently at call sites and satisfy every required constraint.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_interface_constraints(const CallExpr& call, const FnDecl& sig, int line, int col)
        -> std::expected<void, std::string> {
        if (sig.type_params.empty()) return {};
        std::unordered_map<std::string, TypeId> pat_env;
        std::unordered_map<std::string, TypeId> bindings;
        std::unordered_map<std::string, std::pair<int, int>> bound_locs;
        for (std::size_t idx = 0; idx < sig.type_params.size(); ++idx) {
            const auto& tp = sig.type_params[idx];
            pat_env[tp.name] = cf_.type_table.make_type_param(tp.name, tp.constraints);
            if (idx < call.type_args.size()) {
                TypeId ta = resolve_typeref(call.type_args[idx]);
                if (ta.is_valid() && !ta.is_unknown()) {
                    bindings[tp.name] = ta;
                    bound_locs[tp.name] = {line, col};
                }
            }
        }
        // Seed receiver generic bindings if calling a generic method on GenericInst.
        if (sig.receiver) {
            if (const auto* mem = call.callee ? std::get_if<MemberExpr>(&call.callee->node) : nullptr; mem && mem->object) {
                TypeId un_rt = unwrap_receiver_type(infer_expr_type_id_sema(mem->object), true);
                const auto& rd = cf_.type_table.get(un_rt);
                if (rd.kind == TypeKind::GenericInst) {
                    for (std::size_t i = 0; i < sig.receiver->type.type_args.size() && i < rd.type_args.size(); ++i) {
                        if (!sig.receiver->type.type_args[i].name.empty()) {
                            bindings[sig.receiver->type.type_args[i].name] = rd.type_args[i];
                        }
                    }
                }
            }
        }
        for (std::size_t i = 0; i < call.args.size() && i < sig.params.size(); ++i) {
            const auto& arg = call.args[i];
            const ParamDecl* param = &sig.params[i];
            if (!arg.label.empty()) {
                auto it = std::find_if(sig.params.begin(), sig.params.end(), [&](const ParamDecl& pd) { return pd.name == arg.label; });
                if (it == sig.params.end()) continue;
                param = &*it;
            }
            TypeId pat = cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(param->type, sig.package_name), &pat_env);
            if (!cf_.type_table.contains_type_param(pat)) continue;
            TypeId got = infer_expr_type_id_sema(arg.value);
            if (pat.is_unknown() || got.is_unknown() || got.is_error() || got.is_never() || got.is_foreign()) continue;
            // Allow untyped numeric literals to adapt to a previously bound numeric TypeParam.
            if (cf_.type_table.is_type_param(pat) && is_numeric_literal_expr(arg.value)) {
                const auto& pname = cf_.type_table.get(pat).name;
                if (auto bit = bindings.find(pname); bit != bindings.end()) {
                    if (cf_.type_table.is_numeric(bit->second)) {
                        if (cf_.type_table.is_integer(bit->second)) {
                            if (auto r = check_int_literal_fits(arg.value, cf_.type_table.to_string(bit->second), arg.line, arg.col); !r) return r;
                        }
                        continue;
                    }
                }
            }
            if (!cf_.type_table.unify(pat, got, bindings)) {
                TypeId expected = cf_.type_table.substitute(pat, bindings);
                if (cf_.type_table.is_optional(got) && !cf_.type_table.is_optional(expected) &&
                    cf_.type_table.is_assignable(expected, cf_.type_table.unwrap_optional(got))) {
                    if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/UNWRAPPED-OPTIONAL",
                        std::format("argument {} of '{}' expects '{}', got optional '{}' without unwrapping; use 'or' to provide a fallback",
                            i + 1, sig.name, cf_.type_table.to_string(expected), cf_.type_table.to_string(got)))); !r) return r;
                } else {
                    if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/TYPE-MISMATCH",
                        std::format("argument {} of '{}' expects '{}', got '{}'",
                            i + 1, sig.name, cf_.type_table.to_string(expected), cf_.type_table.to_string(got)))); !r) return r;
                }
                continue;
            }
            if (!bound_locs.contains(param->type.name)) {
                bound_locs[param->type.name] = {arg.line, arg.col};
            }
        }
        for (std::size_t tp_idx = 0; tp_idx < sig.type_params.size(); ++tp_idx) {
            const auto& tp = sig.type_params[tp_idx];
            if (tp.constraints.empty()) continue;
            auto bit = bindings.find(tp.name);
            if (bit == bindings.end()) continue;
            TypeId concrete_tid = bit->second;
            if (concrete_tid.is_unknown() || concrete_tid.is_error() || concrete_tid.is_foreign() ||
                cf_.type_table.contains_foreign(concrete_tid)) {
                continue;
            }
            std::string concrete = cf_.type_table.to_string(concrete_tid);
            if (concrete == "unknown" || concrete == "any") continue;
            int arg_line = line, arg_col = col;
            if (auto lit = bound_locs.find(tp.name); lit != bound_locs.end()) {
                arg_line = lit->second.first;
                arg_col = lit->second.second;
            } else {
                for (std::size_t i = 0; i < sig.params.size() && i < call.args.size(); ++i) {
                    if (format_type_ref_sema(sig.params[i].type) == tp.name) {
                        arg_line = call.args[i].line;
                        arg_col = call.args[i].col;
                        break;
                    }
                }
            }
            std::string lookup_owner = canonical_type_name_sema(concrete);
            if (auto lb = lookup_owner.find('['); lb != std::string::npos) lookup_owner = lookup_owner.substr(0, lb);
            std::string mangled_owner = lookup_owner;
            if (auto dot = lookup_owner.find('.'); dot != std::string::npos) {
                mangled_owner = std::format("cfire_pkg_{}__{}", lookup_owner.substr(0, dot), lookup_owner.substr(dot + 1));
            }
            auto find_impl_method = [&](const std::string& mname) -> const FnDecl* {
                if (auto mit = cf_.methods.find(mangled_owner + "::" + mname); mit != cf_.methods.end()) return &mit->second;
                if (auto mit = cf_.methods.find(lookup_owner + "::" + mname); mit != cf_.methods.end()) return &mit->second;
                if (auto dot = lookup_owner.find('.'); dot != std::string::npos) {
                    if (auto mit = cf_.methods.find(lookup_owner.substr(dot + 1) + "::" + mname); mit != cf_.methods.end()) return &mit->second;
                }
                return nullptr;
            };
            std::string full_bounds;
            for (const auto& b : tp.constraints) full_bounds += (full_bounds.empty() ? "" : " + ") + b;
            for (const auto& bound : tp.constraints) {
                auto iit = cf_.interface_defs.find(bound);
                if (iit == cf_.interface_defs.end()) {
                    if (!satisfies_constraint_sema(concrete_tid, bound)) {
                        if (cf_.type_table.is_optional(concrete_tid) &&
                            satisfies_constraint_sema(cf_.type_table.unwrap_optional(concrete_tid), bound)) {
                            if (auto r = emit_error(diag_at(arg_line, arg_col, "cfire/UNWRAPPED-OPTIONAL",
                                std::format("cannot pass optional '{}' where '{}' requires '{}: {}'; unwrap first with 'or'",
                                    concrete, sig.name, tp.name, full_bounds))); !r) return r;
                        } else {
                            if (auto r = emit_error(diag_at(arg_line, arg_col, "cfire/UNSATISFIED-CONSTRAINT",
                                std::format("type '{}' does not satisfy constraint '{}' required by '{}[{}: {}]'",
                                    concrete, bound, sig.name, tp.name, full_bounds))); !r) return r;
                        }
                    }
                    continue;
                }
                if (cf_.type_table.is_optional(concrete_tid) &&
                    satisfies_constraint_sema(cf_.type_table.unwrap_optional(concrete_tid), bound)) {
                    if (auto r = emit_error(diag_at(arg_line, arg_col, "cfire/UNWRAPPED-OPTIONAL",
                        std::format("cannot pass optional '{}' where '{}' requires '{}: {}'; unwrap first with 'or'",
                            concrete, sig.name, tp.name, full_bounds))); !r) return r;
                    continue;
                }
                if (cf_.type_table.is_type_param(concrete_tid)) {
                    if (!satisfies_constraint_sema(concrete_tid, bound)) {
                        if (auto r = emit_error(diag_at(arg_line, arg_col, "cfire/UNSATISFIED-CONSTRAINT",
                            std::format("type parameter '{}' does not satisfy interface '{}' required by '{}[{}: {}]'",
                                concrete, bound, sig.name, tp.name, full_bounds))); !r) return r;
                    }
                    continue;
                }
                std::vector<std::string> problems;
                for (const auto& m : iit->second.methods) {
                    const FnDecl* impl_ptr = find_impl_method(m.name);
                    if (!impl_ptr) {
                        problems.push_back(std::format("missing method '{}'", m.name));
                        continue;
                    }
                    const FnDecl& impl = *impl_ptr;
                    if (!m.is_mut_receiver && impl.receiver && impl.receiver->intent == "mut") {
                        problems.push_back(std::format("'{}' requires 'mut' receiver, interface specifies immutable receiver", m.name));
                    }
                    if (impl.params.size() != m.param_types.size()) {
                        problems.push_back(std::format("'{}' takes {} parameter(s), interface expects {}", m.name, impl.params.size(), m.param_types.size()));
                        continue;
                    }
                    for (std::size_t k = 0; k < m.param_types.size(); ++k) {
                        std::string exp_intent = (k < m.param_intents.size() && !m.param_intents[k].empty()) ? m.param_intents[k] : std::string("in");
                        if (impl.params[k].intent != exp_intent) {
                            problems.push_back(std::format("'{}' parameter {} has '{}' intent, interface expects '{}'", m.name, k + 1, impl.params[k].intent, exp_intent));
                        }
                        std::string impl_pt = canonical_type_name_sema(format_type_ref_sema(impl.params[k].type));
                        std::string ifc_pt = canonical_type_name_sema(format_type_ref_sema(m.param_types[k]));
                        if (impl_pt != ifc_pt) {
                            problems.push_back(std::format("'{}' parameter {} is '{}', interface expects '{}'", m.name, k + 1,
                                format_type_ref_sema(impl.params[k].type), format_type_ref_sema(m.param_types[k])));
                        }
                    }
                    std::string impl_rt = canonical_type_name_sema(format_type_ref_sema(impl.return_type));
                    std::string ifc_rt = canonical_type_name_sema(format_type_ref_sema(m.return_type));
                    if (impl_rt != ifc_rt) {
                        problems.push_back(std::format("'{}' returns '{}', interface expects '{}'", m.name,
                            format_type_ref_sema(impl.return_type), format_type_ref_sema(m.return_type)));
                    }
                }
                if (!problems.empty()) {
                    std::string joined;
                    for (const auto& p : problems) joined += (joined.empty() ? "" : "; ") + p;
                    if (auto r = emit_error(diag_at(arg_line, arg_col, "cfire/UNSATISFIED-CONSTRAINT",
                        std::format("type '{}' does not satisfy interface '{}' required by '{}[{}: {}]': {}",
                            concrete, bound, sig.name, tp.name, full_bounds, joined))); !r) return r;
                }
            }
        }
        return {};
    }


    // the column where `e` starts in the source (a binary or postfix node points at its operator).
    CFIRE_SEMA_INLINE auto SemaFnChecker::leftmost_col(const ExprPtr& e) -> int {
        if (!e) return 1;
        if (const auto* b = std::get_if<BinaryExpr>(&e->node); b && b->left) return std::min(e->col, leftmost_col(b->left));
        if (const auto* c = std::get_if<CallExpr>(&e->node); c && c->callee) return std::min(e->col, leftmost_col(c->callee));
        if (const auto* m = std::get_if<MemberExpr>(&e->node); m && m->object) return std::min(e->col, leftmost_col(m->object));
        if (const auto* ix = std::get_if<IndexExpr>(&e->node); ix && ix->object) return std::min(e->col, leftmost_col(ix->object));
        return e->col;
    }


    // the value of an integer constant expression (literals combined with `+ - * / % << >> & | ^` and
    // unary `-`); nullopt for anything else, or when it leaves the 64-bit range.
    CFIRE_SEMA_INLINE auto SemaFnChecker::const_int_value(const ExprPtr& e) -> std::optional<__int128> {
        if (!e) return std::nullopt;
        constexpr __int128 kMax = static_cast<__int128>(UINT64_MAX);
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind != TokenKind::Int) return std::nullopt;
            std::string digits;
            for (char c : lit->value) if (c != '_') digits.push_back(c);
            int base = 10;
            std::string_view body = digits;
            if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) { base = 16; body.remove_prefix(2); }
            else if (body.size() > 2 && body[0] == '0' && (body[1] == 'b' || body[1] == 'B')) { base = 2; body.remove_prefix(2); }
            else if (body.size() > 2 && body[0] == '0' && (body[1] == 'o' || body[1] == 'O')) { base = 8; body.remove_prefix(2); }
            if (body.empty()) return std::nullopt;
            unsigned __int128 uv = 0;
            for (char c : body) {
                int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                      : std::isxdigit(static_cast<unsigned char>(c)) ? (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10) : -1;
                if (d < 0 || d >= base) return std::nullopt;
                uv = uv * static_cast<unsigned __int128>(base) + static_cast<unsigned __int128>(d);
                if (uv > static_cast<unsigned __int128>(UINT64_MAX)) return std::nullopt;
            }
            return static_cast<__int128>(uv);
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-") {
            auto v = const_int_value(un->operand);
            if (!v) return std::nullopt;
            return -*v;
        }
        const auto* b = std::get_if<BinaryExpr>(&e->node);
        if (!b) return std::nullopt;
        auto l = const_int_value(b->left);
        auto r = l ? const_int_value(b->right) : std::nullopt;
        if (!l || !r) return std::nullopt;
        __int128 v = 0;
        if (b->op == "+") v = *l + *r;
        else if (b->op == "-") v = *l - *r;
        else if (b->op == "*") {
            const bool neg = (*l < 0) != (*r < 0);
            const unsigned __int128 ul = *l < 0 ? static_cast<unsigned __int128>(-*l) : static_cast<unsigned __int128>(*l);
            const unsigned __int128 ur = *r < 0 ? static_cast<unsigned __int128>(-*r) : static_cast<unsigned __int128>(*r);
            const unsigned __int128 prod = ul * ur;
            if (prod > static_cast<unsigned __int128>(UINT64_MAX)) return std::nullopt;
            v = neg ? -static_cast<__int128>(prod) : static_cast<__int128>(prod);
        }
        else if (b->op == "/") { if (*r == 0) return std::nullopt; v = *l / *r; }
        else if (b->op == "%") { if (*r == 0) return std::nullopt; v = *l % *r; }
        else if (b->op == "<<") { if (*r < 0 || *r > 63 || *l < 0) return std::nullopt; v = *l << static_cast<int>(*r); }
        else if (b->op == ">>") { if (*r < 0 || *r > 63) return std::nullopt; v = *l >> static_cast<int>(*r); }
        else if (b->op == "&") v = *l & *r;
        else if (b->op == "|") v = *l | *r;
        else if (b->op == "^") v = *l ^ *r;
        else return std::nullopt;
        if (v > kMax || v < -kMax) return std::nullopt;
        return v;
    }


    // the integer type of `e` for the narrowing check. A literal operand takes the other operand's type
    // (`8080 + i32(i)` is `i32`, as in C++), and a shift has its left operand's type.
    CFIRE_SEMA_INLINE auto SemaFnChecker::int_value_type(const ExprPtr& e) -> TypeId {
        if (const auto* b = e ? std::get_if<BinaryExpr>(&e->node) : nullptr) {
            static const std::unordered_set<std::string_view> arith = {"+", "-", "*", "/", "%", "&", "|", "^"};
            if (b->op == "<<" || b->op == ">>") return const_int_value(b->left) ? infer_expr_type_id_sema(e) : int_value_type(b->left);
            if (arith.contains(b->op)) {
                const bool lc = const_int_value(b->left).has_value();
                const bool rc = const_int_value(b->right).has_value();
                if (lc && !rc) return int_value_type(b->right);
                if (rc && !lc) return int_value_type(b->left);
                if (!lc && !rc) {
                    TypeId l = int_value_type(b->left);
                    TypeId r = int_value_type(b->right);
                    if (cf_.type_table.is_integer(l) && cf_.type_table.is_integer(r)) return cf_.type_table.int_widens(l, r) ? l : r;
                }
            }
        }
        if (const auto* un = e ? std::get_if<UnaryExpr>(&e->node) : nullptr; un && (un->op == "-" || un->op == "~") && !const_int_value(e)) {
            return int_value_type(un->operand);
        }
        return infer_expr_type_id_sema(e);
    }


    // checks a value against the type it initializes (an argument, a field, a declaration, an
    // assignment, a return). An array literal is checked element by element against the element type, an
    // integer literal against the target's range, and an integer value must widen: narrowing or a sign change
    // needs an explicit, checked `T(x)`. `handled` says this check decided the case, so the caller skips
    // its own whole-type comparison.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_value_for(TypeId want, const ExprPtr& e, bool& handled) -> std::expected<void, std::string> {
        handled = false;
        if (!e || !want.is_valid() || want.is_unknown() || want.is_error() || want.is_foreign()) return {};
        const auto& wd = cf_.type_table.get(want);
        if ((wd.kind == TypeKind::Optional || wd.kind == TypeKind::Fallible) && wd.elem_type.is_valid()) {
            TypeId et = infer_expr_type_id_sema(e);
            if ((wd.kind == TypeKind::Optional && cf_.type_table.is_optional(et)) ||
                (wd.kind == TypeKind::Fallible && cf_.type_table.is_fallible(et))) {
                return {};
            }
            if (const auto* lit = std::get_if<LiteralExpr>(&e->node); lit && lit->kind == TokenKind::KwNone && wd.kind == TypeKind::Optional) {
                handled = true;
                return {};
            }
            return check_value_for(wd.elem_type, e, handled);
        }
        if (wd.kind == TypeKind::TypeParam) {
            if (cf_.type_table.has_constraint(want, "Integer") && is_numeric_literal_expr(e)) {
                const Expr* lit_e = e;
                if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-") lit_e = un->operand;
                const auto* lit = std::get_if<LiteralExpr>(&lit_e->node);
                if (lit && lit->kind == TokenKind::Int) {
                    handled = true;
                    return {};
                }
            } else if (cf_.type_table.has_constraint(want, "Numeric") && is_numeric_literal_expr(e)) {
                handled = true;
                return {};
            }
            return {};
        }
        if (wd.kind == TypeKind::Tuple) {
            if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
                if (tup->elements.size() != wd.param_types.size()) {
                    return emit_error(diag_at(e->line, leftmost_col(e), "cfire/TYPE-MISMATCH",
                        std::format("tuple expects {} element(s), got {}", wd.param_types.size(), tup->elements.size())));
                }
                handled = true;
                for (std::size_t i = 0; i < tup->elements.size(); ++i) {
                    const TypeId elem_want = wd.param_types[i];
                    const ExprPtr& elem_e = tup->elements[i];
                    if (!elem_e) continue;
                    TypeId got = infer_expr_type_id_sema(elem_e);
                    if (auto r = check_type_compatible(elem_want, got, elem_e, std::format("tuple element {}", i), elem_e->line, leftmost_col(elem_e)); !r) return r;
                }
                return {};
            }
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            if (wd.kind != TypeKind::Slice && wd.kind != TypeKind::Array) return {};
            handled = true;
            const TypeId elem = wd.elem_type;
            for (const auto& el : arr->elements) {
                if (!el) continue;
                TypeId got = infer_expr_type_id_sema(el);
                if (auto r = check_type_compatible(elem, got, el, "array element", el->line, leftmost_col(el)); !r) return r;
            }
            return {};
        }
        if (cf_.type_table.is_integer(want) && is_numeric_literal_expr(e)) {
            const Expr* lit_e = e;
            if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-") lit_e = un->operand;
            const auto* lit = std::get_if<LiteralExpr>(&lit_e->node);
            if (!lit || lit->kind != TokenKind::Int) return {};  // a float literal: the caller reports it
            handled = true;
            return check_int_literal_fits(e, cf_.type_table.to_string(want), e->line, leftmost_col(e));
        }
        if (cf_.type_table.is_float(want) && is_numeric_literal_expr(e)) {
            handled = true;
            return {};
        }
        // `match` / `if` expressions: each arm's value initializes the target.
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            bool all = !me->arms.empty();
            for (const auto& arm : me->arms) {
                const ExprStmt* es = arm.body.empty() || !arm.body.back() ? nullptr : std::get_if<ExprStmt>(&arm.body.back()->node);
                if (!es || !es->expr) {
                    if (!block_diverges(arm.body, &cf_)) all = false;
                    continue;
                }
                bool h = false;
                if (auto r = check_value_for(want, es->expr, h); !r) return r;
                if (!h) {
                    TypeId got = infer_expr_type_id_sema(es->expr);
                    if (!got.is_never() && (!is_concrete_checkable_type(got) || !cf_.type_table.is_assignable(want, got))) all = false;
                }
            }
            handled = all;  // otherwise the caller compares the match's own type
            return {};
        }
        if (cf_.type_table.is_integer(want)) {
            if (!std::holds_alternative<LiteralExpr>(e->node) && !std::holds_alternative<UnaryExpr>(e->node)) {
                if (auto v = const_int_value(e)) {
                    handled = true;
                    const auto [bits, sgn] = cf_.type_table.int_shape(want);
                    const __int128 lo = sgn ? -(static_cast<__int128>(1) << (bits - 1)) : 0;
                    const __int128 hi = sgn ? (static_cast<__int128>(1) << (bits - 1)) - 1 : (static_cast<__int128>(1) << bits) - 1;
                    if (*v < lo || *v > hi) {
                        const std::string to = cf_.type_table.to_string(want);
                        return emit_error(diag_at(e->line, leftmost_col(e), "cfire/LITERAL-OUT-OF-RANGE",
                            std::format("constant {} does not fit in '{}'; use a value in range or an explicit 'wrapping_as[{}]()'", static_cast<long long>(*v), to, to)));
                    }
                    return {};
                }
            }
            TypeId got = int_value_type(e);
            if (cf_.type_table.is_integer(got) && !cf_.type_table.int_widens(want, got)) {
                handled = true;
                const std::string to = cf_.type_table.to_string(want);
                return emit_error(diag_at(e->line, leftmost_col(e), "cfire/TYPE-MISMATCH",
                    std::format("'{}' does not convert implicitly to '{}': the value may not fit; write '{}(...)', which panics if it does not",
                        cf_.type_table.to_string(got), to, to),
                    {{"kind", "narrowing"}, {"to", to}}));
            }
        }
        return {};
    }


    // argument types of calls to functions declared in this program (clang used to report these).
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_call_argument_types(const CallExpr& call, const FnDecl& sig) -> std::expected<void, std::string> {
        std::unordered_map<std::string, TypeId> pat_env;
        std::unordered_map<std::string, TypeId> bindings;
        if (!sig.type_params.empty()) {
            for (std::size_t idx = 0; idx < sig.type_params.size(); ++idx) {
                const auto& tp = sig.type_params[idx];
                pat_env[tp.name] = cf_.type_table.make_type_param(tp.name, tp.constraints);
                if (idx < call.type_args.size()) {
                    TypeId ta = resolve_typeref(call.type_args[idx]);
                    if (ta.is_valid() && !ta.is_unknown()) bindings[tp.name] = ta;
                }
            }
        }
        if (sig.receiver) {
            for (const auto& rta : sig.receiver->type.type_args) {
                if (!rta.name.empty() && !pat_env.contains(rta.name)) {
                    pat_env[rta.name] = cf_.type_table.make_type_param(rta.name, {});
                }
            }
            if (const auto* mem = call.callee ? std::get_if<MemberExpr>(&call.callee->node) : nullptr; mem && mem->object) {
                TypeId un_rt = unwrap_receiver_type(infer_expr_type_id_sema(mem->object), true);
                const auto& rd = cf_.type_table.get(un_rt);
                if (rd.kind == TypeKind::GenericInst) {
                    for (std::size_t i = 0; i < sig.receiver->type.type_args.size() && i < rd.type_args.size(); ++i) {
                        if (!sig.receiver->type.type_args[i].name.empty()) {
                            pat_env.try_emplace(sig.receiver->type.type_args[i].name,
                                cf_.type_table.make_type_param(sig.receiver->type.type_args[i].name, {}));
                            bindings[sig.receiver->type.type_args[i].name] = rd.type_args[i];
                        }
                    }
                    if (auto sit = cf_.struct_defs.find(rd.base_name); sit != cf_.struct_defs.end()) {
                        for (std::size_t i = 0; i < sit->second.type_params.size() && i < rd.type_args.size(); ++i) {
                            pat_env.try_emplace(sit->second.type_params[i].name,
                                cf_.type_table.make_type_param(sit->second.type_params[i].name, sit->second.type_params[i].constraints));
                            bindings[sit->second.type_params[i].name] = rd.type_args[i];
                        }
                    }
                }
            }
        }
        if (!pat_env.empty()) {
            for (std::size_t i = 0; i < call.args.size() && i < sig.params.size(); ++i) {
                const ParamDecl* param = &sig.params[i];
                if (!call.args[i].label.empty()) {
                    auto it = std::find_if(sig.params.begin(), sig.params.end(), [&](const ParamDecl& pd) { return pd.name == call.args[i].label; });
                    if (it == sig.params.end()) continue;
                    param = &*it;
                }
                TypeId pat = cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(param->type, sig.package_name), &pat_env);
                TypeId got = infer_expr_type_id_sema(call.args[i].value);
                (void)cf_.type_table.unify(pat, got, bindings);
            }
        }
        for (std::size_t i = 0; i < call.args.size() && i < sig.params.size(); ++i) {
            const auto& arg = call.args[i];
            const ParamDecl* param = &sig.params[i];
            if (!arg.label.empty()) {
                auto it = std::find_if(sig.params.begin(), sig.params.end(), [&](const ParamDecl& pd) { return pd.name == arg.label; });
                if (it == sig.params.end()) continue;
                param = &*it;
            }
            TypeId want = !bindings.empty() || !pat_env.empty()
                ? cf_.type_table.substitute(cf_.type_table.from_typeref(canonical_typeref_in_pkg_sema(param->type, sig.package_name), &pat_env), bindings)
                : resolve_typeref_in_pkg(param->type, sig.package_name);
            if (cf_.type_table.contains_type_param(want)) {
                // Unsubstituted generic param inside a generic body or handled by check_interface_constraints
                bool still_unsubstituted = false;
                for (const auto& tp : sig.type_params) {
                    if (!bindings.contains(tp.name)) still_unsubstituted = true;
                }
                if (still_unsubstituted) continue;
            }
            TypeId got = infer_expr_type_id_sema(arg.value);
            if (auto r = check_type_compatible(want, got, arg.value, std::format("argument {} of '{}'", i + 1, sig.name), arg.line, arg.col); !r) {
                return r;
            }
        }
        return {};
    }


    // a constant shift count outside [0, bit width of the promoted left operand) is rejected.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_const_shift_count(const ExprPtr& left, const ExprPtr& count, int line, int col)
        -> std::expected<void, std::string> {
        if (!count) return {};
        bool negative = false;
        const Expr* lit_expr = count;
        if (const auto* un = std::get_if<UnaryExpr>(&count->node); un && un->op == "-" && un->operand) {
            negative = true;
            lit_expr = un->operand;
        }
        const auto* lit = std::get_if<LiteralExpr>(&lit_expr->node);
        if (!lit || lit->kind != TokenKind::Int) return {};
        auto v = parse_int_literal<std::int64_t>(lit->value);
        if (!v) return {};
        std::string lt = type_name_of(left);
        int bits = 64;  // integer literals and i64/u64/isize/usize
        if (lt == "i8" || lt == "u8" || lt == "byte" || lt == "i16" || lt == "u16" || lt == "i32" || lt == "u32") bits = 32;  // promoted to int
        if (negative || *v >= bits) {
            return emit_error(diag_at(line, col, "cfire/SHIFT-OUT-OF-RANGE", std::format("shift count {}{} is out of range for a {}-bit operand; use 'wrapping_shl'/'checked_shl' for explicit behavior", negative ? "-" : "", lit->value, bits)));
        }
        return {};
    }


    // an integer constant that does not fit its target type is a compile error, not a silent
    // truncation (`a: u8 = 300`, `u8(300)`, `c: u32 = -1`). Runtime values are range-checked by `T(x)`.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_int_literal_fits(const ExprPtr& e, std::string_view tname, int line, int col)
        -> std::expected<void, std::string> {
        struct Range { __int128 lo; __int128 hi; };
        static const std::unordered_map<std::string_view, Range> ranges = {
            {"i8", {INT8_MIN, INT8_MAX}}, {"i16", {INT16_MIN, INT16_MAX}}, {"i32", {INT32_MIN, INT32_MAX}},
            {"int", {INT64_MIN, INT64_MAX}}, {"i64", {INT64_MIN, INT64_MAX}}, {"isize", {INT64_MIN, INT64_MAX}},
            {"u8", {0, UINT8_MAX}}, {"byte", {0, UINT8_MAX}}, {"u16", {0, UINT16_MAX}}, {"u32", {0, UINT32_MAX}},
            {"u64", {0, UINT64_MAX}}, {"usize", {0, UINT64_MAX}}, {"uint", {0, UINT64_MAX}}};
        auto rit = ranges.find(tname);
        if (rit == ranges.end() || !e) return {};
        bool negative = false;
        const Expr* lit_expr = e;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-" && un->operand) {
            negative = true;
            lit_expr = un->operand;
        }
        const auto* lit = std::get_if<LiteralExpr>(&lit_expr->node);
        if (!lit || lit->kind != TokenKind::Int) return {};
        std::string digits;
        for (char c : lit->value) if (c != '_') digits.push_back(c);
        int base = 10;
        std::string_view body = digits;
        if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) { base = 16; body.remove_prefix(2); }
        else if (body.size() > 2 && body[0] == '0' && (body[1] == 'b' || body[1] == 'B')) { base = 2; body.remove_prefix(2); }
        else if (body.size() > 2 && body[0] == '0' && (body[1] == 'o' || body[1] == 'O')) { base = 8; body.remove_prefix(2); }
        unsigned __int128 uv = 0;
        bool overflow = false;
        for (char c : body) {
            int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                  : std::isxdigit(static_cast<unsigned char>(c)) ? (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10) : -1;
            if (d < 0 || d >= base) return {};  // not a plain integer literal; leave it to other checks
            uv = uv * static_cast<unsigned __int128>(base) + static_cast<unsigned __int128>(d);
            if (uv > static_cast<unsigned __int128>(UINT64_MAX)) { overflow = true; break; }
        }
        __int128 v = static_cast<__int128>(uv);
        if (negative) v = -v;
        if (overflow || v < rit->second.lo || v > rit->second.hi) {
            std::string shown = (negative ? "-" : "") + lit->value;
            return emit_error(diag_at(line, col, "cfire/LITERAL-OUT-OF-RANGE", std::format("integer literal {} does not fit in '{}' (range {}..{}); use a value in range or an explicit 'wrapping_as[{}]()'", shown, tname, static_cast<long long>(rit->second.lo), rit->second.hi > static_cast<__int128>(INT64_MAX) ? std::string("18446744073709551615") : std::to_string(static_cast<long long>(rit->second.hi)), tname)));
        }
        return {};
    }
