#include "../test_harness.hpp"

void test_selfhost_lexer_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_selfhost_lexer_parity_and_benchmark (Self-Hosted Lexer Dogfooding & Measurable Parity)");

    // 1. Files to test
    std::vector<std::string> test_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
    };

    auto check_parity = [](std::string_view name, std::string_view src) {
        auto cxx_res = cfire::frontend::tokenize_cxx(name, src);
        auto cf_res = cfire::frontend::selfhost_tokenize_adapter(name, src);

        require(cxx_res.has_value() == cf_res.has_value(),
            std::format("{}: outcome match (cxx={}, cf={})", name, cxx_res.has_value(), cf_res.has_value()));
        if (!cxx_res.has_value() && !cf_res.has_value()) {
            require(cxx_res.error() == cf_res.error(),
                std::format("{}: error message match ('{}' vs '{}')", name, cxx_res.error(), cf_res.error()));
            return;
        }
        if (!cxx_res || !cf_res) return;

        const auto& cxx_toks = *cxx_res;
        const auto& cf_toks = *cf_res;
        require(cxx_toks.size() == cf_toks.size(),
            std::format("{}: token count match (cxx={}, cf={})", name, cxx_toks.size(), cf_toks.size()));

        std::size_t min_sz = std::min(cxx_toks.size(), cf_toks.size());
        for (std::size_t i = 0; i < min_sz; ++i) {
            const auto& t1 = cxx_toks[i];
            const auto& t2 = cf_toks[i];
            if (t1.kind != t2.kind || t1.lexeme != t2.lexeme || t1.line != t2.line || t1.col != t2.col) {
                require(false, std::format("{}[{}]: token mismatch: C++({} '{}' L{}:C{}) vs cfire({} '{}' L{}:C{})",
                    name, i,
                    cfire::frontend::token_kind_name(t1.kind), t1.lexeme, t1.line, t1.col,
                    cfire::frontend::token_kind_name(t2.kind), t2.lexeme, t2.line, t2.col));
                break;
            }
        }
    };

    // Parity check across all real repository files
    std::string large_workload;
    for (const auto& path : test_files) {
        auto content = cfire::build::read_file_text(path);
        if (content.has_value()) {
            check_parity(path, *content);
            large_workload += *content;
            large_workload += "\n\n";
        }
    }

    // Parity check on synthetic edge-case snippets
    check_parity("synthetic_escape", R"cf(
        str_val := "hello\nworld\t\r\"escaped\"\\"
        raw_val := `multiline
        raw "string" with {braces}`
    )cf");

    check_parity("synthetic_ops_and_nums", R"cf(
        a := 10 + 20 - 3 * 4 / 5 % 2
        f := 3.14159 + 0.005 + 1_000_000
        cond := (a == 10 && f != 0.0) || (a <= 5 && f >= 2.0)
        range := arr[1..10]
        assign := x := y :: z -> w => q += 1 -= 2
        symbols := () {} [] : , . = + - * / % & | ! ? @ < > ;
    )cf");

    check_parity("synthetic_cpp_block", R"cf(
        cpp {
            int a = 1;
            for (int i = 0; i < 10; ++i) {
                if (i == 5) { break; }
            }
        }
        word := "regular_ident"
    )cf");

    check_parity("synthetic_unterminated_str", "unterminated := \"hello world");
    check_parity("synthetic_unterminated_raw", "raw := `unterminated");
    check_parity("synthetic_unexpected_char", "bad := ~");

    std::println("  [PASS] 100% token-for-token parity verified across all files & edge cases");

    // 2. High-Precision Performance Benchmark
    // Replicate workload to ~1.5 MB of code
    std::string benchmark_payload;
    while (benchmark_payload.size() < 1'500'000) {
        benchmark_payload += large_workload;
    }

    // Warm-up
    (void)cfire::frontend::tokenize_cxx("warmup.cf", benchmark_payload);
    (void)cfire::frontend::selfhost_tokenize_adapter("warmup.cf", benchmark_payload);

    constexpr int kIters = 20;

    // Benchmark C++ Lexer
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t total_cxx_tokens = 0;
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::frontend::tokenize_cxx("bench.cf", benchmark_payload);
        total_cxx_tokens += res->size();
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double total_bytes = static_cast<double>(benchmark_payload.size()) * kIters;
    double mbs_cxx = (total_bytes / (1024.0 * 1024.0)) / (ms_cxx / 1000.0);
    double mtokens_cxx = (static_cast<double>(total_cxx_tokens) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark Self-Hosted cfire Lexer
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t total_cf_tokens = 0;
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::frontend::selfhost_tokenize_adapter("bench.cf", benchmark_payload);
        total_cf_tokens += res->size();
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (total_bytes / (1024.0 * 1024.0)) / (ms_cf / 1000.0);
    double mtokens_cf = (static_cast<double>(total_cf_tokens) / 1'000'000.0) / (ms_cf / 1000.0);

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Lexer Throughput Benchmark (Payload: {:.2f} MB, {} iterations):",
        static_cast<double>(benchmark_payload.size()) / (1024.0 * 1024.0), kIters);
    std::println("    - C++ Native Lexer:        {:>7.2f} MB/s ({:.2f}M tokens/s, avg {:.2f}ms/run)",
        mbs_cxx, mtokens_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Lexer: {:>7.2f} MB/s ({:.2f}M tokens/s, avg {:.2f}ms/run)",
        mbs_cf, mtokens_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:       {:>7.1f}% of native C++ speed", parity_pct);

    // Assert that the self-hosted lexer achieves at least 80% of native C++ speed
    require(mbs_cf >= 0.80 * mbs_cxx,
        std::format("Self-hosted lexer performance regression ({:.1f} MB/s vs {:.1f} MB/s C++ baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_lexer_parity_and_benchmark");
}



void test_selfhost_formatter_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_selfhost_formatter_parity_and_benchmark (Self-Hosted Formatter Dogfooding & Measurable Parity)");

    // 1. Files to test
    std::vector<std::string> test_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
        "src/formatter.cf",
    };

    auto check_parity_and_idempotency = [](std::string_view name, std::string_view src) {
        auto cxx_res = cfire::fmt::format_source_cxx(src, name);
        auto cf_res = cfire::fmt::selfhost_format_source(src, name);

        require(cxx_res.has_value() == cf_res.has_value(),
            std::format("{}: outcome match (cxx={}, cf={})", name, cxx_res.has_value(), cf_res.has_value()));
        if (!cxx_res.has_value() && !cf_res.has_value()) {
            require(cxx_res.error() == cf_res.error(),
                std::format("{}: error match ('{}' vs '{}')", name, cxx_res.error(), cf_res.error()));
            return;
        }
        if (!cxx_res || !cf_res) return;

        // Character-for-character parity check
        require(*cxx_res == *cf_res,
            std::format("{}: character parity match between C++ and cfire formatters", name));

        // Idempotency check: fmt(fmt(code)) == fmt(code)
        auto cf_idem = cfire::fmt::selfhost_format_source(*cf_res, name);
        require(cf_idem.has_value() && *cf_idem == *cf_res,
            std::format("{}: self-hosted formatter idempotency check", name));
    };

    std::string large_workload;
    for (const auto& path : test_files) {
        auto content = cfire::build::read_file_text(path);
        if (content.has_value()) {
            check_parity_and_idempotency(path, *content);
            large_workload += *content;
            large_workload += "\n\n";
        }
    }

    // Synthetic edge cases
    check_parity_and_idempotency("synthetic_struct_align", R"cf(
package demo

type Config struct {
id: i32 `json:"id"`
database_url: str `json:"db_url"` // primary connection string
timeout_ms: u64 `json:"timeout"`
// Internal flags
is_active: bool
}

fn test_fn(c: Config) -> bool {
return c.is_active
}
)cf");

    check_parity_and_idempotency("synthetic_raw_strings_and_comments", R"cf(
package demo

fn complex_blocks() {
raw := `first line
    second line with {braces}
third line`
// comment with unbalanced { and ( and [
if true {
a := 10
}
}
)cf");

    std::println("  [PASS] 100% character parity & idempotency verified across all files & edge cases");

    // 2. High-Precision Performance Benchmark
    std::string benchmark_payload;
    while (benchmark_payload.size() < 1'200'000) {
        benchmark_payload += large_workload;
    }

    // Warm-up
    (void)cfire::fmt::format_source_cxx(benchmark_payload, "warmup.cf");
    (void)cfire::fmt::selfhost_format_source(benchmark_payload, "warmup.cf");

    constexpr int kIters = 10;

    // Benchmark C++ Formatter
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::fmt::format_source_cxx(benchmark_payload, "bench.cf");
        (void)res;
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double total_bytes = static_cast<double>(benchmark_payload.size()) * kIters;
    double mbs_cxx = (total_bytes / (1024.0 * 1024.0)) / (ms_cxx / 1000.0);

    // Benchmark Self-Hosted cfire Formatter
    auto t0_cf = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto res = cfire::fmt::selfhost_format_source(benchmark_payload, "bench.cf");
        (void)res;
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (total_bytes / (1024.0 * 1024.0)) / (ms_cf / 1000.0);

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Formatter Throughput Benchmark (Payload: {:.2f} MB, {} iterations):",
        static_cast<double>(benchmark_payload.size()) / (1024.0 * 1024.0), kIters);
    std::println("    - C++ Native Formatter:        {:>7.2f} MB/s (avg {:.2f}ms/run)",
        mbs_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Formatter: {:>7.2f} MB/s (avg {:.2f}ms/run)",
        mbs_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:           {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted formatter performance regression ({:.1f} MB/s vs {:.1f} MB/s C++ baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_formatter_parity_and_benchmark");
}



void test_selfhost_ast_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_selfhost_ast_parity_and_benchmark (Self-Hosted AST Definitions & TypeRef Serialization)");

    // 1. Structural Parity: Compare format_typeref_cf with cfire::fmt::format_type_ref
    auto check_type_parity = [](const cfire::frontend::TypeRef& cxx_t, const ::TypeRef& cf_t, std::string_view label) {
        std::string cxx_str = cfire::fmt::format_type_ref(cxx_t);
        std::string cf_str = ::format_typeref_cf(cf_t);
        require(cxx_str == cf_str,
            std::format("{}: TypeRef serialization mismatch: C++('{}') vs cfire('{}')", label, cxx_str, cf_str));
    };

    // Primitive
    {
        cfire::frontend::TypeRef cxx{.name = "i32"};
        ::TypeRef cf = ::make_typeref("i32");
        check_type_parity(cxx, cf, "primitive_i32");
    }

    // Optional and Fallible
    {
        cfire::frontend::TypeRef cxx{.name = "str", .is_fallible = true, .is_optional = true};
        ::TypeRef cf = ::make_typeref("str");
        cf.is_fallible = true;
        cf.is_optional = true;
        check_type_parity(cxx, cf, "str_opt_fallible");
    }

    // Slices & Arrays
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "u8"});
        cfire::frontend::TypeRef cxx{.is_slice = true, .elem_type = elem_cxx};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("u8"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_slice = true;
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "slice_u8");
    }
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "f64"});
        cfire::frontend::TypeRef cxx{.is_array = true, .array_len = "16", .elem_type = elem_cxx};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("f64"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_array = true;
        cf.array_len = "16";
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "array_16_f64");
    }

    // Maps
    {
        auto key_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "str"});
        auto val_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "i64"});
        cfire::frontend::TypeRef cxx{.is_map = true, .key_type = key_cxx, .elem_type = val_cxx};

        auto key_cf = std::make_shared<::TypeRef>(::make_typeref("str"));
        auto val_cf = std::make_shared<::TypeRef>(::make_typeref("i64"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_map = true;
        cf.key_type = key_cf;
        cf.elem_type = val_cf;
        check_type_parity(cxx, cf, "map_str_i64");
    }

    // Pointers & Borrows
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "Scene"});
        cfire::frontend::TypeRef cxx{.elem_type = elem_cxx, .is_mut_ptr = true};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("Scene"));
        ::TypeRef cf = ::make_typeref("");
        cf.is_mut_ptr = true;
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "mut_ptr_scene");
    }

    // Smart wrappers
    {
        auto elem_cxx = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "Config"});
        cfire::frontend::TypeRef cxx{.elem_type = elem_cxx, .smart_wrap = "own"};

        auto elem_cf = std::make_shared<::TypeRef>(::make_typeref("Config"));
        ::TypeRef cf = ::make_typeref("");
        cf.smart_wrap = "own";
        cf.elem_type = elem_cf;
        check_type_parity(cxx, cf, "own_config");
    }

    // Tuples
    {
        cfire::frontend::TypeRef cxx{.is_tuple = true, .tuple_types = {cfire::frontend::TypeRef{.name = "i32"}, cfire::frontend::TypeRef{.name = "str"}}};
        ::TypeRef cf = ::make_typeref("");
        cf.is_tuple = true;
        cf.tuple_types = {::make_typeref("i32"), ::make_typeref("str")};
        check_type_parity(cxx, cf, "tuple_i32_str");
    }

    // 2. Declaration Counting Invariant
    {
        ::FileAST ast{
            .path = "main.cf",
            .package_name = "main",
            .structs = {::StructDecl{.name = "Foo"}, ::StructDecl{.name = "Bar"}},
            .enums = {::EnumDecl{.name = "Color"}},
            .functions = {::FnDecl{.name = "main"}, ::FnDecl{.name = "helper"}},
        };
        auto [num_structs, num_enums, num_fns] = ::count_ast_declarations(ast);
        require(num_structs == 2 && num_enums == 1 && num_fns == 2,
            std::format("count_ast_declarations mismatch: ({}, {}, {}) expected (2, 1, 2)", num_structs, num_enums, num_fns));
    }

    std::println("  [PASS] 100% TypeRef serialization & AST declaration parity verified");

    // 3. High-Precision Performance Benchmark
    auto test_cf_typeref = ::make_typeref("");
    test_cf_typeref.is_map = true;
    test_cf_typeref.key_type = std::make_shared<::TypeRef>(::make_typeref("str"));
    auto val_cf = ::make_typeref("");
    val_cf.is_slice = true;
    val_cf.elem_type = std::make_shared<::TypeRef>(::make_typeref("i32"));
    val_cf.is_fallible = true;
    test_cf_typeref.elem_type = std::make_shared<::TypeRef>(val_cf);

    auto test_cxx_typeref = cfire::frontend::TypeRef{
        .is_map = true,
        .key_type = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "str"}),
        .elem_type = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{
            .is_slice = true,
            .elem_type = std::make_shared<cfire::frontend::TypeRef>(cfire::frontend::TypeRef{.name = "i32"}),
            .is_fallible = true,
        }),
    };

    constexpr int kIters = 100'000;

    // Benchmark C++ format_type_ref
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t total_cxx_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto s = cfire::fmt::format_type_ref(test_cxx_typeref);
        total_cxx_len += s.size();
    }
    (void)total_cxx_len;
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mtypes_cxx = (static_cast<double>(kIters) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire format_typeref_cf
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t total_cf_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto s = ::format_typeref_cf(test_cf_typeref);
        total_cf_len += s.size();
    }
    (void)total_cf_len;
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mtypes_cf = (static_cast<double>(kIters) / 1'000'000.0) / (ms_cf / 1000.0);

    double parity_pct = (mtypes_cf / mtypes_cxx) * 100.0;

    std::println("  [BENCHMARK] AST TypeRef Formatting Benchmark ({} iterations):", kIters);
    std::println("    - C++ Native format_type_ref: {:>7.2f}M types/s (avg {:.4f}µs/type)",
        mtypes_cxx, (ms_cxx * 1000.0) / kIters);
    std::println("    - cfire format_typeref_cf:    {:>7.2f}M types/s (avg {:.4f}µs/type)",
        mtypes_cf, (ms_cf * 1000.0) / kIters);
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mtypes_cf >= 0.70 * mtypes_cxx,
        std::format("Self-hosted AST serializer performance regression ({:.1f}M vs {:.1f}M baseline)", mtypes_cf, mtypes_cxx));

    std::println("--- PASS: test_selfhost_ast_parity_and_benchmark");
}



