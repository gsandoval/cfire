#pragma once
// src/codegen/test_runner.hpp — Test, Fuzz & Benchmark Runner Code Generation

namespace cfire::frontend {

namespace {
inline void emit_auto_json_and_main_harness(
    Emitter& em,
    const FileAST& ast,
    const std::vector<Emitter::ShardedTestEntry>& entries,
    const std::vector<std::pair<std::string, int>>& auto_json_structs
) {
    for (std::size_t j = 0; j < auto_json_structs.size(); ++j) {
        const auto& [sname, sline] = auto_json_structs[j];
        em.writeln(std::format("inline void __cfire_auto_json_{}(std::uint64_t seed) {{", j));
        ++em.indent_;
        em.writeln("cfire::testing_detail::FuzzRng rng(seed);");
        em.writeln("for (std::size_t iter = 0; iter < 16; ++iter) {");
        ++em.indent_;
        em.writeln(std::format("auto val = cfire::testing_detail::Arbitrary<{}>::generate(rng, iter);", sname));
        em.writeln("std::string encoded = cfire::json::stringify(val);");
        em.writeln(std::format("auto decoded = cfire::json::parse<{}>(encoded);", sname));
        em.writeln(std::format("cfire::assert_true_at(decoded.has_value(), \"auto JSON round-trip parse failed for '{}'\", \"{}\", {}, \"json.parse(json.stringify(val))\");",
            sname, Emitter::escape_line_path(em.source_path_), sline));
        em.writeln("if (decoded.has_value()) {");
        ++em.indent_;
        em.writeln("std::string reencoded = cfire::json::stringify(*decoded);");
        em.writeln(std::format("cfire::assert_binop_at(reencoded, encoded, [](const auto& _l, const auto& _r) {{ return _l == _r; }}, \"==\", \"auto JSON round-trip mismatch for '{}'\", \"{}\", {}, \"json.stringify(decoded) == json.stringify(val)\");",
            sname, Emitter::escape_line_path(em.source_path_), sline));
        --em.indent_;
        em.writeln("}");
        --em.indent_;
        em.writeln("}");
        em.writeln(std::format("auto bad = cfire::json::parse<{}>(\"{{malformed_json\");", sname));
        em.writeln(std::format("cfire::assert_true_at(!bad.has_value(), \"expected malformed JSON to return Err for '{}'\", \"{}\", {}, \"!json.parse(\\\"{{malformed\\\").has_value()\");",
            sname, Emitter::escape_line_path(em.source_path_), sline));
        --em.indent_;
        em.writeln("}");
        em.writeln();
    }

    em.writeln("auto main(int argc, char** argv) -> int {");
    ++em.indent_;
    em.writeln("cfire::detail::set_process_args(argc, argv);");
    em.writeln("bool json_mode = false;");
    em.writeln("bool bench_mode = false;");
    em.writeln("double bench_time_ms = 200.0;");
    em.writeln("std::size_t fuzz_runs = 100;");
    em.writeln("std::uint64_t fuzz_seed = 0x9E3779B97F4A7C15ULL;");
    em.writeln("std::string filter;");
    em.writeln("for (int i = 1; i < argc; ++i) {");
    em.writeln("    std::string_view a = argv[i];");
    em.writeln("    if (a == \"--cfire-test-json\") json_mode = true;");
    em.writeln("    else if (a == \"--cfire-bench\") bench_mode = true;");
    em.writeln("    else if (a.starts_with(\"--cfire-test-filter=\")) filter = a.substr(20);");
    em.writeln("    else if (a.starts_with(\"--cfire-fuzz-runs=\")) {");
    em.writeln("        std::size_t v = 100;");
    em.writeln("        auto sv = a.substr(18);");
    em.writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), v);");
    em.writeln("        if (v > 0) fuzz_runs = v;");
    em.writeln("    } else if (a.starts_with(\"--cfire-seed=\")) {");
    em.writeln("        std::uint64_t s = 0;");
    em.writeln("        auto sv = a.substr(13);");
    em.writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), s);");
    em.writeln("        if (s > 0) fuzz_seed = s;");
    em.writeln("    } else if (a.starts_with(\"--cfire-bench-time=\")) {");
    em.writeln("        std::uint64_t ms = 200;");
    em.writeln("        auto sv = a.substr(19);");
    em.writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), ms);");
    em.writeln("        if (ms > 0) bench_time_ms = static_cast<double>(ms);");
    em.writeln("    }");
    em.writeln("}");
    em.writeln("(void)bench_mode; (void)bench_time_ms; (void)fuzz_runs; (void)fuzz_seed;");
    em.writeln("cfire::testing_detail::in_test_harness = true;");
    em.writeln("cfire::testing_detail::install_crash_handlers();");
    if (em.used_core_.contains("sqlite")) {
        em.writeln("#if defined(CFIRE_USE_SQLITE)");
        em.writeln("{ unsigned char _sq_warm = 0; sqlite3_randomness(1, &_sq_warm); }");
        em.writeln("#endif");
    }
    if (em.used_core_.contains("sodium")) {
        em.writeln("(void)cfire::sodium::detail::ensure_init();");
    }
    bool has_cpp_import = false;
    for (const auto& imp : ast.imports) {
        if (imp.is_cpp) {
            has_cpp_import = true;
            break;
        }
    }
    em.writeln("int passed_count = 0;");
    em.writeln("int failed_count = 0;");
    em.writeln("struct TestResultRecord { std::string name; bool passed; double duration_ms; std::string file; int line; std::string error; };");
    em.writeln("std::vector<TestResultRecord> records;");
    em.writeln("std::vector<cfire::testing_detail::BenchStats> bench_records;");
    for (const auto& entry : entries) {
        const auto& td = ast.tests[entry.ast_test_idx];
        const std::string& sfx = entry.sym_suffix;
        std::string t_name = Emitter::escape_line_path(td.name);
        if (td.is_bench) {
            em.writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
            ++em.indent_;
            em.writeln(std::format("if (!json_mode) std::println(\"=== BENCH {}\");", t_name));
            em.writeln(std::format("cfire::testing_detail::begin_test(\"{}\", \"{}\", {});", t_name,
                Emitter::escape_line_path(td.source_path.empty() ? em.source_path_ : td.source_path), td.line));
            em.writeln("cfire::testing_detail::current_test_failed = false;");
            em.writeln("cfire::testing_detail::failure_file.clear();");
            em.writeln("cfire::testing_detail::failure_line = 0;");
            em.writeln("cfire::testing_detail::failure_expr.clear();");
            em.writeln("cfire::testing_detail::failure_msg.clear();");
            em.writeln("auto start_t = std::chrono::steady_clock::now();");
            em.writeln(std::format("(void)cfire::testing_detail::run_test_guarded([&] {{ __cfire_bench_runner_{}(!bench_mode, bench_time_ms, json_mode, bench_records); }});", sfx));
            em.writeln("auto end_t = std::chrono::steady_clock::now();");
            em.writeln("cfire::testing_detail::end_test();");
            em.writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
            em.writeln("if (!cfire::testing_detail::current_test_failed) {");
            em.writeln("    passed_count++;");
            em.writeln(std::format("    if (!json_mode && !bench_mode) std::println(\"--- PASS: {}\");", t_name));
            em.writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
            em.writeln("} else {");
            em.writeln("    failed_count++;");
            em.writeln("    std::string fail_detail = cfire::testing_detail::failure_msg.empty() ? cfire::testing_detail::failure_expr : cfire::testing_detail::failure_msg;");
            em.writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
            em.writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", t_name));
            em.writeln("}");
            --em.indent_;
            em.writeln("}");
            continue;
        }

        em.writeln("if (!bench_mode) {");
        ++em.indent_;
        em.writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
        ++em.indent_;
        em.writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", t_name));
        em.writeln(std::format("cfire::testing_detail::begin_test(\"{}\", \"{}\", {});", t_name,
            Emitter::escape_line_path(td.source_path.empty() ? em.source_path_ : td.source_path), td.line));
        em.writeln("cfire::testing_detail::current_test_failed = false;");
        em.writeln("cfire::testing_detail::failure_file.clear();");
        em.writeln("cfire::testing_detail::failure_line = 0;");
        em.writeln("cfire::testing_detail::failure_expr.clear();");
        em.writeln("cfire::testing_detail::failure_msg.clear();");
        bool check_fd_leak = !has_cpp_import &&
                             !Emitter::stmts_reference_ident(td.body, "InitWindow") &&
                             !Emitter::stmts_reference_ident(td.body, "run_webview_test") &&
                             !Emitter::stmts_reference_ident(td.body, "keychain");
        if (check_fd_leak) {
            for (const auto& fn : ast.functions) {
                if (!fn.receiver && Emitter::stmts_reference_ident(td.body, fn.name)) {
                    if (Emitter::stmts_reference_ident(fn.body, "InitWindow") ||
                        Emitter::stmts_reference_ident(fn.body, "run_webview_test") ||
                        Emitter::stmts_reference_ident(fn.body, "keychain")) {
                        check_fd_leak = false;
                        break;
                    }
                }
            }
        }
        if (check_fd_leak) {
            em.writeln("const std::size_t fds_before = cfire::testing_detail::count_open_fds();");
        }
        em.writeln("auto start_t = std::chrono::steady_clock::now();");
        if (td.is_fuzz || !td.params.empty()) {
            em.writeln(std::format("__cfire_fuzz_runner_{}(fuzz_runs, fuzz_seed);", sfx));
        } else {
            em.writeln(std::format("(void)cfire::testing_detail::run_test_guarded([&] {{ __cfire_test_{}(); }});", sfx));
        }
        em.writeln("auto end_t = std::chrono::steady_clock::now();");
        em.writeln("cfire::testing_detail::end_test();");
        if (check_fd_leak) {
            std::string td_file = Emitter::escape_line_path(td.source_path.empty() ? em.source_path_ : td.source_path);
            em.writeln("const std::size_t fds_after = cfire::testing_detail::count_open_fds();");
            em.writeln("if (!cfire::testing_detail::current_test_failed && fds_before > 0 && fds_after > fds_before) {");
            em.writeln("    cfire::testing_detail::current_test_failed = true;");
            em.writeln(std::format("    cfire::testing_detail::failure_file = \"{}\";", td_file));
            em.writeln(std::format("    cfire::testing_detail::failure_line = {};", td.line));
            em.writeln("    cfire::testing_detail::failure_expr = \"resource leak canary\";");
            em.writeln("    cfire::testing_detail::failure_msg = std::format(\"test leaked {} open file descriptor(s) (missing 'defer file.close()' or 'defer db.close()'?)\", fds_after - fds_before);");
            em.writeln("}");
        }
        em.writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
        em.writeln("bool test_pass = !cfire::testing_detail::current_test_failed;");
        em.writeln("if (test_pass) {");
        em.writeln("    passed_count++;");
        em.writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", t_name));
        em.writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
        em.writeln("} else {");
        em.writeln("    failed_count++;");
        em.writeln("    std::string fail_detail;");
        em.writeln("    if (!cfire::testing_detail::failure_expr.empty() && !cfire::testing_detail::failure_msg.empty()) {");
        em.writeln("        fail_detail = cfire::testing_detail::failure_expr + \" (\" + cfire::testing_detail::failure_msg + \")\";");
        em.writeln("    } else if (!cfire::testing_detail::failure_expr.empty()) {");
        em.writeln("        fail_detail = cfire::testing_detail::failure_expr;");
        em.writeln("    } else {");
        em.writeln("        fail_detail = cfire::testing_detail::failure_msg;");
        em.writeln("    }");
        em.writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
        em.writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", t_name));
        em.writeln("}");
        --em.indent_;
        em.writeln("}");
        --em.indent_;
        em.writeln("}");
    }
    for (std::size_t j = 0; j < auto_json_structs.size(); ++j) {
        const auto& [sname, sline] = auto_json_structs[j];
        std::string auto_name = std::format("[auto] json round-trip: {}", sname);
        em.writeln("if (!bench_mode) {");
        ++em.indent_;
        em.writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", auto_name));
        ++em.indent_;
        em.writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", auto_name));
        em.writeln(std::format("cfire::testing_detail::begin_test(\"{}\", \"{}\", {});", auto_name,
            Emitter::escape_line_path(em.source_path_), sline));
        em.writeln("cfire::testing_detail::current_test_failed = false;");
        em.writeln("cfire::testing_detail::failure_file.clear();");
        em.writeln("cfire::testing_detail::failure_line = 0;");
        em.writeln("cfire::testing_detail::failure_expr.clear();");
        em.writeln("cfire::testing_detail::failure_msg.clear();");
        em.writeln("auto start_t = std::chrono::steady_clock::now();");
        em.writeln(std::format("(void)cfire::testing_detail::run_test_guarded([&] {{ __cfire_auto_json_{}(fuzz_seed); }});", j));
        em.writeln("auto end_t = std::chrono::steady_clock::now();");
        em.writeln("cfire::testing_detail::end_test();");
        em.writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
        em.writeln("if (!cfire::testing_detail::current_test_failed) {");
        em.writeln("    passed_count++;");
        em.writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", auto_name));
        em.writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", auto_name));
        em.writeln("} else {");
        em.writeln("    failed_count++;");
        em.writeln("    std::string fail_detail = cfire::testing_detail::failure_expr + \" (\" + cfire::testing_detail::failure_msg + \")\";");
        em.writeln("    if (!json_mode) std::println(\"--- FAIL: " + auto_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
        em.writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", auto_name));
        em.writeln("}");
        --em.indent_;
        em.writeln("}");
        --em.indent_;
        em.writeln("}");
    }
    em.writeln("if (json_mode) {");
    em.writeln("    auto escape_json = [](std::string_view s) -> std::string {");
    em.writeln("        std::string out;");
    em.writeln("        for (char c : s) {");
    em.writeln("            if (c == '\"') out += \"\\\\\\\"\";");
    em.writeln("            else if (c == '\\\\') out += \"\\\\\\\\\";");
    em.writeln("            else if (c == '\\n') out += \"\\\\n\";");
    em.writeln("            else out += c;");
    em.writeln("        }");
    em.writeln("        return out;");
    em.writeln("    };");
    em.writeln("    std::print(\"{{\\\"success\\\": {}, \\\"passed\\\": {}, \\\"failed\\\": {}, \\\"tests\\\": [\", (failed_count == 0 ? \"true\" : \"false\"), passed_count, failed_count);");
    em.writeln("    for (std::size_t r = 0; r < records.size(); ++r) {");
    em.writeln("        if (r > 0) std::print(\", \");");
    em.writeln("        const auto& rec = records[r];");
    em.writeln("        std::print(\"{{\\\"name\\\": \\\"{}\\\", \\\"status\\\": \\\"{}\\\", \\\"duration_ms\\\": {:.2f}, \\\"file\\\": \\\"{}\\\", \\\"line\\\": {}, \\\"error\\\": \\\"{}\\\"}}\", escape_json(rec.name), (rec.passed ? \"pass\" : \"fail\"), rec.duration_ms, escape_json(rec.file), rec.line, escape_json(rec.error));");
    em.writeln("    }");
    em.writeln("    std::print(\"], \\\"benchmarks\\\": [\");");
    em.writeln("    for (std::size_t b = 0; b < bench_records.size(); ++b) {");
    em.writeln("        if (b > 0) std::print(\", \");");
    em.writeln("        const auto& br = bench_records[b];");
    em.writeln("        std::print(\"{{\\\"name\\\": \\\"{}\\\", \\\"iters\\\": {}, \\\"min_ns\\\": {:.2f}, \\\"p50_ns\\\": {:.2f}, \\\"mean_ns\\\": {:.2f}, \\\"p99_ns\\\": {:.2f}, \\\"stddev_pct\\\": {:.2f}, \\\"ops_per_sec\\\": {:.1f}}}\", escape_json(br.name), br.total_iters, br.min_ns, br.p50_ns, br.mean_ns, br.p99_ns, br.stddev_pct, br.ops_per_sec);");
    em.writeln("    }");
    em.writeln("    std::println(\"]}}\");");
    em.writeln("} else {");
    em.writeln("    if (failed_count == 0) {");
    em.writeln("        std::println(\"PASS ({} cfire tests succeeded)\", passed_count);");
    em.writeln("    } else {");
    em.writeln("        std::println(\"FAIL ({} of {} cfire tests failed)\", failed_count, (passed_count + failed_count));");
    em.writeln("    }");
    em.writeln("}");
    em.writeln("return failed_count > 0 ? 1 : 0;");
    --em.indent_;
    em.writeln("}");
}
} // namespace

