// Generated shard for SemaFnChecker (call)



    // `make(T[, args])`, `make(own[T][, args])`, `make(shared[T][, args])`, `make([]T, len[, cap])`, `make(map[K]V)`.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_make_call(const CallExpr& call, int line, int col) -> std::expected<void, std::string> {
        if (call.type_args.empty()) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", "'make' needs a type as its first argument: 'make(T, args...)'"));
        }
        if (!call.make_type_first) {
            return emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'make[{0}](...)' has been replaced by 'make({0}, ...)'", format_type_ref_sema(call.type_args[0])), {{"rewrite", "make_bracket"}}));
        }
        const TypeRef& t = call.type_args[0];
        const std::size_t n = call.args.size();
        std::string tname = format_type_ref_sema(t);
        for (auto p = tname.find("::"); p != std::string::npos; p = tname.find("::")) tname.replace(p, 2, ".");
        auto check_ctor_args = [&](const FnDecl& ctor, std::string_view shown) -> std::expected<void, std::string> {
            if (n != ctor.params.size()) {
                return emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("constructor of '{}' expects {} argument(s), got {}", shown, ctor.params.size(), n)));
            }
            return check_call_argument_types(call, ctor);
        };
        auto integer_args = [&](std::size_t from) -> std::expected<void, std::string> {
            for (std::size_t i = from; i < n; ++i) {
                TypeId at = infer_expr_type_id_sema(call.args[i].value);
                if (is_concrete_checkable_type(at) && !cf_.type_table.is_integer(at)) {
                    return emit_error(diag_at(call.args[i].line, call.args[i].col, "cfire/TYPE-MISMATCH", std::format("'make({}, ...)' sizes must be integers, got '{}'", tname, cf_.type_table.to_string(at))));
                }
            }
            return {};
        };
        if (!t.smart_wrap.empty() && t.elem_type) {
            if (n == 1) {
                TypeId want = type_id_of(*t.elem_type);
                TypeId got = infer_expr_type_id_sema(call.args[0].value);
                if (want.is_valid() && got.is_valid() && cf_.type_table.is_assignable(want, got)) {
                    call.make_from_value = true;  // `make(own[T], v)` boxes an existing `T`
                    return {};
                }
            }
            if (const FnDecl* ctor = find_user_ctor(*t.elem_type)) return check_ctor_args(*ctor, format_type_ref_sema(*t.elem_type));
            if (n > 1) {
                return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({}, ...)' takes one '{}' value, or the arguments of a constructor 'fn ({}) make(...)', which '{}' does not declare", tname, format_type_ref_sema(*t.elem_type), t.elem_type->name, t.elem_type->name)));
            }
            return {};
        }
        if (t.is_slice) {
            if (n < 1 || n > 2) {
                return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({}, len)' or 'make({}, len, cap)' expects 1 or 2 sizes, got {}", tname, tname, n)));
            }
            return integer_args(0);
        }
        if (t.is_map || t.is_array) {
            if (n != 0) {
                return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({})' takes no further arguments, got {}", tname, n)));
            }
            return {};
        }
        if (const FnDecl* ctor = find_user_ctor(t)) return check_ctor_args(*ctor, tname);
        // Foreign C++ / vendor types (`make(cpp.std.pair[i32, str], 1, "x")`) take their C++ constructor's arguments.
        if (auto sep = tname.find('.'); sep != std::string::npos) {
            std::string q = tname.substr(0, sep);
            if (q == "cpp" || is_cpp_qualifier_sema(q) || is_vendor_alias_sema(q)) return {};
        }
        if (const auto* sc = find_stdlib_ctor(t.name)) {
            if (sc->type == "Mutex" && !t.type_args.empty() && t.type_args[0].name == "void") {
                if (n != 0) return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({})' is a pure lock and takes no initial value", tname)));
                return {};
            }
            const auto& idx = StdlibCatalogIndex::instance();
            if (const auto* sig = idx.find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                if (n < sig->min_args || n > sig->max_args) {
                    std::string expected = sig->min_args == sig->max_args ? std::to_string(sig->min_args) : std::format("{} to {}", sig->min_args, sig->max_args);
                    return emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("'make({}, ...)' expects {} argument(s), got {}", tname, expected, n)));
                }
            }
            return {};
        }
        if (n != 0) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("type '{}' declares no constructor, so 'make({})' takes no arguments; declare 'fn ({}) make(...) -> {}' or use a struct literal", tname, tname, t.name, t.name)));
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_call_expr(const CallExpr& call, int line, int col, const ExprPtr& self_expr) -> std::expected<void, std::string> {
        // `.data` / `.data_ptr` / `.byte_ptr` handed out raw pointers. A slice or `str` passed to an
        // `import cpp` function lowers to its buffer pointer by itself.
        if (const auto* pm = call.callee ? std::get_if<MemberExpr>(&call.callee->node) : nullptr;
            pm && pm->object && (pm->field == "data" || pm->field == "data_ptr" || pm->field == "byte_ptr" || pm->field == "byte_ptr_at")) {
            std::string rt = type_name_of(pm->object);
            if (rt.starts_with("mut ")) rt.erase(0, 4);
            if (builtin_recv_bits_for_type_name(rt) != 0) {
                return emit_error(diag_at(line, col, "cfire/RAW-POINTER", std::format(
                    "'.{}()' returned a raw pointer, which cfire keeps to C++ interop: pass the slice or 'str' itself to the "
                    "'import cpp' function (it lowers to the buffer pointer), or take the pointer inside a 'cpp {{ }}' block", pm->field)));
            }
        }
        record_closure_param_hints(call);
        if (call.callee) {
            if (const auto* cm = std::get_if<MemberExpr>(&call.callee->node)) callee_members_.insert(cm);
        }
        // Go-style free `len(x)` / `cap(x)` are reserved names but not functions; they used to reach clang.
        if (const auto* cid = call.callee ? std::get_if<IdentExpr>(&call.callee->node) : nullptr;
            cid && (cid->name == "len" || cid->name == "cap") && !is_ident_in_scope_sema(cid->name) && !cf_.fn_sigs.contains(cid->name)) {
            std::string recv = "x";
            if (call.args.size() == 1 && call.args[0].value) {
                if (const auto* aid = std::get_if<IdentExpr>(&call.args[0].value->node)) recv = aid->name;
            }
            return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("'{0}' is a method, not a function; use '{1}.{0}()'", cid->name, recv)));
        }
        if (auto r = check_expr(call.callee); !r) return r;
        const FnDecl* target_sig = nullptr;
        const StdFnSigInfo* std_sig = nullptr;
        const StdlibStruct* build_cfg_st = nullptr;
        const auto& catalog_idx = StdlibCatalogIndex::instance();

        for (const auto& ta : call.type_args) {
            if (auto iface = find_interface_in_slice(ta, cf_.interfaces)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface))); !r) return r;
            }
        }

        if (const auto* conv = std::get_if<IdentExpr>(&call.callee->node);
            conv && call.args.size() == 1 && !is_ident_in_scope_sema(conv->name)) {
            if (auto r = check_int_literal_fits(call.args[0].value, conv->name, line, col); !r) return r;
            // `usize(x)` on an optional (`s.index_of(":")` is `usize?`) must unwrap first; clang used to report it.
            if (primitive_type_prefix(call.callee).is_valid() && conv->name != "bool" && expr_returns_optional_sema(call.args[0].value)) {
                if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("cannot convert an optional value to '{}'; unwrap it first ('x or default', 'x or {{ ... }}')", conv->name))); !r) return r;
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node); id && id->name == "make" && !is_ident_in_scope_sema("make")) {
            if (auto r = check_make_call(call, line, col); !r) return r;
        }
        // an optional has no text form; `println("{}", s.index_of("z"))` must unwrap (`x or default`) first.
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node);
            id && (id->name == "println" || id->name == "print" || id->name == "eprintln") && !is_ident_in_scope_sema(id->name)) {
            for (std::size_t i = call.args.size() > 1 ? 1 : 0; i < call.args.size(); ++i) {
                const auto& a = call.args[i];
                if (!a.value || std::holds_alternative<LiteralExpr>(a.value->node) || !expr_returns_optional_sema(a.value)) continue;
                if (auto r = emit_error(diag_at(a.line, a.col, "cfire/TYPE-MISMATCH", std::format("'{}' cannot format an optional value; unwrap it first ('x or default', 'x or {{ ... }}', or compare with 'none')", id->name))); !r) return r;
            }
        }
        // `println(a, b)`: with more than one argument the first is the format string (clang used to report this).
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node);
            id && (id->name == "println" || id->name == "print" || id->name == "eprintln") && !is_ident_in_scope_sema(id->name) && call.args.size() > 1) {
            TypeId ft = infer_expr_type_id_sema(call.args[0].value);
            if (is_concrete_checkable_type(ft) && ft != TypeId::Str() && !expr_returns_optional_sema(call.args[0].value)) {
                if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("the first argument of '{}' with several arguments is the format string, got '{}'; write '{}(\"{{}} {{}}\", a, b)'", id->name, cf_.type_table.to_string(ft), id->name))); !r) return r;
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if ((id->name == "error" || id->name == "Err") && !is_ident_in_scope_sema(id->name)) {
                if (call.args.empty() || call.args.size() > 2) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/WRONG-ARG-COUNT", std::format("'error' requires 1 or 2 arguments (message [, code]); got {}", call.args.size()))); !r) return r;
                }
            }
            if ((id->name == "readln" || id->name == "input") && !is_ident_in_scope_sema(id->name)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("builtin '{}' has been removed; use 'cli.prompt(...)' for interactive prompts or 'os.stdin().read_line()' for stdin streams", id->name))); !r) return r;
            }
            if ((id->name == "shared" || id->name == "own") && !is_ident_in_scope_sema(id->name)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/INVALID-SMART-PTR-CTOR", std::format("cannot call '{}' directly as a constructor; use 'make({}[T], ...)' instead", id->name, id->name))); !r) return r;
            }
            if (!pkg_name_.empty()) {
                if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + id->name); pit != cf_.fn_sigs.end()) {
                    target_sig = &pit->second;
                }
            }
            if (!target_sig) {
                if (auto it = cf_.fn_sigs.find(id->name); it != cf_.fn_sigs.end()) {
                    target_sig = &it->second;
                }
            }
        } else if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
            if (obj_id && obj_id->name == "ai" && !is_ident_in_scope_sema("ai") &&
                (mem->field == "embed" || mem->field == "embed_image" || mem->field == "ocr" || mem->field == "detect_language")) {
                if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'ai.{}' is a stateful Client method; construct a client first (e.g. 'client := ai.local()') and call 'client.{}(...)'", mem->field, mem->field))); !r) return r;
            }
            if (obj_id && obj_id->name == "fs" && !is_ident_in_scope_sema("fs")) {
                if (mem->field == "read_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'fs.read_all' has been renamed to 'fs.read_bytes' (or 'fs.read_text' for UTF-8 strings)"), {{"from", "fs.read_all"}, {"to", "fs.read_bytes"}})); !r) return r;
                } else if (mem->field == "write_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'fs.write_all' has been renamed to 'fs.write_bytes' (or 'fs.write_text' for UTF-8 strings)"), {{"from", "fs.write_all"}, {"to", "fs.write_bytes"}})); !r) return r;
                }
            }
            if (obj_id && obj_id->name == "os" && !is_ident_in_scope_sema("os")) {
                if (mem->field == "read_stdin" || mem->field == "read_stdin_bytes" || mem->field == "read_stdin_lines") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.{}' has been replaced by 'os.stdin().read_text()', 'os.stdin().read_bytes()', or 'os.stdin().read_lines()'", mem->field), {{"from", "os." + std::string(mem->field)},
                        {"to", mem->field == "read_stdin_lines" ? "try os.stdin().read_lines"
                             : mem->field == "read_stdin_bytes" ? "try os.stdin().read_bytes" : "try os.stdin().read_text"}})); !r) return r;
                } else if (mem->field == "fd_read" || mem->field == "fd_read_bytes" || mem->field == "fd_write" ||
                           mem->field == "fd_close" || mem->field == "poll_fd") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.{}' has been removed; use methods on 'fs.File' ('os.stdin()', 'os.stdout()', 'os.stderr()', 'os.pipe()') or 'net.TcpConn'", mem->field))); !r) return r;
                } else if (mem->field == "socketpair") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.socketpair' has been moved to 'net.socketpair() -> (net.TcpConn, net.TcpConn)!'"))); !r) return r;
                } else if (mem->field == "is_tty" && !call.args.empty()) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.is_tty(fd)' no longer accepts a raw FD; use 'os.is_tty()' for stdin or call '.is_tty()' on an 'fs.File' handle (e.g. 'os.stdout().is_tty()')"))); !r) return r;
                }
            }
            // `o.release` moves ownership out as a raw pointer; `own[T].from_raw(p)` adopts one.
            if (mem->field == "release" && smart_wrap_of(mem->object) == "own") {
                if (!call.args.empty()) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", "'release' takes no arguments")); !r) return r;
                }
                if (auto pi = extract_path_info(mem->object)) moved_at_line_[pi->root] = line;
                return {};
            }
            if (std::string own_t = own_type_prefix(mem->object); !own_t.empty() && mem->field == "from_raw") {
                if (call.args.size() != 1) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("'{}.from_raw' expects 1 argument (a raw pointer), got {}", own_t, call.args.size()))); !r) return r;
                } else {
                    if (auto r = check_expr(call.args[0].value); !r) return r;
                    TypeId at = infer_expr_type_id_sema(call.args[0].value);
                    if (is_concrete_checkable_type(at)) {
                        const auto k = cf_.type_table.get(at).kind;
                        if (k != TypeKind::RawPtr && k != TypeKind::MutPtr) {
                            if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("'{}.from_raw' expects a raw pointer, got '{}'", own_t, cf_.type_table.to_string(at)))); !r) return r;
                        }
                    }
                }
                return {};
            }
            bool retired_call_reported = false;
            if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) {
                std::string tname = cf_.type_table.to_string(prim);
                if (mem->field != "parse") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-FIELD", std::format("type '{}' has no associated function '{}'; use '{}.parse(s)'", tname, mem->field, tname))); !r) return r;
                } else if (call.args.size() != 1) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("'{}.parse' expects 1 argument, got {}", tname, call.args.size()))); !r) return r;
                } else if (TypeId at = infer_expr_type_id_sema(call.args[0].value); is_concrete_checkable_type(at) && !cf_.type_table.is_assignable(TypeId::Str(), at)) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("'{}.parse' expects 'str', got '{}'", tname, cf_.type_table.to_string(at)))); !r) return r;
                }
                if (!call.args.empty()) { if (auto r = check_expr(call.args[0].value); !r) return r; }
                return {};
            }
            if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                std::string ns = resolve_stdlib_ns_sema(obj_id->name);
                if (ns.empty()) ns = obj_id->name;
                if (const StdlibCtor* sc = find_stdlib_ctor_by_old_fn(ns, mem->field)) {
                    // library constructors are `make(ns.Type, args...)`. Generic ones need the type argument,
                    // taken from `[T]` when spelled or from the initial value otherwise.
                    std::string to_type = obj_id->name + "." + std::string(sc->type);
                    if (sc->generic) {
                        if (mem->field == "new_mutex") {
                            to_type += "[void]";
                        } else if (call.type_args.empty() && !call.args.empty()) {
                            TypeId at = infer_expr_type_id_sema(call.args[0].value);
                            to_type += "[" + (is_concrete_checkable_type(at) ? cf_.type_table.to_string(at) : std::string("T")) + "]";
                        }
                    }
                    std::string shown = to_type.find('[') == std::string::npos && !call.type_args.empty()
                        ? to_type + "[" + format_type_ref_sema(call.type_args[0]) + "]" : to_type;
                    retired_call_reported = true;
                    if (auto r = emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'{}.{}(...)' has been replaced by 'make({}, ...)'", obj_id->name, mem->field, shown), {{"rewrite", "ctor_to_make"}, {"from", obj_id->name + "." + std::string(mem->field)}, {"to_type", to_type}})); !r) return r;
                }
                if (const ApiMigration* mig = find_api_migration(ns, mem->field)) {
                    std::string from = obj_id->name + "." + mem->field;
                    std::string msg;
                    std::unordered_map<std::string, std::string> facts;
                    switch (mig->kind) {
                        case ApiMigrationKind::CallToMethod:
                            msg = std::format("'{}(x, ...)' has been removed; call the method 'x.{}(...)' instead", from, mig->to);
                            facts = {{"rewrite", "call_to_method"}, {"from", from}, {"method", std::string(mig->to)}};
                            break;
                        case ApiMigrationKind::Rename:
                            msg = std::format("'{}' has been removed; use '{}'", from, mig->to);
                            facts = {{"from", from + "("}, {"to", std::string(mig->to) + "("}};
                            break;
                        case ApiMigrationKind::CallToConst:
                            msg = std::format("'{}()' has been removed; use the constant '{}'", from, mig->to);
                            facts = {{"from", from + "()"}, {"to", std::string(mig->to)}};
                            break;
                        case ApiMigrationKind::RenameFn:
                            msg = std::format("'{}' has been renamed to '{}.{}'", from, obj_id->name, mig->to);
                            facts = {{"from", from + "("}, {"to", obj_id->name + "." + std::string(mig->to) + "("}};
                            break;
                        case ApiMigrationKind::SwapToFn:
                            msg = std::format("'{}(host, port)' has been replaced by '{}.{}(port, host)'", from, obj_id->name, mig->to);
                            facts = {{"rewrite", "swap_to_fn"}, {"from", from}, {"to", obj_id->name + "." + std::string(mig->to)}};
                            break;
                        case ApiMigrationKind::TlsServerFiles:
                        case ApiMigrationKind::TlsServerPem: {
                            const bool pem = mig->kind == ApiMigrationKind::TlsServerPem;
                            msg = std::format("'{}' has been replaced by 'make({}.Server, port, tls: {}.TlsConfig{{{}: ..., {}: ...}})'", from, obj_id->name, obj_id->name, pem ? "cert_pem" : "cert_file", pem ? "key_pem" : "key_file");
                            facts = {{"rewrite", "tls_server"}, {"from", from}, {"ns", obj_id->name}, {"pem", pem ? "1" : ""}};
                            break;
                        }
                        case ApiMigrationKind::AppendVerifyOff:
                            msg = std::format("'{}' has been removed; pass 'verify_peer: false' to '{}.{}' instead", from, obj_id->name, mig->to);
                            facts = {{"rewrite", "append_verify_off"}, {"from", from}, {"to", obj_id->name + "." + std::string(mig->to)}};
                            break;
                    }
                    if (auto r = emit_error(diag_at(line, col, "cfire/RETIRED-API", std::move(msg), std::move(facts))); !r) return r;
                    retired_call_reported = true;
                }
            }
            if (mem->field == "find" || mem->field == "rfind") {
                bool is_str_ns = (obj_id && obj_id->name == "str" && !is_ident_in_scope_sema(obj_id->name));
                if (!is_str_ns && mem->object && type_name_of(mem->object) == "str") {
                    std::string_view canonical = (mem->field == "rfind") ? "last_index_of" : "index_of";
                    if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD", std::format("unknown str method '{}'; use '{}' instead (returns 'usize?')", mem->field, canonical))); !r) return r;
                }
            }
            if (mem->object && !(obj_id && catalog_idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name))) {
                if (auto r = check_builtin_method_surface(*mem, line, col); !r) return r;
                if (auto r = check_builtin_method_call(call, *mem, line, col); !r) return r;
            }
            target_sig = resolve_member_call_sig(*mem);
            if (!target_sig && (!obj_id || !catalog_idx.has_namespace(obj_id->name) || is_ident_in_scope_sema(obj_id->name))) {
                if (mem->field == "read_str") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.read_str' has been renamed to '.read_text'"), {{"from", ".read_str"}, {"to", ".read_text"}})); !r) return r;
                } else if (mem->field == "write_str") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.write_str' has been renamed to '.write_text'"), {{"from", ".write_str"}, {"to", ".write_text"}})); !r) return r;
                } else if (mem->field == "sftp_read_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.sftp_read_all' has been renamed to '.sftp_read_text' or '.sftp_read_bytes'"))); !r) return r;
                } else if (mem->field == "sftp_write_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.sftp_write_all' has been renamed to '.sftp_write_text' or '.sftp_write_bytes'"))); !r) return r;
                }
                std::string recv_t = mem->object ? type_name_of(mem->object) : "";
                bool is_stream_handle = (recv_t == "File" || recv_t == "fs.File" || recv_t == "fs::File" ||
                                         recv_t == "TcpConn" || recv_t == "net.TcpConn" || recv_t == "net::TcpConn" ||
                                         recv_t == "ssh.Channel" || recv_t == "ssh::Channel" ||
                                         (recv_t == "Channel" && is_stdlib_ns_imported("ssh")));
                if (is_stream_handle) {
                    if (mem->field == "read_all") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.read_all' on File has been replaced by '.read_bytes()' or '.read_text()'"))); !r) return r;
                    } else if (mem->field == "read") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.read' has been replaced by explicit '.read_bytes', '.read_text', or '.read_into'"))); !r) return r;
                    } else if (mem->field == "write") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.write' has been replaced by explicit '.write_bytes' or '.write_text'"))); !r) return r;
                    }
                }
            }
            if (!target_sig && obj_id) {
                std::string resolved_ns = resolve_stdlib_ns_sema(obj_id->name);
                if (!resolved_ns.empty() && catalog_idx.has_namespace(resolved_ns) && !is_ident_in_scope_sema(obj_id->name)) {
                    std_sig = catalog_idx.find_ns_fn(resolved_ns, mem->field);
                    if (std_sig && resolved_ns == "build" && mem->field != "os" && mem->field != "arch") {
                        const bool manifest = std::filesystem::path(cf_.ast.path).filename() == "build.cf" || cf_.ast.package_name == "build";
                        if (!manifest) {
                            if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-STDLIB-FN", std::format("'{}.{}' is a build.cf manifest function and cannot be called in a program; only 'build.os()' and 'build.arch()' are available outside build.cf", obj_id->name, mem->field))); !r) return r;
                        } else {
                            build_cfg_st = find_stdlib_struct("build", mem->field);
                        }
                    }
                    if (!std_sig && !retired_call_reported && !is_vendor_alias_sema(obj_id->name) && obj_id->name != "cpp" && !is_cpp_qualifier_sema(obj_id->name) &&
                        !catalog_idx.is_known_ns_member(resolved_ns, mem->field)) {
                        std::string sugg = catalog_idx.suggest_closest_fn(resolved_ns, mem->field);
                        std::string hint = sugg.empty() ? "" : std::format("; did you mean '{}.{}'?", obj_id->name, sugg);
                        if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-STDLIB-FN", std::format("unknown function '{}.{}' in module '{}'{}", obj_id->name, mem->field, catalog_idx.import_path_for(resolved_ns), hint))); !r) return r;
                    }
                } else if (auto dot = obj_id->name.find('.'); dot != std::string::npos) {
                    std::string ns_part = obj_id->name.substr(0, dot);
                    std::string type_part = obj_id->name.substr(dot + 1);
                    if (catalog_idx.has_namespace(ns_part) && !is_ident_in_scope_sema(ns_part)) {
                        std_sig = catalog_idx.find_ns_fn(ns_part, type_part + "." + mem->field);
                    }
                }
            }
            if (!target_sig && !std_sig && mem->object) {
                std::string recv_t = type_name_of(mem->object);
                if (recv_t == "File" || recv_t == "fs.File" || recv_t == "fs::File" ||
                    recv_t == "TcpConn" || recv_t == "net.TcpConn" || recv_t == "net::TcpConn") {
                    std::string norm_t = (recv_t.ends_with("File")) ? "File" : "TcpConn";
                    if (auto mit = catalog_idx.receiver_methods.find(norm_t + "." + mem->field); mit != catalog_idx.receiver_methods.end()) {
                        std_sig = &mit->second;
                    }
                }
            }
            if (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut") {
                if (obj_id && imm_vars_.contains(obj_id->name) && !smart_ptr_vars_.contains(obj_id->name)) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/MUT-RECEIVER", std::format("cannot call mutating method '{}' on immutable variable '{}'", mem->field, obj_id->name), {{"var", obj_id->name}})); !r) return r;
                }
            }
            if (mem->field == "push" && !call.args.empty() && is_byte_slice_expr(mem->object)) {
                if (is_non_byte_int_or_u8_expr(call.args[0].value)) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("cannot push a wider integer into '[]byte' without an explicit 'byte(...)' conversion"))); !r) return r;
                }
            }
            const BuiltinMethod* mut_bm = lookup_builtin_method(*mem);
            if ((mut_bm && mut_bm->mutates) || mem->field == "push" || mem->field == "pop" || mem->field == "clear" ||
                mem->field == "erase" || mem->field == "remove") {
                if (auto pi = extract_path_info(mem->object); pi && slot_captured_vars_.contains(pi->root) && !active_slot_captures_.contains(pi->root)) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/SLOT-CAPTURE", std::format(
                            "'{}' cannot change while tasks write its elements; call '.{}()' after the 'async.scope' ends", pi->root, mem->field))); !r) return r;
                }
                if (auto pi = extract_path_info(mem->object)) {
                    if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying container '{}' was mutated at line {}", pi->root, line),
                        line);
                }
            }
            if (!call.args.empty() && !is_constant_string_expr(call.args[0].value)) {
                // an error, not a warning, and it follows data through locals and helper calls.
                const auto* recv_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (recv_id && recv_id->name == "os" && !is_ident_in_scope_sema("os") &&
                    (mem->field == "exec" || mem->field == "exec_output")) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/SHELL-INJECTION-RISK", std::format("shell command passed to 'os.{}' is not a constant string, so runtime data can inject shell syntax; use 'os.command(binary, args)' with separate arguments", mem->field))); !r) return r;
                } else if ((!recv_id || recv_id->name != "os") && (mem->field == "exec" || mem->field == "query") &&
                           is_sql_receiver(mem->object)) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/SQL-INJECTION-RISK", std::format("SQL passed to '{}' is not a constant string, so runtime data can inject SQL; keep the statement constant and bind values with '?1', '?2' parameters", mem->field))); !r) return r;
                }
            }
        }

        if (target_sig) {
            std::size_t actual_args = call.args.size() + (call.block_arg ? 1 : 0);
            if (actual_args != target_sig->params.size()) {
                if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("call to '{}' expects {} argument(s), got {}", target_sig->name, target_sig->params.size(), actual_args))); !r) return r;
            }
            if (auto r = check_call_argument_types(call, *target_sig); !r) return r;
            if (auto r = check_interface_constraints(call, *target_sig, line, col); !r) return r;
        } else if (std_sig) {
            std::size_t actual_args = call.args.size() + (call.block_arg ? 1 : 0);
            if (actual_args < std_sig->min_args || actual_args > std_sig->max_args) {
                std::string exp_str = (std_sig->min_args == std_sig->max_args)
                    ? std::format("{}", std_sig->min_args)
                    : (std_sig->max_args >= 512
                        ? std::format("at least {}", std_sig->min_args)
                        : std::format("{}..{}", std_sig->min_args, std_sig->max_args));
                if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("call to '{}.{}' expects {} argument(s), got {}", std_sig->ns_alias, std_sig->name, exp_str, actual_args))); !r) return r;
            }
            for (const auto& a : call.args) {
                if (a.label.empty()) continue;
                if (std::none_of(std_sig->params.begin(), std_sig->params.end(), [&](const StdParamInfo& p) { return p.name == a.label; })) {
                    std::string names;
                    for (const auto& p : std_sig->params) names += (names.empty() ? "" : ", ") + p.name;
                    if (auto r = emit_error(diag_at(a.line, a.col, "cfire/UNKNOWN-ARG-LABEL", std::format("'{}.{}' has no parameter named '{}'; parameters: {}", std_sig->ns_alias, std_sig->name, a.label, names))); !r) return r;
                }
            }
        }

        if (self_expr && !unwrapped_exprs_.contains(self_expr)) {
            std::string fallible_callee;
            std::string fallible_ret;
            if (expr_returns_fallible_sema(self_expr, &fallible_callee, &fallible_ret)) {
                auto [start_ln, start_col] = expr_start_loc(self_expr);
                if (auto r = emit_error(diag_at(start_ln, start_col, "cfire/UNHANDLED-RESULT", std::format("fallible call to '{}' returns '{}' and must be handled with 'try' or 'or'", fallible_callee, fallible_ret))); !r) return r;
            }
        }

        struct SeenArgPath {
            PathInfo info;
            bool is_mut_or_out;
        };
        std::vector<SeenArgPath> seen_paths;

        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (auto p = extract_path_info(mem->object)) {
                bool is_mut = (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut");
                seen_paths.push_back({*p, is_mut});
            }
        }

        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const auto& arg = call.args[i];
            if (auto r = check_expr(arg.value); !r) return r;
            if (build_cfg_st && arg.value) {
                if (const auto* st = std::get_if<StructLitExpr>(&arg.value->node); st && st->type_name.empty()) {
                    if (auto r = check_stdlib_struct_fields(*build_cfg_st, *st, arg.value->line, arg.value->col); !r) return r;
                }
            }
            bool expected_is_move = false;
            if (target_sig && i < target_sig->params.size()) {
                const auto& expected = target_sig->params[i].intent;
                expected_is_move = (expected == "move");
                if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                    std::string_view rule = (expected == "out")
                        ? "cfire/OUT-CALLSITE"
                        : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                    if (auto r = emit_error(diag_at(arg.line, arg.col, rule, std::format("parameter '{}' of {} requires explicit '{}' annotation at call-site", target_sig->params[i].name, target_sig->name, expected), {{"keyword", expected}})); !r) return r;
                } else if (expected == "in" && (arg.intent == "mut" || arg.intent == "out")) {
                    if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/REDUNDANT-CALLSITE-INTENT", std::format("parameter '{}' of '{}' has 'in' intent; remove redundant '{}' annotation at call-site", target_sig->params[i].name, target_sig->name, arg.intent), {{"keyword", arg.intent}})); !r) return r;
                }
                const auto& pt = target_sig->params[i].type;
                if ((pt.is_slice || pt.is_array) && pt.name == "byte") {
                    if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&arg.value->node)) {
                        for (const auto& el : arr_lit->elements) {
                            if (is_non_byte_int_or_u8_expr(el)) {
                                if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("a '[]byte' literal element must be 0..=255 or a 'byte'; convert wider integers with 'byte(...)'"))); !r) return r;
                            }
                        }
                    }
                }
            } else if (std_sig && i < std_sig->params.size()) {
                const auto& expected = std_sig->params[i].intent;
                expected_is_move = (expected == "move");
                if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                    std::string_view rule = (expected == "out")
                        ? "cfire/OUT-CALLSITE"
                        : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                    if (auto r = emit_error(diag_at(arg.line, arg.col, rule, std::format("parameter '{}' of {}.{} requires explicit '{}' annotation at call-site", std_sig->params[i].name, std_sig->ns_alias, std_sig->name, expected), {{"keyword", expected}})); !r) return r;
                } else if (expected == "in" && (arg.intent == "mut" || arg.intent == "out")) {
                    if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/REDUNDANT-CALLSITE-INTENT", std::format("parameter '{}' of '{}.{}' has 'in' intent; remove redundant '{}' annotation at call-site", std_sig->params[i].name, std_sig->ns_alias, std_sig->name, arg.intent))); !r) return r;
                }
            } else if ((arg.intent == "mut" || arg.intent == "out") && call.callee) {
                if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
                    if (!is_ident_in_scope_sema(cid->name) &&
                        (cid->name == "print" || cid->name == "println" || cid->name == "assert" ||
                         cid->name == "len" || cid->name == "cap" || cid->name == "str" ||
                         cid->name == "int" || cid->name == "i8" || cid->name == "i16" ||
                         cid->name == "i32" || cid->name == "i64" || cid->name == "isize" ||
                         cid->name == "u8" || cid->name == "u16" || cid->name == "u32" ||
                         cid->name == "u64" || cid->name == "usize" || cid->name == "byte" ||
                         cid->name == "f32" || cid->name == "f64" || cid->name == "bool" ||
                         cid->name == "make" || cid->name == "panic" || cid->name == "exit")) {
                        if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/REDUNDANT-CALLSITE-INTENT", std::format("builtin '{}' takes read-only 'in' arguments; remove redundant '{}' annotation at call-site", cid->name, arg.intent), {{"keyword", arg.intent}})); !r) return r;
                    }
                }
            }
            if (arg.intent == "move") {
                if (auto r = validate_move_operand(arg.value, expected_is_move, arg.line, arg.col); !r) return r;
            } else {
                if (const auto* mem_c = call.callee ? std::get_if<MemberExpr>(&call.callee->node) : nullptr;
                    mem_c && i == 0 && (mem_c->field == "push" || mem_c->field == "push_back" || mem_c->field == "push_front" || mem_c->field == "insert")) {
                    if (auto r = check_move_only_copy(arg.value, std::format("collection '{}'", mem_c->field), arg.line, arg.col); !r) return r;
                } else if (const auto* id_c = call.callee ? std::get_if<IdentExpr>(&call.callee->node) : nullptr;
                           id_c && id_c->name == "append" && i == 1) {
                    if (auto r = check_move_only_copy(arg.value, "'append'", arg.line, arg.col); !r) return r;
                }
            }
            if (auto pi = extract_path_info(arg.value)) {
                bool is_mut_or_out = (arg.intent == "mut" || arg.intent == "out");
                for (const auto& prev : seen_paths) {
                    if (prev.info.root == pi->root && (prev.is_mut_or_out || is_mut_or_out)) {
                        if (paths_conflict(prev.info.path, pi->path)) {
                            std::string entity = (pi->path == pi->root && prev.info.path == prev.info.root)
                                ? std::format("variable '{}'", pi->root)
                                : std::format("projection '{}'", pi->path);
                            if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/MUT-ALIAS", std::format("cannot alias {} with a mut/out parameter in the same call", entity))); !r) return r;
                        }
                    }
                }
                seen_paths.push_back({*pi, is_mut_or_out});

                if (is_mut_or_out) {
                    if (imm_vars_.contains(pi->root)) {
                        std::string entity = (pi->path == pi->root) ? pi->root : pi->path;
                        if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/IMMUTABLE-MUT-ARG", std::format("cannot pass immutable variable '{}' as '{}' argument", entity, arg.intent), {{"var", entity}})); !r) return r;
                    }
                    if (pi->path == pi->root) {
                        if (auto r = check_not_iterated_collection(pi->root, arg.line, arg.col); !r) return r;
                    }
                }
                if (arg.intent == "move") {
                    if (auto r = check_not_iterated_collection(pi->root, arg.line, arg.col); !r) return r;
                    moved_at_line_[pi->root] = arg.line;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying owner '{}' was moved at line {}", pi->root, arg.line),
                        arg.line);
                }
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if (id->name == "assert" && !call.args.empty()) {
                if (auto nc = extract_null_check(call.args[0].value); nc && nc->second) {
                    ptr_null_state_[nc->first] = NullState::NonNull;
                }
            }
        }
        bool is_foreign_call = false;
        if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
            if (cid->name.starts_with("cpp.") || is_cpp_qualifier_sema(cid->name) || is_vendor_alias_sema(cid->name) || cf_.cpp_fn_params.contains(cid->name)) {
                is_foreign_call = true;
            }
        } else if (const auto* cmem = std::get_if<MemberExpr>(&call.callee->node)) {
            const auto* obj_id = cmem->object ? std::get_if<IdentExpr>(&cmem->object->node) : nullptr;
            if (obj_id && (obj_id->name == "cpp" || is_cpp_qualifier_sema(obj_id->name) || is_vendor_alias_sema(obj_id->name))) {
                is_foreign_call = true;
            }
        }
        if (is_foreign_call) {
            // foreign C++ calls invalidate loans and null facts of arguments passed
            for (const auto& a : call.args) {
                if (!a.value) continue;
                if (auto pi = extract_path_info(a.value)) {
                    invalidate_loans_for_owner(pi->root, "invalidated by foreign C++ call", line);
                    if (auto it = ptr_null_state_.find(pi->root); it != ptr_null_state_.end()) {
                        it->second = NullState::MaybeNull;
                    }
                    if (borrow_origin_.contains(pi->root)) {
                        invalidated_loans_[pi->root] = InvalidatedLoan{"loan invalidated by foreign C++ call", line};
                    }
                }
            }
        }
        if (in_escaping_closure_depth_ > 0 && call.callee) {
            if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
                const BuiltinMethod* esc_bm = lookup_builtin_method(*mem);
                if ((esc_bm && esc_bm->mutates) || mem->field == "push" || mem->field == "pop" || mem->field == "clear" ||
                    mem->field == "insert" || mem->field == "erase" || mem->field == "remove") {
                    std::string root = get_root_variable_name(mem->object);
                    if (!root.empty() && root != "_") {
                        auto sit = var_scope_depth_.find(root);
                        if (sit != var_scope_depth_.end() && sit->second <= escaping_closure_entry_scope_) {
                            std::string r_ty;
                            if (auto tit = var_types_.find(root); tit != var_types_.end()) r_ty = tit->second;
                            if (!is_concurrency_safe_type(r_ty)) {
                                return emit_error(diag_at(line, 0, "cfire/ESCAPING-CAPTURE-MUT", std::format("cannot mutate captured outer variable '{}' inside escaping closure; wrap shared mutable state in 'sync.Mutex[T]', 'sync.Atomic[T]', or communicate via 'sync.Channel[T]'", root)));
                            }
                        }
                    }
                }
            }
        }
        bool is_spawn_call = false;
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (mem->field == "spawn") is_spawn_call = true;
        }
        if (is_spawn_call) {
            const auto* sm = std::get_if<MemberExpr>(&call.callee->node);
            const auto* sid = sm && sm->object ? std::get_if<IdentExpr>(&sm->object->node) : nullptr;
            auto vit = sid ? value_scope_spawns_.find(sid->name) : value_scope_spawns_.end();
            for (const auto& a : call.args) {
                if (a.value) {
                    if (auto* cl = std::get_if<ClosureExpr>(&a.value->node)) {
                        cl->is_escaping = true;
                        spawn_closures_.insert(cl);
                        if (vit != value_scope_spawns_.end()) {
                            if (in_closure_depth_ != 0) {
                                return emit_error(diag_at(line, 0, "cfire/SCOPE-RESULT", std::format(
                                    "tasks of a value-producing 'async.scope' are spawned from its body, not from inside another closure ('{}.spawn')", sid->name)));
                            }
                            vit->second.push_back(cl);
                        }
                    }
                }
            }
        }
        if (call.block_arg) {
            bool was_escaping = call.block_arg->is_escaping || is_spawn_call;
            int saved_escaping_entry_scope = escaping_closure_entry_scope_;
            if (was_escaping) {
                ++in_escaping_closure_depth_;
                escaping_closure_entry_scope_ = scope_depth_;
            }
            int saved_spawn_entry_scope = spawn_entry_scope_;
            if (is_spawn_call) {
                ++in_spawn_closure_depth_;
                spawn_entry_scope_ = scope_depth_;
            }
            auto spawn_guard = cfire::scope_exit([this, is_spawn_call, saved_spawn_entry_scope] {
                if (is_spawn_call) {
                    --in_spawn_closure_depth_;
                    spawn_entry_scope_ = saved_spawn_entry_scope;
                }
            });
            if (is_spawn_call) spawn_closures_.insert(&*call.block_arg);
            if (auto r = check_slot_captures(*call.block_arg, line); !r) return r;
            std::unordered_set<std::string> saved_slots = active_slot_captures_;
            for (const auto& sc : call.block_arg->slot_captures) active_slot_captures_.insert(sc.first);
            auto slots_guard = cfire::scope_exit([this, saved_slots] { active_slot_captures_ = saved_slots; });
            ++in_closure_depth_;
            closure_ret_stack_.emplace_back(&*call.block_arg, in_closure_depth_);
            auto ret_guard = cfire::scope_exit([this] { closure_ret_stack_.pop_back(); });
            if (const auto* sm = std::get_if<MemberExpr>(&call.callee->node); is_spawn_call && sm && sm->object) {
                if (const auto* sid = std::get_if<IdentExpr>(&sm->object->node)) {
                    if (auto vit = value_scope_spawns_.find(sid->name); vit != value_scope_spawns_.end()) {
                        if (in_closure_depth_ != 1) {
                            return emit_error(diag_at(line, 0, "cfire/SCOPE-RESULT", std::format(
                                "tasks of a value-producing 'async.scope' are spawned from its body, not from inside another closure ('{}.spawn')", sid->name)));
                        }
                        vit->second.push_back(&*call.block_arg);
                    }
                }
            }
            int saved_loop_depth = loop_depth_;
            int saved_invalid_unwrap = in_invalid_unwrap_context_;
            loop_depth_ = 0;
            in_invalid_unwrap_context_ = 0;
            auto cl_guard = cfire::scope_exit([this, saved_loop_depth, saved_invalid_unwrap, was_escaping, saved_escaping_entry_scope] {
                --in_closure_depth_;
                if (was_escaping) {
                    --in_escaping_closure_depth_;
                    escaping_closure_entry_scope_ = saved_escaping_entry_scope;
                }
                loop_depth_ = saved_loop_depth;
                in_invalid_unwrap_context_ = saved_invalid_unwrap;
            });
            ScopeGuard sg(*this);
            auto hint_it = closure_param_hints_.find(&*call.block_arg);
            for (std::size_t i = 0; i < call.block_arg->params.size(); ++i) {
                bool is_mut = (i < call.block_arg->param_intents.size()) &&
                              (call.block_arg->param_intents[i] == "mut" || call.block_arg->param_intents[i] == "out");
                bind_scoped_var(call.block_arg->params[i], is_mut);
                bool typed = i < call.block_arg->param_types.size() && call.block_arg->param_types[i].has_value();
                if (typed) {
                    var_types_[call.block_arg->params[i]] = format_type_ref_sema(*call.block_arg->param_types[i]);
                } else if (hint_it != closure_param_hints_.end() && i < hint_it->second.size()) {
                    var_types_[call.block_arg->params[i]] = hint_it->second[i];
                }
            }
            if (auto r = check_block_stmts(call.block_arg->body); !r) return r;
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_match_common(
        const ExprPtr& target,
        bool is_move,
        const std::vector<MatchArm>& arms,
        int line,
        int col,
        bool is_expr_match
    ) -> std::expected<void, std::string> {
        if (auto r = check_expr(target); !r) return r;
        if (is_move && target) {
            if (auto r = validate_move_operand(target, false, line, col); !r) return r;
            if (auto pi = extract_path_info(target)) {
                if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                moved_at_line_[pi->root] = line;
                invalidate_loans_for_owner(
                    pi->root,
                    std::format("underlying owner '{}' was moved at line {}", pi->root, line),
                    line);
            }
        }
        if (auto r = check_match_arms_reachability(arms, line, col); !r) return r;
        bool is_slice_match = false;
        bool is_primitive_match = false;
        for (const auto& a : arms) {
            if (a.is_slice_pattern) is_slice_match = true;
            if (!a.literal_val.empty()) is_primitive_match = true;
        }
        if (!is_expr_match && is_slice_match) {
            for (const auto& arm : arms) {
                ScopeGuard sg(*this);
                bind_match_arm_vars(arm);
                if (auto r = check_block_stmts(arm.body); !r) return r;
            }
            return {};
        }

        std::string enum_name;
        std::string target_t;
        if (target) {
            target_t = type_name_of(target);
            if (cf_.enums.contains(target_t)) {
                enum_name = target_t;
            }
        }
        std::unordered_set<std::string> covered;
        bool has_wildcard = false;

        for (const auto& arm : arms) {
            std::vector<MatchPattern> pats = arm.patterns.empty()
                ? std::vector<MatchPattern>{arm.to_pattern()}
                : arm.patterns;
            for (const auto& pat : pats) {
                if (pat.variant_name == "_") {
                    if (!arm.guard) has_wildcard = true;
                } else {
                    if (!arm.guard) covered.insert(pat.variant_name);
                    if (enum_name.empty() && !pat.enum_prefix.empty() && cf_.enums.contains(pat.enum_prefix)) {
                        enum_name = pat.enum_prefix;
                    }
                }
            }
            ScopeGuard sg(*this);
            bind_match_arm_vars(arm, target_t);
            if (arm.guard) {
                if (auto r = check_expr(arm.guard); !r) return r;
            }
            if (auto r = check_block_stmts(arm.body, is_expr_match); !r) return r;
        }

        bool is_opt = target_t.ends_with('?') || target_t.starts_with("std::optional");
        bool is_res = target_t.ends_with('!') || target_t.starts_with("cfire::Result");

        if (is_opt && !has_wildcard) {
            bool has_some = covered.contains("Some");
            bool has_none = covered.contains("none");
            if (!has_some || !has_none) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("non-exhaustive match on optional '{}': missing '{}'", target_t, !has_some ? "Some" : "none"))); !r) return r;
            }
        } else if (is_res && !has_wildcard) {
            bool has_ok = covered.contains("Ok");
            bool has_err = covered.contains("Err");
            if (!has_ok || !has_err) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("non-exhaustive match on fallible '{}': missing '{}'", target_t, !has_ok ? "Ok" : "Err"))); !r) return r;
            }
        } else if (!enum_name.empty()) {
            if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
                for (const auto& arm : arms) {
                    std::vector<MatchPattern> pats = arm.patterns.empty()
                        ? std::vector<MatchPattern>{arm.to_pattern()}
                        : arm.patterns;
                    for (const auto& pat : pats) {
                        if (pat.variant_name != "_" && pat.literal_val.empty() && !pat.is_slice_pattern) {
                            if (!pat.enum_prefix.empty() && pat.enum_prefix != enum_name) {
                                if (auto r = emit_error(diag_at(pat.line, 0, "cfire/TYPE-MISMATCH", std::format("variant pattern '{}::{}' does not match target enum '{}'", pat.enum_prefix, pat.variant_name, enum_name))); !r) return r;
                            }
                            bool found_var = false;
                            for (const auto& v : eit->second.variants) {
                                if (v.name == pat.variant_name) {
                                    found_var = true;
                                    break;
                                }
                            }
                            if (!found_var) {
                                if (auto r = emit_error(diag_at(pat.line, 1, "cfire/UNKNOWN-ENUM-VARIANT", std::format("unknown variant '{}' for enum '{}'", pat.variant_name, enum_name))); !r) return r;
                            }
                        }
                    }
                }
                if (!has_wildcard) {
                    for (const auto& v : eit->second.variants) {
                        if (!covered.contains(v.name)) {
                            if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("non-exhaustive match on enum '{}': missing variant '{}'", enum_name, v.name), {{"enum", enum_name}, {"missing", v.name}})); !r) return r;
                        }
                    }
                }
            }
        } else if ((is_primitive_match || is_scalar_primitive_type_name(target_t) || target_t == "str") && !has_wildcard) {
            if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("match on '{}' must have an exhaustive wildcard '_' fallback arm", target_t.empty() ? "primitive scalar" : target_t))); !r) return r;
        } else if (is_expr_match && !has_wildcard) {
            if (!is_slice_match || !is_slice_exhaustively_covered(arms)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("match expression must have a wildcard '_' fallback arm"))); !r) return r;
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_slice_exhaustively_covered(const std::vector<MatchArm>& arms) -> bool {
        std::unordered_set<std::size_t> exact_covered;
        std::optional<std::size_t> min_variadic_covered;
        for (const auto& arm : arms) {
            if (arm.variant_name == "_" && !arm.is_slice_pattern && arm.literal_val.empty()) {
                return true;
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    exact_covered.insert(min_sz);
                } else {
                    if (!min_variadic_covered || min_sz < *min_variadic_covered) {
                        min_variadic_covered = min_sz;
                    }
                }
            }
        }
        if (!min_variadic_covered) return false;
        for (std::size_t k = 0; k < *min_variadic_covered; ++k) {
            if (!exact_covered.contains(k)) return false;
        }
        return true;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_match_arms_reachability(
        const std::vector<MatchArm>& arms,
        int match_line,
        int match_col
    ) -> std::expected<void, std::string> {
        bool seen_wildcard = false;
        std::unordered_set<std::string> seen_variants;
        std::unordered_set<std::string> seen_literals;
        std::unordered_set<std::size_t> exact_slice_sizes;
        std::optional<std::size_t> min_variadic_slice;

        auto is_size_covered = [&](std::size_t n) -> bool {
            if (exact_slice_sizes.contains(n)) return true;
            if (min_variadic_slice && n >= *min_variadic_slice) return true;
            return false;
        };

        auto all_sizes_from_covered = [&](std::size_t m) -> bool {
            if (!min_variadic_slice) return false;
            for (std::size_t k = m; k < *min_variadic_slice; ++k) {
                if (!exact_slice_sizes.contains(k)) return false;
            }
            return true;
        };

        for (std::size_t idx = 0; idx < arms.size(); ++idx) {
            const auto& arm = arms[idx];
            if (seen_wildcard) {
                return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable match arm #{}: previous wildcard '_' arm already matches all remaining cases", idx + 1)));
            }
            if (arm.guard != nullptr) {
                // Guarded arms are conditional; they do not fully cover the pattern and cannot be statically proven unreachable
                continue;
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    if (is_size_covered(min_sz)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable slice match arm #{} (exact length {} is already matched by an earlier arm)", idx + 1, min_sz)));
                    }
                    exact_slice_sizes.insert(min_sz);
                } else {
                    if (all_sizes_from_covered(min_sz)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable slice match arm #{} (slices of length >= {} are already matched by earlier arms)", idx + 1, min_sz)));
                    }
                    if (!min_variadic_slice || min_sz < *min_variadic_slice) {
                        min_variadic_slice = min_sz;
                    }
                }
                continue;
            }
            if (arm.variant_name == "_" && arm.literal_val.empty()) {
                if (min_variadic_slice && all_sizes_from_covered(0)) {
                    return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable wildcard '_' match arm #{}: all slice lengths are already covered by earlier arms", idx + 1)));
                }
                seen_wildcard = true;
                continue;
            }
            std::vector<MatchPattern> pats = arm.patterns.empty()
                ? std::vector<MatchPattern>{arm.to_pattern()}
                : arm.patterns;
            for (const auto& pat : pats) {
                if (!pat.literal_val.empty()) {
                    if (seen_literals.contains(pat.literal_val)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable match arm #{}: duplicate literal pattern '{}'", idx + 1, pat.literal_val)));
                    }
                    seen_literals.insert(pat.literal_val);
                } else if (!pat.variant_name.empty() && pat.variant_name != "_") {
                    if (seen_variants.contains(pat.variant_name)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable match arm #{}: duplicate variant pattern '{}'", idx + 1, pat.variant_name)));
                    }
                    seen_variants.insert(pat.variant_name);
                }
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::stmt_has_break(const StmtPtr& s) -> bool {
        if (!s) return false;
        if (std::holds_alternative<BreakStmt>(s->node)) return true;
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* id = std::get_if<IdentExpr>(&es->expr->node)) {
                    if (id->name == "break") return true;
                }
            }
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (block_has_break(ifs->then_body) || block_has_break(ifs->else_body)) return true;
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            for (const auto& arm : ms->arms) {
                if (block_has_break(arm.body)) return true;
            }
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::block_has_break(const std::vector<StmtPtr>& body) -> bool {
        for (const auto& s : body) {
            if (stmt_has_break(s)) return true;
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::stmt_diverges(const StmtPtr& s, const CheckedFile* cf) -> bool {
        if (!s) return false;
        if (std::holds_alternative<ReturnStmt>(s->node)) return true;
        if (std::holds_alternative<BreakStmt>(s->node) || std::holds_alternative<ContinueStmt>(s->node)) return true;
        if (const auto* as = std::get_if<AsyncScopeStmt>(&s->node)) {
            return as->result_return || block_diverges(as->body, cf);  // `return async.scope ...`
        }
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* id = std::get_if<IdentExpr>(&es->expr->node)) {
                    if (id->name == "break" || id->name == "continue") return true;
                }
                if (const auto* call = std::get_if<CallExpr>(&es->expr->node)) {
                    if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                        if (id->name == "panic" || id->name == "exit") return true;
                    } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                        if (mem->field == "exit" && mem->object) {
                            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (obj_id->name == "os") return true;
                            }
                        }
                    }
                }
            }
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (!ifs->else_body.empty() && block_diverges(ifs->then_body, cf) && block_diverges(ifs->else_body, cf)) {
                return true;
            }
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            bool is_infinite = false;
            if (fs->item_var.empty()) {
                if (!fs->iter_expr) {
                    is_infinite = true;
                } else if (const auto* lit = std::get_if<LiteralExpr>(&fs->iter_expr->node)) {
                    if (lit->kind == TokenKind::KwTrue) is_infinite = true;
                }
            }
            if (is_infinite && !block_has_break(fs->body)) {
                return true;
            }
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (!ms->arms.empty()) {
                bool all_diverge = true;
                bool has_wildcard = false;
                std::unordered_set<std::string> arm_variants;
                for (const auto& arm : ms->arms) {
                    if (arm.variant_name == "_") has_wildcard = true;
                    else arm_variants.insert(arm.variant_name);
                    if (!block_diverges(arm.body, cf)) {
                        all_diverge = false;
                    }
                }
                if (all_diverge) {
                    if (has_wildcard) return true;
                    if (arm_variants.contains("Some") && arm_variants.contains("none")) return true;
                    if (arm_variants.contains("Ok") && arm_variants.contains("Err")) return true;
                    if (cf) {
                        for (const auto& [ename, edef] : cf->enums) {
                            if (edef.variants.empty()) continue;
                            bool all_found = true;
                            for (const auto& v : edef.variants) {
                                if (!arm_variants.contains(v.name)) {
                                    all_found = false;
                                    break;
                                }
                            }
                            if (all_found) return true;
                        }
                    }
                }
            }
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::block_diverges(const std::vector<StmtPtr>& body, const CheckedFile* cf) -> bool {
        for (const auto& s : body) {
            if (stmt_diverges(s, cf)) return true;
        }
        return false;
    }
