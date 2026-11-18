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
        for (const auto& tp : fn.type_params) {
            fn_type_params_.insert(tp.name);
            if (!tp.constraints.empty()) {
                fn_type_param_bounds_[tp.name] = tp.constraints;
            }
        }
        if (fn.receiver) {
            for (const auto& st : cf_.ast.structs) {
                if (st.name == fn.receiver->type.name) {
                    for (std::size_t idx = 0; idx < st.type_params.size(); ++idx) {
                        const auto& tp = st.type_params[idx];
                        fn_type_params_.insert(tp.name);
                        if (!tp.constraints.empty()) fn_type_param_bounds_[tp.name] = tp.constraints;
                        if (idx < fn.receiver->type.type_args.size() && !fn.receiver->type.type_args[idx].name.empty()) {
                            const auto& arg_name = fn.receiver->type.type_args[idx].name;
                            fn_type_params_.insert(arg_name);
                            if (!tp.constraints.empty()) fn_type_param_bounds_[arg_name] = tp.constraints;
                        }
                    }
                }
            }
            for (const auto& ta : fn.receiver->type.type_args) fn_type_params_.insert(ta.name);  // `fn (b: Box[T])`
        }
        if (fn.receiver) {
            (void)check_type_ref_separators(fn.receiver->type, fn.receiver->line, fn.receiver->col);  // recorded by emit_error
            param_names_.insert(fn.receiver->name);
            var_scope_depth_[fn.receiver->name] = 0;
            if (!fn.receiver->type.name.empty()) {
                var_types_[fn.receiver->name] = format_type_ref_sema(fn.receiver->type);
            }
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
            if (!p.type.smart_wrap.empty() && p.type.elem_type && !p.type.elem_type->name.empty()) {
                var_types_[p.name] = p.type.elem_type->name;
            } else if (!p.type.name.empty()) {
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
            // Builtin view-returning members borrow from their receiver, not from a temporary.
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
            for (const auto& f : st->fields) {
                for (auto& o : extract_borrow_sources(f.value)) add(o);
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
               name == "unlock" || name == "reset" || name == "has_value" || name == "value_or" ||
               name == "value" || name == "parse_i64" || name == "parse_i32" || name == "parse_u64" ||
               name == "parse_f64" || name == "parse_bool";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_member_call_sig(const MemberExpr& mem) const -> const FnDecl* {
        const auto* obj_id = mem.object ? std::get_if<IdentExpr>(&mem.object->node) : nullptr;
        if (obj_id && is_namespace_ident_sema(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
            if (auto it = cf_.fn_sigs.find(obj_id->name + "." + mem.field); it != cf_.fn_sigs.end()) {
                return &it->second;
            }
            return nullptr;
        }
        if (obj_id) {
            if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                if (auto mit = cf_.methods.find(vt->second + "::" + mem.field); mit != cf_.methods.end()) {
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
                if (mem->field == "parse" && primitive_type_prefix(mem->object).is_valid()) return true;  // T.parse(s) -> T?
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
                        if (recv_id->name == "os" && mem->field == "getenv_opt") {
                            return true;
                        }
                        if (recv_id->name == "str" && (
                            mem->field == "find" ||
                            mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                            mem->field == "parse_f64" || mem->field == "parse_bool")) {
                            return true;
                        }
                        if (recv_id->name == "cli" &&
                            (mem->field == "try_parse" || mem->field == "prompt" || mem->field == "prompt_password")) {
                            return true;
                        }
                        if (const auto* sig = resolve_member_call_sig(*mem)) {
                            return sig->return_type.is_optional && !sig->return_type.is_fallible;
                        }
                        return false;
                    }
                }
                if (mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                    mem->field == "parse_f64" || mem->field == "parse_bool" ||
                    mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "getenv_opt" || mem->field == "read_line") {
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
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String) return type_id_of("str");
            if (lit->kind == TokenKind::Int) return type_id_of("i64");
            if (lit->kind == TokenKind::Float) return type_id_of("f64");
            if (lit->kind == TokenKind::Char) return TypeId::Byte();
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return type_id_of("bool");
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            // a generic stdlib queue keeps its element type, so `pop` can be typed `T?`.
            if (st->type_args.size() == 1 && (st->type_name.ends_with("PriorityQueue") || st->type_name.ends_with("MinQueue"))) {
                return type_id_of(st->type_name + "[" + cf_.type_table.to_string(type_id_of(st->type_args[0])) + "]");
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
            TypeId inner_t = infer_expr_type_id_sema(or_ex->inner);
            inner_t = cf_.type_table.unwrap_fallible(inner_t);
            return cf_.type_table.unwrap_optional(inner_t);
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
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                return type_id_of(it->second);
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
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->object) {
                if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (cf_.enums.contains(id->name)) return type_id_of(id->name);
                } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                    if (const auto* sid = sub_mem->object ? std::get_if<IdentExpr>(&sub_mem->object->node) : nullptr) {
                        if (const auto* en = resolve_enum_by_prefix_sema(sid->name + "." + sub_mem->field)) {
                            return type_id_of(en->name);
                        }
                    }
                    if (cf_.enums.contains(sub_mem->field)) return type_id_of(sub_mem->field);
                }
            }
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    if (auto sit = cf_.struct_tags.find(owner_t); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return type_id_of(f.field_type.name);
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cm = std::get_if<MemberExpr>(&call->callee->node);
                cm && call->type_args.size() == 1 &&
                (cm->field == "wrapping_as" || cm->field == "saturating_as" || cm->field == "try_as")) {
                return type_id_of(call->type_args[0].name);  // optional-ness of try_as is tracked by expr_returns_optional_sema
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (auto pos = cid->name.find("::"); pos != std::string::npos) {
                    std::string prefix = cid->name.substr(0, pos);
                    if (cf_.enums.contains(prefix)) return type_id_of(prefix);
                }
                if (auto pos = cid->name.find('.'); pos != std::string::npos) {
                    std::string prefix = cid->name.substr(0, pos);
                    if (cf_.enums.contains(prefix)) return type_id_of(prefix);
                }
                if (auto vit = cf_.variant_to_enum.find(cid->name); vit != cf_.variant_to_enum.end()) {
                    return type_id_of(vit->second);
                }
                if (cid->name == "make" && call->type_args.size() == 1) {
                    // `make(http.Server, ...)` has the constructor's declared type (`http.HttpServer`).
                    if (const auto* sc = find_stdlib_ctor(call->type_args[0].name); sc && !sc->generic) {
                        if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                            if (!sig->unwrapped_return_type.empty()) return type_id_of(qualify_stdlib_type_sema(*sig, std::string(sc->ns)));
                        }
                    }
                    if (!call->type_args[0].smart_wrap.empty() && call->type_args[0].elem_type) {
                        return type_id_of(call->type_args[0].elem_type->name);
                    }
                    if (!call->type_args[0].name.empty()) {
                        return type_id_of(call->type_args[0].name);
                    }
                }
                if (cid->name == "sync.Mutex" || cid->name == "sync.make_mutex") {
                    if (!call->type_args.empty()) return type_id_of("sync.Mutex[" + call->type_args[0].name + "]");
                    if (!call->args.empty() && call->args[0].value) return type_id_of("sync.Mutex[" + type_name_of(call->args[0].value) + "]");
                    return type_id_of("sync.Mutex[void]");
                }
                if (cid->name == "sync.Atomic" || cid->name == "sync.make_atomic") {
                    if (!call->type_args.empty()) return type_id_of("sync.Atomic[" + call->type_args[0].name + "]");
                    if (!call->args.empty() && call->args[0].value) return type_id_of("sync.Atomic[" + type_name_of(call->args[0].value) + "]");
                    return type_id_of("sync.Atomic[i64]");
                }
                if (cid->name == "[]byte" || cid->name == "[]u8" || cid->name == "str" ||
                    cid->name == "i8" || cid->name == "i16" || cid->name == "i32" || cid->name == "i64" ||
                    cid->name == "u8" || cid->name == "byte" || cid->name == "u16" || cid->name == "u32" ||
                    cid->name == "u64" || cid->name == "usize" || cid->name == "isize" ||
                    cid->name == "f32" || cid->name == "f64" || cid->name == "bool") {
                    return type_id_of(cid->name);
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        if (!pit->second.return_type.smart_wrap.empty() && pit->second.return_type.elem_type) {
                            return type_id_of(pit->second.return_type.elem_type->name);
                        }
                        TypeRef ret_tr = pit->second.return_type;
                        ret_tr.is_fallible = false;
                        ret_tr.is_optional = false;
                        if (ret_tr.name == "void") return TypeId::Void();
                        TypeId tid = type_id_of(format_type_ref_sema(ret_tr));
                        return tid.is_valid() ? tid : type_id_of(pit->second.return_type.name);
                    }
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    if (!it->second.return_type.smart_wrap.empty() && it->second.return_type.elem_type) {
                        return type_id_of(it->second.return_type.elem_type->name);
                    }
                    TypeRef ret_tr = it->second.return_type;
                    ret_tr.is_fallible = false;
                    ret_tr.is_optional = false;
                    if (ret_tr.name == "void") return TypeId::Void();
                    TypeId tid = type_id_of(format_type_ref_sema(ret_tr));
                    return tid.is_valid() ? tid : type_id_of(it->second.return_type.name);
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->object) {
                    if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(id->name)) return type_id_of(id->name);
                    } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sid = sub_mem->object ? std::get_if<IdentExpr>(&sub_mem->object->node) : nullptr) {
                            if (const auto* en = resolve_enum_by_prefix_sema(sid->name + "." + sub_mem->field)) {
                                return type_id_of(en->name);
                            }
                        }
                        if (cf_.enums.contains(sub_mem->field)) return type_id_of(sub_mem->field);
                    }
                }
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (recv_id->name == "os" && (mem->field == "getenv" || mem->field == "getenv_opt" || mem->field == "cwd" || mem->field == "temp_dir")) {
                        return type_id_of("str");
                    }
                    if (recv_id->name == "fs" && mem->field == "read_text") {
                        return type_id_of("str");
                    }
                    if ((recv_id->name == "fs" && mem->field == "read_bytes") ||
                        (recv_id->name == "hex" && mem->field == "decode") ||
                        (recv_id->name == "base64" && mem->field == "decode") ||
                        (recv_id->name == "rand" && mem->field == "bytes") ||
                        (recv_id->name == "sodium" && (mem->field == "random_bytes" || mem->field == "argon2id" || mem->field == "blake2b" || mem->field == "derive_subkey" || mem->field == "xchacha20poly1305_encrypt" || mem->field == "xchacha20poly1305_decrypt"))) {
                        return type_id_of("[]byte");
                    }
                    if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                        if (std_sig->unwrapped_return_type == "[]byte") return type_id_of("[]byte");
                        if (!std_sig->unwrapped_return_type.empty() && std_sig->unwrapped_return_type != "void" &&
                            !std_sig->unwrapped_return_type.starts_with('[') && !std_sig->unwrapped_return_type.starts_with('&') &&
                            !std_sig->unwrapped_return_type.starts_with("map[") && !std_sig->unwrapped_return_type.starts_with("own[") &&
                            !std_sig->unwrapped_return_type.starts_with("shared[") && !std_sig->unwrapped_return_type.starts_with("tuple[") &&
                            !std_sig->unwrapped_return_type.starts_with('(') &&
                            std_sig->unwrapped_return_type != "T") {
                            std::string ret_type = qualify_stdlib_type_sema(*std_sig, recv_id->name);
                            if (!call->type_args.empty()) {
                                std::string targ = call->type_args[0].name;
                                if (auto p = ret_type.find("[T]"); p != std::string::npos && !targ.empty()) {
                                    ret_type.replace(p, 3, "[" + targ + "]");
                                }
                            }
                            return type_id_of(ret_type);
                        }
                    }
                    if (!is_ident_in_scope_sema(recv_id->name)) {
                        if (auto fit = cf_.fn_sigs.find(mem->field); fit != cf_.fn_sigs.end()) {
                            return type_id_of(fit->second.return_type.name);
                        }
                    }
                } else if (mem->object) {
                    if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                            if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(sub_id->name, sub_mem->field + "." + mem->field)) {
                                if (std_sig->unwrapped_return_type == "[]byte") return type_id_of("[]byte");
                                if (!std_sig->unwrapped_return_type.empty() && std_sig->unwrapped_return_type != "void") {
                                    return type_id_of(qualify_stdlib_type_sema(*std_sig, sub_id->name));
                                }
                            }
                        }
                    }
                }
                if (mem->field == "bytes" && call->args.empty()) {
                    return type_id_of("[]byte");
                }
                if (mem->field == "ocr" || mem->field == "detect_language") {
                    return type_id_of("str");
                }
                if ((mem->field == "parse" || mem->field == "generate_struct" ||
                     mem->field == "generate_struct_from_file" || mem->field == "run_struct" ||
                     mem->field == "run_struct_validated") && call->type_args.size() == 1 &&
                    !call->type_args[0].name.empty()) {
                    return type_id_of(call->type_args[0].name);
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    if (sig->return_type.is_slice && sig->return_type.name == "byte") return type_id_of("[]byte");
                    TypeRef ret_tr = sig->return_type;
                    ret_tr.is_fallible = false;
                    ret_tr.is_optional = false;
                    if (ret_tr.name == "void") return TypeId::Void();
                    TypeId tid = type_id_of(format_type_ref_sema(ret_tr));
                    return tid.is_valid() ? tid : type_id_of(sig->return_type.name);
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem)) {
                    if (im->return_type.is_slice && im->return_type.name == "byte") return type_id_of("[]byte");
                    TypeRef ret_tr = im->return_type;
                    ret_tr.is_fallible = false;
                    ret_tr.is_optional = false;
                    if (ret_tr.name == "void") return TypeId::Void();
                    TypeId tid = type_id_of(format_type_ref_sema(ret_tr));
                    return tid.is_valid() ? tid : type_id_of(im->return_type.name);
                }
                if (mem->object) {
                    std::string recv_t = type_name_of(mem->object);
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

                        auto find_ret = [&](const std::string& key) -> std::string {
                            if (auto mit = idx.receiver_methods.find(key); mit != idx.receiver_methods.end()) {
                                std::string uret = mit->second.unwrapped_return_type;
                                if (!generic_arg.empty() && uret == "T") {
                                    return generic_arg;
                                }
                                if (uret.empty() || uret == "void") return "";
                                const auto dotq = recv_t.rfind('.');
                                std::string qual = (dotq == std::string::npos)
                                    ? local_qualifier_for_module_sema(mit->second.module_import, mit->second.ns_alias)
                                    : recv_t.substr(0, dotq);
                                return qualify_stdlib_type_sema(mit->second, qual);
                            }
                            return "";
                        };

                        if (base_recv == "Error" || base_recv == "&Error") {
                            if (mem->field == "code") return TypeId::Int32();
                            if (mem->field == "message") return TypeId::Str();
                        }

                        if (base_recv == "async.Task" && mem->field == "join") {
                            return type_id_of(generic_arg.empty() ? "void" : generic_arg);
                        }
                        if (base_recv == "sync.Mutex") {
                            if (mem->field == "lock") return type_id_of("sync.MutexGuard[" + generic_arg + "]");
                            if (mem->field == "get") return type_id_of(generic_arg);
                            if (mem->field == "with_lock") {
                                if (call->block_arg && call->block_arg->return_type) return type_id_of(call->block_arg->return_type->name);
                                if (!call->args.empty() && call->args[0].value) {
                                    if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                        return type_id_of(cl->return_type->name);
                                    }
                                }
                                return type_id_of(generic_arg);
                            }
                        }
                        if (base_recv == "sync.MutexGuard" && mem->field == "get") {
                            return type_id_of(generic_arg);
                        }
                        // priority queues hand out `T?` like `[]T.pop`.
                        if ((unqual_recv == "PriorityQueue" || unqual_recv == "MinQueue") && !generic_arg.empty()) {
                            if (mem->field == "pop" || mem->field == "peek") return type_id_of(generic_arg + "?");
                            if (mem->field == "top") return type_id_of(generic_arg);
                            if (mem->field == "empty") return type_id_of("bool");
                        }
                        if (base_recv == "sync.Atomic") {
                            if (mem->field == "load" || mem->field == "add" || mem->field == "sub" ||
                                mem->field == "fetch_add" || mem->field == "fetch_sub") {
                                return type_id_of(generic_arg.empty() ? "i64" : generic_arg);
                            }
                            if (mem->field == "cas") return type_id_of("bool");
                        }
                        if (mem->field == "spawn") {
                            std::string ret_t = "void";
                            if (!call->args.empty() && call->args[0].value) {
                                if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                    ret_t = cl->return_type->name;
                                }
                            }
                            if (call->block_arg && call->block_arg->return_type) ret_t = call->block_arg->return_type->name;
                            return type_id_of("async.Task[" + ret_t + "]");
                        }
                        if (mem->field == "len") {
                            if (unqual_recv == "Set" || unqual_recv == "Deque" || unqual_recv == "PriorityQueue" ||
                                unqual_recv == "MinQueue" || unqual_recv == "BTreeMap" || unqual_recv == "BitSet") {
                                return type_id_of("i64");
                            }
                        }

                        if (std::string r = find_ret(recv_t + "." + mem->field); !r.empty()) return type_id_of(r);
                        if (std::string r = find_ret(base_recv + "." + mem->field); !r.empty()) return type_id_of(r);
                        if (auto dot = base_recv.find('.'); dot != std::string::npos) {
                            if (std::string cns = resolve_stdlib_ns_sema(base_recv.substr(0, dot)); !cns.empty()) {
                                if (std::string r = find_ret(cns + base_recv.substr(dot) + "." + mem->field); !r.empty()) return type_id_of(r);
                            }
                        }
                        if (std::string r = find_ret(unqual_recv + "." + mem->field); !r.empty()) return type_id_of(r);
                    }
                }
            }
        }
        // Receiver of unknown type (e.g. an untyped closure parameter `|req|`): if exactly one stdlib
        // receiver method has this name, its return type is unambiguous.
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->object && !mem->is_namespace) {
                if (infer_expr_type_id_sema(mem->object).is_unknown()) {
                    std::string unique_ret;
                    bool ambiguous = false;
                    const std::string suffix = "." + mem->field;
                    for (const auto& [key, sig] : StdlibCatalogIndex::instance().receiver_methods) {
                        if (!key.ends_with(suffix) || key.find('.') != key.size() - suffix.size()) continue;
                        const std::string& r = sig.unwrapped_return_type;
                        if (r.empty() || r == "void" || r == "T" || r.find("T]") != std::string::npos) { ambiguous = true; break; }
                        if (unique_ret.empty()) unique_ret = r;
                        else if (unique_ret != r) { ambiguous = true; break; }
                    }
                    if (!ambiguous && !unique_ret.empty()) return type_id_of(unique_ret);
                }
            }
        }
        return TypeId::Unknown();
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::infer_expr_type_id_sema(const ExprPtr& e) const -> TypeId {
        if (!e) return TypeId::Unknown();
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
                TypeId ptr_t = op_t;
                if (cf_.type_table.get(ptr_t).kind == TypeKind::Optional) {
                    ptr_t = cf_.type_table.unwrap_optional(ptr_t);
                }
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
            if (bin->op == "+" && (lt == TypeId::Str() || rt == TypeId::Str())) {
                return TypeId::Str();
            }
            // String concatenation always yields an owned `str`, even when both operands are `&str` views.
            if (bin->op == "+") {
                const TypeId str_borrow = type_id_of("&str");
                if (lt == str_borrow || rt == str_borrow) return TypeId::Str();
            }
            // an integer literal takes the other operand's type (`100 + f` is `f`'s type, as in C++);
            // an expression of literals only is `i64`. A shift has its left operand's type.
            if (bin->op != "<<" && bin->op != ">>" && cf_.type_table.is_integer(rt) && const_int_value(bin->left) && !const_int_value(bin->right)) return rt;
            if (lt.is_valid() && !lt.is_unknown()) return lt;
            if (rt.is_valid() && !rt.is_unknown()) return rt;
            return TypeId::Unknown();
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            // `[18, 26, 22]` is a slice of its first element's type (no UNKNOWN-TYPE for literals).
            if (!arr->elements.empty()) {
                TypeId elem = infer_expr_type_id_sema(arr->elements.front());
                if (!elem.is_unknown()) return cf_.type_table.make_slice(elem);
            }
            return TypeId::Unknown();
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            TypeId obj_t = infer_expr_type_id_sema(sl->object);
            if (obj_t == TypeId::Str()) return type_id_of("&str");
            const auto& d = cf_.type_table.get(obj_t);
            if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array) {
                return cf_.type_table.make_slice(d.elem_type);
            }
            return TypeId::Unknown();
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            TypeId obj_t = infer_expr_type_id_sema(idx->object);
            if (obj_t == TypeId::Str()) return TypeId::Byte();
            const auto& d = cf_.type_table.get(obj_t);
            if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array) return d.elem_type;
            if (d.kind == TypeKind::Map) return d.elem_type;
            return TypeId::Unknown();
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            TypeId inner_t = infer_expr_type_id_sema(or_ex->inner);
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
                // `try` on `T?` is already TRY-ON-OPTIONAL; type the result as `T` so it does not cascade.
                if (d.kind == TypeKind::Optional) return cf_.type_table.unwrap_optional(inner_t);
                return inner_t;
            }
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            for (const auto& arm : me->arms) {
                if (!arm.body.empty()) {
                    if (const auto* expr_st = std::get_if<ExprStmt>(&arm.body.back()->node)) {
                        TypeId t = infer_expr_type_id_sema(expr_st->expr);
                        if (t.is_valid() && !t.is_unknown()) return t;
                    }
                }
            }
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
                std::string canon = resolve_stdlib_ns_sema(oid->name);
                if (!canon.empty()) {
                    if (auto cty = StdlibCatalogIndex::instance().find_ns_const_type(canon, mem->field); !cty.empty()) {
                        return type_id_of(cty);
                    }
                }
            }
            // fields of stdlib structs (`req.body: str`, `res.exit_code: i32`), from the catalog's type docs.
            if (mem->object) {
                std::string ot = type_name_of(mem->object);
                if (mem->field == "value") {  // the value a `sync.MutexGuard[T]` holds locked
                    std::string g = ot.starts_with("mut ") ? ot.substr(4) : ot;
                    for (std::string_view pre : {"sync.MutexGuard[", "MutexGuard["}) {
                        if (g.starts_with(pre) && g.ends_with(']')) return type_id_of(g.substr(pre.size(), g.size() - pre.size() - 1));
                    }
                }
                if (ot.starts_with("mut ")) ot.erase(0, 4);
                while (!ot.empty() && ot.front() == '&') ot.erase(ot.begin());
                if (auto dot = ot.find('.'); dot != std::string::npos && ot.find('.', dot + 1) == std::string::npos) {
                    const std::string raw_ns = ot.substr(0, dot);
                    std::string ns = resolve_stdlib_ns_sema(raw_ns);
                    if (ns.empty()) ns = raw_ns;
                    const std::string tname = ot.substr(dot + 1);
                    for (const auto& mod : cfire::doc::stdlib_catalog()) {
                        if (mod.ns_alias != ns) continue;
                        for (const auto& td : mod.type_docs) {
                            if (td.bare_name() != tname) continue;
                            for (const auto& f : td.fields) {
                                if (f.name != mem->field) continue;
                                // Element types of the module's own (`[]ToolCall` in `ext:ai`) are written bare.
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
                if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) return cf_.type_table.make_optional(prim);
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if ((mem->field == "wrapping_as" || mem->field == "saturating_as" || mem->field == "try_as") && call->type_args.size() == 1) {
                    return type_id_of(call->type_args[0].name);
                }
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && !bm->ret.empty()) {
                    TypeId rt = resolve_builtin_type(bm->ret, builtin_self_type(mem->object));
                    if (rt.is_valid()) return rt;
                }
                if (mem->field == "len") return TypeId::Usize();
                if (mem->field == "bytes" && call->args.empty()) return type_id_of("[]byte");
                // Zero-copy pointers: `s.byte_ptr` / `s.byte_ptr_at(i)` -> *c_char, `xs.data_ptr` -> *T.
                if ((mem->field == "byte_ptr" || mem->field == "byte_ptr_at") && mem->object) return type_id_of("*c_char");
                if ((mem->field == "data_ptr" || mem->field == "data") && call->args.empty() && mem->object) {
                    TypeId recv = infer_expr_type_id_sema(mem->object);
                    if (recv == TypeId::Str()) return type_id_of("*c_char");
                    const auto& rd = cf_.type_table.get(recv);
                    if ((rd.kind == TypeKind::Slice || rd.kind == TypeKind::Array) && !rd.elem_type.is_unknown()) {
                        return cf_.type_table.make_raw_ptr(rd.elem_type);
                    }
                }
                if (const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr) {
                    // Aliased imports (`import "core:tako" as tk`) resolve to their canonical namespace.
                    std::string ns_name = obj_id->name;
                    if (!StdlibCatalogIndex::instance().has_namespace(ns_name) && !is_ident_in_scope_sema(ns_name)) {
                        if (std::string canon = resolve_stdlib_ns_sema(ns_name); !canon.empty()) ns_name = canon;
                    }
                    if (const auto* sfn = StdlibCatalogIndex::instance().find_ns_fn(ns_name, mem->field)) {
                        if (!sfn->unwrapped_return_type.empty() && sfn->unwrapped_return_type != "void") {
                            // Generic stdlib functions (`json.parse[T]`, `db.query[T]`) return their type argument.
                            const std::string& ur = sfn->unwrapped_return_type;
                            if (!call->type_args.empty() && (ur == "T" || ur == "[]T" || ur == "T?")) {
                                const std::string& targ = call->type_args[0].name;
                                if (!targ.empty()) return type_id_of(ur == "[]T" ? "[]" + targ : targ);
                            }
                            if (ur == "T" || ur == "[]T") return infer_named_type_id_(e);
                            return type_id_of(qualify_stdlib_type_sema(*sfn, obj_id->name));
                        }
                    }
                    if (auto it = var_types_.find(obj_id->name); it != var_types_.end()) {
                        std::string recv_t = it->second;
                        const std::string recv_full = canonical_type_name_sema(it->second);
                        std::string base_recv = recv_t;
                        std::string generic_arg;
                        if (auto br = base_recv.find('['); br != std::string::npos && base_recv.ends_with(']')) {
                            generic_arg = base_recv.substr(br + 1, base_recv.size() - br - 2);
                            base_recv = base_recv.substr(0, br);
                        }
                        std::string unqual_recv = base_recv;
                        if (auto dot = unqual_recv.rfind('.'); dot != std::string::npos) unqual_recv = unqual_recv.substr(dot + 1);
                        if (auto dcol = unqual_recv.rfind("::"); dcol != std::string::npos) unqual_recv = unqual_recv.substr(dcol + 2);

                        auto check_mit = [&](const std::string& key) -> TypeId {
                            if (auto mit = StdlibCatalogIndex::instance().receiver_methods.find(key);
                                mit != StdlibCatalogIndex::instance().receiver_methods.end()) {
                                if (!mit->second.unwrapped_return_type.empty() && mit->second.unwrapped_return_type != "void") {
                                    if (!generic_arg.empty() && mit->second.unwrapped_return_type == "T") {
                                        return type_id_of(generic_arg);
                                    }
                                    const auto dotq = recv_full.rfind('.');
                                    return type_id_of(qualify_stdlib_type_sema(mit->second, dotq == std::string::npos
                                        ? local_qualifier_for_module_sema(mit->second.module_import, mit->second.ns_alias)
                                        : recv_full.substr(0, dotq)));
                                }
                            }
                            return TypeId::Unknown();
                        };

                        if (TypeId tid = check_mit(recv_t + "." + mem->field); !tid.is_unknown()) return tid;
                        if (TypeId tid = check_mit(base_recv + "." + mem->field); !tid.is_unknown()) return tid;
                        if (auto dot = base_recv.find('.'); dot != std::string::npos) {
                            if (std::string cns = resolve_stdlib_ns_sema(base_recv.substr(0, dot)); !cns.empty()) {
                                if (TypeId tid = check_mit(cns + base_recv.substr(dot) + "." + mem->field); !tid.is_unknown()) return tid;
                            }
                        }
                        if (TypeId tid = check_mit(unqual_recv + "." + mem->field); !tid.is_unknown()) return tid;
                    }
                } else if (mem->object) {
                    if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                            if (const auto* sfn = StdlibCatalogIndex::instance().find_ns_fn(sub_id->name, sub_mem->field + "." + mem->field)) {
                                if (!sfn->unwrapped_return_type.empty() && sfn->unwrapped_return_type != "void") {
                                    return type_id_of(qualify_stdlib_type_sema(*sfn, sub_id->name));
                                }
                            }
                        }
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
    // Primitives, str, &str, slices/arrays of those, and nominal types declared in this program.
    CFIRE_SEMA_INLINE auto SemaFnChecker::is_concrete_checkable_type(TypeId t) const -> bool {
        if (t.is_unknown()) return false;
        if (t.index >= TypeId::Bool().index && t.index <= TypeId::Str().index) return true;
        const auto& d = cf_.type_table.get(t);
        switch (d.kind) {
            case TypeKind::Borrow:
            case TypeKind::Slice:
            case TypeKind::Array:
                return is_concrete_checkable_type(d.elem_type);
            case TypeKind::NominalStruct:
            case TypeKind::NominalEnum:
                return cf_.struct_tags.contains(d.name) || cf_.enums.contains(d.name);
            default:
                return false;
        }
    }


    // a type argument bound to `T: Iface` (including compound `T: A + B` and `pkg.Trait`) must
    // implement every method of every interface bound (name, receiver/param intents, parameter types, return type).
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_interface_constraints(const CallExpr& call, const FnDecl& sig, int line, int col)
        -> std::expected<void, std::string> {
        for (std::size_t tp_idx = 0; tp_idx < sig.type_params.size(); ++tp_idx) {
            const auto& tp = sig.type_params[tp_idx];
            if (tp.constraints.empty()) continue;
            // Find the argument whose parameter is exactly `T` and take its concrete type.
            std::string concrete;
            int arg_line = line, arg_col = col;
            for (std::size_t i = 0; i < sig.params.size() && i < call.args.size(); ++i) {
                if (format_type_ref_sema(sig.params[i].type) != tp.name) continue;
                concrete = type_name_of(call.args[i].value);
                arg_line = call.args[i].line;
                arg_col = call.args[i].col;
                break;
            }
            if (concrete.empty() && tp_idx < call.type_args.size()) {
                concrete = format_type_ref_sema(call.type_args[tp_idx]);
            }
            if (concrete.empty() || concrete == "unknown" || concrete == "any" || fn_type_params_.contains(concrete)) continue;
            if (concrete.find("cpp::") != std::string::npos || concrete.find("cpp.") != std::string::npos) continue;
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
                if (iit == cf_.interface_defs.end()) continue;
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
        if (!e || !want.is_valid() || want.is_unknown()) return {};
        const auto& wd = cf_.type_table.get(want);
        if ((wd.kind == TypeKind::Optional || wd.kind == TypeKind::Fallible) && wd.elem_type.is_valid()) {
            if (!std::holds_alternative<ArrayLitExpr>(e->node) && !std::holds_alternative<TupleExpr>(e->node) &&
                !is_numeric_literal_expr(e) && expr_returns_optional_sema(e)) return {};
            return check_value_for(wd.elem_type, e, handled);
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
                    bool h = false;
                    if (auto r = check_value_for(elem_want, elem_e, h); !r) return r;
                    if (h || !elem_e) continue;
                    TypeId got = infer_expr_type_id_sema(elem_e);
                    if (!is_concrete_checkable_type(elem_want) || !is_concrete_checkable_type(got) || expr_returns_optional_sema(elem_e)) continue;
                    if (!cf_.type_table.is_assignable(elem_want, got)) {
                        if (auto r = emit_error(diag_at(elem_e->line, leftmost_col(elem_e), "cfire/TYPE-MISMATCH",
                            std::format("tuple element {} expects '{}', got '{}'{}", i, cf_.type_table.to_string(elem_want), cf_.type_table.to_string(got), nominal_mismatch_hint_sema(elem_want, got)))); !r) return r;
                    }
                }
                return {};
            }
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            if (wd.kind != TypeKind::Slice && wd.kind != TypeKind::Array) return {};
            handled = true;
            const TypeId elem = wd.elem_type;
            for (const auto& el : arr->elements) {
                bool h = false;
                if (auto r = check_value_for(elem, el, h); !r) return r;
                if (h || !el) continue;
                TypeId got = infer_expr_type_id_sema(el);
                if (!is_concrete_checkable_type(elem) || !is_concrete_checkable_type(got) || expr_returns_optional_sema(el)) continue;
                if (!cf_.type_table.is_assignable(elem, got)) {
                    if (auto r = emit_error(diag_at(el->line, leftmost_col(el), "cfire/TYPE-MISMATCH", std::format("array element expects '{}', got '{}'{}", cf_.type_table.to_string(elem), cf_.type_table.to_string(got), nominal_mismatch_hint_sema(elem, got)))); !r) return r;
                }
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
                if (!es || !es->expr) { all = false; continue; }
                bool h = false;
                if (auto r = check_value_for(want, es->expr, h); !r) return r;
                if (!h) {
                    TypeId got = infer_expr_type_id_sema(es->expr);
                    if (!is_concrete_checkable_type(got) || !cf_.type_table.is_assignable(want, got)) all = false;
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
            if (cf_.type_table.is_integer(got) && !cf_.type_table.int_widens(want, got) && !expr_returns_optional_sema(e)) {
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
        std::unordered_set<std::string> type_params;
        for (const auto& tp : sig.type_params) type_params.insert(tp.name);
        auto mentions_type_param = [&](const TypeRef& tr) {
            std::string n = format_type_ref_sema(tr);
            for (const auto& tp : type_params) {
                if (n == tp || n.find("[" + tp + "]") != std::string::npos || n.ends_with(tp)) return true;
            }
            return false;
        };
        for (std::size_t i = 0; i < call.args.size() && i < sig.params.size(); ++i) {
            const auto& arg = call.args[i];
            const ParamDecl* param = &sig.params[i];
            if (!arg.label.empty()) {
                auto it = std::find_if(sig.params.begin(), sig.params.end(), [&](const ParamDecl& pd) { return pd.name == arg.label; });
                if (it == sig.params.end()) continue;
                param = &*it;
            }
            if (mentions_type_param(param->type) || param->type.name.starts_with("fn")) continue;
            TypeId want = cf_.type_table.from_typeref(param->type);
            bool handled = false;
            if (auto r = check_value_for(want, arg.value, handled); !r) return r;
            if (handled) continue;
            TypeId got = infer_expr_type_id_sema(arg.value);
            if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got)) continue;
            if (expr_returns_optional_sema(arg.value)) continue;  // handled by the optional checks
            if (!cf_.type_table.is_assignable(want, got)) {
                if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/TYPE-MISMATCH", std::format("argument {} of '{}' expects '{}', got '{}'{}", i + 1, sig.name, cf_.type_table.to_string(want), cf_.type_table.to_string(got), nominal_mismatch_hint_sema(want, got)))); !r) return r;
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