CFIRE_CG_INLINE auto Emitter::collect_auto_json_structs(const FileAST& ast) const
    -> std::vector<std::pair<std::string, int>> {
    std::vector<std::pair<std::string, int>> auto_json_structs;
    if (test_mode_ && used_core_.contains("json")) {
        for (const auto& st : ast.structs) {
            auto it = cf_.struct_tags.find(st.name);
            if (it == cf_.struct_tags.end()) continue;
            bool has_json_or_desc = false;
            for (const auto& t : it->second) {
                if (!t.json_key.empty() || t.custom_tags.contains("desc") || t.custom_tags.contains("validate")) {
                    has_json_or_desc = true;
                    break;
                }
            }
            if (!has_json_or_desc) continue;
            bool supported = st.type_params.empty() && json_support::can_emit_json_struct(cf_, st);
            if (supported) {
                for (const auto& fld : st.fields) {
                    if (is_non_value_field_type(fld.type, 0)) {
                        supported = false;
                        break;
                    }
                }
            }
            if (supported) {
                auto_json_structs.emplace_back(st.name, st.line);
            }
        }
    }
    return auto_json_structs;
}

CFIRE_CG_INLINE void Emitter::emit_test_runner(const FileAST& ast, bool has_user_main) {
    auto auto_json_structs = collect_auto_json_structs(ast);

    std::vector<ShardedTestEntry> entries;
    entries.reserve(ast.tests.size());
    for (std::size_t i = 0; i < ast.tests.size(); ++i) {
        std::string sfx = std::to_string(i);
        emit_test_decl(ast.tests[i], sfx, false);
        writeln();
        entries.push_back(ShardedTestEntry{i, std::move(sfx)});
    }

    if ((test_mode_ || !has_user_main) && (!entries.empty() || !auto_json_structs.empty())) {
        emit_auto_json_and_main_harness(*this, ast, entries, auto_json_structs);
    }
}

