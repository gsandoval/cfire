// src/codegen/declarations.hpp — Out-of-line Emitter declaration & type lowering shard
namespace cfire::frontend {


    CFIRE_CG_INLINE auto Emitter::emit_cpp() -> std::string {
        emit_file();
        std::string out = buf_.str();
        if ((used_core_.contains("str") || out.contains("cfire::str::")) && !out.contains("#include \"core/str.hpp\"")) {
            std::string needle = "#include \"cfire_prelude.hpp\"\n";
            if (auto pos = out.find(needle); pos != std::string::npos) {
                out.insert(pos + needle.size(), "#include \"core/str.hpp\"\n");
            }
        }
        if (out.contains("cfire::bm::") && !out.contains("#include \"core/builtin_methods.hpp\"")) {
            std::string needle = "#include \"cfire_prelude.hpp\"\n";
            if (auto pos = out.find(needle); pos != std::string::npos) {
                out.insert(pos + needle.size(), "#include \"core/builtin_methods.hpp\"\n");
            }
        }
        return out;
    }


    CFIRE_CG_INLINE void Emitter::init_imports() {
        std::unordered_map<std::string, std::size_t> fn_counts;
        for (const auto& fn : cf_.ast.functions) {
            if (!fn.receiver) fn_counts[fn.name]++;
        }
        for (const auto& [name, count] : fn_counts) {
            if (count > 1) colliding_fn_names_.insert(name);
        }
        std::unordered_map<std::string, std::size_t> gv_counts;
        for (const auto& gv : cf_.ast.global_vars) {
            gv_counts[gv.name]++;
        }
        for (const auto& [name, count] : gv_counts) {
            if (count > 1) colliding_gv_names_.insert(name);
        }
        for (const auto& st : cf_.ast.structs) {
            for (const auto& fld : st.fields) {
                if (fld.type.is_ptr || fld.type.is_mut_ptr) {
                    ptr_field_names_.insert(fld.name);
                }
                if (!fld.type.smart_wrap.empty()) {
                    smart_ptr_field_names_.insert(fld.name);
                }
            }
        }
        for (const auto& imp : cf_.ast.imports) {
            if (imp.is_cpp) continue;
            auto resolved = cfire::modules::resolve_import(imp.path);
            for (const auto& ck : resolved.core_keys) {
                used_core_.insert(ck);
            }
            if (resolved.is_core && !imp.alias.empty()) {
                auto colon = imp.path.rfind(':');
                auto slash = imp.path.rfind('/');
                std::size_t sep = std::string::npos;
                if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                else if (colon != std::string::npos) sep = colon;
                else sep = slash;
                std::string tail = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                core_aliases_[imp.alias] = tail;
            }
            if (resolved.is_vendor) {
                std::string alias = imp.alias.empty() ? resolved.vendor_key : imp.alias;
                used_vendor_[resolved.vendor_key] = alias;
                for (const auto& iv : resolved.implied_vendors) {
                    if (!used_vendor_.contains(iv)) {
                        used_vendor_[iv] = iv;
                    }
                }
            }
        }
    }