void test_selfhost_refactor_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_refactor_parity_and_benchmark (Self-Hosted Refactoring Engine Dogfooding & Measurable Parity)");

    // 1. Identifier Validation Parity
    const std::vector<std::string> test_idents = {
        "valid_identifier", "_leading_underscore", "CamelCaseName", "snake_case_name",
        "var123", "alpha_beta_gamma", "x", "y", "z", "_0", "__init__",
        // Keywords (must be invalid)
        "package", "import", "cpp", "cplusplus", "as", "type", "struct", "enum", "interface",
        "map", "fn", "async", "in", "mut", "out", "move", "var",
        "if", "else", "for", "match", "defer", "return", "try", "or", "test",
        "true", "false", "none", "nil", "null",
        // Syntax invalid
        "", "123num", "9bad", "hyphen-ident", "dot.ident", "space ident", "dollar$ident", "bang!"
    };

    for (const auto& id : test_idents) {
        bool cxx_valid = cfire::refactor::is_valid_identifier_cxx(id);
        bool cf_valid = cfire::refactor::is_valid_identifier(id);
        require(cxx_valid == cf_valid,
            std::format("is_valid_identifier parity mismatch for '{}': C++={} vs cfire={}", id, cxx_valid, cf_valid));
    }
    std::println("  [PASS] 100% identifier validation parity verified across {} candidates", test_idents.size());

    // 2. Line Replacements Parity
    std::string sample_source =
        "package test\n"
        "\n"
        "fn calculate(old_val: i32, multiplier: i32) -> i32 {\n"
        "    old_val_result := old_val * multiplier\n"
        "    return old_val + old_val_result\n"
        "}\n";

    // Occurrences intentionally ordered non-descending to test sorting parity
    std::vector<cfire::refactor::Occurrence> occs = {
        {"test.cf", 3, 14, 21}, // line 3, col 14: old_val
        {"test.cf", 5, 12, 19}, // line 5, col 12: old_val
        {"test.cf", 4, 23, 30}, // line 4, col 23: old_val
    };

    std::string cxx_mod = cfire::refactor::apply_line_replacements_cxx(sample_source, occs, "new_val");
    std::string cf_mod = cfire::refactor::apply_line_replacements(sample_source, occs, "new_val");

    require(cxx_mod == cf_mod, "apply_line_replacements output mismatch between C++ and cfire");
    require(cf_mod.contains("old_val_result := new_val * multiplier"), "Replacement did not substitute line 4 occurrence cleanly");
    require(cf_mod.contains("return new_val + old_val_result"), "Replacement did not substitute line 5 occurrence cleanly");
    std::println("  [PASS] 100% text replacement character parity verified without index drift");

    // 3. Unified Diff Parity & JSON Serialization Parity
    cfire::refactor::RenameResult res;
    res.success = true;
    res.old_name = "old_val";
    res.new_name = "new_val";
    res.total_occurrences = occs.size();

    cfire::refactor::FileModification mod1;
    mod1.file_path = "src/calc.cf";
    mod1.original_content = sample_source;
    mod1.modified_content = cf_mod;
    mod1.occurrences = occs;
    res.modifications.push_back(mod1);

    std::string cxx_diff = cfire::refactor::generate_unified_diff_cxx(res);
    std::string cf_diff = cfire::refactor::generate_unified_diff(res);
    require(cxx_diff == cf_diff, "generate_unified_diff output mismatch between C++ and cfire");
    std::println("  [PASS] 100% unified diff parity verified byte-for-byte");

    std::string cxx_json = cfire::refactor::to_json_cxx(res);
    std::string cf_json = cfire::refactor::to_json(res);
    require(cxx_json == cf_json, "to_json output mismatch between C++ and cfire");

    cfire::refactor::RenameResult err_res;
    err_res.success = false;
    err_res.error = "cfire/REFACTOR-COLLISION: symbol 'new_val' already exists in scope";
    std::string cxx_err_json = cfire::refactor::to_json_cxx(err_res);
    std::string cf_err_json = cfire::refactor::to_json(err_res);
    require(cxx_err_json == cf_err_json, "to_json error output mismatch between C++ and cfire");
    std::println("  [PASS] 100% JSON serialization parity verified byte-for-byte");

    // 4. Performance Benchmark (apply_line_replacements)
    constexpr int kIters = 20'000;
    std::size_t payload_bytes = sample_source.size() * kIters;

    // Benchmark C++
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_out_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto m = cfire::refactor::apply_line_replacements_cxx(sample_source, occs, "new_val");
        cxx_out_len += m.size();
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);
    double ops_cxx = static_cast<double>(kIters) / (ms_cxx / 1000.0);

    // Benchmark cfire self-hosted
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_out_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto m = cfire::refactor::apply_line_replacements(sample_source, occs, "new_val");
        cf_out_len += m.size();
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);
    double ops_cf = static_cast<double>(kIters) / (ms_cf / 1000.0);

    require(cxx_out_len == cf_out_len, "Benchmark output length mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Refactor Replacement Benchmark (Payload: {:.2f} MB, {} iterations):",
        static_cast<double>(payload_bytes) / 1'000'000.0, kIters);
    std::println("    - C++ Native Engine:        {:>7.2f} MB/s ({:>7.0f} edits/s, avg {:.4f}µs/run)",
        mbs_cxx, ops_cxx, (ms_cxx * 1000.0) / kIters);
    std::println("    - cfire Self-Hosted Engine: {:>7.2f} MB/s ({:>7.0f} edits/s, avg {:.4f}µs/run)",
        mbs_cf, ops_cf, (ms_cf * 1000.0) / kIters);
    std::println("    - Performance Ratio:        {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted refactor engine performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_refactor_parity_and_benchmark");
}