CFIRE_CG_INLINE void Emitter::emit_sharded_test_main(
    const FileAST& ast,
    bool has_user_main,
    const std::vector<ShardedTestEntry>& entries
) {
    auto auto_json_structs = collect_auto_json_structs(ast);
    if (!((test_mode_ || !has_user_main) && (!entries.empty() || !auto_json_structs.empty()))) {
        return;
    }

    for (const auto& entry : entries) {
        const auto& td = ast.tests[entry.ast_test_idx];
        const std::string& sfx = entry.sym_suffix;
        if (td.is_bench) {
            writeln(std::format("void __cfire_bench_runner_{}(bool smoke_only, double budget_ms, bool json_mode, std::vector<cfire::testing_detail::BenchStats>& out_stats);", sfx));
        } else if (td.is_fuzz || !td.params.empty()) {
            writeln(std::format("void __cfire_fuzz_runner_{}(std::size_t runs, std::uint64_t seed);", sfx));
        } else {
            writeln(std::format("void __cfire_test_{}();", sfx));
        }
    }
    if (!entries.empty()) writeln();

    emit_auto_json_and_main_harness(*this, ast, entries, auto_json_structs);
}

CFIRE_CG_INLINE void Emitter::emit_test_decl(const TestDecl& td, std::size_t idx) {
    emit_test_decl(td, std::to_string(idx), false);
}

