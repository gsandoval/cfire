#pragma once
// src/codegen/test_runner.hpp — Test, Fuzz & Benchmark Runner Code Generation

namespace cfire::frontend {

inline void Emitter::emit_test_runner(const FileAST& ast, bool has_user_main) {
    std::vector<std::pair<std::string, int>> auto_json_structs;
    if (test_mode_ && used_core_.contains("json")) {
        for (const auto& st : ast.structs) {
            auto it = cf_.struct_tags.find(st.name);
            if (it == cf_.struct_tags.end()) continue;
            bool has_json_or_desc = false;
            for (const auto& t : it->second) {
                if (!t.json_key.empty() || t.custom_tags.contains("desc")) {
                    has_json_or_desc = true;
                    break;
                }
            }
            if (!has_json_or_desc) continue;
            bool supported = true;
            for (const auto& fld : st.fields) {
                if (fld.type.is_ptr || fld.type.is_mut_ptr || fld.type.is_borrow ||
                    fld.type.is_mut_borrow || fld.type.is_fn || !fld.type.smart_wrap.empty() ||
                    !can_emit_json_field_type(fld.type)) {
                    supported = false;
                    break;
                }
            }
            if (supported) {
                auto_json_structs.emplace_back(st.name, st.line);
            }
        }
    }

    for (std::size_t i = 0; i < ast.tests.size(); ++i) {
        emit_test_decl(ast.tests[i], i);
        writeln();
    }
    for (std::size_t j = 0; j < auto_json_structs.size(); ++j) {
        const auto& [sname, sline] = auto_json_structs[j];
        writeln(std::format("inline void __cfire_auto_json_{}(std::uint64_t seed) {{", j));
        ++indent_;
        writeln("cfire::testing_detail::FuzzRng rng(seed);");
        writeln("for (std::size_t iter = 0; iter < 16; ++iter) {");
        ++indent_;
        writeln(std::format("auto val = cfire::testing_detail::Arbitrary<{}>::generate(rng, iter);", sname));
        writeln("std::string encoded = cfire::json::stringify(val);");
        writeln(std::format("auto decoded = cfire::json::parse<{}>(encoded);", sname));
        writeln(std::format("cfire::assert_true_at(decoded.has_value(), \"auto JSON round-trip parse failed for '{}'\", \"{}\", {}, \"json.parse(json.stringify(val))\");",
            sname, escape_line_path(source_path_), sline));
        writeln("if (decoded.has_value()) {");
        ++indent_;
        writeln("std::string reencoded = cfire::json::stringify(*decoded);");
        writeln(std::format("cfire::assert_binop_at(reencoded, encoded, [](const auto& _l, const auto& _r) {{ return _l == _r; }}, \"==\", \"auto JSON round-trip mismatch for '{}'\", \"{}\", {}, \"json.stringify(decoded) == json.stringify(val)\");",
            sname, escape_line_path(source_path_), sline));
        --indent_;
        writeln("}");
        --indent_;
        writeln("}");
        writeln(std::format("auto bad = cfire::json::parse<{}>(\"{{malformed_json\");", sname));
        writeln(std::format("cfire::assert_true_at(!bad.has_value(), \"expected malformed JSON to return Err for '{}'\", \"{}\", {}, \"!json.parse(\\\"{{malformed\\\").has_value()\");",
            sname, escape_line_path(source_path_), sline));
        --indent_;
        writeln("}");
        writeln();
    }

    if ((test_mode_ || !has_user_main) && (!ast.tests.empty() || !auto_json_structs.empty())) {
        writeln("auto main(int argc, char** argv) -> int {");
        ++indent_;
        writeln("cfire::detail::set_process_args(argc, argv);");
        writeln("bool json_mode = false;");
        writeln("bool bench_mode = false;");
        writeln("double bench_time_ms = 200.0;");
        writeln("std::size_t fuzz_runs = 100;");
        writeln("std::uint64_t fuzz_seed = 0x9E3779B97F4A7C15ULL;");
        writeln("std::string filter;");
        writeln("for (int i = 1; i < argc; ++i) {");
        writeln("    std::string_view a = argv[i];");
        writeln("    if (a == \"--cfire-test-json\") json_mode = true;");
        writeln("    else if (a == \"--cfire-bench\") bench_mode = true;");
        writeln("    else if (a.starts_with(\"--cfire-test-filter=\")) filter = a.substr(20);");
        writeln("    else if (a.starts_with(\"--cfire-fuzz-runs=\")) {");
        writeln("        std::size_t v = 100;");
        writeln("        auto sv = a.substr(18);");
        writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), v);");
        writeln("        if (v > 0) fuzz_runs = v;");
        writeln("    } else if (a.starts_with(\"--cfire-seed=\")) {");
        writeln("        std::uint64_t s = 0;");
        writeln("        auto sv = a.substr(13);");
        writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), s);");
        writeln("        if (s > 0) fuzz_seed = s;");
        writeln("    } else if (a.starts_with(\"--cfire-bench-time=\")) {");
        writeln("        std::uint64_t ms = 200;");
        writeln("        auto sv = a.substr(19);");
        writeln("        std::from_chars(sv.data(), sv.data() + sv.size(), ms);");
        writeln("        if (ms > 0) bench_time_ms = static_cast<double>(ms);");
        writeln("    }");
        writeln("}");
        writeln("(void)bench_mode; (void)bench_time_ms; (void)fuzz_runs; (void)fuzz_seed;");
        writeln("cfire::testing_detail::in_test_harness = true;");
        if (used_core_.contains("sqlite")) {
            writeln("#if defined(CFIRE_USE_SQLITE)");
            writeln("{ unsigned char _sq_warm = 0; sqlite3_randomness(1, &_sq_warm); }");
            writeln("#endif");
        }
        if (used_core_.contains("sodium")) {
            writeln("(void)cfire::sodium::detail::ensure_init();");
        }
        bool has_cpp_import = false;
        for (const auto& imp : ast.imports) {
            if (imp.is_cpp) {
                has_cpp_import = true;
                break;
            }
        }
        writeln("int passed_count = 0;");
        writeln("int failed_count = 0;");
        writeln("struct TestResultRecord { std::string name; bool passed; double duration_ms; std::string file; int line; std::string error; };");
        writeln("std::vector<TestResultRecord> records;");
        writeln("std::vector<cfire::testing_detail::BenchStats> bench_records;");
        for (std::size_t i = 0; i < ast.tests.size(); ++i) {
            const auto& td = ast.tests[i];
            std::string t_name = escape_line_path(td.name);
            if (td.is_bench) {
                writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
                ++indent_;
                writeln(std::format("if (!json_mode) std::println(\"=== BENCH {}\");", t_name));
                writeln("cfire::testing_detail::current_test_failed = false;");
                writeln("cfire::testing_detail::failure_file.clear();");
                writeln("cfire::testing_detail::failure_line = 0;");
                writeln("cfire::testing_detail::failure_expr.clear();");
                writeln("cfire::testing_detail::failure_msg.clear();");
                writeln("auto start_t = std::chrono::steady_clock::now();");
                writeln(std::format("(void)cfire::testing_detail::run_test_guarded([&] {{ __cfire_bench_runner_{}(!bench_mode, bench_time_ms, json_mode, bench_records); }});", i));
                writeln("auto end_t = std::chrono::steady_clock::now();");
                writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
                writeln("if (!cfire::testing_detail::current_test_failed) {");
                writeln("    passed_count++;");
                writeln(std::format("    if (!json_mode && !bench_mode) std::println(\"--- PASS: {}\");", t_name));
                writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
                writeln("} else {");
                writeln("    failed_count++;");
                writeln("    std::string fail_detail = cfire::testing_detail::failure_msg.empty() ? cfire::testing_detail::failure_expr : cfire::testing_detail::failure_msg;");
                writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
                writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", t_name));
                writeln("}");
                --indent_;
                writeln("}");
                continue;
            }

            writeln("if (!bench_mode) {");
            ++indent_;
            writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", t_name));
            ++indent_;
            writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", t_name));
            writeln("cfire::testing_detail::current_test_failed = false;");
            writeln("cfire::testing_detail::failure_file.clear();");
            writeln("cfire::testing_detail::failure_line = 0;");
            writeln("cfire::testing_detail::failure_expr.clear();");
            writeln("cfire::testing_detail::failure_msg.clear();");
            bool check_fd_leak = !has_cpp_import &&
                                 !stmts_reference_ident(td.body, "InitWindow") &&
                                 !stmts_reference_ident(td.body, "run_webview_test") &&
                                 !stmts_reference_ident(td.body, "keychain");
            if (check_fd_leak) {
                for (const auto& fn : ast.functions) {
                    if (!fn.receiver && stmts_reference_ident(td.body, fn.name)) {
                        if (stmts_reference_ident(fn.body, "InitWindow") ||
                            stmts_reference_ident(fn.body, "run_webview_test") ||
                            stmts_reference_ident(fn.body, "keychain")) {
                            check_fd_leak = false;
                            break;
                        }
                    }
                }
            }
            if (check_fd_leak) {
                writeln("const std::size_t fds_before = cfire::testing_detail::count_open_fds();");
            }
            writeln("auto start_t = std::chrono::steady_clock::now();");
            if (td.is_fuzz || !td.params.empty()) {
                writeln(std::format("__cfire_fuzz_runner_{}(fuzz_runs, fuzz_seed);", i));
            } else {
                writeln(std::format("(void)cfire::testing_detail::run_test_guarded([&] {{ __cfire_test_{}(); }});", i));
            }
            writeln("auto end_t = std::chrono::steady_clock::now();");
            if (check_fd_leak) {
                std::string td_file = escape_line_path(td.source_path.empty() ? source_path_ : td.source_path);
                writeln("const std::size_t fds_after = cfire::testing_detail::count_open_fds();");
                writeln("if (!cfire::testing_detail::current_test_failed && fds_before > 0 && fds_after > fds_before) {");
                writeln("    cfire::testing_detail::current_test_failed = true;");
                writeln(std::format("    cfire::testing_detail::failure_file = \"{}\";", td_file));
                writeln(std::format("    cfire::testing_detail::failure_line = {};", td.line));
                writeln("    cfire::testing_detail::failure_expr = \"resource leak canary\";");
                writeln("    cfire::testing_detail::failure_msg = std::format(\"test leaked {} open file descriptor(s) (missing 'defer file.close()' or 'defer db.close()'?)\", fds_after - fds_before);");
                writeln("}");
            }
            writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
            writeln("bool test_pass = !cfire::testing_detail::current_test_failed;");
            writeln("if (test_pass) {");
            writeln("    passed_count++;");
            writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", t_name));
            writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", t_name));
            writeln("} else {");
            writeln("    failed_count++;");
            writeln("    std::string fail_detail;");
            writeln("    if (!cfire::testing_detail::failure_expr.empty() && !cfire::testing_detail::failure_msg.empty()) {");
            writeln("        fail_detail = cfire::testing_detail::failure_expr + \" (\" + cfire::testing_detail::failure_msg + \")\";");
            writeln("    } else if (!cfire::testing_detail::failure_expr.empty()) {");
            writeln("        fail_detail = cfire::testing_detail::failure_expr;");
            writeln("    } else {");
            writeln("        fail_detail = cfire::testing_detail::failure_msg;");
            writeln("    }");
            writeln("    if (!json_mode) std::println(\"--- FAIL: " + t_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
            writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", t_name));
            writeln("}");
            --indent_;
            writeln("}");
            --indent_;
            writeln("}");
        }
        for (std::size_t j = 0; j < auto_json_structs.size(); ++j) {
            const auto& [sname, sline] = auto_json_structs[j];
            std::string auto_name = std::format("[auto] json round-trip: {}", sname);
            writeln("if (!bench_mode) {");
            ++indent_;
            writeln(std::format("if (filter.empty() || std::string_view(\"{}\").find(filter) != std::string_view::npos) {{", auto_name));
            ++indent_;
            writeln(std::format("if (!json_mode) std::println(\"=== RUN   {}\");", auto_name));
            writeln("cfire::testing_detail::current_test_failed = false;");
            writeln("cfire::testing_detail::failure_file.clear();");
            writeln("cfire::testing_detail::failure_line = 0;");
            writeln("cfire::testing_detail::failure_expr.clear();");
            writeln("cfire::testing_detail::failure_msg.clear();");
            writeln("auto start_t = std::chrono::steady_clock::now();");
            writeln(std::format("(void)cfire::testing_detail::run_test_guarded([&] {{ __cfire_auto_json_{}(fuzz_seed); }});", j));
            writeln("auto end_t = std::chrono::steady_clock::now();");
            writeln("double dur_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();");
            writeln("if (!cfire::testing_detail::current_test_failed) {");
            writeln("    passed_count++;");
            writeln(std::format("    if (!json_mode) std::println(\"--- PASS: {}\");", auto_name));
            writeln(std::format("    records.push_back({{\"{}\", true, dur_ms, \"\", 0, \"\"}});", auto_name));
            writeln("} else {");
            writeln("    failed_count++;");
            writeln("    std::string fail_detail = cfire::testing_detail::failure_expr + \" (\" + cfire::testing_detail::failure_msg + \")\";");
            writeln("    if (!json_mode) std::println(\"--- FAIL: " + auto_name + " ({}:{}: {})\", cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail);");
            writeln(std::format("    records.push_back({{\"{}\", false, dur_ms, cfire::testing_detail::failure_file, cfire::testing_detail::failure_line, fail_detail}});", auto_name));
            writeln("}");
            --indent_;
            writeln("}");
            --indent_;
            writeln("}");
        }
        writeln("if (json_mode) {");
        writeln("    auto escape_json = [](std::string_view s) -> std::string {");
        writeln("        std::string out;");
        writeln("        for (char c : s) {");
        writeln("            if (c == '\"') out += \"\\\\\\\"\";");
        writeln("            else if (c == '\\\\') out += \"\\\\\\\\\";");
        writeln("            else if (c == '\\n') out += \"\\\\n\";");
        writeln("            else out += c;");
        writeln("        }");
        writeln("        return out;");
        writeln("    };");
        writeln("    std::print(\"{{\\\"success\\\": {}, \\\"passed\\\": {}, \\\"failed\\\": {}, \\\"tests\\\": [\", (failed_count == 0 ? \"true\" : \"false\"), passed_count, failed_count);");
        writeln("    for (std::size_t r = 0; r < records.size(); ++r) {");
        writeln("        if (r > 0) std::print(\", \");");
        writeln("        const auto& rec = records[r];");
        writeln("        std::print(\"{{\\\"name\\\": \\\"{}\\\", \\\"status\\\": \\\"{}\\\", \\\"duration_ms\\\": {:.2f}, \\\"file\\\": \\\"{}\\\", \\\"line\\\": {}, \\\"error\\\": \\\"{}\\\"}}\", escape_json(rec.name), (rec.passed ? \"pass\" : \"fail\"), rec.duration_ms, escape_json(rec.file), rec.line, escape_json(rec.error));");
        writeln("    }");
        writeln("    std::print(\"], \\\"benchmarks\\\": [\");");
        writeln("    for (std::size_t b = 0; b < bench_records.size(); ++b) {");
        writeln("        if (b > 0) std::print(\", \");");
        writeln("        const auto& br = bench_records[b];");
        writeln("        std::print(\"{{\\\"name\\\": \\\"{}\\\", \\\"iters\\\": {}, \\\"min_ns\\\": {:.2f}, \\\"p50_ns\\\": {:.2f}, \\\"mean_ns\\\": {:.2f}, \\\"p99_ns\\\": {:.2f}, \\\"stddev_pct\\\": {:.2f}, \\\"ops_per_sec\\\": {:.1f}}}\", escape_json(br.name), br.total_iters, br.min_ns, br.p50_ns, br.mean_ns, br.p99_ns, br.stddev_pct, br.ops_per_sec);");
        writeln("    }");
        writeln("    std::println(\"]}}\");");
        writeln("} else {");
        writeln("    if (failed_count == 0) {");
        writeln("        std::println(\"PASS ({} cfire tests succeeded)\", passed_count);");
        writeln("    } else {");
        writeln("        std::println(\"FAIL ({} of {} cfire tests failed)\", failed_count, (passed_count + failed_count));");
        writeln("    }");
        writeln("}");
        writeln("return failed_count > 0 ? 1 : 0;");
        --indent_;
        writeln("}");
    }
}

inline void Emitter::emit_test_decl(const TestDecl& td, std::size_t idx) {
    set_source_path(td.source_path);
    if (td.line > 0) emit_line_directive(td.line);
    cur_fn_ = std::format("__cfire_test_{}", idx);
    defer_count_ = 0;
    in_test_ = true;
    in_fallible_void_ = false;
    local_vars_.clear();
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

    writeln(std::format("inline void {}({}) {{", cur_fn_, param_sig));
    ++indent_;
    for (const auto& st : td.body) emit_stmt(st, false);
    --indent_;
    writeln("}");
    in_test_ = false;
    local_vars_.clear();
    ptr_vars_.clear();
    map_vars_.clear();
    fixed_array_vars_.clear();

    if (td.is_bench) {
        writeln(std::format("inline void __cfire_bench_runner_{}(bool smoke_only, double budget_ms, bool json_mode, std::vector<cfire::testing_detail::BenchStats>& out_stats) {{", idx));
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
        writeln(std::format("inline void __cfire_fuzz_runner_{}(std::size_t runs, std::uint64_t seed) {{", idx));
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