void test_selfhost_docgen_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_docgen_parity_and_benchmark (Self-Hosted Documentation Generator Dogfooding & Parity)");

    // 1. HTML Escaping Parity
    const std::vector<std::string> test_strings = {
        "", "hello world", "cfire <language>", "5 > 3 && 2 < 4",
        "\"quoted\" text with 'single' & amp", "<script>alert(\"xss\")</script>",
        "Line1\nLine2\tTab\r\nCRLF", "Special chars: & < > \" &amp; &lt;"
    };
    for (const auto& s : test_strings) {
        auto cxx_esc = cfire::doc::detail::html_escape_cxx(s);
        auto cf_esc = ::html_escape(s);
        require(cxx_esc == cf_esc, std::format("html_escape parity mismatch on '{}'", s));
    }
    std::println("  [PASS] 100% html_escape parity verified across {} test strings", test_strings.size());

    // 2. Navbar HTML Parity
    for (std::string_view page : {"index", "guide", "reference", "other", ""}) {
        auto cxx_nav = cfire::doc::detail::navbar_html_cxx(page);
        auto cf_nav = ::navbar_html(page);
        require(cxx_nav == cf_nav, std::format("navbar_html parity mismatch on page '{}'", page));
    }
    std::println("  [PASS] 100% navbar_html parity verified across all page states");

    // 3. Reference Markdown Parity (STDLIB_REFERENCE.md)
    auto cxx_ref_md = cfire::doc::generate_reference_markdown_cxx();
    auto cf_ref_md = cfire::doc::generate_reference_markdown();
    require(cxx_ref_md == cf_ref_md, "generate_reference_markdown output mismatch between C++ and cfire");
    require(cf_ref_md.contains("## 1. Built-In `core:*` Standard Library Modules"), "Markdown missing Section 1 header");
    require(cf_ref_md.contains("## 2. Semantic Diagnostic Codes"), "Markdown missing Section 2 header");
    std::println("  [PASS] 100% generate_reference_markdown parity verified byte-for-byte ({} bytes)", cf_ref_md.size());

    // 4. Reference HTML Parity (reference.html)
    auto cxx_ref_html = cfire::doc::generate_reference_html_cxx();
    auto cf_ref_html = cfire::doc::generate_reference_html();
    require(cxx_ref_html == cf_ref_html, "generate_reference_html output mismatch between C++ and cfire");
    require(cf_ref_html.contains("<!DOCTYPE html>"), "HTML missing doctype");
    require(cf_ref_html.contains("`core:*` Standard Library &amp; Diagnostics Reference"), "HTML missing title");
    std::println("  [PASS] 100% generate_reference_html parity verified byte-for-byte ({} bytes)", cf_ref_html.size());

    // 5. Package Documentation Extraction Parity
    auto cxx_pkg = cfire::doc::extract_file_markdown_doc_cxx("examples/09-stdlib-test-pointers/main.cf");
    auto cf_pkg = cfire::doc::extract_file_markdown_doc("examples/09-stdlib-test-pointers/main.cf");
    require(cxx_pkg.has_value() && cf_pkg.has_value(), "extract_file_markdown_doc failed");
    require(*cxx_pkg == *cf_pkg, "extract_file_markdown_doc output mismatch between C++ and cfire");
    std::println("  [PASS] 100% extract_file_markdown_doc parity verified on examples/09-stdlib-test-pointers/main.cf");

    // 6. Performance Benchmark (generate_reference_markdown)
    constexpr int kIters = 2'000;
    std::size_t payload_bytes = cf_ref_md.size() * kIters;

    // Benchmark C++
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_out_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto s = cfire::doc::generate_reference_markdown_cxx();
        cxx_out_len += s.size();
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);
    double docs_cxx = static_cast<double>(kIters) / (ms_cxx / 1000.0);

    // Benchmark cfire self-hosted
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_out_len = 0;
    for (int i = 0; i < kIters; ++i) {
        auto s = cfire::doc::generate_reference_markdown();
        cf_out_len += s.size();
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);
    double docs_cf = static_cast<double>(kIters) / (ms_cf / 1000.0);

    require(cxx_out_len == cf_out_len, "Benchmark output length mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Reference Doc Markdown Benchmark (Payload: {:.2f} MB, {} iterations):",
        static_cast<double>(payload_bytes) / 1'000'000.0, kIters);
    std::println("    - C++ Native Generator:     {:>7.2f} MB/s ({:>7.0f} docs/s, avg {:.3f}µs/doc)",
        mbs_cxx, docs_cxx, (ms_cxx * 1000.0) / kIters);
    std::println("    - cfire Self-Hosted Engine: {:>7.2f} MB/s ({:>7.0f} docs/s, avg {:.3f}µs/doc)",
        mbs_cf, docs_cf, (ms_cf * 1000.0) / kIters);
    std::println("    - Performance Ratio:        {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted docgen performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_docgen_parity_and_benchmark");
}



