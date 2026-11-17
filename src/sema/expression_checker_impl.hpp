// Generated shard for SemaFnChecker (expr)



    CFIRE_SEMA_INLINE auto SemaFnChecker::classify_builtin_receiver(std::string_view t) -> BuiltinReceiver {
        while (t.starts_with("&mut ") || t.starts_with("*mut ")) t.remove_prefix(5);
        while (t.starts_with("&") || t.starts_with("*")) t.remove_prefix(1);
        if (t == "str" || t == "string") return BuiltinReceiver::Str;
        if (t.starts_with("[]")) return BuiltinReceiver::Slice;
        if (t.starts_with("[") && t.size() > 1 && std::isdigit(static_cast<unsigned char>(t[1]))) return BuiltinReceiver::Array;
        if (t.starts_with("map[")) return BuiltinReceiver::Map;
        return BuiltinReceiver::None;
    }


    // the receiver kind of a builtin method call, from the receiver's TypeId (borrows and pointers
    // auto-deref). Falls back to the receiver's spelled type when inference has no TypeId.
    CFIRE_SEMA_INLINE auto SemaFnChecker::builtin_recv_bits(const ExprPtr& obj) const -> std::uint8_t {
        if (!obj) return 0;
        TypeId t = infer_expr_type_id_sema(obj);
        for (int guard = 0; guard < 4 && t.is_valid(); ++guard) {
            const auto& d = cf_.type_table.get(t);
            if (d.kind == TypeKind::Borrow || d.kind == TypeKind::MutBorrow || d.kind == TypeKind::RawPtr || d.kind == TypeKind::MutPtr) {
                t = d.elem_type;
                continue;
            }
            break;
        }
        if (t.is_valid()) {
            switch (cf_.type_table.get(t).kind) {
                case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
                case TypeKind::Uint8: case TypeKind::Uint16: case TypeKind::Uint32: case TypeKind::Uint64:
                case TypeKind::Isize: case TypeKind::Usize:
                    return BmInt;
                case TypeKind::Float32: case TypeKind::Float64: return BmFloat;
                case TypeKind::Bool: return BmBool;
                case TypeKind::Str: return BmStr;
                case TypeKind::Slice: return BmSlice;
                case TypeKind::Array: return BmArray;
                case TypeKind::Map: return BmMap;
                default: break;
            }
        }
        return bm_bits_of(classify_builtin_receiver(type_name_of(obj)));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::builtin_self_type(const ExprPtr& obj) const -> TypeId {
        TypeId t = infer_expr_type_id_sema(obj);
        for (int guard = 0; guard < 4 && t.is_valid(); ++guard) {
            const auto& d = cf_.type_table.get(t);
            if (d.kind != TypeKind::Borrow && d.kind != TypeKind::MutBorrow && d.kind != TypeKind::RawPtr && d.kind != TypeKind::MutPtr) break;
            t = d.elem_type;
        }
        return t;
    }


    // Resolves a builtin-method type spelling (`Self`, `T?`, `[]K`, `&str?`, `usize`) against the receiver.
    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_builtin_type(std::string_view spec, TypeId self) const -> TypeId {
        while (!spec.empty() && spec.front() == ' ') spec.remove_prefix(1);
        while (!spec.empty() && (spec.back() == ' ' || spec.back() == '=')) spec.remove_suffix(1);
        if (spec.empty() || spec == "fn") return TypeId::Unknown();
        if (spec.ends_with('?')) {
            TypeId inner = resolve_builtin_type(spec.substr(0, spec.size() - 1), self);
            return inner.is_valid() ? cf_.type_table.make_optional(inner) : inner;
        }
        if (spec.starts_with("[]")) {
            TypeId inner = resolve_builtin_type(spec.substr(2), self);
            return inner.is_valid() ? cf_.type_table.make_slice(inner) : inner;
        }
        if (spec == "&str") return TypeId::Str();
        if (spec == "void") return TypeId::Void();
        const auto& sd = cf_.type_table.get(self);
        if (spec == "Self") return self;
        if (spec == "T") {
            if (self == TypeId::Str()) return TypeId::Byte();
            if (sd.kind == TypeKind::Slice || sd.kind == TypeKind::Array) return sd.elem_type;
            return TypeId::Unknown();
        }
        if (spec == "K") return sd.kind == TypeKind::Map ? sd.key_type : TypeId::Unknown();
        if (spec == "V") return sd.kind == TypeKind::Map ? sd.elem_type : TypeId::Unknown();
        if (spec == "*T") {
            if (self == TypeId::Str()) return type_id_of("*c_char");
            if ((sd.kind == TypeKind::Slice || sd.kind == TypeKind::Array) && sd.elem_type.is_valid()) return cf_.type_table.make_raw_ptr(sd.elem_type);
            return TypeId::Unknown();
        }
        return type_id_of(spec);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::lookup_builtin_method(const MemberExpr& mem) const -> const BuiltinMethod* {
        if (!mem.object) return nullptr;
        if (const auto* oid = std::get_if<IdentExpr>(&mem.object->node);
            oid && !is_ident_in_scope_sema(oid->name) && (is_namespace_ident_sema(oid->name) || cf_.enums.contains(oid->name))) {
            return nullptr;
        }
        std::uint8_t bits = builtin_recv_bits(mem.object);
        if (bits == 0) return nullptr;
        return find_builtin_method(bits, mem.field);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::builtin_method_allowed(BuiltinReceiver k, std::string_view m) -> bool {
        if (k == BuiltinReceiver::None) return true;
        return find_builtin_method(bm_bits_of(k), m) != nullptr;
    }


    // arity, argument types and `mut` receivers of builtin method calls, all read from the table.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_builtin_method_call(const CallExpr& call, const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        const BuiltinMethod* bm = lookup_builtin_method(mem);
        if (!bm || !bm->lowered) return {};
        const std::size_t want = builtin_param_count(*bm);
        const std::size_t got = call.args.size() + (call.block_arg ? 1 : 0);
        TypeId self = builtin_self_type(mem.object);
        std::string self_name = self.is_valid() ? cf_.type_table.to_string(self) : std::string("value");
        const std::size_t required = builtin_required_param_count(*bm);
        if (got < required || got > want) {
            std::string expected = required == want ? std::to_string(want) : std::format("{} to {}", required, want);
            return emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("method '{}' on '{}' expects {} argument(s), got {}", bm->name, self_name, expected, got)));
        }
        std::vector<std::string_view> params;
        for (std::string_view rest = bm->params; !rest.empty();) {
            auto comma = rest.find(',');
            params.push_back(rest.substr(0, comma));
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        for (std::size_t i = 0; i < call.args.size() && i < params.size(); ++i) {
            const auto& arg = call.args[i];
            TypeId want_t = resolve_builtin_type(params[i], self);
            if (arg.value && std::holds_alternative<ArrayLitExpr>(arg.value->node) && !is_concrete_checkable_type(want_t)) continue;
            bool handled = false;
            if (auto r = check_value_for(want_t, arg.value, handled); !r) return r;
            if (handled) continue;
            TypeId got_t = infer_expr_type_id_sema(arg.value);
            if (!is_concrete_checkable_type(want_t) || !is_concrete_checkable_type(got_t)) continue;
            if (expr_returns_optional_sema(arg.value)) continue;
            // Float literals adapt to the receiver's width too (`x.min(0.5)` on an `f32`).
            if (cf_.type_table.is_numeric(want_t) && cf_.type_table.is_numeric(got_t) && is_numeric_literal_expr(arg.value)) continue;
            if (!cf_.type_table.is_assignable(want_t, got_t)) {
                return emit_error(diag_at(arg.line, arg.col, "cfire/TYPE-MISMATCH", std::format("argument {} of '{}.{}' expects '{}', got '{}'", i + 1, self_name, bm->name, cf_.type_table.to_string(want_t), cf_.type_table.to_string(got_t))));
            }
        }
        if (bm->mutates) {
            if (const auto* oid = std::get_if<IdentExpr>(&mem.object->node);
                oid && imm_vars_.contains(oid->name) && !smart_ptr_vars_.contains(oid->name)) {
                return emit_error(diag_at(line, col, "cfire/MUT-RECEIVER", std::format("cannot call mutating method '{}' on immutable variable '{}'; declare it with 'mut'", bm->name, oid->name), {{"var", oid->name}}));
            }
        }
        return {};
    }


    // Canonical cfire spelling for raw C++ members, used for the diagnostic hint and `cfire fix`.
    CFIRE_SEMA_INLINE auto SemaFnChecker::builtin_method_replacement(BuiltinReceiver k, std::string_view m) -> std::string_view {
        if (m == "size" || m == "length") return "len";
        if (k != BuiltinReceiver::Map && (m == "push_back" || m == "emplace_back")) return "push";
        if (k != BuiltinReceiver::Map && m == "pop_back") return "pop";
        if (k == BuiltinReceiver::Map && m == "erase") return "remove";
        if (k == BuiltinReceiver::Map && (m == "count" || m == "find")) return "contains";
        return "";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_numeric_literal_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-") return is_numeric_literal_expr(un->operand);
        const auto* lit = std::get_if<LiteralExpr>(&e->node);
        return lit && (lit->kind == TokenKind::Int || lit->kind == TokenKind::Float);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_builtin_method_surface(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (mem.field == "find" || mem.field == "rfind") return {};  // has its own diagnostic
        if (const auto* oid = mem.object ? std::get_if<IdentExpr>(&mem.object->node) : nullptr;
            oid && !is_ident_in_scope_sema(oid->name) && (is_namespace_ident_sema(oid->name) || cf_.enums.contains(oid->name))) {
            return {};
        }
        const std::uint8_t bits = builtin_recv_bits(mem.object);
        if (bits == 0 || find_builtin_method(bits, mem.field)) return {};
        std::string_view type_name = bm_kind_name(bits);
        for (const auto& r : kRetiredBuiltinMethods) {
            if ((r.recv & bits) && r.name == mem.field) {
                return emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'.{}()' has been renamed; use '.{}()' instead", mem.field, r.replacement), {{"from", "." + std::string(mem.field) + "("}, {"to", "." + std::string(r.replacement) + "("}}));
            }
        }
        // parsing is associated with the target type (`i32.parse(s)`), not a `str` method.
        if ((bits & BmStr) && mem.field.starts_with("parse_")) {
            std::string target = mem.field.substr(6);
            return emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'.{}()' has been removed; use '{}.parse(s)' (returns '{}?')", mem.field, target, target), {{"rewrite", "method_to_assoc"}, {"method", std::string(mem.field)}, {"to", target + ".parse"}}));
        }
        if ((bits & (BmInt | BmFloat | BmBool)) && mem.field.size() > 0) {
            // Numbers, floats and bool have a closed method set too; anything else is a typo or a C++ member.
            return emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD", std::format("unknown {} method '{}'", type_name, mem.field), {{"method", std::string(mem.field)}, {"replacement", ""}}));
        }
        auto kind = classify_builtin_receiver(type_name_of(mem.object));
        if (mem.field == "clone") {
            return emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD",
                std::format("unknown {} method 'clone'; values copy on assignment in cfire — write 'mut copy := xs' for an owned mutable copy", type_name),
                {{"method", "clone"}, {"replacement", ""}}));
        }
        std::string_view repl = builtin_method_replacement(kind, mem.field);
        std::string hint = repl.empty()
            ? std::string("; raw C++ members are only available inside 'cpp { }' blocks")
            : std::format("; use '.{}()' instead", repl);
        return emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD", std::format("unknown {} method '{}'{}", type_name, mem.field, hint), {{"method", std::string(mem.field)}, {"replacement", std::string(repl)}}));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_stdlib_ns_imported(std::string_view ns, int line, int col) -> std::expected<void, std::string> {
        if (cf_.shadowed_default_qualifiers.contains(std::string(ns))) {
            std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, std::string(ns));
            return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.<member>'", ns, active_alias, active_alias)));
        }
        const auto& idx = StdlibCatalogIndex::instance();
        if (!idx.has_namespace(ns) || is_ident_in_scope_sema(std::string(ns))) return {};
        if (is_stdlib_ns_imported(ns)) return {};
        if (reported_missing_imports_.contains(std::string(ns))) return {};
        reported_missing_imports_.insert(std::string(ns));
        std::string mod_path = idx.import_path_for(ns);
        return emit_error(diag_at(line, col, "cfire/MISSING-IMPORT", std::format("module '{}' is not imported; add 'import \"{}\"'", ns, mod_path), {{"module", mod_path}}));
    }


    // `local_decl`: a local variable's type, which analyze.hpp's declaration pass does not see (stdlib type names).
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_type_ref_separators(const TypeRef& tr, int line, int col, bool local_decl) -> std::expected<void, std::string> {
        if (local_decl && (tr.is_ptr || tr.is_mut_ptr)) {
            return emit_error(diag_at(line, col, "cfire/RAW-POINTER", "raw pointer types ('*T', '*mut T') exist only in C++ interop; hold the value itself, an index, or 'own[T]'/'shared[T]'"));
        }
        if (local_decl && (tr.is_borrow || tr.is_mut_borrow)) {
            return emit_error(diag_at(line, col, "cfire/REFERENCE-TYPE", "'&T' is not a cfire type: write 'T'; a binding of a view is already a view"));
        }
        if (!tr.raw_name.empty()) {
            auto dcol = tr.raw_name.find("::");
            auto dot = tr.raw_name.find('.');
            if (dcol != std::string::npos && (dot == std::string::npos || dcol < dot)) {
                std::string qual = tr.raw_name.substr(0, dcol);
                if (!cf_.enums.contains(qual) &&
                    (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual) ||
                     !resolve_stdlib_ns_sema(qual).empty() || is_namespace_ident_sema(qual) ||
                     cf_.shadowed_default_qualifiers.contains(qual))) {
                    std::string rest = tr.raw_name.substr(dcol + 2);
                    return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", qual, rest, qual, rest), {{"kind", "qualifier"}}));
                }
            }
            if (dot != std::string::npos) {
                std::string qual = tr.raw_name.substr(0, dot);
                if (cf_.shadowed_default_qualifiers.contains(qual)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, qual);
                    std::string rest = tr.raw_name.substr(dot + 1);
                    return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.{}'", qual, active_alias, active_alias, rest)));
                }
                if (const std::string ns = local_decl ? resolve_stdlib_ns_sema(qual) : std::string(); !ns.empty() && !is_cpp_qualifier_sema(qual) && !is_vendor_alias_sema(qual)) {
                    const bool manifest = std::filesystem::path(cf_.ast.path).filename() == "build.cf" || cf_.ast.package_name == "build";
                    if (std::string why = stdlib_type_name_problem(ns, tr.raw_name.substr(dot + 1), manifest); !why.empty()) {
                        return emit_error(diag_at(line, col, "cfire/UNKNOWN-STDLIB-TYPE", why));
                    }
                }
            }
            // a bare type name in a local declaration resolves, or it is a cfire error (not a clang one).
            if (local_decl && dot == std::string::npos && dcol == std::string::npos && !tr.is_fn && !tr.is_tuple) {
                if (std::string why = bare_type_name_problem(tr.raw_name, cf_.declared_type_names, fn_type_params_); !why.empty()) {
                    return emit_error(diag_at(line, col, "cfire/UNDECLARED-TYPE", why, {{"name", tr.raw_name}}));
                }
            }
        }
        if (tr.elem_type) {
            if (auto r = check_type_ref_separators(*tr.elem_type, line, col, local_decl); !r) return r;
        }
        if (tr.key_type) {
            if (auto r = check_type_ref_separators(*tr.key_type, line, col, local_decl); !r) return r;
        }
        for (const auto& arg : tr.type_args) {
            if (auto r = check_type_ref_separators(arg, line, col, local_decl); !r) return r;
        }
        for (const auto& tup : tr.tuple_types) {
            if (auto r = check_type_ref_separators(tup, line, col, local_decl); !r) return r;
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_ident_in_scope_sema(const std::string& ident) const -> bool {
        if (local_defined_vars_.contains(ident) || mut_vars_.contains(ident) ||
            imm_vars_.contains(ident) || move_params_.contains(ident)) {
            return true;
        }
        if (cf_.fn_sigs.contains(ident)) return true;
        if (!pkg_name_.empty() && cf_.fn_sigs.contains(pkg_name_ + "." + ident)) return true;
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_builtin_ident_sema(std::string_view name) -> bool {
        return name == "_" || name == "break" || name == "continue" ||
               name == "nullptr" || name == "null" ||
               name == "println" || name == "print" || name == "eprintln" || name == "eprint" ||
               name == "format" || name == "readln" || name == "input" || name == "black_box" ||
               name == "panic" || name == "assert" ||
               name == "assert_eq" || name == "assert_ne" || name == "assert_true" || name == "assert_false" ||
               name == "assert_err" || name == "assert_ok" || name == "error" || name == "make" ||
               name == "len" || name == "cap" || name == "append" || name == "copy" || name == "delete" ||
               name == "close" || name == "exit" || name == "some" || name == "none" || name == "ok" ||
               name == "err" || name == "own" || name == "shared" || name == "async" ||
               name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "isize" || name == "int" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "usize" || name == "uint" ||
               name == "byte" || name == "f32" || name == "f64" || name == "bool" || name == "str" ||
               name == "char" || name == "rune" || name == "void" || name == "any" ||
               name == "c_int" || name == "c_char" || name == "c_void" || name == "c_long" ||
               name == "c_ulong" || name == "c_float" || name == "c_double" ||
               name.starts_with("[]") || name.starts_with("[") || name.starts_with("map[") ||
               name.starts_with("*") || name.starts_with("&");
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_known_identifier_sema(const std::string& name) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(name)) return false;
        if (name.empty() || is_builtin_ident_sema(name)) return true;
        if (is_ident_in_scope_sema(name)) return true;
        if (is_namespace_ident_sema(name)) return true;
        if (cf_.struct_tags.contains(name) || cf_.enums.contains(name) || cf_.variant_to_enum.contains(name)) return true;
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) return true;
        }
        // Check compound names like `Shape.Circle`, `audio.Waveform::Sine`, `Waveform::Sine`, `http.Response`
        auto dot = name.find('.');
        auto dcol = name.find("::");
        std::size_t sep = std::min(dot, dcol);
        if (sep != std::string::npos) {
            std::string prefix = name.substr(0, sep);
            if (is_known_identifier_sema(prefix)) return true;
            if (prefix == "Waveform" || prefix == "SfxPreset" || prefix == "OpenMode" ||
                prefix == "SeekWhence" || prefix == "Level" || prefix == "Format" || prefix == "Provider") {
                return true;
            }
            if (dcol != std::string::npos) {
                std::string suffix = name.substr(dcol + 2);
                if (cf_.variant_to_enum.contains(suffix)) return true;
            }
        }
        return false;
    }


    // ---------------------------------------------------------------------------------
    // Canonical nominal type identity (canonical type identity)
    //
    // '.' qualifies a package (`webui.App`, `rl.Rectangle`); '::' walks type-level
    // paths (`pkg.Type::Variant`, `Enum::Variant`). The same source type reaches sema in
    // different spellings: the parser joins annotations with '::' (`rl::Rectangle`),
    // struct literals keep the source separator (`rl.Rectangle`), and stdlib catalog
    // signatures are written from inside their module (`App`). Every name -> TypeId
    // conversion goes through `type_id_of`, which restores the package boundary by
    // *resolution* (is the leading segment a package/alias in this file?), never by
    // blind separator rewriting. The canonical key uses the import alias as written.
    // ---------------------------------------------------------------------------------

    CFIRE_SEMA_INLINE auto SemaFnChecker::is_package_qualifier_sema(std::string_view head) const -> bool {
        if (head.empty()) return false;
        return is_cpp_qualifier_sema(head) || is_namespace_ident_sema(head) || is_vendor_alias_sema(head);
    }


    /// Canonicalizes a bare (unwrapped) nominal path: `rl::Rectangle` -> `rl.Rectangle`,
    /// `webui.App` -> `webui.App`, `Enum::Variant` / local `App` unchanged.
    CFIRE_SEMA_INLINE auto SemaFnChecker::canonical_nominal_path_sema(std::string_view name) const -> std::string {
        // Generic application `pkg.Box[T]`: canonicalize the head and each argument.
        if (auto br = name.find('['); br != std::string_view::npos && name.ends_with(']') && br > 0) {
            std::string out = canonical_nominal_path_sema(name.substr(0, br)) + "[";
            std::string_view args = name.substr(br + 1, name.size() - br - 2);
            int depth = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= args.size(); ++i) {
                if (i < args.size() && (args[i] == '[' || args[i] == '(')) ++depth;
                else if (i < args.size() && (args[i] == ']' || args[i] == ')')) --depth;
                else if (i == args.size() || (args[i] == ',' && depth == 0)) {
                    std::string_view a = args.substr(start, i - start);
                    while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
                    if (start > 0) out += ", ";
                    out += canonical_type_name_sema(a);
                    start = i + 1;
                }
            }
            return out + "]";
        }
        auto sep = name.find_first_of(".:");
        if (sep == std::string_view::npos) {
            // Bare name declared in the current (non-main) package is that package's type.
            if (!pkg_name_.empty() && pkg_name_ != "main") {
                for (const auto& st : cf_.ast.structs) {
                    if (st.name == name && st.package_name == pkg_name_) return std::format("{}.{}", pkg_name_, name);
                }
                for (const auto& en : cf_.ast.enums) {
                    if (en.name == name && en.package_name == pkg_name_) return std::format("{}.{}", pkg_name_, name);
                }
            }
            return std::string(name);
        }
        const bool is_dcolon = name[sep] == ':' && sep + 1 < name.size() && name[sep + 1] == ':';
        if (name[sep] == ':' && !is_dcolon) return std::string(name);
        std::string_view head = name.substr(0, sep);
        std::string_view rest = name.substr(sep + (is_dcolon ? 2 : 1));
        if (is_package_qualifier_sema(head)) return std::format("{}.{}", head, rest);
        return std::string(name);
    }


    /// Canonicalizes a full type spelling, recursing through the same wrappers that
    /// `TypeTable::from_name` decomposes.
    CFIRE_SEMA_INLINE auto SemaFnChecker::canonical_type_name_sema(std::string_view name) const -> std::string {
        if (name.empty()) return {};
        if (name.ends_with('?')) return canonical_type_name_sema(name.substr(0, name.size() - 1)) + "?";
        if (name.ends_with('!')) return canonical_type_name_sema(name.substr(0, name.size() - 1)) + "!";
        if (name.starts_with("[]")) return "[]" + canonical_type_name_sema(name.substr(2));
        if (name.starts_with("&mut ")) return "&mut " + canonical_type_name_sema(name.substr(5));
        if (name.starts_with('&')) return "&" + canonical_type_name_sema(name.substr(1));
        if (name.starts_with("*mut ")) return "*mut " + canonical_type_name_sema(name.substr(5));
        if (name.starts_with('*')) return "*" + canonical_type_name_sema(name.substr(1));
        if (name.starts_with("own[") && name.ends_with(']')) return "own[" + canonical_type_name_sema(name.substr(4, name.size() - 5)) + "]";
        if (name.starts_with("shared[") && name.ends_with(']')) return "shared[" + canonical_type_name_sema(name.substr(7, name.size() - 8)) + "]";
        if (name.starts_with('[')) {
            // Fixed array `[N]T`
            if (auto rb = name.find(']'); rb != std::string_view::npos) {
                return std::string(name.substr(0, rb + 1)) + canonical_type_name_sema(name.substr(rb + 1));
            }
        }
        if (name.starts_with("map[")) {
            int depth = 0;
            for (std::size_t i = 3; i < name.size(); ++i) {
                if (name[i] == '[') ++depth;
                else if (name[i] == ']' && --depth == 0) {
                    return "map[" + canonical_type_name_sema(name.substr(4, i - 4)) + "]" + canonical_type_name_sema(name.substr(i + 1));
                }
            }
        }
        if (name.starts_with('(') && name.ends_with(')')) {
            int p_depth = 0;
            int b_depth = 0;
            std::string out = "(";
            std::size_t start = 1;
            bool outer_encloses = true;
            for (std::size_t i = 0; i < name.size(); ++i) {
                if (name[i] == '(') ++p_depth;
                else if (name[i] == ')') {
                    --p_depth;
                    if (p_depth == 0 && i + 1 < name.size()) {
                        outer_encloses = false;
                        break;
                    }
                } else if (name[i] == '[') ++b_depth;
                else if (name[i] == ']') --b_depth;
                else if (name[i] == ',' && p_depth == 1 && b_depth == 0) {
                    std::string_view part = name.substr(start, i - start);
                    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.remove_prefix(1);
                    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.remove_suffix(1);
                    if (out.size() > 1) out += ", ";
                    out += canonical_type_name_sema(part);
                    start = i + 1;
                }
            }
            if (outer_encloses && p_depth == 0) {
                std::string_view last_part = name.substr(start, (name.size() - 1) - start);
                while (!last_part.empty() && std::isspace(static_cast<unsigned char>(last_part.front()))) last_part.remove_prefix(1);
                while (!last_part.empty() && std::isspace(static_cast<unsigned char>(last_part.back()))) last_part.remove_suffix(1);
                if (out.size() > 1) out += ", ";
                out += canonical_type_name_sema(last_part);
                out += ")";
                return out;
            }
        }
        if (name.starts_with('(') || name.starts_with("fn")) return std::string(name); // fn types
        return canonical_nominal_path_sema(name);
    }


    /// Canonicalizes every nominal leaf of a parsed TypeRef (annotations, fields, params).
    CFIRE_SEMA_INLINE auto SemaFnChecker::canonical_typeref_sema(TypeRef tr) const -> TypeRef {
        if (!tr.name.empty() && !tr.is_fn && !tr.is_tuple) tr.name = canonical_type_name_sema(tr.name);
        if (tr.elem_type) tr.elem_type = std::make_shared<TypeRef>(canonical_typeref_sema(*tr.elem_type));
        if (tr.key_type) tr.key_type = std::make_shared<TypeRef>(canonical_typeref_sema(*tr.key_type));
        for (auto& a : tr.type_args) a = canonical_typeref_sema(a);
        for (auto& t : tr.tuple_types) t = canonical_typeref_sema(t);
        return tr;
    }


    /// Single entry point for converting a type spelling into an interned TypeId.
    /// Strict: used for explicit annotations, which must be spelled as canonical type identity requires.
    CFIRE_SEMA_INLINE auto SemaFnChecker::type_id_of(std::string_view name) const -> TypeId {
        return cf_.type_table.from_name(canonical_type_name_sema(name));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::type_id_of(const TypeRef& tr) const -> TypeId {
        return cf_.type_table.from_typeref(canonical_typeref_sema(tr));
    }


    /// Hint: explain local-vs-package mismatches that share a leaf name.
    CFIRE_SEMA_INLINE auto SemaFnChecker::nominal_mismatch_hint_sema(TypeId a, TypeId b) const -> std::string {
        auto is_nominal = [this](TypeId t) {
            if (!t.is_valid() || t.is_unknown()) return false;
            const auto k = cf_.type_table.get(t).kind;
            return k == TypeKind::NominalStruct || k == TypeKind::NominalEnum;
        };
        if (!is_nominal(a) || !is_nominal(b)) return {};
        std::string sa = cf_.type_table.to_string(a);
        std::string sb = cf_.type_table.to_string(b);
        auto leaf_of = [](const std::string& s) -> std::string_view {
            auto p = s.rfind('.');
            return p == std::string::npos ? std::string_view(s) : std::string_view(s).substr(p + 1);
        };
        const bool qa = sa.find('.') != std::string::npos;
        const bool qb = sb.find('.') != std::string::npos;
        if (qa == qb || leaf_of(sa) != leaf_of(sb)) return {};
        const std::string& local = qa ? sb : sa;
        const std::string& pkg = qa ? sa : sb;
        return std::format("; '{}' is a local type, distinct from '{}' (did you mean '{}'?)", local, pkg, pkg);
    }


    /// The qualifier this file uses for a stdlib/vendor module: its import alias if one
    /// was given, otherwise the module's own namespace.
    CFIRE_SEMA_INLINE auto SemaFnChecker::local_qualifier_for_module_sema(std::string_view module_import, std::string_view default_ns) const -> std::string {
        for (const auto& imp : cf_.ast.imports) {
            if (imp.path == module_import && !imp.alias.empty()) return imp.alias;
        }
        return std::string(default_ns);
    }


    /// Stdlib catalog signatures name module-local types bare (`-> App`). Qualify such a
    /// return type with the module's qualifier in this file (`webui.App`), using the
    /// metadata table as the source of truth for which names a module declares.
    CFIRE_SEMA_INLINE auto SemaFnChecker::qualify_stdlib_type_sema(const StdFnSigInfo& sig, std::string_view qualifier) const -> std::string {
        const std::string& t = sig.unwrapped_return_type;
        const auto& idx = StdlibCatalogIndex::instance();
        auto declares = [&](std::string_view bare) {
            if (auto it = idx.ns_types_and_consts.find(sig.ns_alias); it != idx.ns_types_and_consts.end()) {
                return it->second.contains(std::string(bare));
            }
            return false;
        };
        // Find the innermost bare nominal (strip wrappers we know) and qualify it in place.
        std::size_t b = 0;
        std::size_t e = t.size();
        for (bool changed = true; changed;) {
            changed = false;
            std::string_view v(t.data() + b, e - b);
            for (std::string_view p : {"[]", "&mut ", "&", "*mut ", "*"}) {
                if (v.starts_with(p)) { b += p.size(); changed = true; break; }
            }
            if (!changed && (v.ends_with('?') || v.ends_with('!'))) { --e; changed = true; }
        }
        std::string_view core(t.data() + b, e - b);
        std::string_view head = core.substr(0, core.find('['));
        if (head.empty() || head.find_first_of(".:") != std::string_view::npos || !declares(head)) return t;
        // Built-in types (`str`, `i32`, `bool`, ...) are never module-qualified, even when a
        // namespace shares their name (e.g. `core:str` lists `str`).
        {
            const auto& d = cf_.type_table.get(cf_.type_table.from_name(head));
            if (d.kind != TypeKind::NominalStruct && d.kind != TypeKind::NominalEnum) return t;
        }
        return t.substr(0, b) + std::string(qualifier) + "." + std::string(core) + t.substr(e);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_ident(const std::string& name) const -> std::optional<Resolution> {
        Resolution res;
        // 1. Function parameter
        if (param_names_.contains(name)) {
            res.kind = SymbolKind::Param;
            res.canonical_name = sanitize_cpp_ident(name);
            res.is_mut = mut_vars_.contains(name);
            res.is_optional = optional_vars_.contains(name);
            res.is_smart_ptr = smart_ptr_vars_.contains(name);
            res.is_raw_ptr = raw_or_smart_ptr_vars_.contains(name) && !smart_ptr_vars_.contains(name);
            if (auto it = var_types_.find(name); it != var_types_.end()) {
                res.type_name = it->second;
            }
            return res;
        }
        // 2. Local variable
        if (local_defined_vars_.contains(name) || mut_vars_.contains(name) || imm_vars_.contains(name)) {
            res.kind = SymbolKind::Local;
            res.canonical_name = sanitize_cpp_ident(name);
            res.is_mut = mut_vars_.contains(name);
            res.is_optional = optional_vars_.contains(name);
            res.is_smart_ptr = smart_ptr_vars_.contains(name);
            res.is_raw_ptr = raw_or_smart_ptr_vars_.contains(name) && !smart_ptr_vars_.contains(name);
            if (auto it = var_types_.find(name); it != var_types_.end()) {
                res.type_name = it->second;
            }
            return res;
        }
        // 3. Enum Variant
        if (auto vit = cf_.variant_to_enum.find(name); vit != cf_.variant_to_enum.end()) {
            res.kind = SymbolKind::EnumVariant;
            res.owner_type = vit->second;
            res.canonical_name = name;
            res.type_name = vit->second;
            return res;
        }
        // 4. Struct
        if (!pkg_name_.empty()) {
            for (const auto& st : cf_.ast.structs) {
                std::string st_pkg = st.package_name.empty() ? cf_.ast.package_name : st.package_name;
                if (st.name == name && st_pkg == pkg_name_) {
                    res.kind = SymbolKind::Struct;
                    res.canonical_name = st.name;
                    res.type_name = st.name;
                    res.package_name = st.package_name;
                    return res;
                }
            }
        }
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) {
                res.kind = SymbolKind::Struct;
                res.canonical_name = st.name;
                res.type_name = st.name;
                res.package_name = st.package_name;
                return res;
            }
        }
        // 5. Enum
        if (cf_.enums.contains(name)) {
            res.kind = SymbolKind::Struct;
            res.canonical_name = name;
            res.type_name = name;
            return res;
        }
        // 6. Global variable
        {
            const std::string target_pkg = pkg_name_.empty() ? cf_.ast.package_name : pkg_name_;
            for (const auto& gv : cf_.ast.global_vars) {
                std::string gv_pkg = gv.package_name.empty() ? cf_.ast.package_name : gv.package_name;
                if (gv.name == name && gv_pkg == target_pkg) {
                    res.kind = SymbolKind::Global;
                    res.package_name = gv.package_name;
                    res.type_name = gv.type ? gv.type->name : "";
                    if (!gv.package_name.empty() && cf_.colliding_gv_names.contains(gv.name)) {
                        res.canonical_name = std::format("cfire_pkg_{}__{}", gv.package_name, sanitize_cpp_ident(gv.name));
                    } else {
                        res.canonical_name = sanitize_cpp_ident(gv.name);
                    }
                    return res;
                }
            }
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == name && gv.package_name.empty()) {
                res.kind = SymbolKind::Global;
                res.package_name = gv.package_name;
                res.type_name = gv.type ? gv.type->name : "";
                res.canonical_name = sanitize_cpp_ident(gv.name);
                return res;
            }
        }
        // 7. Function
        if (!pkg_name_.empty()) {
            if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + name); pit != cf_.fn_sigs.end()) {
                res.kind = SymbolKind::Function;
                res.package_name = pit->second.package_name;
                if (!pit->second.package_name.empty() && cf_.colliding_fn_names.contains(pit->second.name)) {
                    res.canonical_name = std::format("cfire_pkg_{}__{}", pit->second.package_name, sanitize_cpp_ident(pit->second.name));
                } else {
                    res.canonical_name = sanitize_cpp_ident(pit->second.name);
                }
                return res;
            }
        }
        if (auto it = cf_.fn_sigs.find(name); it != cf_.fn_sigs.end()) {
            res.kind = SymbolKind::Function;
            res.package_name = it->second.package_name;
            if (!it->second.package_name.empty() && cf_.colliding_fn_names.contains(it->second.name)) {
                res.canonical_name = std::format("cfire_pkg_{}__{}", it->second.package_name, sanitize_cpp_ident(it->second.name));
            } else {
                res.canonical_name = sanitize_cpp_ident(it->second.name);
            }
            return res;
        }
        // 8. CoreNamespace or Vendor
        if (is_namespace_ident_sema(name) || is_vendor_alias_sema(name) || name == "cpp") {
            res.kind = SymbolKind::CoreNamespace;
            res.canonical_name = name;
            return res;
        }
        return std::nullopt;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_member(const MemberExpr& mem) const -> std::optional<Resolution> {
        Resolution res;
        if (!mem.object) return std::nullopt;

        // methods on builtin types lower to the `cfire::bm` overload set.
        if (const BuiltinMethod* bm = lookup_builtin_method(mem); bm && bm->lowered) {
            res.kind = SymbolKind::Method;
            res.owner_type = "builtin";
            res.canonical_name = std::format("cfire::bm::{}", bm->name);
            TypeId rt = resolve_builtin_type(bm->ret, builtin_self_type(mem.object));
            if (rt.is_valid()) res.type_name = cf_.type_table.to_string(rt);
            return res;
        }

        // 1. If object is an IdentExpr
        if (const auto* id = std::get_if<IdentExpr>(&mem.object->node)) {
            // Foreign FFI prefix "cpp" or alias
            if (id->name == "cpp" || is_cpp_qualifier_sema(id->name)) {
                res.kind = SymbolKind::CoreNamespace;
                res.canonical_name = mem.field;
                return res;
            }
            // Vendor alias
            if (is_vendor_alias_sema(id->name)) {
                res.kind = SymbolKind::Function;
                res.canonical_name = mem.field;
                return res;
            }
            // Enum variant: Shape::Circle or Shape.Circle
            if (cf_.enums.contains(id->name)) {
                res.kind = SymbolKind::EnumVariant;
                res.owner_type = id->name;
                res.canonical_name = std::format("{}::{}", id->name, mem.field);
                res.type_name = id->name;
                return res;
            }
            // If id is NOT a local/param variable, check core namespace and packages
            if (!param_names_.contains(id->name) && !local_defined_vars_.contains(id->name) &&
                !mut_vars_.contains(id->name) && !imm_vars_.contains(id->name)) {
                std::string pkg_qualified = id->name + "." + mem.field;
                if (auto it = cf_.fn_sigs.find(pkg_qualified); it != cf_.fn_sigs.end()) {
                    res.kind = SymbolKind::Function;
                    res.package_name = id->name;
                    if (cf_.colliding_fn_names.contains(mem.field)) {
                        res.canonical_name = std::format("cfire_pkg_{}__{}", id->name, sanitize_cpp_ident(mem.field));
                    } else {
                        res.canonical_name = sanitize_cpp_ident(mem.field);
                    }
                    return res;
                }
                for (const auto& gv : cf_.ast.global_vars) {
                    if (gv.name == mem.field && gv.package_name == id->name) {
                        res.kind = SymbolKind::Global;
                        res.package_name = id->name;
                        res.type_name = gv.type ? gv.type->name : "";
                        if (cf_.colliding_gv_names.contains(mem.field)) {
                            res.canonical_name = std::format("cfire_pkg_{}__{}", id->name, sanitize_cpp_ident(mem.field));
                        } else {
                            res.canonical_name = sanitize_cpp_ident(mem.field);
                        }
                        return res;
                    }
                }
                if (is_namespace_ident_sema(id->name)) {
                    res.kind = SymbolKind::CoreNamespace;
                    std::string actual_ns = resolve_stdlib_ns_sema(id->name);
                    if (actual_ns.empty()) actual_ns = id->name;
                    for (const auto& imp : cf_.ast.imports) {
                        if (imp.alias == id->name) {
                            auto colon = imp.path.rfind(':');
                            auto slash = imp.path.rfind('/');
                            std::size_t sep = std::string::npos;
                            if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                            else if (colon != std::string::npos) sep = colon;
                            else sep = slash;
                            actual_ns = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                            break;
                        }
                    }
                    if (actual_ns == "os" && (mem.field == "stdin" || mem.field == "stdout" || mem.field == "stderr")) {
                        res.canonical_name = std::format("cfire::os::{}_file", mem.field);
                    } else {
                        res.canonical_name = std::format("cfire::{}::{}", actual_ns, mem.field);
                    }
                    return res;
                }
            }
        }

        // 2. Struct fields and methods
        std::string owner_type = type_name_of(mem.object);
        if (!owner_type.empty()) {
            if (auto sit = cf_.struct_tags.find(owner_type); sit != cf_.struct_tags.end()) {
                for (const auto& f : sit->second) {
                    if (f.field_name == mem.field) {
                        res.kind = SymbolKind::Field;
                        res.owner_type = owner_type;
                        res.canonical_name = sanitize_cpp_ident(mem.field);
                        res.type_name = f.field_type.name;
                        res.is_optional = f.field_type.is_optional;
                        res.is_smart_ptr = !f.field_type.smart_wrap.empty();
                        res.is_raw_ptr = f.field_type.is_ptr || f.field_type.is_mut_ptr;
                        if (const auto* oid = std::get_if<IdentExpr>(&mem.object->node)) {
                            if (smart_ptr_vars_.contains(oid->name) || raw_or_smart_ptr_vars_.contains(oid->name) || optional_vars_.contains(oid->name)) {
                                res.is_auto_deref = true;
                            }
                        }
                        return res;
                    }
                }
            }
            if (cf_.methods.contains(owner_type + "::" + mem.field) || cf_.methods.contains(owner_type + "." + mem.field)) {
                res.kind = SymbolKind::Method;
                res.owner_type = owner_type;
                res.canonical_name = std::format("cfire_method_{}", mem.field);
                return res;
            }
        }

        // 3. User method across types
        if (cf_.method_owners.contains(mem.field) || cf_.method_owner.contains(mem.field)) {
            res.kind = SymbolKind::Method;
            res.canonical_name = std::format("cfire_method_{}", mem.field);
            return res;
        }

        return std::nullopt;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_interface_method_sig_sema(const MemberExpr& mem) const -> const InterfaceMethodSig* {
        if (!mem.object) return nullptr;
        std::string recv_t = type_name_of(mem.object);
        if (recv_t.empty()) return nullptr;
        auto it = fn_type_param_bounds_.find(recv_t);
        if (it == fn_type_param_bounds_.end()) return nullptr;
        for (const auto& bound : it->second) {
            auto iit = cf_.interface_defs.find(bound);
            if (iit == cf_.interface_defs.end()) continue;
            for (const auto& m : iit->second.methods) {
                if (m.name == mem.field) return &m;
            }
        }
        return nullptr;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::resolve_enum_by_prefix_sema(std::string_view prefix) const -> const EnumDecl* {
        if (prefix.empty()) return nullptr;
        std::string p_str(prefix);
        if (auto it = cf_.enums.find(p_str); it != cf_.enums.end()) {
            return &it->second;
        }
        if (auto dot = prefix.find('.'); dot != std::string_view::npos) {
            std::string_view pkg = prefix.substr(0, dot);
            std::string_view ename = prefix.substr(dot + 1);
            std::string mangled = std::format("cfire_pkg_{}__{}", pkg, ename);
            if (auto it = cf_.enums.find(mangled); it != cf_.enums.end()) {
                return &it->second;
            }
            for (const auto& [k, en] : cf_.enums) {
                (void)k;
                std::string orig = en.name;
                if (orig.starts_with("cfire_pkg_")) {
                    if (auto sep = orig.find("__"); sep != std::string::npos) orig = orig.substr(sep + 2);
                }
                if (en.package_name == pkg && orig == ename) {
                    return &en;
                }
            }
        }
        return nullptr;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_expr_type_unresolved_for_redundancy_sema(const ExprPtr& inner) const -> bool {
        if (!inner) return true;
        auto is_resolution_code = [](std::string_view code) -> bool {
            return code == "cfire/UNRESOLVED-PACKAGE" || code == "cfire/UNKNOWN-MODULE" ||
                   code == "cfire/UNDECLARED-TYPE" || code == "cfire/UNDECLARED-IDENT" ||
                   code == "cfire/UNKNOWN-METHOD" || code == "cfire/UNKNOWN-FIELD" ||
                   code == "cfire/UNKNOWN-STDLIB-FN" || code == "cfire/UNKNOWN-STDLIB-TYPE" ||
                   code == "cfire/SYNTAX";
        };
        for (const auto& d : cf_.diagnostics) {
            if (d.severity == "error" && is_resolution_code(d.code)) return true;
        }
        for (const auto& d : diags_) {
            if (d.severity == "error" && is_resolution_code(d.code)) return true;
        }
        TypeId t = infer_expr_type_id_sema(inner);
        if (!t.is_unknown()) return false;
        if (std::holds_alternative<ArrayLitExpr>(inner->node) ||
            std::holds_alternative<TupleExpr>(inner->node) ||
            std::holds_alternative<ClosureExpr>(inner->node)) {
            return false;
        }
        if (const auto* call = std::get_if<CallExpr>(&inner->node)) {
            if (const auto* cid = call->callee ? std::get_if<IdentExpr>(&call->callee->node) : nullptr) {
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end() && it->second.return_type.name == "void" && !it->second.return_type.is_fallible && !it->second.return_type.is_optional) {
                    return false;
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end() && pit->second.return_type.name == "void" && !pit->second.return_type.is_fallible && !pit->second.return_type.is_optional) {
                        return false;
                    }
                }
            } else if (const auto* mem = call->callee ? std::get_if<MemberExpr>(&call->callee->node) : nullptr) {
                if (const auto* sig = resolve_member_call_sig(*mem); sig && sig->return_type.name == "void" && !sig->return_type.is_fallible && !sig->return_type.is_optional) {
                    return false;
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem); im && im->return_type.name == "void" && !im->return_type.is_fallible && !im->return_type.is_optional) {
                    return false;
                }
                const auto& idx = StdlibCatalogIndex::instance();
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name) && idx.has_namespace(obj_id->name)) {
                    if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field); sfn && sfn->unwrapped_return_type == "void" && !sfn->is_fallible && !sfn->is_optional) {
                        return false;
                    }
                }
            }
        }
        return true;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::expr_returns_fallible_sema(const ExprPtr& e, std::string* out_callee, std::string* out_ret) const -> bool {
        if (!e) return false;
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return expr_returns_fallible_sema(or_ex->fallback_val, out_callee, out_ret);
            }
            return false;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node);
                cid && cid->name == "make" && call->type_args.size() == 1 && !is_ident_in_scope_sema("make")) {
                // `make(T, ...)` is fallible exactly when the constructor it calls is.
                const TypeRef& t = call->type_args[0];
                const TypeRef& target = (!t.smart_wrap.empty() && t.elem_type) ? *t.elem_type : t;
                if (const FnDecl* ctor = find_user_ctor(target)) {
                    if (out_callee) *out_callee = "make";
                    if (out_ret) *out_ret = format_type_ref_sema(target);
                    return ctor->return_type.is_fallible;
                }
                if (const auto* sc = find_stdlib_ctor(target.name)) {
                    if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                        if (out_callee) *out_callee = "make";
                        if (out_ret) *out_ret = std::format("{}.{}", sc->ns, sc->type);
                        return sig->is_fallible;
                    }
                }
                return false;
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                const FnDecl* sig = nullptr;
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        sig = &pit->second;
                    }
                }
                if (!sig) {
                    if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                        sig = &it->second;
                    }
                }
                if (sig && sig->return_type.is_fallible) {
                    if (out_callee) *out_callee = cid->name;
                    if (out_ret) *out_ret = format_type_ref_sema(sig->return_type);
                    return true;
                }
                if (fn_ret_fallible_vars_.contains(cid->name)) {
                    if (out_callee) *out_callee = cid->name;
                    if (out_ret) *out_ret = "T!";
                    return true;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                const auto& idx = StdlibCatalogIndex::instance();
                const std::string resolved_ns = obj_id ? resolve_stdlib_ns_sema(obj_id->name) : std::string{};
                if (obj_id && !resolved_ns.empty() && !is_ident_in_scope_sema(obj_id->name)) {
                    if (const auto* sfn = idx.find_ns_fn(resolved_ns, mem->field)) {
                        if (sfn->is_fallible) {
                            if (out_callee) *out_callee = resolved_ns + "." + mem->field;
                            if (out_ret) *out_ret = sfn->return_type;
                            return true;
                        }
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    if (sig->return_type.is_fallible) {
                        if (out_callee) *out_callee = mem->field;
                        if (out_ret) *out_ret = format_type_ref_sema(sig->return_type);
                        return true;
                    }
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem)) {
                    if (im->return_type.is_fallible) {
                        if (out_callee) *out_callee = mem->field;
                        if (out_ret) *out_ret = format_type_ref_sema(im->return_type);
                        return true;
                    }
                }
                auto lookup_recv_fallible = [&](std::string_view raw_t) -> const StdFnSigInfo* {
                    if (raw_t.empty()) return nullptr;
                    if (auto mit = idx.receiver_methods.find(std::string(raw_t) + "." + mem->field);
                        mit != idx.receiver_methods.end()) {
                        return &mit->second;
                    }
                    std::string canon = canonical_nominal_path_sema(raw_t);
                    if (auto dot = canon.find('.'); dot != std::string::npos) {
                        if (std::string cns = resolve_stdlib_ns_sema(canon.substr(0, dot)); !cns.empty()) {
                            canon = cns + canon.substr(dot);
                        }
                    }
                    if (!canon.empty()) {
                        if (auto mit = idx.receiver_methods.find(canon + "." + mem->field);
                            mit != idx.receiver_methods.end()) {
                            return &mit->second;
                        }
                    }
                    return nullptr;
                };
                if (obj_id) {
                    if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                        if (const auto* m = lookup_recv_fallible(vt->second); m && m->is_fallible) {
                            if (out_callee) *out_callee = obj_id->name + "." + mem->field;
                            if (out_ret) *out_ret = m->return_type;
                            return true;
                        }
                    }
                }
                if (mem->object) {
                    if (const auto* m = lookup_recv_fallible(type_name_of(mem->object)); m && m->is_fallible) {
                        if (out_callee) *out_callee = mem->field;
                        if (out_ret) *out_ret = m->return_type;
                        return true;
                    }
                }
            }
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_expr_fallible_for_unwrap_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return is_expr_fallible_for_unwrap_sema(or_ex->fallback_val);
            }
            return false;
        }
        if (std::holds_alternative<TryExpr>(e->node)) {
            return false;
        }
        if (expr_returns_fallible_sema(e)) return true;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return fallible_vars_.contains(id->name);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    for (const auto& st : cf_.ast.structs) {
                        if (st.name == owner_t) {
                            for (const auto& fld : st.fields) {
                                if (fld.name == mem->field) return fld.type.is_fallible;
                            }
                        }
                    }
                }
            }
            return false;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "ok" || cid->name == "err" || cid->name == "error" || cid->name == "Err") {
                    return true;
                }
                if (fn_ret_fallible_vars_.contains(cid->name)) {
                    return true;
                }
                if (cid->name.starts_with("cpp::") || cid->name.starts_with("cpp.") ||
                    cid->name.starts_with("rl::") || cid->name.starts_with("rg::") ||
                    cf_.cpp_fn_params.contains(cid->name)) {
                    return true;
                }
                return false;
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                    if (obj_id->name == "cpp") {
                        return true;
                    }
                    std::string resolved_ns = resolve_stdlib_ns_sema(obj_id->name);
                    if ((resolved_ns == "fs" && (mem->field == "read_all" || mem->field == "write_all")) ||
                        (resolved_ns == "os" && mem->field == "socketpair")) {
                        return true;
                    }
                    if (!resolved_ns.empty() && idx.has_namespace(resolved_ns)) {
                        if (const auto* sfn = idx.find_ns_fn(resolved_ns, mem->field)) {
                            return sfn->is_fallible;
                        }
                        return false;
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_fallible;
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem)) {
                    return im->return_type.is_fallible;
                }
                if (mem->field == "read_str" || mem->field == "write_str" ||
                    mem->field == "read_all" || mem->field == "read" || mem->field == "write" ||
                    mem->field == "call_rpc" || mem->field == "run_webview_test" ||
                    mem->field == "generate_struct" || mem->field == "generate_struct_from_file" ||
                    mem->field == "run_struct" || mem->field == "run_struct_validated" ||
                    mem->field == "embed" || mem->field == "embed_image" ||
                    mem->field == "ocr" || mem->field == "detect_language" ||
                    mem->field == "stream" || mem->field == "ask" || mem->field == "tx") {
                    return true;
                }
                if (auto uit = idx.unambiguous_methods.find(mem->field); uit != idx.unambiguous_methods.end()) {
                    if (uit->second.is_fallible) return true;
                }
                std::string dot_suffix = "." + mem->field;
                for (const auto& [k, minfo] : idx.receiver_methods) {
                    if (k.ends_with(dot_suffix) && minfo.is_fallible) return true;
                }
                std::string col_suffix = "::" + mem->field;
                for (const auto& [k, mdecl] : cf_.methods) {
                    if (k.ends_with(col_suffix) && mdecl.return_type.is_fallible) return true;
                }
                const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (oid && (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name))) return true;
            }
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_expr_optional_for_unwrap_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return is_expr_optional_for_unwrap_sema(or_ex->fallback_val);
            }
            return false;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            // `try f` on a `T?!` function leaves a `T?` (`try regex.find(p, s) or { ... }`).
            if (const auto* call = tr->inner ? std::get_if<CallExpr>(&tr->inner->node) : nullptr) {
                if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                    const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                    const auto& idx = StdlibCatalogIndex::instance();
                    if (obj_id && !is_ident_in_scope_sema(obj_id->name) && idx.has_namespace(obj_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) return sfn->is_fallible && sfn->unwrapped_return_type.ends_with('?');
                    }
                }
            }
            return false;
        }
        if (expr_returns_optional_sema(e)) return true;
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name.starts_with("cpp::") || cid->name.starts_with("cpp.") ||
                    cid->name.starts_with("rl::") || cid->name.starts_with("rg::") ||
                    cf_.cpp_fn_params.contains(cid->name)) {
                    return true;
                }
                return false;
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                if (type_name_of(e).ends_with("?")) return true;  // a typed receiver method (`pq.pop() -> T?`)
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                    if (obj_id->name == "cpp") {
                        return true;
                    }
                    if (idx.has_namespace(obj_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                            return sfn->is_optional;
                        }
                        return false;
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_optional && !sig->return_type.is_fallible;
                }
                if (const auto* im = resolve_interface_method_sig_sema(*mem)) {
                    return im->return_type.is_optional && !im->return_type.is_fallible;
                }
                if (auto uit = idx.unambiguous_methods.find(mem->field); uit != idx.unambiguous_methods.end()) {
                    if (uit->second.is_optional) return true;
                }
                std::string dot_suffix = "." + mem->field;
                for (const auto& [k, minfo] : idx.receiver_methods) {
                    if (k.ends_with(dot_suffix) && minfo.is_optional) return true;
                }
                std::string col_suffix = "::" + mem->field;
                for (const auto& [k, mdecl] : cf_.methods) {
                    if (k.ends_with(col_suffix) && mdecl.return_type.is_optional) return true;
                }
                const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (oid && (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name))) return true;
            }
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::validate_move_operand(
        const ExprPtr& operand,
        bool required_by_sig,
        int line,
        int col
    ) -> std::expected<void, std::string> {
        if (!operand) return {};
        if (std::holds_alternative<UnaryExpr>(operand->node)) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MOVE", std::format("cannot use 'move' on temporary or unary expression; 'move' only applies to owned variables")));
        }
        auto pi = extract_path_info(operand);
        if (!pi) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MOVE", std::format("cannot use 'move' on temporary or literal expression; 'move' only applies to owned variables")));
        }
        if (pi->path == pi->root && scalar_primitive_vars_.contains(pi->root) && !required_by_sig) {
            std::string tname = lookup_or(var_types_, pi->root, "primitive");
            return emit_error(diag_at(line, col, "cfire/INVALID-MOVE", std::format("'move' has no effect on Copy primitive variable '{}' of type '{}'", pi->root, tname)));
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_effectful_stmt_expr(const ExprPtr& e) -> bool {
        if (!e) return true;
        if (std::holds_alternative<CallExpr>(e->node) ||
            std::holds_alternative<MatchExpr>(e->node)) {
            return true;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return is_effectful_stmt_expr(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            return is_effectful_stmt_expr(or_ex->inner) || !or_ex->handler_body.empty();
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return id->name == "break" || id->name == "continue";
        }
        return false;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::extract_root_ident_sema(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return extract_root_ident_sema(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return extract_root_ident_sema(idx->object);
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return extract_root_ident_sema(sl->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return extract_root_ident_sema(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return extract_root_ident_sema(un->operand);
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) return extract_root_ident_sema(tr->inner);
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return extract_root_ident_sema(or_ex->inner);
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            auto l = extract_root_ident_sema(bin->left);
            return !l.empty() ? l : extract_root_ident_sema(bin->right);
        }
        return "";
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_expr(const ExprPtr& e) -> std::expected<void, std::string> {
        if (!e) return {};
        if (auto* id = std::get_if<IdentExpr>(&e->node)) {
            id->resolution = resolve_ident(id->name);
            if (id->name == "_") return {};
            if (id->name == "break" || id->name == "continue") {
                if (loop_depth_ == 0) {
                    return emit_error(diag_at(e->line, e->col, "cfire/BREAK-OUTSIDE-LOOP", std::format("'{}' cannot be used outside of a 'for' loop", id->name)));
                }
                return {};
            }
            if (auto r = record_var_read(id->name, e->line, e->col, e->id); !r) return r;
            auto dot = id->name.find('.');
            auto dcol = id->name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                std::string prefix = id->name.substr(0, sep);
                if (auto r = check_stdlib_ns_imported(prefix, e->line, e->col); !r) return r;
            }
            if (!is_known_identifier_sema(id->name)) {
                if (cf_.shadowed_default_qualifiers.contains(id->name)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, id->name);
                    return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.<member>'", id->name, active_alias, active_alias)));
                }
                if (cf_.cpp_fn_params.contains(id->name)) {
                    std::string cpp_q = cf_.cpp_qualifiers.empty() ? "cpp" : *cf_.cpp_qualifiers.begin();
                    return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("undeclared identifier '{}'; did you mean '{}.{}'?", id->name, cpp_q, id->name)));
                }
                return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("undeclared identifier '{}'", id->name)));
            }
            return {};
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return check_literal_expr(*lit, e->line);
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            // raw pointers belong to C++ interop (`cpp { }`, `import cpp`, `own`/`shared` `from_raw`/`release`).
            if (un->op == "&" || un->op == "&mut") {
                return emit_error(diag_at(e->line, e->col, "cfire/RAW-POINTER", std::format(
                    "'{}x' takes a raw pointer, which cfire keeps to C++ interop: pass the value itself (parameters are views by default, "
                    "'mut' when the callee changes it), give each task its slot with 'scope.spawn |mut xs[i]|', hold heap data in "
                    "'own[T]'/'shared[T]', or write the pointer code in a 'cpp {{ }}' block", un->op == "&mut" ? "&mut " : "&")));
            }
            if (un->op == "*") {
                return emit_error(diag_at(e->line, e->col, "cfire/RAW-POINTER",
                    "'*p' dereferences a raw pointer, which cfire keeps to C++ interop: read a locked value as 'guard.value', "
                    "reach an 'own[T]'/'shared[T]' through its fields, or write the pointer code in a 'cpp { }' block"));
            }
            if (auto r = check_unary_expr(*un, e->line, e->col); !r) return r;
            if (un->op == "!" || un->op == "not") return check_bool_condition(un->operand, "operand of '!'");
            return {};
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            return check_binary_expr(*bin, e->line, e->col);
        }
        if (auto* mem = std::get_if<MemberExpr>(&e->node); mem && mem->field == "value" && mem->object) {
            std::string ot = type_name_of(mem->object);
            if (ot.starts_with("mut ")) ot.erase(0, 4);
            if (ot.starts_with("sync.MutexGuard[") || ot.starts_with("MutexGuard[")) cf_.guard_value_members.insert(mem);
        }
        if (auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (auto r = check_member_expr(*mem, e->line, e->col); !r) return r;
            mem->resolution = resolve_member(*mem);
            return {};
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            return check_index_expr(*idx, e->line, e->col);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto r = check_expr(sl->object); !r) return r;
            if (sl->start) if (auto r = check_expr(sl->start); !r) return r;
            if (sl->end) if (auto r = check_expr(sl->end); !r) return r;
            return {};
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            if (tr->inner) unwrapped_exprs_.insert(tr->inner);
            return check_try_expr(*tr, e->line, e->col);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->inner) unwrapped_exprs_.insert(or_ex->inner);
            if (unwrapped_exprs_.contains(e) && or_ex->fallback_val) {
                unwrapped_exprs_.insert(or_ex->fallback_val);
            }
            return check_or_expr(*or_ex, e->line, e->col, e);
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            auto dot = st->type_name.find('.');
            auto dcol = st->type_name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                std::string prefix = st->type_name.substr(0, sep);
                if (auto r = check_stdlib_ns_imported(prefix, e->line, e->col); !r) return r;
            }
            if (dcol != std::string::npos && (dot == std::string::npos || dcol < dot)) {
                std::string qual = st->type_name.substr(0, dcol);
                if (!cf_.enums.contains(qual) &&
                    (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual) ||
                     is_namespace_ident_sema(qual) || cf_.shadowed_default_qualifiers.contains(qual))) {
                    std::string rest = st->type_name.substr(dcol + 2);
                    return emit_error(diag_at(e->line, e->col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", qual, rest, qual, rest), {{"kind", "qualifier"}}));
                }
            }
            if (dot != std::string::npos) {
                std::string qual = st->type_name.substr(0, dot);
                if (cf_.shadowed_default_qualifiers.contains(qual)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, qual);
                    std::string rest = st->type_name.substr(dot + 1);
                    return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.{}'", qual, active_alias, active_alias, rest)));
                }
                if (std::string ns = resolve_stdlib_ns_sema(qual); !ns.empty() && !is_cpp_qualifier_sema(qual) && !is_vendor_alias_sema(qual)) {
                    const bool manifest = std::filesystem::path(cf_.ast.path).filename() == "build.cf" || cf_.ast.package_name == "build";
                    if (std::string why = stdlib_type_name_problem(ns, st->type_name.substr(dot + 1), manifest); !why.empty()) {
                        return emit_error(diag_at(e->line, e->col, "cfire/UNKNOWN-STDLIB-TYPE", why));
                    }
                }
            }
            for (const auto& f : st->fields) {
                if (auto r = check_expr(f.value); !r) return r;
            }
            if (auto r = check_struct_literal_fields(*st, e->line, e->col); !r) return r;
            for (const auto& f : st->fields) {
                if (f.value) {
                    if (auto r = check_move_only_copy(f.value, std::format("field '{}'", f.name), f.value->line, f.value->col); !r) return r;
                }
            }
            return {};
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            for (const auto& el : arr->elements) {
                if (auto r = check_expr(el); !r) return r;
            }
            return {};
        }
        if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
            for (const auto& el : tup->elements) {
                if (auto r = check_expr(el); !r) return r;
            }
            return {};
        }
        if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) {
            if (auto r = check_slot_captures(*cl, e->line); !r) return r;
            std::unordered_set<std::string> saved_slots = active_slot_captures_;
            for (const auto& sc : cl->slot_captures) active_slot_captures_.insert(sc.first);
            auto slots_guard = cfire::scope_exit([this, saved_slots] { active_slot_captures_ = saved_slots; });
            ++in_closure_depth_;
            closure_ret_stack_.emplace_back(cl, in_closure_depth_);
            auto ret_guard = cfire::scope_exit([this] { closure_ret_stack_.pop_back(); });
            int saved_loop_depth = loop_depth_;
            int saved_invalid_unwrap = in_invalid_unwrap_context_;
            int saved_escaping_entry_scope = escaping_closure_entry_scope_;
            bool was_escaping = cl->is_escaping;
            if (was_escaping) {
                ++in_escaping_closure_depth_;
                escaping_closure_entry_scope_ = scope_depth_;
            }
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
            auto hint_it = closure_param_hints_.find(cl);
            for (std::size_t i = 0; i < cl->params.size(); ++i) {
                bool is_mut = (i < cl->param_intents.size()) &&
                              (cl->param_intents[i] == "mut" || cl->param_intents[i] == "out");
                bind_scoped_var(cl->params[i], is_mut);
                bool typed = i < cl->param_types.size() && cl->param_types[i].has_value();
                if (typed) {
                    var_types_[cl->params[i]] = format_type_ref_sema(*cl->param_types[i]);
                } else if (hint_it != closure_param_hints_.end() && i < hint_it->second.size()) {
                    var_types_[cl->params[i]] = hint_it->second[i];
                }
            }
            return check_block_stmts(cl->body);
        }
        if (auto* call = std::get_if<CallExpr>(&e->node)) {
            if (auto r = check_call_expr(*call, e->line, e->col, e); !r) return r;
            if (call->callee) {
                if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                    call->resolution = cid->resolution;
                } else if (const auto* cmem = std::get_if<MemberExpr>(&call->callee->node)) {
                    call->resolution = cmem->resolution;
                }
            }
            return {};
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            if (auto r = check_match_common(me->target, me->is_move, me->arms, me->line, me->col, true); !r) return r;
            if (me->is_if && if_exprs_checked_.insert(me).second) {
                if (auto r = check_if_expr_branches(*me); !r) return r;
            }
            me->result_type = unified_match_result_type(*me);
            return {};
        }
        return {};
    }
  // an initializer can be checked twice

    CFIRE_SEMA_INLINE auto SemaFnChecker::check_if_expr_branches(const MatchExpr& me) -> std::expected<void, std::string> {
        struct Branch { TypeId t; bool num_literal = false; int line = 0; int col = 0; };
        std::vector<Branch> branches;
        for (const auto& arm : me.arms) {
            if (arm.body.empty()) return {};
            const auto* es = std::get_if<ExprStmt>(&arm.body.back()->node);
            if (!es || !es->expr) return {};
            const Expr* v = es->expr;
            if (const auto* un = std::get_if<UnaryExpr>(&v->node); un && un->op == "-" && un->operand) v = un->operand;
            const auto* lit = std::get_if<LiteralExpr>(&v->node);
            bool num_lit = lit && (lit->kind == TokenKind::Int || lit->kind == TokenKind::Float);
            branches.push_back({infer_expr_type_id_sema(es->expr), num_lit, es->expr->line, es->expr->col});
        }
        if (branches.size() != 2) return {};
        const auto& a = branches[0];
        const auto& b = branches[1];
        if (!a.t.is_valid() || !b.t.is_valid() || a.t.is_unknown() || b.t.is_unknown()) return {};
        if (cf_.type_table.is_assignable(a.t, b.t) || cf_.type_table.is_assignable(b.t, a.t)) return {};
        auto is_num = [&](TypeId t) {
            std::string n = cf_.type_table.to_string(t);
            return n == "i8" || n == "i16" || n == "i32" || n == "i64" || n == "u8" || n == "u16" || n == "u32" || n == "u64" ||
                   n == "isize" || n == "usize" || n == "f32" || n == "f64" || n == "byte";
        };
        if ((a.num_literal && is_num(b.t)) || (b.num_literal && is_num(a.t))) return {};
        return emit_error(diag_at(b.line, b.col, "cfire/TYPE-MISMATCH",
            std::format("the branches of an 'if' expression have different types: '{}' and '{}'",
                        cf_.type_table.to_string(a.t), cf_.type_table.to_string(b.t))));
    }


    // Primitive type shared by all arms of a match expression, treating bare literal arms as adaptable.
    CFIRE_SEMA_INLINE auto SemaFnChecker::unified_match_result_type(const MatchExpr& me) const -> std::string {
        std::string result;
        for (const auto& arm : me.arms) {
            if (arm.body.empty()) continue;
            const auto* es = std::get_if<ExprStmt>(&arm.body.back()->node);
            if (!es || !es->expr) return "";
            bool is_literal = std::holds_alternative<LiteralExpr>(es->expr->node);
            if (const auto* un = std::get_if<UnaryExpr>(&es->expr->node); un && un->op == "-" && un->operand &&
                std::holds_alternative<LiteralExpr>(un->operand->node)) {
                is_literal = true;
            }
            if (is_literal) continue;
            TypeId t = infer_expr_type_id_sema(es->expr);
            if (t.is_unknown() || t.index < TypeId::Bool().index || t.index > TypeId::Str().index) return "";
            std::string n = cf_.type_table.to_string(t);
            if (!result.empty() && result != n) return "";
            result = n;
        }
        return result;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_try_expr(const TryExpr& tr, int line, int col) -> std::expected<void, std::string> {
        if (in_defer_depth_ > 0) {
            if (auto r = emit_error(diag_at(line, col, "cfire/TRY-IN-DEFER", std::format("cannot use 'try' inside a 'defer' block; handle errors explicitly with 'or'"))); !r) return r;
        } else if (!fn_is_fallible_ && in_closure_depth_ == 0) {
            std::string ret_desc = fn_ret_str_.empty() ? "void" : fn_ret_str_;
            if (auto r = emit_error(diag_at(line, col, "cfire/TRY-IN-NON-FALLIBLE-FN", std::format("cannot use 'try' in non-fallible function '{}' (returns '{}'); mark return type with '!' or handle error with 'or'", fn_name_.empty() ? "<anonymous>" : fn_name_, ret_desc))); !r) return r;
        }
        if (in_invalid_unwrap_context_ > 0) {
            if (auto r = emit_error(diag_at(line, col, "cfire/INVALID-TRY-CONTEXT", std::format("cannot use 'try' inside short-circuit operand or 'for' loop condition; hoist into a statement or use 'or fallback'"))); !r) return r;
        }
        std::size_t errors_before = errors_.size();
        if (auto r = check_expr(tr.inner); !r) return r;
        if (errors_.size() == errors_before) {
            if (!is_expr_fallible_for_unwrap_sema(tr.inner)) {
                if (is_expr_optional_for_unwrap_sema(tr.inner)) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/TRY-ON-OPTIONAL", std::format("cannot use 'try' on optional 'T?' value; 'try' only propagates fallible 'T!' errors — use 'or fallback' or 'or {{ ... }}' instead"))); !r) return r;
                } else if (!is_expr_type_unresolved_for_redundancy_sema(tr.inner)) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/REDUNDANT-TRY", std::format("'try' used on non-fallible expression; remove 'try'"))); !r) return r;
                }
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::is_likely_interpolation_placeholder(std::string_view sv, const InterpPlaceholder& ph) -> bool {
        if (ph.start_pos + 1 >= sv.size()) return false;
        char first_raw = sv[ph.start_pos + 1];
        if (std::isspace(static_cast<unsigned char>(first_raw))) return false;
        if (!(std::isalnum(static_cast<unsigned char>(first_raw)) || first_raw == '_' || first_raw == '(' || first_raw == '*')) {
            return false;
        }
        // Exclude JSON/CSS/code blocks containing ';' or single ':'
        bool in_q = false;
        for (std::size_t i = 0; i < ph.expr_text.size(); ++i) {
            char c = ph.expr_text[i];
            if (c == '"' && (i == 0 || ph.expr_text[i - 1] != '\\')) in_q = !in_q;
            else if (!in_q) {
                if (c == ';') return false;
                if (c == ':') {
                    bool is_dcol = (i + 1 < ph.expr_text.size() && ph.expr_text[i + 1] == ':') ||
                                   (i > 0 && ph.expr_text[i - 1] == ':');
                    if (!is_dcol) return false;
                }
            }
        }
        // Exclude pure integer regex quantifiers like `\d{4}`
        bool all_digits = !ph.expr_text.empty() && std::ranges::all_of(ph.expr_text, [](unsigned char c) {
            return std::isdigit(c) || c == ',';
        });
        if (all_digits) return false;
        return true;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_literal_expr(const LiteralExpr& lit, int line, int col) -> std::expected<void, std::string> {
        if (lit.kind == TokenKind::String && lit.value.contains('{')) {
            lit.interp_snippets.clear();
            auto placeholders = scan_interpolation_placeholders(lit.value);
            for (const auto& ph : placeholders) {
                if (!is_likely_interpolation_placeholder(lit.value, ph)) continue;
                int expr_col = std::max(1, col) + 1 + static_cast<int>(ph.expr_offset);
                auto parsed = parse_expr_snippet(path_, ph.expr_text, line, expr_col);
                if (!parsed) {
                    if (auto r = emit_error(diag_at(line, expr_col, "cfire/INVALID-INTERPOLATION", std::format("invalid expression '{}' in string interpolation", ph.expr_text))); !r) return r;
                    continue;
                }
                if (!ph.expr_text.empty() && std::isdigit(static_cast<unsigned char>(ph.expr_text.front()))) {
                    if (auto r = emit_error(diag_at(line, expr_col, "cfire/AMBIGUOUS-INTERPOLATION", std::format("ambiguous string interpolation '{{{}}}'; expressions starting with a number literal must be parenthesized as '{{({})}}' to distinguish from positional format arguments", ph.expr_text, ph.expr_text))); !r) return r;
                    continue;
                }
                lit.interp_snippets.push_back(std::move(*parsed));
                if (auto r = check_expr(lit.interp_snippets.back().expr); !r) return r;
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_unary_expr(const UnaryExpr& un, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(un.operand); !r) return r;
        if (un.op == "move") {
            if (auto r = validate_move_operand(un.operand, false, line, col); !r) return r;
            if (auto pi = extract_path_info(un.operand)) {
                if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                moved_at_line_[pi->root] = line;
                invalidate_loans_for_owner(
                    pi->root,
                    std::format("underlying owner '{}' was moved at line {}", pi->root, line),
                    line);
            }
        } else if (un.op == "*" && un.operand) {
            // `*` on a value (e.g. `*line[1..]`) used to reach clang as "indirection requires pointer operand".
            // Only slices and literals: inference reports a pointer field or call result as its pointee type.
            const bool certain = std::holds_alternative<SliceExpr>(un.operand->node) || std::holds_alternative<LiteralExpr>(un.operand->node);
            if (TypeId ot = infer_expr_type_id_sema(un.operand); certain && is_concrete_checkable_type(ot)) {
                const auto& od = cf_.type_table.get(ot);
                const bool str_view = od.kind == TypeKind::Borrow && od.elem_type == TypeId::Str();
                if (str_view || ot == TypeId::Str()) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", "cannot dereference a 'str' value with '*'; remove the '*' (a 'str' slice is copied automatically where owned text is needed)")); !r) return r;
                } else if (cf_.type_table.is_numeric(ot) || ot == TypeId::Bool() || ot == TypeId::Byte() ||
                           od.kind == TypeKind::Slice || od.kind == TypeKind::Array || od.kind == TypeKind::Map) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("cannot dereference a value of type '{}' with '*'; only pointers, borrows and own/shared handles can be dereferenced", cf_.type_table.to_string(ot)))); !r) return r;
                }
            }
            if (const auto* id = std::get_if<IdentExpr>(&un.operand->node)) {
                if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                    if (it->second == NullState::Null) {
                        if (auto r = emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("dereference of null pointer '{}'", id->name), {{"ptr", id->name}})); !r) return r;
                    } else if (it->second == NullState::MaybeNull) {
                        if (auto r = emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("dereference of potentially null pointer '{}' without '!= none' guard", id->name), {{"ptr", id->name}})); !r) return r;
                    }
                }
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_binary_expr(const BinaryExpr& bin, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(bin.left); !r) return r;
        if (bin.op == "&&" || bin.op == "||") {
            ++in_invalid_unwrap_context_;
            auto guard = cfire::scope_exit([this] { --in_invalid_unwrap_context_; });
            if (auto r = check_expr(bin.right); !r) return r;
            if (auto r = check_bool_condition(bin.left, std::format("left operand of '{}'", bin.op)); !r) return r;
            if (auto r = check_bool_condition(bin.right, std::format("right operand of '{}'", bin.op)); !r) return r;
        } else {
            if (auto r = check_expr(bin.right); !r) return r;
        }
        if (bin.op == "<<" || bin.op == ">>") {
            if (auto r = check_const_shift_count(bin.left, bin.right, line, col); !r) return r;
        }
        // `s[0] == "L"` compares a byte with text; it used to reach clang as a pointer/integer comparison.
        if (bin.op == "==" || bin.op == "!=" || bin.op == "<" || bin.op == "<=" || bin.op == ">" || bin.op == ">=") {
            TypeId lt = infer_expr_type_id_sema(bin.left);
            TypeId rt = infer_expr_type_id_sema(bin.right);
            const TypeId str_view = type_id_of("&str");
            auto is_text = [&](TypeId t) { return t == TypeId::Str() || (str_view.is_valid() && t == str_view); };
            if ((lt == TypeId::Byte() && is_text(rt)) || (rt == TypeId::Byte() && is_text(lt))) {
                const ExprPtr& text_side = lt == TypeId::Byte() ? bin.right : bin.left;
                std::string hint = "compare with a byte literal such as 'a', or slice the string (s[i..i + 1])";
                if (const auto* lit = text_side ? std::get_if<LiteralExpr>(&text_side->node) : nullptr;
                    lit && lit->kind == TokenKind::String && lit->value.size() == 1 && static_cast<unsigned char>(lit->value[0]) < 0x80 && lit->value[0] != '\\') {
                    hint = std::format("use the byte literal '{}'", lit->value == "'" ? std::string("\\'") : lit->value);
                }
                return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("cannot compare 'byte' with '{}' using '{}'; {}", cf_.type_table.to_string(lt == TypeId::Byte() ? rt : lt), bin.op, hint)));
            }
        }
        // arithmetic / ordering on `T?` needs an explicit unwrap (`x or default`); `==`/`!=` stay legal
        // because `x != none` is the idiom.
        if (bin.op != "==" && bin.op != "!=" && bin.op != "&&" && bin.op != "||") {
            for (const auto* side : {&bin.left, &bin.right}) {
                if (*side && expr_returns_optional_sema(*side)) {
                    return emit_error(diag_at((*side)->line, (*side)->col, "cfire/TYPE-MISMATCH", std::format("operator '{}' cannot be applied to an optional value; unwrap it first with 'or' (e.g. '(x or 0) {} y')", bin.op, bin.op)));
                }
            }
        }
        if ((bin.op == "/" || bin.op == "%") && is_const_zero_expr(bin.right) &&
            infer_expr_type_id_sema(bin.left) != TypeId::Float32() && infer_expr_type_id_sema(bin.left) != TypeId::Float64()) {
            return emit_error(diag_at(line, col, "cfire/DIVIDE-BY-ZERO", std::format("division or modulo by constant zero")));
        }
        // Type compatibility check for binary expressions
        {
            TypeId lt = infer_expr_type_id_sema(bin.left);
            TypeId rt = infer_expr_type_id_sema(bin.right);
            // Borrowed string views (e.g. slices `s[a..b]` typed `&str`) behave as `str` for operators.
            auto decay_str_borrow = [this](TypeId t) -> TypeId {
                if (!t.is_valid() || t.is_unknown()) return t;
                const auto& d = cf_.type_table.get(t);
                if ((d.kind == TypeKind::Borrow || d.kind == TypeKind::MutBorrow) && d.elem_type == TypeId::Str()) {
                    return TypeId::Str();
                }
                return t;
            };
            lt = decay_str_borrow(lt);
            rt = decay_str_borrow(rt);
            auto is_generic_placeholder = [this](TypeId t) -> bool {
                if (!t.is_valid() || t.is_unknown()) return false;
                const auto& d = cf_.type_table.get(t);
                return d.name == "T" || d.name == "U" || d.name == "V" || (d.name.size() == 1 && std::isupper(static_cast<unsigned char>(d.name[0])));
            };
            if (lt.is_valid() && rt.is_valid() && !lt.is_unknown() && !rt.is_unknown() &&
                !is_generic_placeholder(lt) && !is_generic_placeholder(rt)) {
                if (bin.op == "+") {
                    if (lt == TypeId::Str() || rt == TypeId::Str()) {
                        if (lt != rt) {
                            return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("cannot concatenate '{}' and '{}' with '+' without explicit conversion", cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                        }
                    } else if (lt == TypeId::Bool() || rt == TypeId::Bool() || !cf_.type_table.is_numeric(lt) || !cf_.type_table.is_numeric(rt)) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("binary operator '+' cannot be applied to expressions of type '{}' and '{}'", cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                } else if (bin.op == "-" || bin.op == "*" || bin.op == "/" || bin.op == "%") {
                    if (lt == TypeId::Str() || rt == TypeId::Str() || lt == TypeId::Bool() || rt == TypeId::Bool()) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("binary operator '{}' cannot be applied to expressions of type '{}' and '{}'", bin.op, cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                } else if (bin.op == "&" || bin.op == "|" || bin.op == "^" || bin.op == "<<" || bin.op == ">>") {
                    if (!cf_.type_table.is_integer(lt) || !cf_.type_table.is_integer(rt)) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("bitwise operator '{}' requires integer operands, got '{}' and '{}'", bin.op, cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                } else if (bin.op == "<" || bin.op == "<=" || bin.op == ">" || bin.op == ">=") {
                    bool l_num = cf_.type_table.is_numeric(lt);
                    bool r_num = cf_.type_table.is_numeric(rt);
                    if ((l_num && !r_num) || (!l_num && r_num) || (lt == TypeId::Str() && rt != TypeId::Str()) || (lt != TypeId::Str() && rt == TypeId::Str())) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("comparison operator '{}' cannot compare expressions of type '{}' and '{}'", bin.op, cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                }
            }
        }
        return {};
    }


    // "own" / "shared" when `obj` is a smart pointer, "" otherwise.
    CFIRE_SEMA_INLINE auto SemaFnChecker::smart_wrap_of(const ExprPtr& obj) const -> std::string {
        if (!obj) return {};
        TypeId t = infer_expr_type_id_sema(obj);
        if (t.is_valid()) {
            const auto k = cf_.type_table.get(t).kind;
            if (k == TypeKind::Own) return "own";
            if (k == TypeKind::Shared) return "shared";
        }
        if (const auto* id = std::get_if<IdentExpr>(&obj->node); id && smart_ptr_vars_.contains(id->name)) {
            if (shared_ptr_var_names_.contains(id->name)) return "shared";
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                if (it->second.starts_with("own[")) return "own";
                if (it->second.starts_with("shared[")) return "shared";
            }
            return "own";
        }
        return {};
    }


    // `own[T].from_raw(p)` — the object is the type expression `own[T]` (parsed as an index).
    CFIRE_SEMA_INLINE auto SemaFnChecker::own_type_prefix(const ExprPtr& obj) -> std::string {
        if (!obj) return {};
        const auto* ix = std::get_if<IndexExpr>(&obj->node);
        if (!ix || !ix->object) return {};
        const auto* base = std::get_if<IdentExpr>(&ix->object->node);
        if (!base || (base->name != "own" && base->name != "shared")) return {};
        const auto* targ = ix->index ? std::get_if<IdentExpr>(&ix->index->node) : nullptr;
        if (!targ) return {};
        return base->name + "[" + targ->name + "]";
    }


    // `p.zz` on a struct declared in this program must name a field or a method.
    CFIRE_SEMA_INLINE auto SemaFnChecker::check_known_member(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (!mem.object || mem.is_namespace) return {};
        // ownership hand-off for C interop.
        if (mem.field == "release") {
            std::string wrap = smart_wrap_of(mem.object);
            if (wrap == "own") return {};
            if (wrap == "shared") {
                return emit_error(diag_at(line, col, "cfire/INVALID-RELEASE", "'release' is only available on 'own[T]'; a 'shared[T]' may have other owners"));
            }
        }
        std::string owner = type_name_of(mem.object);
        if (owner == "Error" || owner == "&Error") {
            if (mem.field == "code" || mem.field == "message") return {};
            return emit_error(diag_at(line, col + 1, "cfire/UNKNOWN-FIELD", std::format("type 'Error' has no field or method '{}' (methods: code, message)", mem.field)));
        }
        auto sit = cf_.struct_tags.find(owner);
        if (sit == cf_.struct_tags.end()) return {};
        for (const auto& f : sit->second) {
            if (f.field_name == mem.field) return {};
        }
        if (cf_.methods.contains(owner + "::" + mem.field) || cf_.methods.contains(owner + "." + mem.field)) return {};
        if (auto oit = cf_.method_owners.find(mem.field); oit != cf_.method_owners.end() && oit->second.contains(owner)) return {};
        std::string known;
        for (const auto& f : sit->second) known += (known.empty() ? "" : ", ") + f.field_name;
        return emit_error(diag_at(line, col > 0 ? col + (mem.is_namespace ? 2 : 1) : col, "cfire/UNKNOWN-FIELD", std::format("struct '{}' has no field or method '{}' (fields: {})", owner, mem.field, known.empty() ? "none" : known)));
    }


    // struct literal fields must exist and have compatible types.
    // `http.ServerLimits` / `h::ServerLimits` -> the stdlib struct row, resolving import aliases.
    CFIRE_SEMA_INLINE auto SemaFnChecker::lookup_stdlib_struct(std::string_view spelled_in) const -> const StdlibStruct* {
        std::string spelled(spelled_in);
        for (auto p = spelled.find("::"); p != std::string::npos; p = spelled.find("::")) spelled.replace(p, 2, ".");
        auto dot = spelled.find('.');
        if (dot == std::string::npos) return nullptr;
        std::string ns = spelled.substr(0, dot);
        if (is_ident_in_scope_sema(ns)) return nullptr;
        if (std::string canon = resolve_stdlib_ns_sema(ns); !canon.empty()) ns = canon;
        return find_stdlib_struct(ns, std::string_view(spelled).substr(dot + 1));
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_stdlib_struct_fields(const StdlibStruct& sst, const StructLitExpr& st, int line, int col) -> std::expected<void, std::string> {
        const auto fields = stdlib_struct_fields(sst);
        for (const auto& f : st.fields) {
            auto fit = std::find_if(fields.begin(), fields.end(), [&](const StdlibStructField& sf) { return sf.name == f.name; });
            if (fit == fields.end()) {
                std::string names;
                for (const auto& sf : fields) names += (names.empty() ? "" : ", ") + std::string(sf.name);
                if (auto r = emit_error(diag_at(f.value ? f.value->line : line, f.value ? f.value->col : col, "cfire/UNKNOWN-FIELD", std::format("struct '{}.{}' has no field '{}'; fields: {}", sst.ns, sst.name, f.name, names), {{"field", f.name}})); !r) return r;
                continue;
            }
            if (!f.value) continue;
            TypeId want = type_id_of(fit->type);
            bool handled = false;
            if (auto r = check_value_for(want, f.value, handled); !r) return r;
            if (handled) continue;
            TypeId got = infer_expr_type_id_sema(f.value);
            if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got) || expr_returns_optional_sema(f.value)) continue;
            if (cf_.type_table.is_numeric(want) && cf_.type_table.is_numeric(got) && is_numeric_literal_expr(f.value)) continue;
            if (!cf_.type_table.is_assignable(want, got)) {
                if (auto r = emit_error(diag_at(f.value->line, f.value->col, "cfire/TYPE-MISMATCH", std::format("field '{}.{}.{}' expects '{}', got '{}'", sst.ns, sst.name, f.name, cf_.type_table.to_string(want), cf_.type_table.to_string(got)))); !r) return r;
            }
        }
        return {};
    }

    CFIRE_SEMA_INLINE auto SemaFnChecker::is_move_only_type(TypeId t) const -> bool {
        if (!t.is_valid() || t.is_unknown()) return false;
        std::unordered_set<std::uint32_t> visited;
        auto check = [&](auto& self, TypeId cur) -> bool {
            if (!cur.is_valid() || cur.is_unknown() || !visited.insert(cur.index).second || visited.size() > 32) return false;
            const auto& d = cf_.type_table.get(cur);
            switch (d.kind) {
                case TypeKind::Own:
                    return true;
                case TypeKind::NominalStruct:
                    if (cf_.move_only_structs.contains(d.name)) return true;
                    if (d.name.size() >= 2 && d.name.front() == '(' && d.name.back() == ')') {
                        std::string_view inner(d.name.data() + 1, d.name.size() - 2);
                        int depth = 0;
                        std::size_t start = 0;
                        for (std::size_t i = 0; i <= inner.size(); ++i) {
                            if (i < inner.size()) {
                                if (inner[i] == '[' || inner[i] == '(') ++depth;
                                else if ((inner[i] == ']' || inner[i] == ')') && depth > 0) --depth;
                                else if (inner[i] != ',' || depth != 0) continue;
                            }
                            if (i == inner.size() || (inner[i] == ',' && depth == 0)) {
                                std::string_view elem = inner.substr(start, i - start);
                                while (!elem.empty() && std::isspace(static_cast<unsigned char>(elem.front()))) elem.remove_prefix(1);
                                while (!elem.empty() && std::isspace(static_cast<unsigned char>(elem.back()))) elem.remove_suffix(1);
                                if (!elem.empty() && self(self, cf_.type_table.from_name(elem))) return true;
                                start = i + 1;
                            }
                        }
                    }
                    return false;
                case TypeKind::Optional:
                case TypeKind::Array:
                case TypeKind::Slice:
                    return self(self, d.elem_type);
                case TypeKind::Map:
                    return self(self, d.key_type) || self(self, d.elem_type);
                case TypeKind::Tuple:
                    return std::any_of(d.param_types.begin(), d.param_types.end(), [&](TypeId e) { return self(self, e); });
                default:
                    return false;
            }
        };
        return check(check, t);
    }

    CFIRE_SEMA_INLINE auto SemaFnChecker::check_move_only_copy(const ExprPtr& rhs, std::string_view target_desc, int line, int col) -> std::expected<void, std::string> {
        if (!rhs) return {};
        std::string expr_desc;
        if (const auto* id = std::get_if<IdentExpr>(&rhs->node)) {
            if (id->name == "none" || cf_.variant_to_enum.contains(id->name)) return {};
            const bool is_var = var_types_.contains(id->name) ||
                std::any_of(cf_.ast.global_vars.begin(), cf_.ast.global_vars.end(), [&](const GlobalVarDecl& gv) { return gv.name == id->name; });
            if (!is_var) return {};
            expr_desc = id->name;
        } else if (const auto* mem = std::get_if<MemberExpr>(&rhs->node)) {
            if (mem->is_namespace || !mem->object) return {};
            expr_desc = mem->field;
            if (auto pi = extract_path_info(rhs); pi && !pi->path.empty()) expr_desc = pi->path;
        } else if (const auto* idx = std::get_if<IndexExpr>(&rhs->node)) {
            if (!idx->object) return {};
            expr_desc = "element";
            if (auto pi = extract_path_info(idx->object); pi && !pi->path.empty()) expr_desc = pi->path + "[...]";
        } else {
            return {};
        }
        if (auto pi = extract_path_info(rhs); pi && moved_at_line_.contains(pi->root)) return {};
        TypeId rt = infer_expr_type_id_sema(rhs);
        if (!is_move_only_type(rt)) return {};
        return emit_error(diag_at(line, col, "cfire/MOVE-REQUIRED", std::format(
            "cannot copy move-only value '{}' of type '{}' into {} without 'move'; write 'move {}'",
            expr_desc, cf_.type_table.to_string(rt), target_desc, expr_desc)));
    }

    CFIRE_SEMA_INLINE auto SemaFnChecker::check_struct_literal_fields(const StructLitExpr& st, int line, int col) -> std::expected<void, std::string> {
        if (const StdlibStruct* sst = lookup_stdlib_struct(st.type_name)) {
            return check_stdlib_struct_fields(*sst, st, line, col);
        }
        auto sit = cf_.struct_tags.find(st.type_name);
        if (sit == cf_.struct_tags.end() || !st.type_args.empty()) return {};
        for (const auto& f : st.fields) {
            auto fit = std::find_if(sit->second.begin(), sit->second.end(), [&](const StructTagInfo& t) { return t.field_name == f.name; });
            if (fit == sit->second.end()) {
                if (auto r = emit_error(diag_at(f.value ? f.value->line : line, f.value ? f.value->col : col, "cfire/UNKNOWN-FIELD", std::format("struct '{}' has no field '{}'", st.type_name, f.name))); !r) return r;
                continue;
            }
            if (!f.value) continue;
            TypeId want = cf_.type_table.from_typeref(fit->field_type);
            bool handled = false;
            if (auto r = check_value_for(want, f.value, handled); !r) return r;
            if (handled || std::holds_alternative<ArrayLitExpr>(f.value->node)) continue;
            TypeId got = infer_expr_type_id_sema(f.value);
            if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got) || expr_returns_optional_sema(f.value)) continue;
            if (!cf_.type_table.is_assignable(want, got)) {
                if (auto r = emit_error(diag_at(f.value->line, f.value->col, "cfire/TYPE-MISMATCH", std::format("field '{}.{}' expects '{}', got '{}'{}", st.type_name, f.name, cf_.type_table.to_string(want), cf_.type_table.to_string(got), nominal_mismatch_hint_sema(want, got)))); !r) return r;
            }
        }
        return {};
    }


    // `i32`, `f64`, `bool`, ... used as a value prefix (`i32.MAX`, `f64.parse(s)`).
    CFIRE_SEMA_INLINE auto SemaFnChecker::primitive_type_prefix(const ExprPtr& obj) const -> TypeId {
        if (!obj) return TypeId::Unknown();
        const auto* id = std::get_if<IdentExpr>(&obj->node);
        if (!id || is_ident_in_scope_sema(id->name)) return TypeId::Unknown();
        static constexpr std::array<std::string_view, 13> kPrims = {
            "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "usize", "f32", "f64", "bool"};
        if (std::find(kPrims.begin(), kPrims.end(), id->name) == kPrims.end()) return TypeId::Unknown();
        return cf_.type_table.from_name(id->name);
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::primitive_bits(TypeId t) const -> std::uint8_t {
        if (t == TypeId::Bool()) return BmBool;
        if (cf_.type_table.is_float(t)) return BmFloat;
        if (cf_.type_table.is_integer(t)) return BmInt;
        return 0;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_member_expr(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (std::string own_t = own_type_prefix(mem.object); !own_t.empty()) {
            if (mem.field == "from_raw" && own_t.starts_with("own[")) return {};
            return emit_error(diag_at(line, col, "cfire/UNKNOWN-FIELD", std::format("'{}' has no associated function '{}'; the only one is 'own[T].from_raw(p)'", own_t, mem.field)));
        }
        // length is a method on every builtin container; the property form `xs.len` is retired.
        if (mem.field == "len" && !callee_members_.contains(&mem) && mem.object && (builtin_recv_bits(mem.object) & BmSized)) {
            return emit_error(diag_at(line, col, "cfire/RETIRED-API", "'.len' is a method; write '.len()'", {{"from", ".len"}, {"to", ".len()"}, {"rewrite", "len_call"}}));
        }
        if (TypeId prim = primitive_type_prefix(mem.object); prim.is_valid()) {
            if (mem.field == "parse" || find_builtin_assoc_const(primitive_bits(prim), mem.field)) return {};
            std::string avail;
            for (const auto& c : kBuiltinAssocConsts) {
                if ((c.recv & primitive_bits(prim)) == 0) continue;
                avail += (avail.empty() ? "" : ", ") + std::string(c.name);
            }
            avail += (avail.empty() ? "" : ", ") + std::string("parse");
            std::string tname = cf_.type_table.to_string(prim);
            return emit_error(diag_at(line, col + 1 + static_cast<int>(tname.size()), "cfire/UNKNOWN-FIELD", std::format("type '{}' has no associated item '{}'; available: {}", tname, mem.field, avail), {{"field", std::string(mem.field)}}));
        }
        if (mem.object) {
            if (auto r = check_known_member(mem, line, col); !r) return r;
            if (const auto* un = std::get_if<UnaryExpr>(&mem.object->node)) {
                if (un->op == "*" && un->operand) {
                    if (const auto* id = std::get_if<IdentExpr>(&un->operand->node)) {
                        if (smart_ptr_vars_.contains(id->name)) {
                            return emit_error(diag_at(line, col, "cfire/REDUNDANT-DEREF", std::format("explicit dereference '(*{}).{}' is redundant on smart pointer '{}'; use '{}.{}' directly", id->name, mem.field, id->name, id->name, mem.field)));
                        }
                    }
                }
            }
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (cf_.shadowed_default_qualifiers.contains(obj_id->name)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, obj_id->name);
                    return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.{}'", obj_id->name, active_alias, active_alias, mem.field)));
                }
                if (mem.is_namespace) {
                    if (!cf_.enums.contains(obj_id->name) &&
                        (obj_id->name == "cpp" || is_cpp_qualifier_sema(obj_id->name) ||
                         is_vendor_alias_sema(obj_id->name) || is_namespace_ident_sema(obj_id->name))) {
                        return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", obj_id->name, mem.field, obj_id->name, mem.field), {{"kind", "qualifier"}}));
                    }
                }
                if (auto r = check_stdlib_ns_imported(obj_id->name, mem.object->line > 0 ? mem.object->line : line, mem.object->col > 0 ? mem.object->col : col); !r) return r;
                if (obj_id->name == "os" && !is_ident_in_scope_sema("os") &&
                    (mem.field == "STDIN" || mem.field == "STDOUT" || mem.field == "STDERR")) {
                    std::string lower_fn = mem.field;
                    for (char& c : lower_fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.{}' has been replaced by 'os.{}()' returning an RAII 'fs.File' stream handle", mem.field, lower_fn)));
                }
                if (!is_ident_in_scope_sema(obj_id->name) && !is_namespace_ident_sema(obj_id->name) &&
                    cf_.fn_sigs.contains(mem.field)) {
                    return {};
                }
            }
        }
        if (auto r = check_expr(mem.object); !r) return r;
        if (!mem.is_namespace && mem.object) {
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (cf_.enums.contains(obj_id->name)) {
                    if (mem.field != "from_i32" && mem.field != "from_i64" && mem.field != "from_string") {
                        return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("'.' is not a valid separator for enum variant '{}'; use '::' (e.g. '{}::{}')", mem.field, obj_id->name, mem.field), {{"kind", "enum-variant"}}));
                    }
                }
            } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem.object->node)) {
                if (cf_.enums.contains(sub_mem->field) ||
                    sub_mem->field == "Level" || sub_mem->field == "OpenMode" || sub_mem->field == "Waveform" ||
                    sub_mem->field == "SfxPreset" || sub_mem->field == "SeekWhence" || sub_mem->field == "Provider" ||
                    sub_mem->field == "Format") {
                    std::string pkg = std::holds_alternative<IdentExpr>(sub_mem->object->node) ? std::get<IdentExpr>(sub_mem->object->node).name : "";
                    return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("'.' is not a valid separator for enum variant '{}'; use '::' (e.g. '{}.{}::{}')", mem.field, pkg, sub_mem->field, mem.field), {{"kind", "enum-variant"}}));
                }
            }
            if (const auto* id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (optional_vars_.contains(id->name) && !raw_or_smart_ptr_vars_.contains(id->name) &&
                    (mem.field == "len" || mem.field == "empty" || mem.field == "data")) {
                    return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot access '.{}' on optional value '{}' without unwrapping; use 'or' to provide a fallback", mem.field, id->name)));
                }
                if (fallible_vars_.contains(id->name) && !reported_unhandled_vars_.contains(id->name) &&
                    mem.field != "has_value" && mem.field != "value" && mem.field != "value_or" && mem.field != "error") {
                    return emit_error(diag_at(line, col, "cfire/UNHANDLED-RESULT", std::format("cannot access '.{}' on fallible result '{}' without unwrapping with 'try' or 'or'", mem.field, id->name)));
                }
                if (scalar_primitive_vars_.contains(id->name) && mem.field == "len") {
                    std::string tname = lookup_or(var_types_, id->name, "primitive");
                    return emit_error(diag_at(line, col, "cfire/INVALID-MEMBER", std::format("primitive type '{}' has no member '.{}'", tname, mem.field)));
                }
                if (ptr_or_smart_vars_.contains(id->name) &&
                    mem.field != "reset" && mem.field != "get" &&
                    mem.field != "has_value" && mem.field != "value_or") {
                    if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                        if (it->second == NullState::Null) {
                            return emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("member access on '{}', which is none here", id->name), {{"ptr", id->name}}));
                        }
                        if (it->second == NullState::MaybeNull) {
                            return emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("member access on '{0}', which may be none, without checking it first ('if {0} != none {{ ... }}' or 'if v := {0} {{ ... }}')", id->name), {{"ptr", id->name}}));
                        }
                    }
                }
            } else if (expr_returns_optional_sema(mem.object) &&
                       mem.field != "has_value" && mem.field != "value" && mem.field != "value_or") {
                return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot access '.{}' on optional value without unwrapping; use 'or' to provide a fallback", mem.field)));
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_index_expr(const IndexExpr& idx, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(idx.object); !r) return r;
        if (auto r = check_expr(idx.index); !r) return r;

        bool is_map_obj = false;
        const IdentExpr* obj_id = idx.object ? std::get_if<IdentExpr>(&idx.object->node) : nullptr;
        if (obj_id) {
            if (optional_vars_.contains(obj_id->name)) {
                return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot index into optional value '{}' without unwrapping; use 'or' to provide a fallback", obj_id->name)));
            }
            if (fallible_vars_.contains(obj_id->name) && !reported_unhandled_vars_.contains(obj_id->name)) {
                return emit_error(diag_at(line, col, "cfire/UNHANDLED-RESULT", std::format("cannot index into fallible result '{}' without unwrapping with 'try' or 'or'", obj_id->name)));
            }
            if (scalar_primitive_vars_.contains(obj_id->name)) {
                std::string tname = lookup_or(var_types_, obj_id->name, "primitive");
                return emit_error(diag_at(line, col, "cfire/INVALID-INDEX", std::format("cannot index into non-indexable primitive type '{}'", tname)));
            }
            if (map_vars_.contains(obj_id->name)) {
                is_map_obj = true;
            }
        } else if (idx.object && expr_returns_optional_sema(idx.object)) {
            return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot index into optional value without unwrapping; use 'or' to provide a fallback")));
        }
        if (!is_map_obj && idx.index) {
            if (const auto* un_idx = std::get_if<UnaryExpr>(&idx.index->node)) {
                if (un_idx->op == "-" && un_idx->operand) {
                    if (const auto* neg_lit = std::get_if<LiteralExpr>(&un_idx->operand->node);
                        neg_lit && neg_lit->kind == TokenKind::Int && neg_lit->value != "0") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("negative index -{} is out of bounds", neg_lit->value))); !r) return r;
                    }
                }
            } else if (const auto* idx_lit = std::get_if<LiteralExpr>(&idx.index->node)) {
                if (idx_lit->kind == TokenKind::Int) {
                    if (idx_lit->value.starts_with('-') && idx_lit->value != "-0") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("negative index {} is out of bounds", idx_lit->value))); !r) return r;
                    } else if (auto k = cfire::frontend::parse_int_literal<std::size_t>(idx_lit->value)) {
                        if (obj_id) {
                            if (auto fit = fixed_array_lens_.find(obj_id->name); fit != fixed_array_lens_.end()) {
                                if (*k >= fit->second) {
                                    if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("index {} is out of bounds for array '{}' of length {}", *k, obj_id->name, fit->second))); !r) return r;
                                }
                            }
                        } else if (idx.object) {
                            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&idx.object->node)) {
                                if (*k >= arr_lit->elements.size()) {
                                    if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("index {} is out of bounds for array literal of length {}", *k, arr_lit->elements.size()))); !r) return r;
                                }
                            }
                        }
                    }
                }
            }
        }
        return {};
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::check_or_expr(const OrExpr& or_ex, int line, int col, const ExprPtr& self_expr) -> std::expected<void, std::string> {
        std::size_t errors_before = errors_.size();
        if (self_expr && unwrapped_exprs_.contains(self_expr)) {
            if (or_ex.inner) unwrapped_exprs_.insert(or_ex.inner);
            if (or_ex.fallback_val) unwrapped_exprs_.insert(or_ex.fallback_val);
        }
        if (auto r = check_expr(or_ex.inner); !r) return r;
        bool is_opt = is_expr_optional_for_unwrap_sema(or_ex.inner);
        bool is_fall = is_expr_fallible_for_unwrap_sema(or_ex.inner);
        if (!or_ex.err_var.empty() && is_opt && !is_fall) {
            if (auto r = emit_error(diag_at(line, col, "cfire/OPTIONAL-OR-ERR", std::format("cannot bind error variable '{}' on optional 'T?' in 'or' block; use 'or {{ ... }}' or 'or fallback' instead", or_ex.err_var))); !r) return r;
        } else if (errors_.size() == errors_before && !is_fall && !is_opt && !is_expr_type_unresolved_for_redundancy_sema(or_ex.inner)) {
            if (auto r = emit_error(diag_at(line, col, "cfire/REDUNDANT-OR", std::format("'or' used on non-fallible, non-optional expression; remove 'or' fallback"))); !r) return r;
        }
        if (or_ex.fallback_val) {
            if (auto r = check_expr(or_ex.fallback_val); !r) return r;
        }
        if (!or_ex.handler_body.empty()) {
            if (in_invalid_unwrap_context_ > 0) {
                if (auto r = emit_error(diag_at(line, col, "cfire/INVALID-OR-BLOCK-CONTEXT", std::format("cannot use block 'or {{ ... }}' inside short-circuit operand or 'for' loop condition; hoist into a statement or use 'or fallback'"))); !r) return r;
            }
            if ((!self_expr || !discarded_or_exprs_.contains(self_expr)) && !block_diverges(or_ex.handler_body)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-DIVERGING-OR-BLOCK", std::format("block 'or {} {{ ... }}' must definitely diverge with return or panic, otherwise unwrapping empty value causes undefined behavior", or_ex.err_var.empty() ? "err" : or_ex.err_var))); !r) return r;
            }
            ScopeGuard sg(*this);
            if (!or_ex.err_var.empty()) {
                bind_scoped_var(or_ex.err_var, false);
                var_types_[or_ex.err_var] = "Error";
            }
            if (auto r = check_block_stmts(or_ex.handler_body); !r) return r;
        }
        return {};
    }


    // parameter types for untyped closure arguments, taken from the callee's `fn(A, B) -> R` parameter
    // (`router.get("/x", |req| ...)` binds `req: http.Request`).
    CFIRE_SEMA_INLINE auto SemaFnChecker::split_fn_type_params(std::string_view fn_type) -> std::vector<std::string> {
        std::vector<std::string> out;
        auto open = fn_type.find('(');
        if (!fn_type.starts_with("fn") || open == std::string_view::npos) return out;
        int depth = 0;
        std::string cur;
        for (std::size_t i = open + 1; i < fn_type.size(); ++i) {
            char c = fn_type[i];
            if ((c == '(' || c == '[') ) ++depth;
            if ((c == ')' || c == ']') && depth-- == 0) { if (!cur.empty()) out.push_back(cur); break; }
            if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); continue; }
            if (!(cur.empty() && c == ' ')) cur.push_back(c);
        }
        for (auto& t : out) {
            while (!t.empty() && t.back() == ' ') t.pop_back();
            for (std::string_view pre : {"mut ", "move "}) if (t.starts_with(pre)) t = t.substr(pre.size());
        }
        return out;
    }


    CFIRE_SEMA_INLINE void SemaFnChecker::record_closure_param_hints(const CallExpr& call) {
        std::vector<std::string> param_types;  // declared type text of each callee parameter
        std::vector<std::string> param_names;
        std::string qualifier;
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node); mem && mem->object) {
            const auto& idx = StdlibCatalogIndex::instance();
            const StdFnSigInfo* sig = nullptr;
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node); obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                std::string resolved_ns = resolve_stdlib_ns_sema(obj_id->name);
                if (!resolved_ns.empty() && idx.has_namespace(resolved_ns)) {
                    sig = idx.find_ns_fn(resolved_ns, mem->field);
                    qualifier = obj_id->name;
                }
            }
            if (!sig && mem->object) {
                std::string recv = type_name_of(mem->object);
                std::string base = recv.substr(0, recv.find('['));
                std::string unqual = base.substr(base.rfind('.') == std::string::npos ? 0 : base.rfind('.') + 1);
                for (const auto& key : {recv + "." + mem->field, base + "." + mem->field, unqual + "." + mem->field}) {
                    if (auto it = idx.receiver_methods.find(key); it != idx.receiver_methods.end()) { sig = &it->second; break; }
                }
                if (sig) qualifier = local_qualifier_for_module_sema(sig->module_import, sig->ns_alias);
            }
            if (sig) {
                for (const auto& p : sig->params) {
                    param_types.push_back(p.type_str);
                    param_names.push_back(p.name);
                }
            }
            if (const FnDecl* user = (sig ? nullptr : resolve_member_call_sig(*mem))) {
                for (const auto& p : user->params) {
                    param_types.push_back(format_type_ref_sema(p.type));
                    param_names.push_back(p.name);
                }
            }
        } else if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
            if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                for (const auto& p : it->second.params) {
                    param_types.push_back(format_type_ref_sema(p.type));
                    param_names.push_back(p.name);
                }
            }
        }
        auto qualify = [&](std::string t) {
            // `app.bind[PresetConfig](...) |p|`: the handler's generic parameter (`fn(T)`, `fn(Args)`) is the call's
            // type argument, so builtin methods on `p.name` resolve.
            if (!call.type_args.empty() && !t.empty() && std::isupper(static_cast<unsigned char>(t[0])) &&
                t.find_first_of(".[") == std::string::npos && (t.size() <= 4 || t == "Args") && !cf_.struct_tags.contains(t)) {
                return format_type_ref_sema(call.type_args[0]);
            }
            if (qualifier.empty() || t.empty() || t.find('.') != std::string::npos) return t;
            static const std::unordered_set<std::string> builtin = {"str", "bool", "i8", "i16", "i32", "i64", "u8", "u16",
                "u32", "u64", "usize", "isize", "f32", "f64", "byte", "void"};
            if (builtin.contains(t) || t.starts_with("[") || t.starts_with("map[")) return t;
            return qualifier + "." + t;
        };
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const auto* cl = call.args[i].value ? std::get_if<ClosureExpr>(&call.args[i].value->node) : nullptr;
            if (!cl) continue;
            std::size_t slot = i;
            if (!call.args[i].label.empty()) {
                for (std::size_t k = 0; k < param_names.size(); ++k) {
                    if (param_names[k] == call.args[i].label) { slot = k; break; }
                }
            } else if (slot < param_types.size() && split_fn_type_params(param_types[slot]).empty()) {
                for (std::size_t k = slot + 1; k < param_types.size(); ++k) {
                    if (!split_fn_type_params(param_types[k]).empty()) { slot = k; break; }
                }
            }
            if (slot >= param_types.size()) continue;
            auto hinted = split_fn_type_params(param_types[slot]);
            for (auto& h : hinted) h = qualify(h);
            if (!hinted.empty()) closure_param_hints_[cl] = std::move(hinted);
        }
        if (call.block_arg && !param_types.empty()) {
            std::string ptype = param_types.back();
            if (split_fn_type_params(ptype).empty()) {
                for (std::size_t k = 0; k < param_types.size(); ++k) {
                    if (!split_fn_type_params(param_types[k]).empty()) ptype = param_types[k];
                }
            }
            auto hinted = split_fn_type_params(ptype);
            for (auto& h : hinted) h = qualify(h);
            if (!hinted.empty()) closure_param_hints_[&*call.block_arg] = std::move(hinted);
        }
    }


    // the declared constructor `fn (T) make(...)` of a local type, lowered as `cfire_ctor_T`.
    CFIRE_SEMA_INLINE auto SemaFnChecker::find_user_ctor(const TypeRef& t) const -> const FnDecl* {
        std::string name = t.name;
        for (auto p = name.find("::"); p != std::string::npos; p = name.find("::")) name.replace(p, 2, ".");
        std::string ns;
        if (auto dot = name.rfind('.'); dot != std::string::npos) {
            ns = name.substr(0, dot);
            name = name.substr(dot + 1);
        }
        if (auto br = name.find('['); br != std::string::npos) name = name.substr(0, br);
        const std::string fn_name = "cfire_ctor_" + name;
        if (!ns.empty()) {
            if (auto it = cf_.fn_sigs.find(ns + "." + fn_name); it != cf_.fn_sigs.end()) return &it->second;
        }
        if (!pkg_name_.empty()) {
            if (auto it = cf_.fn_sigs.find(pkg_name_ + "." + fn_name); it != cf_.fn_sigs.end()) return &it->second;
        }
        if (auto it = cf_.fn_sigs.find(fn_name); it != cf_.fn_sigs.end()) return &it->second;
        return nullptr;
    }


    CFIRE_SEMA_INLINE auto SemaFnChecker::find_stdlib_ctor(std::string_view spelled_in) const -> const StdlibCtor* {
        std::string spelled_s(spelled_in);
        for (auto p = spelled_s.find("::"); p != std::string::npos; p = spelled_s.find("::")) spelled_s.replace(p, 2, ".");
        std::string_view spelled = spelled_s;
        auto dot = spelled.find('.');
        if (dot == std::string_view::npos) return nullptr;
        std::string ns(spelled.substr(0, dot));
        if (std::string canon = resolve_stdlib_ns_sema(ns); !canon.empty()) ns = canon;
        return find_stdlib_ctor_for(ns, spelled.substr(dot + 1));
    }