CFIRE_CG_INLINE void Emitter::emit_test_decl(const TestDecl& td, const std::string& sym_suffix, bool non_inline) {
    struct PkgGuard {
        std::string& slot;
        std::string prev;
        explicit PkgGuard(std::string& s, const std::string& next) : slot(s), prev(s) { slot = next; }
        ~PkgGuard() { slot = prev; }
    } pkg_guard(cur_pkg_, td.package_name);
    set_source_path(td.source_path);
    if (td.line > 0) emit_line_directive(td.line);
    cur_fn_ = std::format("__cfire_test_{}", sym_suffix);
    defer_count_ = 0;
    in_test_ = true;
    in_fallible_void_ = false;
    local_vars_.clear();
    error_vars_.clear();
    ptr_vars_.clear();
    smart_ptr_vars_.clear();
    map_vars_.clear();
    fixed_array_vars_.clear();

    std::string param_sig;
    for (std::size_t i = 0; i < td.params.size(); ++i) {
        if (!param_sig.empty()) param_sig += ", ";
        param_sig += std::format("{} {}", lower_param_type("in", td.params[i].type), td.params[i].name);
        local_vars_.insert(td.params[i].name);
    }
    for (std::size_t i = 0; i < td.bench_variations.size(); ++i) {
        if (!param_sig.empty()) param_sig += ", ";
        param_sig += std::format("{} {}", lower_param_type("in", td.bench_variations[i].inferred_type), td.bench_variations[i].name);
        local_vars_.insert(td.bench_variations[i].name);
    }

    const bool has_external_runner = td.is_bench || td.is_fuzz || !td.params.empty();
    const std::string body_fn_qual = non_inline ? (has_external_runner ? "static " : "") : "inline ";
    const std::string runner_qual = non_inline ? "" : "inline ";

    writeln(std::format("{}void {}({}) {{", body_fn_qual, cur_fn_, param_sig));
    ++indent_;
    for (const auto& st : td.body) emit_stmt(st, false);
    --indent_;
    writeln("}");
    in_test_ = false;
    local_vars_.clear();
    error_vars_.clear();
    ptr_vars_.clear();
    map_vars_.clear();
    fixed_array_vars_.clear();

    if (td.is_bench) {
        writeln(std::format("{}void __cfire_bench_runner_{}(bool smoke_only, double budget_ms, bool json_mode, std::vector<cfire::testing_detail::BenchStats>& out_stats) {{", runner_qual, sym_suffix));
        ++indent_;
        std::string base_name = escape_line_path(td.name);
        if (!td.bench_variations.empty()) {
            for (std::size_t v = 0; v < td.bench_variations.size(); ++v) {
                const auto& bv = td.bench_variations[v];
                std::string elem_t = lower_owning_type(bv.inferred_type);
                std::string vals;
                for (std::size_t k = 0; k < bv.values.size(); ++k) {
                    if (k > 0) vals += ", ";
                    vals += emit_expr(bv.values[k]);
                }
                writeln(std::format("for (const {}& _bv_{} : std::vector<{}>{{{}}}) {{", elem_t, v, elem_t, vals));
                ++indent_;
            }
            std::string label_fmt = base_name;
            std::string label_args;
            std::string call_args;
            for (std::size_t v = 0; v < td.bench_variations.size(); ++v) {
                label_fmt += std::format("/{}={{}}", td.bench_variations[v].name);
                if (!label_args.empty()) label_args += ", ";
                label_args += std::format("cfire::testing_detail::format_fuzz_arg(_bv_{})", v);
                if (!call_args.empty()) call_args += ", ";
                call_args += std::format("_bv_{}", v);
            }
            writeln(std::format("std::string _bname = std::format(\"{}\", {});", label_fmt, label_args));
            writeln(std::format("auto _st = cfire::testing_detail::run_benchmark(_bname, [&]() {{ {}({}); }}, budget_ms, smoke_only);", cur_fn_, call_args));
            writeln("out_stats.push_back(_st);");
            writeln("if (!smoke_only && !json_mode) cfire::testing_detail::print_bench_row(_st);");
            for (std::size_t v = 0; v < td.bench_variations.size(); ++v) {
                --indent_;
                writeln("}");
            }
        } else if (!td.params.empty()) {
            for (std::size_t p = 0; p < td.params.size(); ++p) {
                std::string elem_t = lower_owning_type(td.params[p].type);
                if (td.params[p].type.name == "str") {
                    writeln(std::format("for (const std::string& _bp_{} : std::vector<std::string>{{\"tiny\", \"medium_payload_string_64_bytes\", std::string(512, 'x')}}) {{", p));
                } else {
                    writeln(std::format("for ({} _bp_{} : std::vector<{}>{{8, 64, 512, 4096}}) {{", elem_t, p, elem_t));
                }
                ++indent_;
            }
            std::string label_fmt = base_name;
            std::string label_args;
            std::string call_args;
            for (std::size_t p = 0; p < td.params.size(); ++p) {
                label_fmt += std::format("/{}={{}}", td.params[p].name);
                if (!label_args.empty()) label_args += ", ";
                label_args += std::format("cfire::testing_detail::format_fuzz_arg(_bp_{})", p);
                if (!call_args.empty()) call_args += ", ";
                call_args += std::format("_bp_{}", p);
            }
            writeln(std::format("std::string _bname = std::format(\"{}\", {});", label_fmt, label_args));
            writeln(std::format("auto _st = cfire::testing_detail::run_benchmark(_bname, [&]() {{ {}({}); }}, budget_ms, smoke_only);", cur_fn_, call_args));
            writeln("out_stats.push_back(_st);");
            writeln("if (!smoke_only && !json_mode) cfire::testing_detail::print_bench_row(_st);");
            for (std::size_t p = 0; p < td.params.size(); ++p) {
                --indent_;
                writeln("}");
            }
        } else {
            writeln(std::format("auto _st = cfire::testing_detail::run_benchmark(\"{}\", [&]() {{ {}(); }}, budget_ms, smoke_only);", base_name, cur_fn_));
            writeln("out_stats.push_back(_st);");
            writeln("if (!smoke_only && !json_mode) cfire::testing_detail::print_bench_row(_st);");
        }
        --indent_;
        writeln("}");
        return;
    }

    if (td.is_fuzz || !td.params.empty()) {
        writeln(std::format("{}void __cfire_fuzz_runner_{}(std::size_t runs, std::uint64_t seed) {{", runner_qual, sym_suffix));
        ++indent_;
        writeln("cfire::testing_detail::FuzzRng rng(seed);");
        writeln("for (std::size_t iter = 0; iter < runs; ++iter) {");
        ++indent_;
        std::string call_args;
        for (std::size_t p = 0; p < td.params.size(); ++p) {
            std::string p_cpp = lower_owning_type(td.params[p].type);
            writeln(std::format("{} arg_{} = cfire::testing_detail::Arbitrary<{}>::generate(rng, iter);", p_cpp, p, p_cpp));
            if (p > 0) call_args += ", ";
            call_args += std::format("arg_{}", p);
        }
        writeln(std::format("if (cfire::testing_detail::run_test_guarded([&] {{ {}({}); }})) {{", cur_fn_, call_args));
        writeln("} else {");
        ++indent_;
        writeln("std::string orig_file = cfire::testing_detail::failure_file;");
        writeln("int orig_line = cfire::testing_detail::failure_line;");
        writeln("std::string orig_expr = cfire::testing_detail::failure_expr;");
        writeln("std::string orig_msg = cfire::testing_detail::failure_msg;");
        if (!td.params.empty()) {
            writeln("for (int shrink_round = 0; shrink_round < 128; ++shrink_round) {");
            ++indent_;
            writeln("bool shrunk_any = false;");
            for (std::size_t p = 0; p < td.params.size(); ++p) {
                writeln(std::format("auto cands_{} = cfire::testing_detail::shrink_candidates(arg_{});", p, p));
                writeln(std::format("auto saved_{} = arg_{};", p, p));
                writeln(std::format("for (const auto& cand : cands_{}) {{", p));
                ++indent_;
                writeln(std::format("arg_{} = cand;", p));
                writeln("cfire::testing_detail::current_test_failed = false;");
                writeln(std::format("if (cfire::testing_detail::run_test_guarded([&] {{ {}({}); }})) {{", cur_fn_, call_args));
                writeln(std::format("    arg_{} = saved_{};", p, p));
                writeln("} else {");
                writeln("    orig_file = cfire::testing_detail::failure_file;");
                writeln("    orig_line = cfire::testing_detail::failure_line;");
                writeln("    orig_expr = cfire::testing_detail::failure_expr;");
                writeln("    orig_msg = cfire::testing_detail::failure_msg;");
                writeln("    shrunk_any = true;");
                writeln("    break;");
                writeln("}");
                --indent_;
                writeln("}");
            }
            writeln("if (!shrunk_any) break;");
            --indent_;
            writeln("}");
        }
        std::string ce_fmt;
        std::string ce_args;
        for (std::size_t p = 0; p < td.params.size(); ++p) {
            if (p > 0) {
                ce_fmt += ", ";
                ce_args += ", ";
            }
            ce_fmt += std::format("{}={{}}", td.params[p].name);
            ce_args += std::format("cfire::testing_detail::format_fuzz_arg(arg_{})", p);
        }
        writeln("cfire::testing_detail::current_test_failed = true;");
        writeln("cfire::testing_detail::failure_file = orig_file;");
        writeln("cfire::testing_detail::failure_line = orig_line;");
        writeln("cfire::testing_detail::failure_expr = orig_expr;");
        if (!td.params.empty()) {
            writeln(std::format("std::string ce_str = std::format(\"{}\", {});", ce_fmt, ce_args));
            writeln("cfire::testing_detail::failure_msg = std::format(\"{} [shrunk counterexample after {} run(s): ({}) | reproduce: --seed {}]\", orig_msg, iter + 1, ce_str, seed);");
        } else {
            writeln("cfire::testing_detail::failure_msg = std::format(\"{} [failed at run {} | reproduce: --seed {}]\", orig_msg, iter + 1, seed);");
        }
        writeln("return;");
        --indent_;
        writeln("}");
        --indent_;
        writeln("}");
        --indent_;
        writeln("}");
    }
}

} // namespace cfire::frontend