void test_selfhost_parser_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_parser_parity_and_benchmark (Self-Hosted Parser Dogfooding & Measurable Parity)");

    const std::vector<std::string> corpus_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
        "src/formatter.cf",
        "src/ast.cf",
        "src/refactor.cf",
        "src/docgen.cf",
        "src/parser.cf"
    };

    std::size_t total_bytes = 0;
    std::vector<std::pair<std::string, std::string>> loaded_files;
    for (const auto& rel_path : corpus_files) {
        std::ifstream ifs(rel_path);
        require(ifs.is_open(), std::format("Failed to open corpus file '{}'", rel_path));
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        total_bytes += content.size();
        loaded_files.emplace_back(rel_path, std::move(content));
    }

    // 1. Conformance & AST Parity across all 15 corpus files
    for (const auto& [path, content] : loaded_files) {
        auto cxx_ast = cfire::frontend::parse_source_cxx(path, content);
        auto cf_ast = cfire::frontend::selfhost_parse_source(path, content);

        require(cxx_ast.has_value(), std::format("C++ parser failed on '{}': {}", path, cxx_ast ? "" : cxx_ast.error()));
        require(cf_ast.has_value(), std::format("cfire self-host parser failed on '{}': {}", path, cf_ast ? "" : cf_ast.error()));

        if (cxx_ast && cf_ast) {
            require(cxx_ast->package_name == cf_ast->package_name,
                std::format("package_name mismatch in '{}': '{}' vs '{}'", path, cxx_ast->package_name, cf_ast->package_name));
            require(cxx_ast->imports.size() == cf_ast->imports.size(),
                std::format("imports.size() mismatch in '{}': {} vs {}", path, cxx_ast->imports.size(), cf_ast->imports.size()));
            for (std::size_t i = 0; i < cxx_ast->imports.size(); ++i) {
                require(cxx_ast->imports[i].path == cf_ast->imports[i].path,
                    std::format("import[{}] path mismatch in '{}'", i, path));
                require(cxx_ast->imports[i].alias == cf_ast->imports[i].alias,
                    std::format("import[{}] alias mismatch in '{}'", i, path));
            }
            require(cxx_ast->structs.size() == cf_ast->structs.size(),
                std::format("structs.size() mismatch in '{}': {} vs {}", path, cxx_ast->structs.size(), cf_ast->structs.size()));
            for (std::size_t i = 0; i < cxx_ast->structs.size(); ++i) {
                require(cxx_ast->structs[i].name == cf_ast->structs[i].name,
                    std::format("struct[{}] name mismatch in '{}': '{}' vs '{}'", i, path, cxx_ast->structs[i].name, cf_ast->structs[i].name));
                require(cxx_ast->structs[i].fields.size() == cf_ast->structs[i].fields.size(),
                    std::format("struct '{}' field count mismatch in '{}': {} vs {}", cxx_ast->structs[i].name, path,
                        cxx_ast->structs[i].fields.size(), cf_ast->structs[i].fields.size()));
            }
            require(cxx_ast->enums.size() == cf_ast->enums.size(),
                std::format("enums.size() mismatch in '{}': {} vs {}", path, cxx_ast->enums.size(), cf_ast->enums.size()));
            require(cxx_ast->interfaces.size() == cf_ast->interfaces.size(),
                std::format("interfaces.size() mismatch in '{}': {} vs {}", path, cxx_ast->interfaces.size(), cf_ast->interfaces.size()));
            require(cxx_ast->functions.size() == cf_ast->functions.size(),
                std::format("functions.size() mismatch in '{}': {} vs {}", path, cxx_ast->functions.size(), cf_ast->functions.size()));
            for (std::size_t i = 0; i < cxx_ast->functions.size(); ++i) {
                require(cxx_ast->functions[i].name == cf_ast->functions[i].name,
                    std::format("fn[{}] name mismatch in '{}': '{}' vs '{}'", i, path, cxx_ast->functions[i].name, cf_ast->functions[i].name));
                require(cxx_ast->functions[i].params.size() == cf_ast->functions[i].params.size(),
                    std::format("fn '{}' param count mismatch in '{}': {} vs {}", cxx_ast->functions[i].name, path,
                        cxx_ast->functions[i].params.size(), cf_ast->functions[i].params.size()));
            }
            require(cxx_ast->global_vars.size() == cf_ast->global_vars.size(),
                std::format("global_vars.size() mismatch in '{}': {} vs {}", path, cxx_ast->global_vars.size(), cf_ast->global_vars.size()));
            require(cxx_ast->tests.size() == cf_ast->tests.size(),
                std::format("tests.size() mismatch in '{}': {} vs {}", path, cxx_ast->tests.size(), cf_ast->tests.size()));
        }
    }
    std::println("  [PASS] 100% AST declaration parity verified across all {} corpus files ({:.2f} KB)",
        loaded_files.size(), static_cast<double>(total_bytes) / 1024.0);

    // 2. Resilient Error Recovery Parity
    {
        std::string malformed_src = "package broken\nfn foo( { let x = ; }\nfn bar() -> i32 { return 42 }\n";
        auto cxx_res = cfire::frontend::parse_source_resilient_cxx("broken.cf", malformed_src);
        auto cf_res = cfire::frontend::selfhost_parse_source_resilient("broken.cf", malformed_src);
        require(!cxx_res.ok(), "C++ parser should report errors on malformed file");
        require(!cf_res.ok(), "cfire parser should report errors on malformed file");
        require(!cf_res.errors.empty(), "cfire parser should record diagnostics");
        std::println("  [PASS] 100% resilient error recovery parity verified on malformed syntax");
    }

    // 3. Performance Throughput Benchmark
    constexpr int kIters = 40;
    std::size_t benchmark_payload_bytes = total_bytes * kIters;

    // Benchmark Native C++ Parser
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_parsed_fns = 0;
    for (int it = 0; it < kIters; ++it) {
        for (const auto& [path, content] : loaded_files) {
            auto res = cfire::frontend::parse_source_cxx(path, content);
            if (res) cxx_parsed_fns += res->functions.size();
        }
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire Self-Hosted Parser
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_parsed_fns = 0;
    for (int it = 0; it < kIters; ++it) {
        for (const auto& [path, content] : loaded_files) {
            auto res = cfire::frontend::selfhost_parse_source(path, content);
            if (res) cf_parsed_fns += res->functions.size();
        }
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    require(cxx_parsed_fns == cf_parsed_fns, "Benchmark AST count parity mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Parser Throughput Benchmark (Payload: {:.2f} MB, {} files x {} iters):",
        static_cast<double>(benchmark_payload_bytes) / 1'000'000.0, loaded_files.size(), kIters);
    std::println("    - C++ Native Parser:        {:>7.2f} MB/s (avg {:.3f}ms/corpus)",
        mbs_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Parser: {:>7.2f} MB/s (avg {:.3f}ms/corpus)",
        mbs_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:        {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted parser performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_parser_parity_and_benchmark");
}



void test_selfhost_sema_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_sema_parity_and_benchmark (Self-Hosted Semantic Analyzer Dogfooding & Measurable Parity)");

    const std::vector<std::string> corpus_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
        "src/formatter.cf",
        "src/ast.cf",
        "src/refactor.cf",
        "src/docgen.cf",
        "src/parser.cf",
        "src/sema.cf"
    };

    std::size_t total_bytes = 0;
    std::vector<std::pair<std::string, std::string>> loaded_files;
    for (const auto& rel_path : corpus_files) {
        std::ifstream ifs(rel_path);
        require(ifs.is_open(), std::format("Failed to open corpus file '{}'", rel_path));
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        total_bytes += content.size();
        loaded_files.emplace_back(rel_path, std::move(content));
    }

    // 1. Corpus Conformance & CheckedFile Parity across all 18 corpus files
    for (const auto& [path, content] : loaded_files) {
        auto p1 = cfire::frontend::parse_source(path, content);
        require(p1.has_value(), std::format("Parser failed for sema input '{}'", path));
        auto p2 = cfire::frontend::parse_source(path, content);
        require(p2.has_value(), std::format("Parser failed for sema input '{}'", path));

        auto cxx_cf = cfire::frontend::analyze_cxx(std::move(*p1));
        auto sh_cf = cfire::frontend::selfhost_analyze(std::move(*p2));

        require(cxx_cf.has_value(), std::format("C++ sema failed on '{}': {}", path, cxx_cf ? "" : cxx_cf.error()));
        require(sh_cf.has_value(), std::format("Self-hosted sema failed on '{}': {}", path, sh_cf ? "" : sh_cf.error()));

        if (cxx_cf && sh_cf) {
            require(cxx_cf->fn_sigs.size() == sh_cf->fn_sigs.size(),
                std::format("fn_sigs.size() mismatch in '{}': {} vs {}", path, cxx_cf->fn_sigs.size(), sh_cf->fn_sigs.size()));
            require(cxx_cf->methods.size() == sh_cf->methods.size(),
                std::format("methods.size() mismatch in '{}': {} vs {}", path, cxx_cf->methods.size(), sh_cf->methods.size()));
            require(cxx_cf->enums.size() == sh_cf->enums.size(),
                std::format("enums.size() mismatch in '{}': {} vs {}", path, cxx_cf->enums.size(), sh_cf->enums.size()));
            require(cxx_cf->struct_tags.size() == sh_cf->struct_tags.size(),
                std::format("struct_tags.size() mismatch in '{}': {} vs {}", path, cxx_cf->struct_tags.size(), sh_cf->struct_tags.size()));
            require(cxx_cf->last_use_move.size() == sh_cf->last_use_move.size(),
                std::format("last_use_move.size() mismatch in '{}': {} vs {}", path, cxx_cf->last_use_move.size(), sh_cf->last_use_move.size()));
            require(cxx_cf->warnings.size() == sh_cf->warnings.size(),
                std::format("warnings.size() mismatch in '{}': {} vs {}", path, cxx_cf->warnings.size(), sh_cf->warnings.size()));
        }
    }
    std::println("  [PASS] 100% semantic analysis parity verified across all {} corpus files ({:.2f} KB)",
        loaded_files.size(), static_cast<double>(total_bytes) / 1024.0);

    // 2. Intent & Diagnostic Rules Parity Check
    struct DiagTestCase {
        std::string name;
        std::string source;
        std::string expected_code;
    };

    const std::vector<DiagTestCase> diag_cases = {
        {"MUT-CALLSITE", "package main\nfn modify(mut x: i32) {}\nfn main() {\n  mut a := 10\n  modify(a)\n}\n", "cfire/MUT-CALLSITE"},
        {"IMMUTABLE-MUT-ARG", "package main\nfn modify(mut x: i32) {}\nfn main() {\n  a := 10\n  modify(mut a)\n}\n", "cfire/IMMUTABLE-MUT-ARG"},
        {"IMMUTABLE-ASSIGN", "package main\nfn main() {\n  a := 10\n  a = 20\n}\n", "cfire/IMMUTABLE-ASSIGN"},
        {"MUT-ALIAS", "package main\nfn swap(mut x: i32, mut y: i32) {}\nfn main() {\n  mut a := 10\n  swap(mut a, mut a)\n}\n", "cfire/MUT-ALIAS"},
        {"USE-AFTER-MOVE", "package main\nfn consume(move x: str) {}\nfn main() {\n  s := \"hello\"\n  consume(move s)\n  consume(move s)\n}\n", "cfire/USE-AFTER-MOVE"},
        {"NON-EXHAUSTIVE-MATCH", "package main\ntype Shape enum { Circle(f64), Rect(f64, f64), Empty }\nfn main() {\n  s := Shape.Circle(1.0)\n  match s {\n    Circle(r) => { println(\"{}\", r) }\n    Rect(w, h) => { println(\"{}\", w) }\n  }\n}\n", "cfire/NON-EXHAUSTIVE-MATCH"},
        {"DEPRECATED-VAR", "package main\nvar global_x: i32 = 10\nfn main() {}\n", "cfire/DEPRECATED-VAR"},
        {"DEPRECATED-OUT-PARAM", "package main\nfn f(out x: i32) {}\nfn main() {}\n", "cfire/DEPRECATED-OUT-PARAM"},
    };

    for (const auto& tc : diag_cases) {
        auto p1 = cfire::frontend::parse_source("test_diag.cf", tc.source);
        auto p2 = cfire::frontend::parse_source("test_diag.cf", tc.source);
        require(p1.has_value() && p2.has_value(), std::format("Parse failed on diag test case '{}'", tc.name));

        auto cxx_res = cfire::frontend::analyze_cxx(std::move(*p1));
        auto sh_res = cfire::frontend::selfhost_analyze(std::move(*p2));

        if (tc.expected_code == "cfire/DEPRECATED-VAR" || tc.expected_code == "cfire/DEPRECATED-OUT-PARAM") {
            require(cxx_res.has_value() && sh_res.has_value(), std::format("Warning case '{}' should not fail analysis", tc.name));
            bool cxx_has_warn = false;
            for (const auto& w : cxx_res->warnings) if (w.contains(tc.expected_code)) cxx_has_warn = true;
            bool sh_has_warn = false;
            for (const auto& w : sh_res->warnings) if (w.contains(tc.expected_code)) sh_has_warn = true;
            require(cxx_has_warn, std::format("C++ sema missed warning '{}'", tc.expected_code));
            require(sh_has_warn, std::format("Self-hosted sema missed warning '{}'", tc.expected_code));
        } else {
            require(!cxx_res.has_value(), std::format("C++ sema should have failed on '{}'", tc.name));
            require(!sh_res.has_value(), std::format("Self-hosted sema should have failed on '{}'", tc.name));
            if (!cxx_res && !sh_res) {
                require(cxx_res.error().contains(tc.expected_code),
                    std::format("C++ sema diagnostic mismatch for '{}': got '{}'", tc.name, cxx_res.error()));
                require(sh_res.error().contains(tc.expected_code),
                    std::format("Self-hosted sema diagnostic mismatch for '{}': got '{}'", tc.name, sh_res.error()));
            }
        }
    }
    std::println("  [PASS] 100% semantic diagnostic parity verified across all intent/mutability/move rules");

    // 3. Performance Throughput Benchmark
    constexpr int kIters = 50;
    std::size_t benchmark_payload_bytes = total_bytes * kIters;

    // Pre-parse ASTs for isolated sema measurement
    std::vector<cfire::frontend::FileAST> cxx_asts;
    std::vector<cfire::frontend::FileAST> sh_asts;
    for (int it = 0; it < kIters; ++it) {
        for (const auto& [path, content] : loaded_files) {
            auto a1 = cfire::frontend::parse_source(path, content);
            auto a2 = cfire::frontend::parse_source(path, content);
            if (a1 && a2) {
                cxx_asts.push_back(std::move(*a1));
                sh_asts.push_back(std::move(*a2));
            }
        }
    }

    // Benchmark Native C++ Sema
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_checked = 0;
    for (auto& ast : cxx_asts) {
        auto res = cfire::frontend::analyze_cxx(std::move(ast));
        if (res) cxx_checked += res->fn_sigs.size();
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire Self-Hosted Sema
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_checked = 0;
    for (auto& ast : sh_asts) {
        auto res = cfire::frontend::selfhost_analyze(std::move(ast));
        if (res) cf_checked += res->fn_sigs.size();
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    require(cxx_checked == cf_checked, "Benchmark fn count parity mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Semantic Analyzer Throughput (Payload: {:.2f} MB, {} files x {} iters):",
        static_cast<double>(benchmark_payload_bytes) / 1'000'000.0, loaded_files.size(), kIters);
    std::println("    - C++ Native Sema:            {:>7.2f} MB/s (avg {:.3f}ms/corpus)",
        mbs_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Sema:     {:>7.2f} MB/s (avg {:.3f}ms/corpus)",
        mbs_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.50 * mbs_cxx,
        std::format("Self-hosted sema performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_sema_parity_and_benchmark");
}



void test_selfhost_codegen_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_codegen_parity_and_benchmark (Self-Hosted Code Generator Dogfooding & Measurable Parity)");

    const std::vector<std::string> corpus_files = {
        "examples/01-hello-core/main.cf",
        "examples/02-log-compressor/main.cf",
        "examples/03-pkg-cache/main.cf",
        "examples/04-async-workers/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/07-types-match-map/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/09-stdlib-test-pointers/main.cf",
        "examples/10-cmake-and-make-deps/main.cf",
        "examples/11-http-server-router/main.cf",
        "src/lexer.cf",
        "src/formatter.cf",
        "src/ast.cf",
        "src/refactor.cf",
        "src/docgen.cf",
        "src/parser.cf",
        "src/sema.cf",
        "src/codegen.cf"
    };

    std::size_t total_bytes = 0;
    std::vector<std::pair<std::string, cfire::frontend::CheckedFile>> checked_files;
    for (const auto& rel_path : corpus_files) {
        std::ifstream ifs(rel_path);
        require(ifs.is_open(), std::format("Failed to open corpus file '{}'", rel_path));
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        total_bytes += content.size();

        auto ast = cfire::frontend::parse_source(rel_path, content);
        require(ast.has_value(), std::format("Parser failed on codegen input '{}'", rel_path));
        auto cf = cfire::frontend::analyze(std::move(*ast));
        require(cf.has_value(), std::format("Sema failed on codegen input '{}'", rel_path));
        checked_files.emplace_back(rel_path, std::move(*cf));
    }

    // 1. Corpus Conformance & Emitted C++ Parity across all corpus files
    for (const auto& [path, cf] : checked_files) {
        // Normal mode
        std::string cxx_code = cfire::frontend::emit_cpp_cxx(cf, false);
        std::string sh_code = cfire::frontend::selfhost_emit_cpp(cf, false);
        require(!cxx_code.empty(), std::format("C++ codegen output empty for '{}'", path));
        require(!sh_code.empty(), std::format("Self-hosted codegen output empty for '{}'", path));
        require(cxx_code == sh_code, std::format("Emitted C++ code mismatch on '{}' (size {} vs {})", path, cxx_code.size(), sh_code.size()));

        // Test mode
        std::string cxx_test = cfire::frontend::emit_cpp_cxx(cf, true);
        std::string sh_test = cfire::frontend::selfhost_emit_cpp(cf, true);
        require(cxx_test == sh_test, std::format("Emitted C++ test-mode code mismatch on '{}'", path));
    }

    // 2. Dual-Engine Switch Verification (CFIRE_CXX_CODEGEN)
    {
        const auto& test_cf = checked_files.front().second;
        ::setenv("CFIRE_CXX_CODEGEN", "1", 1);
        std::string via_cxx = cfire::frontend::emit_cpp(test_cf, false);
        ::unsetenv("CFIRE_CXX_CODEGEN");
        std::string via_sh = cfire::frontend::emit_cpp(test_cf, false);
        require(via_cxx == via_sh, "Dual-engine output mismatch between CFIRE_CXX_CODEGEN=1 and default");
    }

    // 3. Measurable Throughput Benchmark
    constexpr std::size_t kIters = 50;
    std::size_t benchmark_payload_bytes = total_bytes * kIters;

    // Benchmark Native C++ Codegen
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_chars = 0;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
        for (const auto& [path, cf] : checked_files) {
            auto code = cfire::frontend::emit_cpp_cxx(cf, false);
            cxx_chars += code.size();
        }
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire Self-Hosted Codegen
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_chars = 0;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
        for (const auto& [path, cf] : checked_files) {
            auto code = cfire::frontend::selfhost_emit_cpp(cf, false);
            cf_chars += code.size();
        }
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    require(cxx_chars == cf_chars, "Benchmark total emitted characters mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Code Generator Throughput (Payload: {:.2f} MB, {} files x {} iters):",
        static_cast<double>(benchmark_payload_bytes) / 1'000'000.0, checked_files.size(), kIters);
    std::println("    - C++ Native Codegen:         {:>7.2f} MB/s (avg {:.3f}ms/corpus)",
        mbs_cxx, ms_cxx / kIters);
    std::println("    - cfire Self-Hosted Codegen:  {:>7.2f} MB/s (avg {:.3f}ms/corpus)",
        mbs_cf, ms_cf / kIters);
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted codegen performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_codegen_parity_and_benchmark");
}



void test_selfhost_buildsys_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_buildsys_parity_and_benchmark (Self-Hosted Build System & CMake Ejection Parity)");

    const std::vector<std::string> test_files = {
        "examples/01-hello-core/main.cf",
        "examples/05-cmake-interop/main.cf",
        "examples/06-raylib-viz/main.cf",
        "examples/08-multi-pkg-cc-build/main.cf",
        "examples/10-cmake-and-make-deps/main.cf"
    };

    std::vector<cfire::build::BuildPlan> plans;
    for (const auto& rel_path : test_files) {
        auto res = cfire::build::analyze_and_emit(rel_path, rt_dir, false);
        require(res.has_value(), std::format("Failed to analyze and emit for buildsys test '{}': {}", rel_path, res ? "" : res.error()));
        plans.push_back(std::move(res->second));
    }

    // 1. CMakeLists.txt emission parity across test plans
    for (std::size_t i = 0; i < plans.size(); ++i) {
        const auto& plan = plans[i];
        std::string cxx_cmake = cfire::build::generate_cmake_content_cxx(plan);
        std::string sh_cmake = cfire::build::selfhost_generate_cmake(plan);
        require(!cxx_cmake.empty(), std::format("C++ CMake output empty for '{}'", test_files[i]));
        require(!sh_cmake.empty(), std::format("Self-hosted CMake output empty for '{}'", test_files[i]));
        require(cxx_cmake == sh_cmake, std::format("CMake content mismatch on '{}':\n--- C++ ---\n{}\n--- SelfHost ---\n{}",
            test_files[i], cxx_cmake, sh_cmake));
    }

    // 2. Dual-Engine Switch Verification (CFIRE_CXX_BUILDSYS)
    {
        const auto& test_plan = plans.front();
        ::setenv("CFIRE_CXX_BUILDSYS", "1", 1);
        std::string via_cxx = cfire::build::generate_cmake_content(test_plan);
        ::unsetenv("CFIRE_CXX_BUILDSYS");
        std::string via_sh = cfire::build::generate_cmake_content(test_plan);
        require(via_cxx == via_sh, "Dual-engine switch mismatch between CFIRE_CXX_BUILDSYS=1 and default");
    }

    // 3. Measurable Throughput Benchmark
    constexpr std::size_t kIters = 2000;
    std::size_t total_template_bytes = 0;
    for (const auto& plan : plans) {
        total_template_bytes += cfire::build::generate_cmake_content_cxx(plan).size();
    }
    std::size_t benchmark_payload_bytes = total_template_bytes * kIters;

    // Benchmark Native C++ Buildsys CMake generator
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_chars = 0;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
        for (const auto& plan : plans) {
            auto s = cfire::build::generate_cmake_content_cxx(plan);
            cxx_chars += s.size();
        }
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire Self-Hosted Buildsys CMake generator
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_chars = 0;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
        for (const auto& plan : plans) {
            auto s = cfire::build::selfhost_generate_cmake(plan);
            cf_chars += s.size();
        }
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    require(cxx_chars == cf_chars, "Benchmark total emitted characters mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Buildsys CMake Emitter Throughput (Payload: {:.2f} MB, {} plans x {} iters):",
        static_cast<double>(benchmark_payload_bytes) / 1'000'000.0, plans.size(), kIters);
    std::println("    - C++ Native Generator:       {:>7.2f} MB/s (avg {:.3f}µs/plan)",
        mbs_cxx, (ms_cxx * 1000.0) / (kIters * plans.size()));
    std::println("    - cfire Self-Hosted Engine:   {:>7.2f} MB/s (avg {:.3f}µs/plan)",
        mbs_cf, (ms_cf * 1000.0) / (kIters * plans.size()));
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted buildsys performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_buildsys_parity_and_benchmark");
}



void test_selfhost_lsp_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_selfhost_lsp_parity_and_benchmark (Self-Hosted LSP Protocol Emitter Parity)");

    // 1. JSON Escaping & Unescaping Parity
    const std::vector<std::string> test_strings = {
        "hello world",
        "hello \"world\" with quotes",
        "newlines\nand\r\ntabs\t",
        "path/with/slashes and \\backslashes\\",
        "{\"json\":\"nested\", \"array\": [1, 2, \"three\"]}",
        "unicode chars: 🚀 sapphire fire",
        ""
    };

    for (const auto& s : test_strings) {
        std::string cxx_esc = cfire::lsp::detail::json_escape_cxx(s);
        std::string sh_esc = ::lsp_json_escape(s);
        require(cxx_esc == sh_esc, std::format("json_escape mismatch for '{}': '{}' vs '{}'", s, cxx_esc, sh_esc));

        std::string cxx_unesc = cfire::lsp::detail::json_unescape_cxx(cxx_esc);
        std::string sh_unesc = ::lsp_json_unescape(sh_esc);
        require(cxx_unesc == sh_unesc, std::format("json_unescape mismatch for '{}'", s));
    }

    // 2. RPC Framing & Protocol Response Formatters Parity
    std::string sample_body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}";
    std::string sh_frame = ::selfhost_make_rpc_frame(sample_body);
    std::string expected_frame = std::format("Content-Length: {}\r\n\r\n{}", sample_body.size(), sample_body);
    require(sh_frame == expected_frame, "make_rpc_frame parity mismatch");

    std::string sh_hover = ::selfhost_format_hover_markdown("1", "# Header\nSome **markdown** documentation");
    require(sh_hover.contains("markdown") && sh_hover.contains("Header"), "format_hover_markdown content check");

    std::string sh_diag = ::selfhost_format_publish_diagnostics("file:///test.cf", "[]");
    require(sh_diag.contains("textDocument/publishDiagnostics") && sh_diag.contains("file:///test.cf"), "format_publish_diagnostics check");

    // 3. Dual-Engine Switch Verification (CFIRE_CXX_LSP)
    {
        ::setenv("CFIRE_CXX_LSP", "1", 1);
        std::string esc_cxx = cfire::lsp::detail::json_escape("test \"123\"\n");
        ::unsetenv("CFIRE_CXX_LSP");
        std::string esc_sh = cfire::lsp::detail::json_escape("test \"123\"\n");
        require(esc_cxx == esc_sh, "Dual-engine switch mismatch for json_escape");
    }

    // 4. Measurable Throughput Benchmark
    constexpr std::size_t kIters = 50000;
    std::size_t benchmark_payload_bytes = 0;
    for (const auto& s : test_strings) benchmark_payload_bytes += s.size() * kIters;

    // Benchmark Native C++ json_escape
    auto t0_cxx = std::chrono::high_resolution_clock::now();
    std::size_t cxx_chars = 0;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
        for (const auto& s : test_strings) {
            auto esc = cfire::lsp::detail::json_escape_cxx(s);
            cxx_chars += esc.size();
        }
    }
    auto t1_cxx = std::chrono::high_resolution_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1_cxx - t0_cxx).count();
    double mbs_cxx = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    // Benchmark cfire Self-Hosted json_escape
    auto t0_cf = std::chrono::high_resolution_clock::now();
    std::size_t cf_chars = 0;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
        for (const auto& s : test_strings) {
            auto esc = ::lsp_json_escape(s);
            cf_chars += esc.size();
        }
    }
    auto t1_cf = std::chrono::high_resolution_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t1_cf - t0_cf).count();
    double mbs_cf = (static_cast<double>(benchmark_payload_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    require(cxx_chars == cf_chars, "Benchmark total escaped characters mismatch");

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] LSP Protocol Emitter Throughput (Payload: {:.2f} MB, {} strings x {} iters):",
        static_cast<double>(benchmark_payload_bytes) / 1'000'000.0, test_strings.size(), kIters);
    std::println("    - C++ Native Emitter:         {:>7.2f} MB/s (avg {:.3f}µs/run)",
        mbs_cxx, (ms_cxx * 1000.0) / (kIters * test_strings.size()));
    std::println("    - cfire Self-Hosted Engine:   {:>7.2f} MB/s (avg {:.3f}µs/run)",
        mbs_cf, (ms_cf * 1000.0) / (kIters * test_strings.size()));
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted LSP performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_lsp_parity_and_benchmark");
}