    [[nodiscard]] inline auto sanitize_pkg_unit_id(std::string_view pkg) -> std::string {
        if (pkg.empty()) return "main";
        std::string out;
        out.reserve(pkg.size());
        for (unsigned char c : pkg) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out.push_back(static_cast<char>(c));
            } else {
                out += std::format("_{:02x}", static_cast<unsigned int>(c));
            }
        }
        return out;
    }

    struct CppHeaderMeta {
        std::string path;
    };

    struct CollectedImportMeta {
        std::vector<std::string> link_tags;
        std::vector<std::string> defines;
        std::vector<CppHeaderMeta> cpp_headers;
        bool has_cpp_impl_define = false;
    };

    inline auto collect_import_meta(Emitter& em) -> CollectedImportMeta {
        CollectedImportMeta meta;
        auto add_unique = [](std::vector<std::string>& vec, std::string val) {
            if (std::find(vec.begin(), vec.end(), val) == vec.end()) {
                vec.push_back(std::move(val));
            }
        };

        for (const auto& imp : em.cf_.ast.imports) {
            if (imp.is_cpp) {
                CppHeaderMeta header;
                header.path = imp.path;
                meta.cpp_headers.push_back(std::move(header));
                continue;
            }
            auto resolved = cfire::modules::resolve_import(imp.path);
            for (const auto& ck : resolved.core_keys) {
                em.used_core_.insert(ck);
            }
            for (const auto& def : resolved.defines) {
                if (def.contains("IMPLEMENTATION")) {
                    meta.has_cpp_impl_define = true;
                }
                add_unique(meta.defines, def);
            }
            for (const auto& lt : resolved.link_tags) {
                add_unique(meta.link_tags, lt);
            }
            if (resolved.is_core && !imp.alias.empty()) {
                auto colon = imp.path.rfind(':');
                auto slash = imp.path.rfind('/');
                std::size_t sep = std::string::npos;
                if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                else if (colon != std::string::npos) sep = colon;
                else sep = slash;
                std::string tail = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                em.core_aliases_[imp.alias] = tail;
            }
            if (resolved.is_vendor) {
                std::string alias = imp.alias.empty() ? resolved.vendor_key : imp.alias;
                em.used_vendor_[resolved.vendor_key] = alias;
                for (const auto& iv : resolved.implied_vendors) {
                    if (!em.used_vendor_.contains(iv)) {
                        em.used_vendor_[iv] = iv;
                    }
                }
            }
        }

        bool any_json_tag = false;
        for (const auto& [sname, tags] : em.cf_.struct_tags) {
            for (const auto& t : tags) {
                if (!t.json_key.empty() || t.custom_tags.contains("desc") || t.custom_tags.contains("validate")) { any_json_tag = true; break; }
            }
            if (any_json_tag) break;
        }
        if (any_json_tag && !em.used_core_.contains("json")) {
            em.used_core_.insert("json");
        }
        if (em.used_core_.contains("json")) {
            if (std::find(meta.defines.begin(), meta.defines.end(), "#define CFIRE_USE_SIMDJSON 1") == meta.defines.end()) {
                meta.defines.push_back("#define CFIRE_USE_SIMDJSON 1");
            }
            if (std::find(meta.link_tags.begin(), meta.link_tags.end(), "-lsimdjson") == meta.link_tags.end()) {
                meta.link_tags.push_back("-lsimdjson");
            }
        }
        return meta;
    }

    inline void emit_decls_types_and_globals(Emitter& em) {
        const auto& ast = em.cf_.ast;

        // 1. Emit Interfaces as C++20 `concept`s
        for (const auto& iface : ast.interfaces) {
            em.emit_interface(iface);
            em.writeln();
        }

        // Forward declare all structs & enums so recursive shared_ptr/pointers work
        for (const auto& en : ast.enums) {
            if (!en.type_params.empty()) {
                em.writeln(std::format("{} struct {};", em.format_template_clause(en.type_params), en.name));
            } else {
                em.writeln(std::format("struct {};", en.name));
            }
        }
        for (const auto& st : ast.structs) {
            if (!st.type_params.empty()) {
                em.writeln(std::format("{} struct {};", em.format_template_clause(st.type_params), st.name));
            } else {
                em.writeln(std::format("struct {};", st.name));
            }
        }
        if (!ast.structs.empty() || !ast.enums.empty()) em.writeln();

        // Emit Structs & Enums in dependency-resolved topological order (preserving source order when independent)
        struct TypeItem {
            int line;
            bool is_enum;
            std::size_t idx;
            std::string name;
        };
        std::vector<TypeItem> items;
        for (std::size_t i = 0; i < ast.enums.size(); ++i) {
            items.push_back(TypeItem{ast.enums[i].line, true, i, ast.enums[i].name});
        }
        for (std::size_t i = 0; i < ast.structs.size(); ++i) {
            items.push_back(TypeItem{ast.structs[i].line, false, i, ast.structs[i].name});
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            return a.line < b.line;
        });

        std::unordered_map<std::string, std::size_t> name_to_item;
        for (std::size_t i = 0; i < items.size(); ++i) {
            name_to_item[items[i].name] = i;
        }
        auto collect_type_deps = [&](const auto& self, const TypeRef& tr, std::vector<std::size_t>& deps) -> void {
            if (tr.is_ptr || tr.is_mut_ptr || !tr.smart_wrap.empty()) {
                return;
            }
            std::string clean = tr.name;
            if (auto dot_pos = clean.rfind('.'); dot_pos != std::string::npos) clean = clean.substr(dot_pos + 1);
            if (auto col_pos = clean.rfind("::"); col_pos != std::string::npos) clean = clean.substr(col_pos + 2);
            if (auto it = name_to_item.find(clean); it != name_to_item.end()) {
                deps.push_back(it->second);
            }
            if (tr.elem_type) self(self, *tr.elem_type, deps);
            if (tr.key_type) self(self, *tr.key_type, deps);
            for (const auto& ta : tr.type_args) self(self, ta, deps);
            for (const auto& tt : tr.tuple_types) self(self, tt, deps);
        };

        std::vector<int> visit_state(items.size(), 0); // 0 = unvisited, 1 = visiting, 2 = emitted
        auto emit_item_dfs = [&](const auto& self, std::size_t k) -> void {
            if (visit_state[k] != 0) return;
            visit_state[k] = 1;
            std::vector<std::size_t> deps;
            if (items[k].is_enum) {
                for (const auto& v : ast.enums[items[k].idx].variants) {
                    for (const auto& pt : v.payload_types) collect_type_deps(collect_type_deps, pt, deps);
                }
            } else {
                for (const auto& fld : ast.structs[items[k].idx].fields) {
                    collect_type_deps(collect_type_deps, fld.type, deps);
                }
            }
            for (std::size_t d : deps) {
                if (d != k && visit_state[d] == 0) {
                    self(self, d);
                }
            }
            visit_state[k] = 2;
            if (items[k].is_enum) {
                em.emit_enum(ast.enums[items[k].idx]);
            } else {
                em.emit_struct(ast.structs[items[k].idx]);
            }
            em.writeln();
        };

        for (std::size_t k = 0; k < items.size(); ++k) {
            emit_item_dfs(emit_item_dfs, k);
        }

        // 4. Forward Declarations for Functions & Receiver Methods (before global vars so globals can reference functions)
        std::set<std::string> emitted_method_fallbacks;
        for (const auto& fn : ast.functions) {
            if (fn.receiver && !emitted_method_fallbacks.contains(fn.name)) {
                emitted_method_fallbacks.insert(fn.name);
                em.writeln(std::format(
                    "template <typename Self, typename... Args> requires requires(Self&& s, Args&&... a) {{ std::forward<Self>(s).{0}(std::forward<Args>(a)...); }} inline auto cfire_method_{0}(Self&& s, Args&&... a) -> decltype(auto) {{ return std::forward<Self>(s).{0}(std::forward<Args>(a)...); }}",
                    fn.name));
                if (fn.name == "push") {
                    em.writeln("template <typename T, typename Arg> inline void cfire_method_push(std::vector<T>& s, Arg&& a) { cfire::vec_push(s, std::forward<Arg>(a)); }");
                } else if (fn.name == "pop") {
                    em.writeln("template <typename T> inline void cfire_method_pop(std::vector<T>& s) { s.pop_back(); }");
                }
            }
            em.emit_function_forward_decl(fn);
        }
        for (const auto& mname : emitted_method_fallbacks) {
            em.writeln(std::format(
                "template <typename T, typename... Args> inline auto cfire_method_{0}(const std::shared_ptr<T>& s, Args&&... a) -> decltype(auto) {{ return cfire_method_{0}(*s, std::forward<Args>(a)...); }}",
                mname));
            em.writeln(std::format(
                "template <typename T, typename... Args> inline auto cfire_method_{0}(const std::unique_ptr<T>& s, Args&&... a) -> decltype(auto) {{ return cfire_method_{0}(*s, std::forward<Args>(a)...); }}",
                mname));
        }
        if (!ast.functions.empty()) em.writeln();

        // 5a. Emit Global / Package-level Variables
        for (const auto& gv : ast.global_vars) {
            em.set_source_path(gv.source_path);
            if (gv.line > 0) em.emit_line_directive(gv.line);
            em.cur_pkg_ = gv.package_name;
            std::string type_str = gv.type ? em.lower_owning_type(*gv.type) : "";
            if (type_str.empty() && (!gv.value || !std::holds_alternative<ClosureExpr>(gv.value->node))) {
                if (auto lowered = em.lower_type_id(TypeId{gv.resolved_type_id}, Emitter::LowerContext::Value)) {
                    type_str = std::move(*lowered);
                }
            }
            if (type_str.empty() && gv.value) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&gv.value->node); arr_lit && !arr_lit->elements.empty()) {
                    std::string inferred = em.infer_array_lit_cpp_type(*arr_lit);
                    if (!inferred.empty()) type_str = std::move(inferred);
                }
            }
            if (type_str.empty()) type_str = "auto";
            std::string mut_str = gv.is_mut ? "" : "const ";
            std::string gname = (!gv.package_name.empty() && em.colliding_gv_names_.contains(gv.name))
                ? std::format("cfire_pkg_{}__{}", gv.package_name, em.sanitize_cpp_ident(gv.name))
                : em.sanitize_cpp_ident(gv.name);
            if (gv.value) {
                em.writeln(std::format("inline {}{} {} = {};", mut_str, type_str, gname, em.emit_expr(gv.value)));
            } else {
                em.writeln(std::format("inline {}{} {}{{}};", mut_str, type_str, gname));
            }
            em.cur_pkg_.clear();
        }
        if (!ast.global_vars.empty()) em.writeln();
    }

    template <typename EmitFn>
    inline void emit_guarded_define_with(std::string_view def, EmitFn&& emit_line) {
        std::string_view rest = def;
        if (rest.starts_with("#define")) {
            rest.remove_prefix(7);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
                rest.remove_prefix(1);
            }
            std::size_t end = 0;
            while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t' && rest[end] != '(') {
                ++end;
            }
            std::string_view macro = rest.substr(0, end);
            if (!macro.empty()) {
                emit_line(std::format("#ifndef {}", macro));
                emit_line(def);
                emit_line("#endif");
                return;
            }
        }
        emit_line(def);
    }

    inline void emit_pkg_decls_types_and_globals(Emitter& em, const std::string& target_pkg, const std::string& default_pkg) {
        const auto& ast = em.cf_.ast;
        auto item_pkg = [&](const std::string& p) -> std::string {
            return p.empty() ? default_pkg : p;
        };

        // 1. Interfaces as C++20 `concept`s
        for (const auto& iface : ast.interfaces) {
            if (item_pkg(iface.package_name) != target_pkg) continue;
            em.emit_interface(iface);
            em.writeln();
        }

        // 2. Forward declare structs & enums in this package
        bool any_fwd = false;
        for (const auto& en : ast.enums) {
            if (item_pkg(en.package_name) != target_pkg) continue;
            any_fwd = true;
            if (!en.type_params.empty()) {
                em.writeln(std::format("{} struct {};", em.format_template_clause(en.type_params), en.name));
            } else {
                em.writeln(std::format("struct {};", en.name));
            }
        }
        for (const auto& st : ast.structs) {
            if (item_pkg(st.package_name) != target_pkg) continue;
            any_fwd = true;
            if (!st.type_params.empty()) {
                em.writeln(std::format("{} struct {};", em.format_template_clause(st.type_params), st.name));
            } else {
                em.writeln(std::format("struct {};", st.name));
            }
        }
        if (any_fwd) em.writeln();

        // 3. Emit structs & enums in dependency-resolved topological order within this package
        struct TypeItem {
            int line;
            bool is_enum;
            std::size_t idx;
            std::string name;
        };
        std::vector<TypeItem> items;
        for (std::size_t i = 0; i < ast.enums.size(); ++i) {
            if (item_pkg(ast.enums[i].package_name) != target_pkg) continue;
            items.push_back(TypeItem{ast.enums[i].line, true, i, ast.enums[i].name});
        }
        for (std::size_t i = 0; i < ast.structs.size(); ++i) {
            if (item_pkg(ast.structs[i].package_name) != target_pkg) continue;
            items.push_back(TypeItem{ast.structs[i].line, false, i, ast.structs[i].name});
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            return a.line < b.line;
        });

        std::unordered_map<std::string, std::size_t> name_to_item;
        for (std::size_t i = 0; i < items.size(); ++i) {
            name_to_item[items[i].name] = i;
        }
        auto collect_type_deps = [&](const auto& self, const TypeRef& tr, std::vector<std::size_t>& deps) -> void {
            if (tr.is_ptr || tr.is_mut_ptr || !tr.smart_wrap.empty()) {
                return;
            }
            std::string clean = tr.name;
            if (auto dot_pos = clean.rfind('.'); dot_pos != std::string::npos) clean = clean.substr(dot_pos + 1);
            if (auto col_pos = clean.rfind("::"); col_pos != std::string::npos) clean = clean.substr(col_pos + 2);
            if (auto it = name_to_item.find(clean); it != name_to_item.end()) {
                deps.push_back(it->second);
            }
            if (tr.elem_type) self(self, *tr.elem_type, deps);
            if (tr.key_type) self(self, *tr.key_type, deps);
            for (const auto& ta : tr.type_args) self(self, ta, deps);
            for (const auto& tt : tr.tuple_types) self(self, tt, deps);
        };

        std::vector<int> visit_state(items.size(), 0);
        auto emit_item_dfs = [&](const auto& self, std::size_t k) -> void {
            if (visit_state[k] != 0) return;
            visit_state[k] = 1;
            std::vector<std::size_t> deps;
            if (items[k].is_enum) {
                for (const auto& v : ast.enums[items[k].idx].variants) {
                    for (const auto& pt : v.payload_types) collect_type_deps(collect_type_deps, pt, deps);
                }
            } else {
                for (const auto& fld : ast.structs[items[k].idx].fields) {
                    collect_type_deps(collect_type_deps, fld.type, deps);
                }
            }
            for (std::size_t d : deps) {
                if (d != k && visit_state[d] == 0) {
                    self(self, d);
                }
            }
            visit_state[k] = 2;
            if (items[k].is_enum) {
                em.emit_enum(ast.enums[items[k].idx]);
            } else {
                em.emit_struct(ast.structs[items[k].idx]);
            }
            em.writeln();
        };

        for (std::size_t k = 0; k < items.size(); ++k) {
            emit_item_dfs(emit_item_dfs, k);
        }

        // 4. Forward declarations for functions & receiver methods in this package (ODR-guarded fallbacks)
        std::set<std::string> emitted_method_fallbacks;
        bool any_fn = false;
        for (const auto& fn : ast.functions) {
            if (item_pkg(fn.package_name) != target_pkg) continue;
            any_fn = true;
            if (fn.receiver && !emitted_method_fallbacks.contains(fn.name)) {
                emitted_method_fallbacks.insert(fn.name);
                const std::string fb_guard = "CFIRE_METHOD_FALLBACK_" + sanitize_pkg_unit_id(fn.name);
                em.writeln(std::format("#ifndef {}", fb_guard));
                em.writeln(std::format("#define {} 1", fb_guard));
                em.writeln(std::format(
                    "template <typename Self, typename... Args> requires requires(Self&& s, Args&&... a) {{ std::forward<Self>(s).{0}(std::forward<Args>(a)...); }} inline auto cfire_method_{0}(Self&& s, Args&&... a) -> decltype(auto) {{ return std::forward<Self>(s).{0}(std::forward<Args>(a)...); }}",
                    fn.name));
                if (fn.name == "push") {
                    em.writeln("template <typename T, typename Arg> inline void cfire_method_push(std::vector<T>& s, Arg&& a) { cfire::vec_push(s, std::forward<Arg>(a)); }");
                } else if (fn.name == "pop") {
                    em.writeln("template <typename T> inline void cfire_method_pop(std::vector<T>& s) { s.pop_back(); }");
                }
                em.writeln("#endif");
            }
            em.emit_function_forward_decl(fn);
        }
        for (const auto& mname : emitted_method_fallbacks) {
            const std::string ptr_guard = "CFIRE_METHOD_PTR_FALLBACK_" + sanitize_pkg_unit_id(mname);
            em.writeln(std::format("#ifndef {}", ptr_guard));
            em.writeln(std::format("#define {} 1", ptr_guard));
            em.writeln(std::format(
                "template <typename T, typename... Args> inline auto cfire_method_{0}(const std::shared_ptr<T>& s, Args&&... a) -> decltype(auto) {{ return cfire_method_{0}(*s, std::forward<Args>(a)...); }}",
                mname));
            em.writeln(std::format(
                "template <typename T, typename... Args> inline auto cfire_method_{0}(const std::unique_ptr<T>& s, Args&&... a) -> decltype(auto) {{ return cfire_method_{0}(*s, std::forward<Args>(a)...); }}",
                mname));
            em.writeln("#endif");
        }
        if (any_fn) em.writeln();

        // 5. Global / Package-level variables in this package
        bool any_gv = false;
        for (const auto& gv : ast.global_vars) {
            if (item_pkg(gv.package_name) != target_pkg) continue;
            any_gv = true;
            em.set_source_path(gv.source_path);
            if (gv.line > 0) em.emit_line_directive(gv.line);
            em.cur_pkg_ = gv.package_name;
            std::string type_str = gv.type ? em.lower_owning_type(*gv.type) : "";
            if (type_str.empty() && (!gv.value || !std::holds_alternative<ClosureExpr>(gv.value->node))) {
                if (auto lowered = em.lower_type_id(TypeId{gv.resolved_type_id}, Emitter::LowerContext::Value)) {
                    type_str = std::move(*lowered);
                }
            }
            if (type_str.empty() && gv.value) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&gv.value->node); arr_lit && !arr_lit->elements.empty()) {
                    std::string inferred = em.infer_array_lit_cpp_type(*arr_lit);
                    if (!inferred.empty()) type_str = std::move(inferred);
                }
            }
            if (type_str.empty()) type_str = "auto";
            std::string mut_str = gv.is_mut ? "" : "const ";
            std::string gname = (!gv.package_name.empty() && em.colliding_gv_names_.contains(gv.name))
                ? std::format("cfire_pkg_{}__{}", gv.package_name, em.sanitize_cpp_ident(gv.name))
                : em.sanitize_cpp_ident(gv.name);
            if (gv.value) {
                em.writeln(std::format("inline {}{} {} = {};", mut_str, type_str, gname, em.emit_expr(gv.value)));
            } else {
                em.writeln(std::format("inline {}{} {}{{}};", mut_str, type_str, gname));
            }
            em.cur_pkg_.clear();
        }
        if (any_gv) em.writeln();

        // 6. Generic function & generic method bodies in this package
        for (const auto& fn : ast.functions) {
            if (item_pkg(fn.package_name) != target_pkg) continue;
            if (!em.effective_type_params(fn).empty()) {
                em.emit_function(fn);
                em.writeln();
            }
        }
    }

    CFIRE_CG_INLINE auto Emitter::emit_sharded() -> ShardedEmission {
        const auto& ast = cf_.ast;
        ShardedEmission sharded;
        CollectedImportMeta meta = collect_import_meta(*this);
        sharded.has_cpp_impl_define = meta.has_cpp_impl_define;

        const std::string default_pkg = ast.package_name.empty() ? std::string("main") : ast.package_name;
        auto item_pkg = [&](const std::string& p) -> std::string {
            return p.empty() ? default_pkg : p;
        };
        auto item_src = [&](const std::string& s) -> std::string {
            return s.empty() ? ast.path : s;
        };
        auto norm_src = [&](const std::string& s) -> std::string {
            std::string raw = item_src(s);
            if (raw.empty()) return {};
            return std::filesystem::path(raw).lexically_normal().generic_string();
        };

        bool has_user_main = false;
        std::string entry_pkg = default_pkg;
        std::string entry_src = norm_src(ast.path);
        for (const auto& fn : ast.functions) {
            if (fn.name == "main" && !fn.receiver) {
                has_user_main = true;
                entry_pkg = item_pkg(fn.package_name);
                entry_src = norm_src(fn.source_path);
                break;
            }
        }

        // Discover all packages in topological import order (dependencies first)
        std::vector<std::string> pkg_header_order;
        std::unordered_set<std::string> seen_pkgs;
        auto add_pkg_dfs = [&](const auto& self, const std::string& p) -> void {
            if (p.empty() || seen_pkgs.contains(p)) return;
            seen_pkgs.insert(p);
            if (auto it = ast.pkg_imports.find(p); it != ast.pkg_imports.end()) {
                for (const auto& dep : it->second) {
                    self(self, dep);
                }
            }
            pkg_header_order.push_back(p);
        };
        for (const auto& [p, _] : ast.pkg_imports) add_pkg_dfs(add_pkg_dfs, p);
        for (const auto& st : ast.structs) add_pkg_dfs(add_pkg_dfs, item_pkg(st.package_name));
        for (const auto& en : ast.enums) add_pkg_dfs(add_pkg_dfs, item_pkg(en.package_name));
        for (const auto& ifc : ast.interfaces) add_pkg_dfs(add_pkg_dfs, item_pkg(ifc.package_name));
        for (const auto& gv : ast.global_vars) add_pkg_dfs(add_pkg_dfs, item_pkg(gv.package_name));
        for (const auto& fn : ast.functions) add_pkg_dfs(add_pkg_dfs, item_pkg(fn.package_name));
        for (const auto& td : ast.tests) add_pkg_dfs(add_pkg_dfs, item_pkg(td.package_name));
        if (test_mode_ || !ast.tests.empty() || has_user_main) add_pkg_dfs(add_pkg_dfs, entry_pkg);
        if (pkg_header_order.empty()) add_pkg_dfs(add_pkg_dfs, default_pkg);

        // Group concrete functions 1:1 by (package, normalized source file path)
        struct UnitGroup {
            std::string pkg;
            std::string norm_source;
            std::string raw_source;
            std::vector<std::size_t> fn_indices;
        };
        std::vector<UnitGroup> unit_groups;
        std::unordered_map<std::string, std::size_t> unit_group_map;
        auto ensure_unit_group = [&](const std::string& p, const std::string& src_raw) -> std::size_t {
            std::string src_n = norm_src(src_raw);
            std::string key = p + "\n" + src_n;
            if (auto it = unit_group_map.find(key); it != unit_group_map.end()) {
                return it->second;
            }
            std::size_t idx = unit_groups.size();
            unit_group_map[key] = idx;
            unit_groups.push_back(UnitGroup{p, std::move(src_n), item_src(src_raw), {}});
            return idx;
        };

        for (std::size_t i = 0; i < ast.functions.size(); ++i) {
            const auto& fn = ast.functions[i];
            if (fn.name == "main" && !fn.receiver && test_mode_) {
                continue;
            }
            if (!effective_type_params(fn).empty()) {
                continue; // Generic functions & methods live inline in pkg_<id>.hpp
            }
            std::size_t gidx = ensure_unit_group(item_pkg(fn.package_name), fn.source_path);
            unit_groups[gidx].fn_indices.push_back(i);
        }
        std::size_t entry_group_idx = std::string::npos;
        if (test_mode_ || !ast.tests.empty() || has_user_main) {
            entry_group_idx = ensure_unit_group(entry_pkg, entry_src);
        }

        auto reset_buffer = [&]() {
            buf_.str("");
            buf_.clear();
            indent_ = 0;
            last_source_line_ = -1;
            source_path_ = cf_.ast.path;
            try_count_ = 0;
            defer_count_ = 0;
            hoisted_tries_.clear();
        };

        auto collect_pkg_dep_headers = [&](const std::string& upkg) -> std::vector<std::string> {
            std::vector<std::string> deps_out;
            deps_out.push_back("preamble.hpp");
            std::unordered_set<std::string> visited_deps;
            std::vector<std::string> dep_pkgs;
            auto dfs = [&](const auto& self, const std::string& p) -> void {
                if (p.empty() || !visited_deps.insert(p).second) return;
                if (auto it = ast.pkg_imports.find(p); it != ast.pkg_imports.end()) {
                    for (const auto& d : it->second) {
                        self(self, d);
                    }
                }
                dep_pkgs.push_back(p);
            };
            dfs(dfs, upkg);
            for (const auto& p : dep_pkgs) {
                deps_out.push_back(std::format("pkg_{}.hpp", sanitize_pkg_unit_id(p)));
            }
            return deps_out;
        };

        auto short_path_hash = [](std::string_view s) -> std::string {
            std::uint64_t h = 0xcbf29ce484222325ULL;
            for (unsigned char c : s) {
                h ^= c;
                h *= 0x100000001b3ULL;
            }
            return std::format("{:08x}", static_cast<std::uint32_t>(h ^ (h >> 32)));
        };

        sharded_non_inline_ = true;

        // Step 2: Emit 1:1 per-file concrete translation units first so `used_core_` is completely populated before `preamble.hpp`.
        std::unordered_set<std::string> used_unit_ids;
        for (std::size_t gidx = 0; gidx < unit_groups.size(); ++gidx) {
            const auto& ug = unit_groups[gidx];
            const std::string pkg_id = sanitize_pkg_unit_id(ug.pkg);
            const std::string pkg_hdr = std::format("pkg_{}.hpp", pkg_id);
            std::string file_stem = ug.norm_source.empty()
                ? std::string("unit")
                : std::filesystem::path(ug.norm_source).stem().string();
            std::string uid = pkg_id + "__" + sanitize_pkg_unit_id(file_stem);
            if (!used_unit_ids.insert(uid).second) {
                uid = uid + "_" + short_path_hash(ug.norm_source);
                used_unit_ids.insert(uid);
            }

            const bool emit_tests_here = (gidx == entry_group_idx);
            reset_buffer();
            writeln("// Generated by cfire sharded codegen (1:1 per-file TU)");
            writeln(std::format("#include \"{}\"", pkg_hdr));
            writeln();
            const auto& idxs = ug.fn_indices;
            for (std::size_t k = 0; k < idxs.size(); ++k) {
                emit_function(ast.functions[idxs[k]]);
                if (k + 1 < idxs.size() || (emit_tests_here && (!ast.tests.empty() || test_mode_))) {
                    writeln();
                }
            }
            if (emit_tests_here) {
                emit_test_runner(ast, has_user_main);
            }

            sharded.units.push_back(CppUnit{
                .id = uid,
                .pkg_name = ug.pkg,
                .source_path = ug.raw_source,
                .filename = std::format("unit_{}.cpp", uid),
                .header_file = pkg_hdr,
                .dep_headers = collect_pkg_dep_headers(ug.pkg),
                .cpp = buf_.str(),
            });
        }

        // Step 3: Emit per-package public headers (`pkg_<id>.hpp`).
        std::vector<CppHeader> pkg_headers;
        pkg_headers.reserve(pkg_header_order.size());
        for (const auto& pkg : pkg_header_order) {
            const std::string pkg_id = sanitize_pkg_unit_id(pkg);
            const std::string hdr_name = std::format("pkg_{}.hpp", pkg_id);
            reset_buffer();
            writeln("#pragma once");
            writeln("#include \"preamble.hpp\"");
            if (auto it = ast.pkg_imports.find(pkg); it != ast.pkg_imports.end()) {
                for (const auto& dep : it->second) {
                    if (dep == pkg) continue;
                    writeln(std::format("#include \"pkg_{}.hpp\"", sanitize_pkg_unit_id(dep)));
                }
            }
            writeln();
            emit_pkg_decls_types_and_globals(*this, pkg, default_pkg);
            pkg_headers.push_back(CppHeader{
                .id = "pkg_" + pkg_id,
                .pkg_name = pkg,
                .filename = hdr_name,
                .hpp = buf_.str(),
            });
        }

        sharded_non_inline_ = false;
        reset_buffer();

        // Step 4: Build stable `preamble.hpp` with `#ifndef`-guarded defines and unconditional `str`/`bm`.
        std::ostringstream pre;
        if (!meta.link_tags.empty()) {
            std::string joined;
            for (std::size_t i = 0; i < meta.link_tags.size(); ++i) {
                if (i > 0) joined += " ";
                joined += meta.link_tags[i];
            }
            pre << std::format("// @cfire-link: {}\n", joined);
        }
        pre << "#pragma once\n";
        auto emit_pre_line = [&](std::string_view line) { pre << line << "\n"; };
        for (const auto& d : meta.defines) {
            emit_guarded_define_with(d, emit_pre_line);
        }
        pre << "#include \"cfire_prelude.hpp\"\n";
        {
            std::set<std::string> core_headers;
            core_headers.insert("core/str.hpp");
            core_headers.insert("core/builtin_methods.hpp");
            for (const auto& mod : used_core_) {
                if (std::string hdr = cfire::modules::core_module_header(mod); !hdr.empty()) {
                    core_headers.insert(std::move(hdr));
                }
            }
            for (const auto& hdr : core_headers) {
                pre << std::format("#include \"{}\"\n", hdr);
            }
        }
        for (const auto& h : meta.cpp_headers) {
            std::string clean = h.path;
            while (!clean.empty() && (clean.front() == '<' || clean.front() == '"')) clean.erase(clean.begin());
            while (!clean.empty() && (clean.back() == '>' || clean.back() == '"')) clean.pop_back();
            pre << std::format("#include <{}>\n", clean);
        }
        pre << "#if defined(_WIN32)\n#include \"detail/win32_clean_macros.hpp\"\n#ifdef interface\n#undef interface\n#endif\n#endif\n";

        sharded.preamble_hpp = pre.str();
        sharded.headers.push_back(CppHeader{
            .id = "preamble",
            .pkg_name = "",
            .filename = "preamble.hpp",
            .hpp = sharded.preamble_hpp,
        });
        std::ostringstream umbrella;
        umbrella << "#pragma once\n#include \"preamble.hpp\"\n";
        for (auto& ph : pkg_headers) {
            umbrella << std::format("#include \"{}\"\n", ph.filename);
            sharded.headers.push_back(std::move(ph));
        }
        sharded.decls_hpp = umbrella.str();
        return sharded;
    }

    CFIRE_CG_INLINE void Emitter::emit_file() {
        const auto& ast = cf_.ast;
        CollectedImportMeta meta = collect_import_meta(*this);

        if (!meta.link_tags.empty()) {
            std::string joined;
            for (std::size_t i = 0; i < meta.link_tags.size(); ++i) {
                if (i > 0) joined += " ";
                joined += meta.link_tags[i];
            }
            writeln(std::format("// @cfire-link: {}", joined));
        }
        for (const auto& d : meta.defines) writeln(d);
        writeln("#include \"cfire_prelude.hpp\"");
        if (!used_core_.empty()) {
            std::set<std::string> core_headers;
            for (const auto& mod : used_core_) {
                if (std::string hdr = cfire::modules::core_module_header(mod); !hdr.empty()) {
                    core_headers.insert(std::move(hdr));
                }
            }
            for (const auto& hdr : core_headers) {
                writeln(std::format("#include \"{}\"", hdr));
            }
        }
        for (const auto& h : meta.cpp_headers) {
            std::string clean = h.path;
            while (!clean.empty() && (clean.front() == '<' || clean.front() == '"')) clean.erase(clean.begin());
            while (!clean.empty() && (clean.back() == '>' || clean.back() == '"')) clean.pop_back();
            writeln(std::format("#include <{}>", clean));
        }
        writeln("#if defined(_WIN32)");
        writeln("#include \"detail/win32_clean_macros.hpp\"");
        writeln("#ifdef interface");
        writeln("#undef interface");
        writeln("#endif");
        writeln("#endif");
        writeln();

        emit_decls_types_and_globals(*this);

        // 5b. Emit Functions & Receiver Methods
        bool has_user_main = false;
        for (std::size_t i = 0; i < ast.functions.size(); ++i) {
            if (ast.functions[i].name == "main" && !ast.functions[i].receiver) {
                has_user_main = true;
                if (test_mode_) continue; // Omit user main when compiling in `cfire test` mode
            }
            emit_function(ast.functions[i]);
            if (i + 1 < ast.functions.size() || !ast.tests.empty()) writeln();
        }

        // 5. Emit `test "..."`, `fuzz "..."`, `bench "..."`, and `[auto]` Invariant Blocks & Test Harness Runner
        emit_test_runner(ast, has_user_main);
    }


    CFIRE_CG_INLINE void Emitter::emit_interface(const InterfaceDecl& iface) {
        writeln(std::format("// Interface '{}' -> C++20 concept", iface.name));
        writeln(std::format("template <typename T>"));
        writeln(std::format("concept {} = true;", iface.name));
    }


    CFIRE_CG_INLINE void Emitter::emit_enum(const EnumDecl& en) {
        set_source_path(en.source_path);
        if (en.line > 0) emit_line_directive(en.line);
        if (!en.type_params.empty()) {
            writeln(format_template_clause(en.type_params));
        }
        writeln(std::format("struct {} {{", en.name));
        ++indent_;
        std::vector<std::string> var_types;
        for (const auto& v : en.variants) {
            writeln(std::format("struct _{} {{", v.name));
            ++indent_;
            for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                writeln(std::format("{} _{}{{}};", lower_owning_type(v.payload_types[i]), i));
            }
            writeln(std::format("bool operator==(const _{}&) const = default;", v.name));
            --indent_;
            writeln("};");
            var_types.push_back("_" + v.name);
        }

        std::string vlist;
        for (std::size_t i = 0; i < var_types.size(); ++i) {
            if (i > 0) vlist += ", ";
            vlist += var_types[i];
        }
        writeln(std::format("std::variant<{}> _v;", vlist));
        writeln(std::format("bool operator==(const {}&) const = default;", en.name));

        // Explicit integer conversion operators
        writeln("explicit operator std::int8_t() const noexcept { return static_cast<std::int8_t>(_v.index()); }");
        writeln("explicit operator std::int16_t() const noexcept { return static_cast<std::int16_t>(_v.index()); }");
        writeln("explicit operator std::int32_t() const noexcept { return static_cast<std::int32_t>(_v.index()); }");
        writeln("explicit operator std::int64_t() const noexcept { return static_cast<std::int64_t>(_v.index()); }");
        writeln("explicit operator std::uint8_t() const noexcept { return static_cast<std::uint8_t>(_v.index()); }");
        writeln("explicit operator std::uint16_t() const noexcept { return static_cast<std::uint16_t>(_v.index()); }");
        writeln("explicit operator std::uint32_t() const noexcept { return static_cast<std::uint32_t>(_v.index()); }");
        writeln("explicit operator std::uint64_t() const noexcept { return static_cast<std::uint64_t>(_v.index()); }");
        writeln("template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, std::int8_t> && !std::is_same_v<T, std::int16_t> && !std::is_same_v<T, std::int32_t> && !std::is_same_v<T, std::int64_t> && !std::is_same_v<T, std::uint8_t> && !std::is_same_v<T, std::uint16_t> && !std::is_same_v<T, std::uint32_t> && !std::is_same_v<T, std::uint64_t>)");
        writeln("explicit operator T() const noexcept { return static_cast<T>(_v.index()); }");
        writeln("[[nodiscard]] auto to_i32() const noexcept -> std::int32_t { return static_cast<std::int32_t>(_v.index()); }");
        writeln("[[nodiscard]] auto to_i64() const noexcept -> std::int64_t { return static_cast<std::int64_t>(_v.index()); }");

        // String representation
        writeln("[[nodiscard]] auto to_string() const -> std::string_view {");
        ++indent_;
        writeln("switch (_v.index()) {");
        for (std::size_t i = 0; i < en.variants.size(); ++i) {
            writeln(std::format("case {}: return \"{}\";", i, en.variants[i].name));
        }
        writeln("default: return \"\";");
        writeln("}");
        --indent_;
        writeln("}");

        // Emit static variant constructors (`Shape::Circle(r)`, `Shape::Empty()`)
        for (const auto& v : en.variants) {
            std::string params, args;
            for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                if (i > 0) {
                    params += ", ";
                    args += ", ";
                }
                params += std::format("{} a{}", lower_owning_type(v.payload_types[i]), i);
                args += std::format("std::move(a{})", i);
            }
            writeln(std::format("[[nodiscard]] static auto {}({}) -> {} {{ return {}{{_{}{{{}}}}}; }}",
                v.name, params, en.name, en.name, v.name, args));
        }

        // Static factory from integer
        writeln(std::format("[[nodiscard]] static auto from_i32(std::int32_t val) -> std::optional<{}> {{", en.name));
        ++indent_;
        writeln("switch (val) {");
        for (std::size_t i = 0; i < en.variants.size(); ++i) {
            if (en.variants[i].payload_types.empty()) {
                writeln(std::format("case {}: return {}();", i, en.variants[i].name));
            }
        }
        writeln("default: return std::nullopt;");
        writeln("}");
        --indent_;
        writeln("}");

        writeln(std::format("[[nodiscard]] static auto from_i64(std::int64_t val) -> std::optional<{}> {{", en.name));
        writeln(std::format("    return from_i32(static_cast<std::int32_t>(val));"));
        writeln("}");

        // Static factory from string
        writeln(std::format("[[nodiscard]] static auto from_string(std::string_view s) -> std::optional<{}> {{", en.name));
        ++indent_;
        for (const auto& v : en.variants) {
            if (v.payload_types.empty()) {
                writeln(std::format("if (s == \"{}\") return {}();", v.name, v.name));
            }
        }
        writeln("return std::nullopt;");
        --indent_;
        writeln("}");

        --indent_;
        writeln("};");

        writeln();
        if (!en.type_params.empty()) {
            std::string tparams;
            std::string targs;
            for (std::size_t i = 0; i < en.type_params.size(); ++i) {
                if (i > 0) {
                    tparams += ", ";
                    targs += ", ";
                }
                tparams += std::format("typename {}", en.type_params[i].name);
                targs += en.type_params[i].name;
            }
            writeln(std::format("template <{}>", tparams));
            writeln(std::format("struct std::formatter<{}<{}> > : std::formatter<std::string_view> {{", en.name, targs));
        } else {
            writeln("template <>");
            writeln(std::format("struct std::formatter<{}> : std::formatter<std::string_view> {{", en.name));
        }
        writeln(std::format("    auto format(const {}& e, std::format_context& ctx) const {{", en.name));
        writeln("        return std::formatter<std::string_view>::format(e.to_string(), ctx);");
        writeln("    }");
        writeln("};");

        emit_enum_reflection(en);
    }


    CFIRE_CG_INLINE void Emitter::emit_struct(const StructDecl& st) {
        set_source_path(st.source_path);
        if (st.line > 0) emit_line_directive(st.line);
        if (!st.type_params.empty()) {
            writeln(format_template_clause(st.type_params));
        }
        writeln(std::format("struct {} {{", st.name));
        ++indent_;
        for (const auto& fld : st.fields) {
            std::string cpp_type = lower_owning_type(fld.type);
            std::string fname = sanitize_cpp_ident(fld.name);
            if (fld.default_val) {
                std::string dval = emit_expr(fld.default_val);
                if ((fld.type.is_ptr || fld.type.is_mut_ptr) && dval == "std::nullopt") {
                    dval = "nullptr";
                }
                writeln(std::format("{} {} = {};", cpp_type, fname, dval));
            } else {
                writeln(std::format("{} {}{{}};", cpp_type, fname));
            }
        }
        // field-by-field equality, ordering and hashing, so a struct can be a map key or a heap
        // element. A defaulted comparison is deleted when a field lacks it, and `cfire_hash` is constrained
        // on every field being hashable, so a struct with a non-comparable field is simply not a key.
        const std::string self = st.type_params.empty() ? st.name : st.name + "<" + [&] {
            std::string a;
            for (std::size_t i = 0; i < st.type_params.size(); ++i) a += (i ? ", " : "") + st.type_params[i].name;
            return a;
        }() + ">";
        writeln(std::format("friend auto operator==(const {0}&, const {0}&) -> bool = default;", self));
        writeln(std::format("friend auto operator<=>(const {0}&, const {0}&) = default;", self));
        {
            std::string reqs = "true";
            std::string body;
            for (const auto& fld : st.fields) {
                std::string fname = sanitize_cpp_ident(fld.name);
                reqs += std::format(" && cfire::detail::key_hashable<std::remove_cvref_t<decltype(std::declval<const S&>().{})>>", fname);
                body += std::format(" cfire::detail::hash_field(seed, {});", fname);
            }
            writeln(std::format("template <typename S = {}> requires({}) [[nodiscard]] auto cfire_hash() const -> std::size_t {{ std::size_t seed = {};{} return seed; }}",
                                self, reqs, st.fields.size(), body));
        }
        --indent_;
        writeln("};");

        emit_struct_reflection(st);
    }


    CFIRE_CG_INLINE void Emitter::emit_function_forward_decl(const FnDecl& fn) {
        if (fn.name == "main" && !fn.receiver) return;
        std::string cur_fn = emitted_fn_symbol(fn);
        set_generic_params(fn);

        const auto eff_tparams = effective_type_params(fn);
        if (!eff_tparams.empty()) {
            writeln(format_template_clause(eff_tparams));
        }

        std::string param_list;
        if (fn.receiver) {
            param_list += std::format("{} {}", lower_param_type(fn.receiver->intent, fn.receiver->type), sanitize_cpp_ident(fn.receiver->name));
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (!param_list.empty()) param_list += ", ";
            param_list += std::format("{} {}", lower_param_type(fn.params[i].intent, fn.params[i].type), sanitize_cpp_ident(fn.params[i].name));
        }
        std::string ret_str = lower_return_type(fn.return_type);
        std::string prefix = (ret_str == "void") ? "" : "[[nodiscard]] ";
        const std::string inline_kw = (sharded_non_inline_ && eff_tparams.empty()) ? "" : "inline ";
        if (fn.receiver) {
            writeln(std::format("{}{}auto cfire_method_{}({}) -> {};", prefix, inline_kw, fn.name, param_list, ret_str));
            if (!eff_tparams.empty()) writeln(format_template_clause(eff_tparams));
            writeln(std::format("{}{}auto {}({}) -> {};", prefix, inline_kw, cur_fn, param_list, ret_str));
        } else {
            writeln(std::format("{}{}auto {}({}) -> {};", prefix, inline_kw, cur_fn, param_list, ret_str));
        }
    }


    CFIRE_CG_INLINE void Emitter::emit_function(const FnDecl& fn) {
        set_generic_params(fn);
        set_source_path(fn.source_path);
        if (fn.line > 0) emit_line_directive(fn.line);
        cur_pkg_ = fn.package_name;
        cur_fn_ = emitted_fn_symbol(fn);
        defer_count_ = 0;
        local_vars_.clear();
        error_vars_.clear();
        ptr_vars_.clear();
        smart_ptr_vars_.clear();
        optional_vars_.clear();
        map_vars_.clear();
        fixed_array_vars_.clear();
        var_types_.clear();
        view_params_.clear();
        if (fn.receiver) {
            local_vars_.insert(fn.receiver->name);
            var_types_[fn.receiver->name] = fn.receiver->type.name;
            if (fn.receiver->type.is_ptr || fn.receiver->type.is_mut_ptr) ptr_vars_.insert(fn.receiver->name);
            if (!fn.receiver->type.smart_wrap.empty()) smart_ptr_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_optional) optional_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_map && fn.receiver->intent == "mut") map_vars_.insert(fn.receiver->name);
            if (fn.receiver->type.is_array && !fn.receiver->type.array_len.empty()) {
                if (auto n = parse_int_literal<std::size_t>(fn.receiver->type.array_len)) {
                    fixed_array_vars_[fn.receiver->name] = *n;
                }
            }
        }
        for (const auto& p : fn.params) {
            local_vars_.insert(p.name);
            if (p.intent != "mut" && p.intent != "out" && p.intent != "move") {
                const std::string lowered = lower_param_type(p.intent, p.type);
                if (lowered == "cfire::str_ref") view_params_.emplace_back(p.name, "std::string");
                else if (lowered.starts_with("cfire::slice_ref<")) view_params_.emplace_back(p.name, lower_owning_type(p.type));
            }
            var_types_[p.name] = p.type.name;
            if (p.type.is_ptr || p.type.is_mut_ptr) ptr_vars_.insert(p.name);
            if (!p.type.smart_wrap.empty()) smart_ptr_vars_.insert(p.name);
            if (p.type.is_optional) optional_vars_.insert(p.name);
            if (p.type.is_map && p.intent == "mut") map_vars_.insert(p.name);
            if (p.type.is_array && !p.type.array_len.empty()) {
                if (auto n = parse_int_literal<std::size_t>(p.type.array_len)) {
                    fixed_array_vars_[p.name] = *n;
                }
            }
        }

        if (fn.name == "main" && !fn.receiver) {
            in_main_ = true;
            in_fallible_void_ = false;
            in_ptr_return_ = false;
            local_vars_.insert("argc");
            local_vars_.insert("argv");
            writeln("auto main(int argc, char** argv) -> int {");
            ++indent_;
            writeln("cfire::detail::set_process_args(argc, argv);");
            for (const auto& st : fn.body) emit_stmt(st, true);
            writeln("return 0;");
            --indent_;
            writeln("}");
            in_main_ = false;
            local_vars_.clear();
            error_vars_.clear();
            ptr_vars_.clear();
            smart_ptr_vars_.clear();
            optional_vars_.clear();
            return;
        }
        in_main_ = false;
        in_fallible_void_ = (fn.return_type.name == "void" && fn.return_type.is_fallible);
        in_ptr_return_ = (fn.return_type.is_ptr || fn.return_type.is_mut_ptr);
        cur_fn_raw_return_type_ = (fn.return_type.is_fallible || fn.return_type.is_optional)
            ? lower_raw_owning_type(fn.return_type)
            : "";
        cur_fn_return_typeref_ = fn.return_type;
        cur_fn_return_typeref_.is_fallible = false;
        cur_fn_return_typeref_.is_optional = false;
        has_cur_fn_return_typeref_ = true;

        const auto eff_tparams = effective_type_params(fn);
        if (!eff_tparams.empty()) {
            writeln(format_template_clause(eff_tparams));
        }

        std::string param_list;
        std::string pass_args;
        if (fn.receiver) {
            std::string rname = sanitize_cpp_ident(fn.receiver->name);
            param_list += std::format("{} {}", lower_param_type(fn.receiver->intent, fn.receiver->type), rname);
            pass_args += rname;
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            std::string pname = sanitize_cpp_ident(fn.params[i].name);
            if (!param_list.empty()) param_list += ", ";
            param_list += std::format("{} {}", lower_param_type(fn.params[i].intent, fn.params[i].type), pname);
            if (!pass_args.empty()) pass_args += ", ";
            pass_args += (fn.params[i].intent == "move") ? std::format("std::move({})", pname) : pname;
        }
        std::string ret_str = lower_return_type(fn.return_type);
        std::string prefix = (ret_str == "void") ? "" : "[[nodiscard]] ";
        const std::string inline_kw = (sharded_non_inline_ && eff_tparams.empty()) ? "" : "inline ";
        std::string method_fn = fn.receiver ? std::format("cfire_method_{}", fn.name) : cur_fn_;
        writeln(std::format("{}{}auto {}({}) -> {} {{", prefix, inline_kw, method_fn, param_list, ret_str));
        ++indent_;
        for (const auto& st : fn.body) emit_stmt(st, false);
        if (fn.return_type.name == "void" && fn.return_type.is_fallible) {
            bool has_ret = !fn.body.empty() && std::holds_alternative<ReturnStmt>(fn.body.back()->node);
            if (!has_ret) writeln("return {};");
        }
        --indent_;
        writeln("}");
        in_fallible_void_ = false;
        in_ptr_return_ = false;
        cur_fn_raw_return_type_.clear();
        has_cur_fn_return_typeref_ = false;
        cur_fn_return_typeref_ = TypeRef{};
        cur_pkg_.clear();
        local_vars_.clear();
        error_vars_.clear();
        ptr_vars_.clear();
        smart_ptr_vars_.clear();
        optional_vars_.clear();

        if (fn.receiver) {
            if (!eff_tparams.empty()) {
                std::string targs;
                for (std::size_t i = 0; i < eff_tparams.size(); ++i) {
                    if (i > 0) targs += ", ";
                    targs += eff_tparams[i].name;
                }
                writeln(format_template_clause(eff_tparams));
                writeln(std::format("{}inline auto {}({}) -> {} {{", prefix, cur_fn_, param_list, ret_str));
                ++indent_;
                if (ret_str == "void") {
                    writeln(std::format("cfire_method_{}<{}>({});", fn.name, targs, pass_args));
                } else {
                    writeln(std::format("return cfire_method_{}<{}>({});", fn.name, targs, pass_args));
                }
                --indent_;
                writeln("}");
            } else {
                const std::string wrap_inline_kw = sharded_non_inline_ ? "" : "inline ";
                writeln(std::format("{}{}auto {}({}) -> {} {{", prefix, wrap_inline_kw, cur_fn_, param_list, ret_str));
                ++indent_;
                if (ret_str == "void") {
                    writeln(std::format("cfire_method_{}({});", fn.name, pass_args));
                } else {
                    writeln(std::format("return cfire_method_{}({});", fn.name, pass_args));
                }
                --indent_;
                writeln("}");
            }
        }
    }


    CFIRE_CG_INLINE void Emitter::hoist_subexpr_tries(const ExprPtr& e, bool in_main, int stmt_line, bool is_top_handled) {
        if (!e) return;
        std::visit([&](const auto& ex) {
            using T = std::decay_t<decltype(ex)>;
            if constexpr (std::is_same_v<T, UnaryExpr>) {
                hoist_subexpr_tries(ex.operand, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                hoist_subexpr_tries(ex.left, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.right, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, MemberExpr>) {
                hoist_subexpr_tries(ex.object, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, IndexExpr>) {
                hoist_subexpr_tries(ex.object, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.index, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, SliceExpr>) {
                hoist_subexpr_tries(ex.object, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.start, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.end, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, StructLitExpr>) {
                for (const auto& f : ex.fields) {
                    hoist_subexpr_tries(f.value, in_main, stmt_line, false);
                }
            } else if constexpr (std::is_same_v<T, ArrayLitExpr> || std::is_same_v<T, TupleExpr>) {
                for (const auto& el : ex.elements) {
                    hoist_subexpr_tries(el, in_main, stmt_line, false);
                }
            } else if constexpr (std::is_same_v<T, OrExpr>) {
                hoist_subexpr_tries(ex.inner, in_main, stmt_line, false);
                hoist_subexpr_tries(ex.fallback_val, in_main, stmt_line, false);
                if (!ex.handler_body.empty() && !is_top_handled) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_res_{}", id);
                    std::string tmp_t = lower_type_id(ex.inner ? TypeId{ex.inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                    writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(ex.inner)));
                    writeln(std::format("if (!{}.has_value()) {{", tmp));
                    ++indent_;
                    emit_or_handler_body(ex, tmp, in_main);
                    --indent_;
                    writeln("}");
                    hoisted_tries_[e] = std::format("cfire::unwrap_val(std::move({}))", tmp);
                    last_source_line_ = -1;
                }
            } else if constexpr (std::is_same_v<T, MatchExpr>) {
                hoist_subexpr_tries(ex.target, in_main, stmt_line, false);
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                hoist_subexpr_tries(ex.callee, in_main, stmt_line, false);
                for (const auto& a : ex.args) {
                    hoist_subexpr_tries(a.value, in_main, stmt_line, false);
                }
            } else if constexpr (std::is_same_v<T, TryExpr>) {
                hoist_subexpr_tries(ex.inner, in_main, stmt_line, false);
                if (!is_top_handled) {
                    int id = ++try_count_;
                    std::string tmp = std::format("_try_{}", id);
                    std::string tmp_t = lower_type_id(ex.inner ? TypeId{ex.inner->resolved_type_id} : TypeId::Unknown(), LowerContext::Value).value_or("auto");
                    writeln(std::format("{} {} = {};", tmp_t, tmp, emit_expr(ex.inner)));
                    emit_try_unwrap_check(tmp, stmt_line, in_main);
                    hoisted_tries_[e] = std::format("cfire::unwrap_val(std::move({}))", tmp);
                    last_source_line_ = -1;
                }
            }
        }, e->node);
    }


    CFIRE_CG_INLINE auto Emitter::lower_named_cpp_type(std::string_view raw_name, bool has_type_args) const -> std::string {
        std::string name(raw_name);
        if (!is_local_type(name)) {
            if (name == "Error") return "cfire::Error";
            if (name == "sqlite::Conn" || name == "sqlite.Conn") return "cfire::sqlite::Conn";
            if (name == "ipc::Conn" || name == "ipc.Conn" ||
                (name == "Conn" && used_core_.contains("ipc") && !used_core_.contains("websocket") && !used_core_.contains("sqlite"))) {
                return "cfire::ipc::Conn";
            }
            if (name == "ipc::Listener" || name == "ipc.Listener" || (name == "Listener" && used_core_.contains("ipc"))) {
                return "cfire::ipc::Listener";
            }
            if (name == "ipc::PeerCred" || name == "ipc.PeerCred" || (name == "PeerCred" && used_core_.contains("ipc"))) {
                return "cfire::ipc::PeerCred";
            }
            if (name == "fs::FileInfo" || name == "fs.FileInfo" || (name == "FileInfo" && used_core_.contains("fs"))) {
                return "cfire::fs::FileInfo";
            }
            if (name == "context::Context" || name == "context.Context" || name == "Context") return "cfire::sync::context::Context";
            if (name == "websocket::Conn" || name == "websocket.Conn" || name == "Conn" || name == "websocket::WebSocket" || name == "websocket.WebSocket" || name == "WebSocket") return "cfire::websocket::Conn";
            if (name == "websocket::Server" || name == "websocket.Server") return "cfire::websocket::Server";
            if (name == "websocket::TlsConfig" || name == "websocket.TlsConfig") return "cfire::http::TlsConfig";
            if (name == "http::Request" || name == "http.Request" || name == "Request") return "cfire::http::Request";
            if (name == "http::Response" || name == "http.Response" || name == "Response") return "cfire::http::Response";
            if (name == "http::Router" || name == "http.Router" || name == "Router") return "cfire::http::Router";
            if (name == "http::HttpServer" || name == "http.HttpServer" || name == "HttpServer") return "cfire::http::HttpServer";
            // the names `.cf` code uses for these differ from the C++ class or namespace.
            if (name == "http::Server" || name == "http.Server") return "cfire::http::HttpServer";
            if (name == "sync::Context" || name == "sync.Context") return "cfire::sync::context::Context";
            if (name == "sync::MutexGuard" || name == "sync.MutexGuard") return "cfire::sync::MutexGuard";
            if (name == "url::Url" || name == "url.Url" || name == "Url") return "cfire::url::Url";
            if (name == "tls::ServerContext" || name == "tls.ServerContext" || name == "ServerContext") return "cfire::tls::ServerContext";
            if (name == "tls::CertKeyPair" || name == "tls.CertKeyPair" || name == "CertKeyPair") return "cfire::tls::CertKeyPair";
            if (name == "fs::File" || name == "fs.File" || (name == "File" && used_core_.contains("fs"))) return "cfire::fs::File";
            if (name == "net::TcpConn" || name == "net.TcpConn" || name == "TcpConn") return "cfire::net::TcpConn";
            if (name == "net::TcpListener" || name == "net.TcpListener" || name == "TcpListener") return "cfire::net::TcpListener";
            if (name == "ssh::Channel" || name == "ssh.Channel") return "cfire::ssh::Channel";
            if (name == "ssh::Session" || name == "ssh.Session") return "cfire::ssh::Session";
            if (name == "audio::Wave" || name == "audio.Wave") return "cfire::audio::Wave";
            if (name == "audio::Sound" || name == "audio.Sound") return "cfire::audio::Sound";
            if (name == "audio::Stream" || name == "audio.Stream") return "cfire::audio::Stream";
            if (name == "audio::Waveform" || name == "audio.Waveform") return "cfire::audio::Waveform";
            if (name == "audio::SfxPreset" || name == "audio.SfxPreset") return "cfire::audio::SfxPreset";
            if (name == "http::SseEvent" || name == "http.SseEvent" || name == "SseEvent") return "cfire::http::SseEvent";
            if (name == "ai::Provider" || name == "ai.Provider") return "cfire::ai::Provider";
            if (name == "ai::Client" || name == "ai.Client") return "cfire::ai::Client";
            if (name == "ai::Message" || name == "ai.Message") return "cfire::ai::Message";
            if (name == "ai::Tool" || name == "ai.Tool") return "cfire::ai::Tool";
            if (name == "ai::ToolCall" || name == "ai.ToolCall") return "cfire::ai::ToolCall";
            if (name == "ai::ChatResponse" || name == "ai.ChatResponse") return "cfire::ai::ChatResponse";
            if (name == "ai::McpServer" || name == "ai.McpServer") return "cfire::ai::McpServer";
            if (name == "ai::Agent" || name == "ai.Agent") return "cfire::ai::Agent";
            if (name == "webui::App" || name == "webui.App" || (name == "App" && used_core_.contains("webui"))) return "cfire::webui::App";
        }
        std::string clean = std::move(name);
        if (clean.starts_with("cpp.")) clean.erase(0, 4);
        else if (clean.starts_with("cpp::")) clean.erase(0, 5);
        for (const auto& q : cf_.cpp_qualifiers) {
            if (clean.starts_with(q + ".")) {
                clean.erase(0, q.size() + 1);
                break;
            }
            if (clean.starts_with(q + "::")) {
                clean.erase(0, q.size() + 2);
                break;
            }
        }
        auto sep_pos = clean.find_first_of(".:");
        if (sep_pos != std::string::npos) {
            bool is_dcolon = (clean[sep_pos] == ':' && sep_pos + 1 < clean.size() && clean[sep_pos + 1] == ':');
            std::string mod = clean.substr(0, sep_pos);
            std::string rest = clean.substr(sep_pos + (is_dcolon ? 2 : 1));
            if (auto it = core_aliases_.find(mod); it != core_aliases_.end()) {
                mod = it->second;
            }
            if (auto it = cf_.core_aliases.find(mod); it != cf_.core_aliases.end()) {
                mod = it->second;
            }
            std::string mangled = std::format("cfire_pkg_{}__{}", mod, rest);
            if (is_local_type(mangled)) {
                clean = std::move(mangled);
            } else if (used_core_.contains(mod)) {
                clean = std::format("cfire::{}::{}", mod, rest);
            } else if (is_local_type(rest) || is_vendor_alias(mod) || is_cpp_qualifier(mod)) {
                clean = rest;
            } else if (cfire::frontend::StdlibCatalogIndex::instance().has_namespace(mod)) {
                // every stdlib module's types live in `cfire::<ns>` (`aes.GcmResult`, `http.ServerLimits`),
                // including modules whose core key differs from their namespace (`core:net/http`, `core:crypto/aes`).
                clean = std::format("cfire::{}::{}", mod, rest);
            }
        } else {
            if (clean == "Channel" && used_core_.contains("sync")) {
                clean = "cfire::sync::Channel";
            } else if (clean == "Mutex" && used_core_.contains("sync")) {
                clean = has_type_args ? "cfire::sync::MutexT" : "cfire::sync::Mutex";
            } else if (clean == "Atomic" && used_core_.contains("sync")) {
                clean = "cfire::sync::Atomic";
            } else if (clean == "Task" && used_core_.contains("async")) {
                clean = "cfire::async::Task";
            } else if ((clean == "Set" || clean == "PriorityQueue" || clean == "MinQueue" || clean == "Deque" || clean == "BTreeMap" || clean == "BitSet") && used_core_.contains("collections")) {
                clean = std::format("cfire::collections::{}", clean);
            }
        }
        if (has_type_args && (clean == "cfire::sync::Mutex" || clean == "sync.Mutex" || clean == "sync::Mutex")) {
            clean = "cfire::sync::MutexT";
        }
        return clean;
    }

    CFIRE_CG_INLINE auto Emitter::lower_type_id(TypeId tid, LowerContext ctx) const -> std::optional<std::string> {
        if (!tid.is_valid() || tid.is_unknown() || tid.is_error() || tid.is_foreign() || tid.is_never()) {
            return std::nullopt;
        }
        if (cf_.type_table.contains_unknown(tid) || cf_.type_table.contains_error(tid) || cf_.type_table.contains_foreign(tid)) {
            return std::nullopt;
        }
        if (ctx == LowerContext::Value && cf_.type_table.contains_type_param(tid)) {
            return std::nullopt;
        }
        const TypeDesc& d = cf_.type_table.get(tid);
        auto wrap_param = [ctx](std::string base, bool by_val_param_in = false) -> std::string {
            switch (ctx) {
                case LowerContext::ParamMut: return base + "&";
                case LowerContext::ParamMove: return base + "&&";
                case LowerContext::ParamIn: return by_val_param_in ? base : ("const " + base + "&");
                case LowerContext::Value:
                case LowerContext::Return:
                case LowerContext::RangeIndex: return base;
            }
            return base;
        };
        auto is_scalar_prim = [this](TypeId t) -> bool {
            const TypeDesc& td = cf_.type_table.get(t);
            switch (td.kind) {
                case TypeKind::Bool:
                case TypeKind::Int8:
                case TypeKind::Int16:
                case TypeKind::Int32:
                case TypeKind::Int64:
                case TypeKind::Uint8:
                case TypeKind::Uint16:
                case TypeKind::Uint32:
                case TypeKind::Uint64:
                case TypeKind::Isize:
                case TypeKind::Usize:
                case TypeKind::Float32:
                case TypeKind::Float64:
                case TypeKind::Byte:
                    return true;
                default:
                    return td.kind == TypeKind::NominalStruct && (td.name == "c_char" || td.name == "char");
            }
        };
        switch (d.kind) {
            case TypeKind::Unknown:
            case TypeKind::Never:
            case TypeKind::Error:
            case TypeKind::Foreign:
            case TypeKind::Interface:
                return std::nullopt;
            case TypeKind::Void:
                return ctx == LowerContext::Return ? std::optional<std::string>{"void"} : std::nullopt;
            case TypeKind::TypeParam: {
                if (!generic_params_.contains(d.name)) return std::nullopt;
                if (ctx == LowerContext::ParamMut) return d.name + "&";
                if (ctx == LowerContext::ParamMove) return d.name + "&&";
                if (ctx == LowerContext::ParamIn) return "const " + d.name + "&";
                return d.name;
            }
            case TypeKind::Bool: return wrap_param("bool", true);
            case TypeKind::Int8: return wrap_param("std::int8_t", true);
            case TypeKind::Int16: return wrap_param("std::int16_t", true);
            case TypeKind::Int32: return wrap_param("std::int32_t", true);
            case TypeKind::Int64: return wrap_param("std::int64_t", true);
            case TypeKind::Uint8:
            case TypeKind::Byte: return wrap_param("std::uint8_t", true);
            case TypeKind::Uint16: return wrap_param("std::uint16_t", true);
            case TypeKind::Uint32: return wrap_param("std::uint32_t", true);
            case TypeKind::Uint64: return wrap_param("std::uint64_t", true);
            case TypeKind::Isize: return wrap_param("std::ptrdiff_t", true);
            case TypeKind::Usize: return wrap_param("std::size_t", true);
            case TypeKind::Float32: return wrap_param("float", true);
            case TypeKind::Float64: return wrap_param("double", true);
            case TypeKind::Str:
                if (ctx == LowerContext::ParamIn) return std::string("cfire::str_ref");
                return wrap_param("std::string", false);
            case TypeKind::Borrow: {
                if (d.elem_type == TypeId::Str()) {
                    if (ctx == LowerContext::Return) return std::string("std::string");
                    return std::string("cfire::str_ref");
                }
                const auto& ed = cf_.type_table.get(d.elem_type);
                if (ed.kind == TypeKind::Slice) {
                    auto elem = lower_type_id(ed.elem_type, LowerContext::Return);
                    if (!elem) return std::nullopt;
                    return std::format("std::span<const {}>", *elem);
                }
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return std::format("const {}&", *elem);
            }
            case TypeKind::MutBorrow: {
                if (d.elem_type == TypeId::Str()) return std::string("std::string&");
                const auto& ed = cf_.type_table.get(d.elem_type);
                if (ed.kind == TypeKind::Slice) {
                    auto elem = lower_type_id(ed.elem_type, LowerContext::Return);
                    if (!elem) return std::nullopt;
                    return std::format("std::span<{}>", *elem);
                }
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return std::format("{}&", *elem);
            }
            case TypeKind::RawPtr: {
                const auto& ed = cf_.type_table.get(d.elem_type);
                if (ed.name == "c_void") return std::string("const void*");
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return std::format("const {}*", *elem);
            }
            case TypeKind::MutPtr: {
                const auto& ed = cf_.type_table.get(d.elem_type);
                if (ed.name == "c_void") return std::string("void*");
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return std::format("{}*", *elem);
            }
            case TypeKind::Own: {
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return wrap_param(std::format("std::unique_ptr<{}>", *elem), false);
            }
            case TypeKind::Shared: {
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return wrap_param(std::format("std::shared_ptr<{}>", *elem), false);
            }
            case TypeKind::Slice: {
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                if (ctx == LowerContext::ParamIn && *elem != "bool" && !cf_.type_table.contains_type_param(d.elem_type)) {
                    return std::format("cfire::slice_ref<{}>", *elem);
                }
                return wrap_param(std::format("std::vector<{}>", *elem), false);
            }
            case TypeKind::Array: {
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return wrap_param(std::format("std::array<{}, {}>", *elem, d.array_len), false);
            }
            case TypeKind::Map: {
                auto k = lower_type_id(d.key_type, LowerContext::Return);
                auto v = lower_type_id(d.elem_type, LowerContext::Return);
                if (!k || !v) return std::nullopt;
                return wrap_param(std::format("cfire::Map<{}, {}>", *k, *v), false);
            }
            case TypeKind::Optional: {
                std::optional<std::string> elem;
                const auto& ed = cf_.type_table.get(d.elem_type);
                if (ed.kind == TypeKind::Borrow && ed.elem_type == TypeId::Str()) {
                    elem = std::string("std::string");
                } else {
                    elem = lower_type_id(d.elem_type, LowerContext::Return);
                }
                if (!elem) return std::nullopt;
                return wrap_param(std::format("std::optional<{}>", *elem), is_scalar_prim(d.elem_type));
            }
            case TypeKind::Fallible: {
                auto elem = lower_type_id(d.elem_type, LowerContext::Return);
                if (!elem) return std::nullopt;
                return wrap_param(std::format("std::expected<{}, cfire::Error>", *elem), is_scalar_prim(d.elem_type));
            }
            case TypeKind::Tuple: {
                if (d.param_types.empty()) return std::nullopt;
                std::string inner;
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) inner += ", ";
                    auto pt = lower_type_id(d.param_types[i], LowerContext::Return);
                    if (!pt) return std::nullopt;
                    inner += *pt;
                }
                return wrap_param(std::format("std::tuple<{}>", inner), false);
            }
            case TypeKind::Function: {
                auto ret = lower_type_id(d.ret_type, LowerContext::Return);
                if (!ret) return std::nullopt;
                std::string params;
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) params += ", ";
                    auto pt = lower_type_id(d.param_types[i], LowerContext::ParamIn);
                    if (!pt) return std::nullopt;
                    params += *pt;
                }
                return wrap_param(std::format("std::function<{}({})>", *ret, params), false);
            }
            case TypeKind::GenericInst: {
                std::string base = lower_named_cpp_type(d.base_name, !d.type_args.empty());
                if (!base.starts_with("cfire::") && !is_local_type(base) &&
                    !cf_.struct_defs.contains(base) && !cf_.enums.contains(base) &&
                    !cf_.declared_type_names.contains(base)) {
                    return std::nullopt;
                }
                if (d.type_args.empty()) return wrap_param(base, false);
                std::string args;
                for (std::size_t i = 0; i < d.type_args.size(); ++i) {
                    if (i > 0) args += ", ";
                    auto at = lower_type_id(d.type_args[i], LowerContext::Return);
                    if (!at) return std::nullopt;
                    args += *at;
                }
                return wrap_param(std::format("{}<{}>", base, args), false);
            }
            case TypeKind::NominalStruct:
            case TypeKind::NominalEnum: {
                if (d.name.empty() || d.name == "any" || d.name == "Self" || d.name == "Fn" ||
                    d.name == "unknown" || d.name == "<error>" || d.name == "<foreign>") {
                    return std::nullopt;
                }
                if (d.name == "c_void") {
                    return ctx == LowerContext::Return ? std::optional<std::string>{"void"} : std::nullopt;
                }
                if (d.name == "c_char" || d.name == "char") return wrap_param("char", true);
                if (d.name == "path") {
                    if (ctx == LowerContext::ParamIn) return std::string("const std::filesystem::path&");
                    return wrap_param("std::filesystem::path", false);
                }
                if (d.name == "Error" && !is_local_type("Error")) return wrap_param("cfire::Error", false);
                std::string mapped = lower_named_cpp_type(d.name, false);
                if (auto sdit = cf_.struct_defs.find(mapped);
                    sdit != cf_.struct_defs.end() && !sdit->second.type_params.empty()) {
                    return std::nullopt;
                }
                if (auto edit = cf_.enums.find(mapped);
                    edit != cf_.enums.end() && !edit->second.type_params.empty()) {
                    return std::nullopt;
                }
                if (!mapped.starts_with("cfire::") && !is_local_type(mapped) &&
                    !cf_.struct_defs.contains(mapped) && !cf_.enums.contains(mapped) &&
                    !cf_.declared_type_names.contains(mapped)) {
                    return std::nullopt;
                }
                return wrap_param(mapped, false);
            }
        }
        return std::nullopt;
    }

    CFIRE_CG_INLINE auto Emitter::lower_raw_owning_type(const TypeRef& tr) const -> std::string {
        if (tr.is_mut_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<{}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string&";
            return (tr.elem_type ? lower_owning_type(*tr.elem_type) : tr.name) + "&";
        }
        if (tr.is_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<const {}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "cfire::str_ref";
            return "const " + (tr.elem_type ? lower_owning_type(*tr.elem_type) : tr.name) + "&";
        }
        if (tr.smart_wrap == "own" && tr.elem_type) {
            return std::format("std::unique_ptr<{}>", lower_owning_type(*tr.elem_type));
        }
        if (tr.smart_wrap == "shared" && tr.elem_type) {
            return std::format("std::shared_ptr<{}>", lower_owning_type(*tr.elem_type));
        }
        if (tr.is_tuple) {
            std::string s = "std::tuple<";
            for (std::size_t i = 0; i < tr.tuple_types.size(); ++i) {
                if (i > 0) s += ", ";
                s += lower_owning_type(tr.tuple_types[i]);
            }
            s += ">";
            return s;
        }
        if (tr.is_fn) {
            std::string ret_s = tr.fn_ret ? lower_return_type(*tr.fn_ret) : "void";
            std::string params_s;
            for (std::size_t i = 0; i < tr.fn_params.size(); ++i) {
                if (i > 0) params_s += ", ";
                params_s += lower_param_type("in", tr.fn_params[i]);
            }
            return std::format("std::function<{}({})>", ret_s, params_s);
        }
        if (tr.is_mut_ptr && tr.elem_type) return lower_owning_type(*tr.elem_type) + "*";
        if (tr.is_ptr && tr.elem_type) return "const " + lower_owning_type(*tr.elem_type) + "*";
        if (tr.is_map && tr.key_type && tr.elem_type) {
            return std::format("cfire::Map<{}, {}>", lower_owning_type(*tr.key_type), lower_owning_type(*tr.elem_type));
        }
        if (tr.is_array && tr.elem_type) {
            return std::format("std::array<{}, {}>", lower_owning_type(*tr.elem_type), lower_cpp_int_literal(tr.array_len));
        }
        if (tr.is_slice && tr.elem_type) {
            return std::format("std::vector<{}>", lower_owning_type(*tr.elem_type));
        }
        if (tr.name == "str") return "std::string";
        if (tr.name == "path") return "std::filesystem::path";
        if (tr.name == "i8") return "std::int8_t";
        if (tr.name == "i16") return "std::int16_t";
        if (tr.name == "i32") return "std::int32_t";
        if (tr.name == "i64") return "std::int64_t";
        if (tr.name == "u8") return "std::uint8_t";
        if (tr.name == "byte") return "std::uint8_t";  // `byte` is `u8`
        if (tr.name == "c_char" || tr.name == "char") return "char";
        if (tr.name == "u16") return "std::uint16_t";
        if (tr.name == "u32") return "std::uint32_t";
        if (tr.name == "u64") return "std::uint64_t";
        if (tr.name == "usize") return "std::size_t";
        if (tr.name == "isize") return "std::ptrdiff_t";
        if (tr.name == "f32") return "float";
        if (tr.name == "f64") return "double";
        if (tr.name == "bool") return "bool";
        if (tr.name == "void") return "void";
        std::string clean = lower_named_cpp_type(tr.name, !tr.type_args.empty());
        if (!tr.type_args.empty()) {
            std::string args;
            for (std::size_t i = 0; i < tr.type_args.size(); ++i) {
                if (i > 0) args += ", ";
                args += lower_owning_type(tr.type_args[i]);
            }
            return std::format("{}<{}>", clean, args);
        }
        if (tr.elem_type) {
            return std::format("{}<{}>", clean, lower_owning_type(*tr.elem_type));
        }
        return clean;
    }

} // namespace cfire::frontend