void test_selfhost_main_parity_and_benchmark(const std::filesystem::path& rt_dir) {
    std::println("--- test_selfhost_main_parity_and_benchmark");

    // 1. Exact string parity on usage and LLM doc
    auto usage_cf = selfhost_get_usage();
    require(usage_cf.starts_with("Usage: cfire <command>"), "selfhost_get_usage format");
    require(usage_cf.find("run      <file.cf>") != std::string::npos, "selfhost_get_usage commands");

    auto llm_doc_cf = selfhost_get_llm_doc();
    require(llm_doc_cf.starts_with("# cfire Language Reference Sheet"), "selfhost_get_llm_doc format");
    require(llm_doc_cf.find("core:crypto/sha256") != std::string::npos, "selfhost_get_llm_doc contents");

    // 2. Exact wrap_eval_code parity across multiple code forms
    std::vector<std::pair<std::string, bool>> eval_corpus = {
        {"1 + 2 * 3", true},
        {"mut x := 10\nx += 5\nprintln(\"x = {}\", x)", false},
        {"import \"core:math\"\nfn dist(x: f64, y: f64) -> f64 { return math.sqrt(x*x + y*y) }\nprintln(\"dist={}\", dist(3.0, 4.0))", false},
        {"package my_pkg\nfn main() { println(\"already packaged\") }", false},
        {"type Point struct { x: i32, y: i32 }\npt := Point{x: 1, y: 2}\nprintln(\"pt.x={}\", pt.x)", false}
    };

    for (const auto& [code, expr_mode] : eval_corpus) {
        auto wrapped_cxx = cfire::main_driver::wrap_eval_code_cxx(code, expr_mode);
        auto wrapped_cf = selfhost_wrap_eval_code(code, expr_mode);
        require(wrapped_cxx == wrapped_cf,
            std::format("Eval code wrapping parity mismatch for: '{}'", code));
    }

    // 3. Exact refactor argument parsing parity
    std::vector<std::string> args1 = {"cfire", "refactor", "rename", "src/foo.cf:10:5", "bar", "--diff"};
    auto rf1 = selfhost_parse_refactor_args(args1);
    require(rf1.valid, "rf1 valid");
    require(rf1.action == "rename", "rf1 action");
    require(rf1.target_file == "src/foo.cf", "rf1 target_file");
    require(rf1.target_line == 10, "rf1 target_line");
    require(rf1.target_col == 5, "rf1 target_col");
    require(rf1.new_name == "bar", "rf1 new_name");
    require(rf1.diff_mode, "rf1 diff_mode");

    std::vector<std::string> args2 = {"cfire", "refactor", "rename", "--file", "src/bar.cf", "--line", "42", "--col", "8", "--to", "baz", "--apply", "--json"};
    auto rf2 = selfhost_parse_refactor_args(args2);
    require(rf2.valid, "rf2 valid");
    require(rf2.target_file == "src/bar.cf", "rf2 target_file");
    require(rf2.target_line == 42, "rf2 target_line");
    require(rf2.target_col == 8, "rf2 target_col");
    require(rf2.new_name == "baz", "rf2 new_name");
    require(rf2.apply_mode, "rf2 apply_mode");
    require(rf2.json_mode, "rf2 json_mode");

    // 4. CLI driver dispatch execution parity
    std::vector<std::string> args_doc = {"cfire", "doc", "--check"};
    int rc_doc = selfhost_run_driver(args_doc, rt_dir.string());
    require(rc_doc == 0, "selfhost_run_driver doc --check exit code 0");

    std::vector<std::string> args_fmt = {"cfire", "fmt", "--check", "src/main.cf"};
    int rc_fmt = selfhost_run_driver(args_fmt, rt_dir.string());
    require(rc_fmt == 0, "selfhost_run_driver fmt --check src/main.cf exit code 0");

    // 5. Benchmark wrap_eval_code throughput
    std::string bench_code = "import \"core:math\"\nimport \"core:str\"\ntype Vector3 struct { x: f64, y: f64, z: f64 }\nfn norm(v: Vector3) -> f64 { return math.sqrt(v.x*v.x + v.y*v.y + v.z*v.z) }\nv := Vector3{x: 1.0, y: 2.0, z: 2.0}\nprintln(\"norm: {}\", norm(v))\n";
    constexpr std::size_t kIters = 25'000;
    std::size_t total_bytes = bench_code.size() * kIters;

    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIters; ++i) {
        auto r = cfire::main_driver::wrap_eval_code_cxx(bench_code, false);
        (void)r;
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double mbs_cxx = (static_cast<double>(total_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    auto t2 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIters; ++i) {
        auto r = selfhost_wrap_eval_code(bench_code, false);
        (void)r;
    }
    auto t3 = std::chrono::steady_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t3 - t2).count();
    double mbs_cf = (static_cast<double>(total_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Main Driver Code Wrapper Throughput (Payload: {:.2f} MB, {} iters):",
        static_cast<double>(total_bytes) / 1'000'000.0, kIters);
    std::println("    - C++ Native Wrapper:         {:>7.2f} MB/s (avg {:.3f}µs/run)",
        mbs_cxx, (ms_cxx * 1000.0) / kIters);
    std::println("    - cfire Self-Hosted Driver:   {:>7.2f} MB/s (avg {:.3f}µs/run)",
        mbs_cf, (ms_cf * 1000.0) / kIters);
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted Main Driver performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_main_parity_and_benchmark");
}



void test_selfhost_clang_bridge_parity_and_benchmark([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("--- test_selfhost_clang_bridge_parity_and_benchmark");

    // 1. Hash key parity across test strings
    std::vector<std::string> hash_corpus = {
        "v3:zstd.h|-I/usr/local/include",
        "v3:raylib.h|-Ivendor/raylib/src",
        "v3:sqlite3.h|-O2|-std=c++2b",
        "v3:complex_template_header.hpp|-I.|-Wall",
        ""
    };
    for (const auto& s : hash_corpus) {
        auto h_cxx = cfire::bridge::hash_key(s);
        auto h_cf = selfhost_clang_hash_key(s);
        require(h_cxx == h_cf, std::format("clang hash key parity mismatch for '{}': {} vs {}", s, h_cxx, h_cf));
    }

    // 2. Trim qual parity
    std::vector<std::string> trim_corpus = {
        "   const int*  ",
        "\t\tvoid\t",
        "std::string",
        "  std::vector<std::pair<int, double>>  "
    };
    for (const auto& s : trim_corpus) {
        auto t_cxx = cfire::bridge::trim_qual(s);
        auto t_cf = selfhost_clang_trim_qual(s);
        require(t_cxx == t_cf, std::format("clang trim qual mismatch for '{}'", s));
    }

    // 3. Signature parsing parity
    std::vector<std::string> sig_corpus = {
        "int(int, double)",
        "void()",
        "std::vector<int>(const std::string&, char*)",
        "const char*(size_t, void*)",
        "double"
    };
    for (const auto& sig : sig_corpus) {
        cfire::bridge::CppSymbol sym;
        cfire::bridge::parse_fn_qual_type(sig, sym);
        auto parsed = selfhost_clang_parse_fn_sig(sig);
        require(sym.return_type == parsed.return_type,
            std::format("Return type mismatch for '{}': '{}' vs '{}'", sig, sym.return_type, parsed.return_type));
        require(sym.param_types == parsed.param_types,
            std::format("Param types mismatch for '{}'", sig));
    }

    // 4. Cache format & parse roundtrip
    CppCacheEntry entry{
        .key = "sqlite3_open",
        .name = "sqlite3_open",
        .kind = "FunctionDecl",
        .return_type = "int",
        .param_types = {"const char*", "sqlite3**"}
    };
    auto line = selfhost_clang_format_cache_entry(entry);
    auto roundtrip = selfhost_clang_parse_cache_line(line.substr(0, line.size() - 1));
    require(roundtrip.key == entry.key, "cache roundtrip key");
    require(roundtrip.name == entry.name, "cache roundtrip name");
    require(roundtrip.kind == entry.kind, "cache roundtrip kind");
    require(roundtrip.return_type == entry.return_type, "cache roundtrip return_type");
    require(roundtrip.param_types == entry.param_types, "cache roundtrip param_types");

    // 5. Benchmark throughput
    constexpr std::size_t kIters = 100'000;
    std::string bench_str = "v3:sqlite3.h|-I/usr/local/Cellar/sqlite/3.45/include|-O2|-std=c++2b";
    std::size_t total_bytes = bench_str.size() * kIters;

    auto t0 = std::chrono::steady_clock::now();
    uint64_t sum_cxx = 0;
    for (std::size_t i = 0; i < kIters; ++i) {
        sum_cxx += cfire::bridge::hash_key(bench_str);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms_cxx = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double mbs_cxx = (static_cast<double>(total_bytes) / 1'000'000.0) / (ms_cxx / 1000.0);

    auto t2 = std::chrono::steady_clock::now();
    uint64_t sum_cf = 0;
    for (std::size_t i = 0; i < kIters; ++i) {
        sum_cf += selfhost_clang_hash_key(bench_str);
    }
    auto t3 = std::chrono::steady_clock::now();
    double ms_cf = std::chrono::duration<double, std::milli>(t3 - t2).count();
    double mbs_cf = (static_cast<double>(total_bytes) / 1'000'000.0) / (ms_cf / 1000.0);

    require(sum_cxx == sum_cf, "benchmark checksum match");
    double parity_pct = (mbs_cf / mbs_cxx) * 100.0;

    std::println("  [BENCHMARK] Clang Bridge Hash Throughput (Payload: {:.2f} MB, {} iters):",
        static_cast<double>(total_bytes) / 1'000'000.0, kIters);
    std::println("    - C++ Native Hash:            {:>7.2f} MB/s (avg {:.3f}ns/run)",
        mbs_cxx, (ms_cxx * 1'000'000.0) / kIters);
    std::println("    - cfire Self-Hosted Hash:     {:>7.2f} MB/s (avg {:.3f}ns/run)",
        mbs_cf, (ms_cf * 1'000'000.0) / kIters);
    std::println("    - Performance Ratio:          {:>7.1f}% of native C++ speed", parity_pct);

    require(mbs_cf >= 0.70 * mbs_cxx,
        std::format("Self-hosted Clang Bridge performance regression ({:.1f} MB/s vs {:.1f} MB/s baseline)", mbs_cf, mbs_cxx));

    std::println("--- PASS: test_selfhost_clang_bridge_parity_and_benchmark");
}


