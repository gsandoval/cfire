#pragma once
// tests/runner_self_tests.hpp — Unit tests for the conformance and GUIDE runners themselves (they replace
// `run_conformance.py --self-test`): directive parsing, and the PASS/FAIL/XFAIL/XPASS classification of small files.

#include "conformance_runner.hpp"
#include "guide_runner.hpp"
#include "../tools/vmtest/vmtest.hpp"

inline void test_conformance_and_guide_runner_self_tests(const std::filesystem::path& rt_dir) {
    std::println("--- Running conformance/GUIDE runner self-tests ---");
    using conformance::Outcome;

    // Directive parsing, no processes involved.
    {
        auto s = conformance::parse_spec("// expect: stdout\n// | a\n// |  b\n// expect: error cfire/X-Y @7:3\n// flags: --sanitize\npackage main\n");
        require(s.has_value(), "parse_spec accepts a stdout + error header");
        require(s && s->stdout_lines && *s->stdout_lines == std::vector<std::string>{"a", " b"}, "stdout lines keep inner spacing");
        require(s && s->errors.contains({"cfire/X-Y", 7, 3}), "error directive with line and column");
        require(s && s->flags == std::vector<std::string>{"--sanitize"}, "flags directive");
        require(!conformance::parse_spec("// expect: xfail later\npackage main\nfn main() {}\n").has_value(), "xfail must specify a topic and reason");
        require(!conformance::parse_spec("package main\nfn main() {}\n").has_value(), "a file must declare an expectation");
        require(!conformance::parse_spec("package main\nfn main() {\n    a() //~ panic \"x\"\n    b() //~ panic \"y\"\n}\n").has_value(),
                "only one panic expectation");
        auto inl = conformance::parse_spec("package main\nfn main() {\n    x = 2 //~ error cfire/ASSIGN-IMMUTABLE\n}\n");
        require(inl && inl->errors.contains({"cfire/ASSIGN-IMMUTABLE", 3, 0}), "inline error is attributed to its own line");
        require(conformance::glob_match("ownership/*", "ownership/a/b.cf") && !conformance::glob_match("ownership/*", "syntax/a.cf"),
                "--filter globs match across directories like fnmatch");
        require(conformance::glob_match("types/slice_[ab]*", "types/slice_a.cf") && !conformance::glob_match("types/slice_[ab]*", "types/slice_c.cf") &&
                    conformance::glob_match("t/[!a]x", "t/bx") && conformance::glob_match("t/[a-c]", "t/b") && !conformance::glob_match("t/[a-c]", "t/d"),
                "--filter supports fnmatch character classes");
        auto rlib = conformance::parse_spec("// requires-lib: libssh\n// expect: stdout\npackage main\n");
        require(rlib && rlib->requires_libs == std::vector<std::string>{"libssh"}, "requires-lib directive parsed");
        require(!conformance::parse_spec("// requires-lib:\n// expect: stdout\npackage main\n").has_value(),
                "requires-lib must name a library");
        require(!conformance::parse_spec("// requires-lib: unknown_lib\n// expect: stdout\npackage main\n").has_value(),
                "requires-lib rejects unknown library");
    }

    // Guide block extraction.
    {
        const std::string md = "x\n```cfire\nA\n```\n\n```text\n# Output:\nhi\n```\n```cfire\nB\n```\n```bash\ncfire eval 'a b' c\n```\n";
        auto t = guide::output_tests(md);
        require(t.size() == 1 && t[0].first == "A" && t[0].second == "hi", "a cfire block followed by # Output: is one doc-test");
        require(guide::shell_words("cfire eval 'a b' \"c\\\"d\" e\\ f") == std::vector<std::string>{"cfire", "eval", "a b", "c\"d", "e f"},
                "shell_words splits like shlex");
    }

    // VM driver settings and the file server's request rules.
    {
        auto none = [](std::string_view) -> std::optional<std::string> { return std::nullopt; };
        auto d = vmtest::load_config(none);
        require(d.vm_linux == "Linux" && d.vm_windows == "Windows" && d.port == 8765 && d.host_ip == "192.168.64.1", "vmtest defaults");
        auto env = [](std::string_view k) -> std::optional<std::string> {
            if (k == "CFIRE_VM_LINUX") return "ubuntu";
            if (k == "CFIRE_VM_PORT") return "9000";
            if (k == "CFIRE_VM_TIMEOUT") return "-5";
            if (k == "CFIRE_VM_SHARE") return "/srv/share";
            return std::nullopt;
        };
        auto c = vmtest::load_config(env);
        require(c.vm_linux == "ubuntu" && c.port == 9000 && c.share == "/srv/share", "CFIRE_VM_* overrides");
        require(c.timeout_s == 10800, "a non-positive CFIRE_VM_TIMEOUT keeps the default");
        const std::filesystem::path root = "/share";
        require(vmtest::resolve_request(root, "GET", "/cfire.tar") == root / "cfire.tar", "GET a share file");
        require(vmtest::resolve_request(root, "GET", "/cfire/scripts/vm/windows_gate.sh?x=1") == root / "cfire/scripts/vm/windows_gate.sh",
                "GET a nested file, query ignored");
        require(vmtest::resolve_request(root, "PUT", "/results/job.log") == root / "results/job.log", "PUT under results/");
        for (std::string_view bad : {"/../etc/passwd", "/results/../x", "/results/.hidden", "/results/a/b", "/", "results/x",
                                     "/results/%2e%2e", "/a\\b", "/c:/x"}) {
            require(!vmtest::resolve_request(root, bad.starts_with("/results") || bad == "results/x" ? "PUT" : "GET", bad),
                    std::format("vmtest refuses '{}'", bad));
        }
        require(!vmtest::resolve_request(root, "PUT", "/cfire.tar") && !vmtest::resolve_request(root, "PUT", "/jobs/x.cmd") && !vmtest::resolve_request(root, "DELETE", "/cfire.tar"),
                "only GET/HEAD outside results/, and PUT only into it");
    }

    // Classification of whole files, through the real ./cfire.
    struct Case {
        std::string_view name, src;
        Outcome want;
    };
    const Case cases[] = {
        {"pass_stdout.cf", "// expect: stdout\n// | hi\npackage main\nfn main() {\n    println(\"hi\")\n}\n", Outcome::Pass},
        {"fail_stdout.cf", "// expect: stdout\n// | bye\npackage main\nfn main() {\n    println(\"hi\")\n}\n", Outcome::Fail},
        {"fail_extra_error.cf", "// expect: stdout\n// | 1\npackage main\nfn main() {\n    x := 1\n    x = 2\n    println(\"{}\", x)\n}\n", Outcome::Fail},
        {"pass_error.cf", "// expect: error cfire/ASSIGN-IMMUTABLE @5\npackage main\nfn main() {\n    x := 1\n    x = 2\n}\n", Outcome::Pass},
        {"fail_wrong_line.cf", "// expect: error cfire/ASSIGN-IMMUTABLE @4\npackage main\nfn main() {\n    x := 1\n    x = 2\n}\n", Outcome::Fail},
        {"xfail_unmet.cf", "// expect: stdout\n// | bye\n// expect: xfail demo-hole: demo\npackage main\nfn main() {\n    println(\"hi\")\n}\n", Outcome::Xfail},
        {"xpass_met.cf", "// expect: stdout\n// | hi\n// expect: xfail demo-hole: demo\npackage main\nfn main() {\n    println(\"hi\")\n}\n", Outcome::Xpass},
        {"bad_directive.cf", "// expect: stdot\npackage main\nfn main() {\n}\n", Outcome::Fail},
        {"pass_inline_error.cf",
         "// expect: xfail demo-hole: shifts must not matter\n// expect: stdout\n// | never\npackage main\nfn main() {\n    x := 1\n    x = 2 //~ error cfire/ASSIGN-IMMUTABLE\n}\n",
         Outcome::Xpass},
        {"bad_inline.cf", "package main\nfn main() {\n    x := 1 //~ eror cfire/X\n}\n", Outcome::Fail},
        {"bad_type_error_expectation.cf", "package main\nfn main() {\n    x := 1 //~ error cfire/TYPE-ERROR\n}\n", Outcome::Fail},
        {"pass_panic.cf", "// expect: panic \"index out of bounds\" @6\npackage main\nfn main() {\n    xs: []i32 = [1]\n    i := 3\n    println(\"{}\", xs[i])\n}\n",
         Outcome::Pass},
    };
    ScopedTempDir dir("cfire_runner_selftest");
    conformance::Options opt;
    opt.cfire = std::filesystem::absolute(cfire_exe()).string();
    opt.runtime_dir = rt_dir;
    opt.timeout = 300;
    std::vector<Outcome> got(std::size(cases));
    parallel_for_each_index(std::size(cases), default_jobs(), [&](std::size_t i) {
        got[i] = conformance::classify(opt, dir.file(cases[i].name, cases[i].src)).outcome;
    });
    for (std::size_t i = 0; i < std::size(cases); ++i) {
        require(got[i] == cases[i].want, std::format("runner classifies {} as {} (got {})", cases[i].name, conformance::outcome_name(cases[i].want),
                                                     conformance::outcome_name(got[i])));
    }

    // Missing optional library yields SKIP (never PASS or FAIL).
    test_set_env("CFIRE_TEST_FORCE_MISSING_LIB", "libssh");
    auto skip_file = dir.file("missing_lib.cf", "// requires-lib: libssh\n// expect: stdout\n// | ok\npackage main\nfn main() {\n    println(\"ok\")\n}\n");
    auto skip_res = conformance::classify(opt, skip_file);
    test_set_env("CFIRE_TEST_FORCE_MISSING_LIB", "");
    require(skip_res.outcome == Outcome::Skip, std::format("missing requires-lib yields SKIP (got {})", conformance::outcome_name(skip_res.outcome)));

    // Composite PCH stem invariance against ephemeral temp -I flags, plus LRU budget eviction.
    {
        ScopedTempDir ephem_inc("pch_ephem_inc");
        const std::string umb = "#pragma once\n#include \"cfire_prelude.hpp\"\n";
        const std::vector<std::string> base_cflags = {"-std=c++2b", "-I" + rt_dir.string()};
        std::vector<std::string> ephem_cflags = base_cflags;
        ephem_cflags.push_back("-I" + ephem_inc.path.string());
        std::vector<std::string> diff_cflags = base_cflags;
        diff_cflags.push_back("-DTEST_PCH_STEM_CHANGE=1");

        const std::string stem_base = cfire::build::compute_composite_pch_stem(rt_dir, umb, "-O2", base_cflags);
        const std::string stem_ephem = cfire::build::compute_composite_pch_stem(rt_dir, umb, "-O2", ephem_cflags);
        const std::string stem_diff = cfire::build::compute_composite_pch_stem(rt_dir, umb, "-O2", diff_cflags);
        require(stem_base == stem_ephem, "compute_composite_pch_stem ignores ephemeral temp -I directories");
        require(stem_base != stem_diff, "compute_composite_pch_stem changes when semantic flags change");

        ScopedTempDir lru_dir("pch_lru_budget");
        const auto write_entry = [&](std::string_view stem, std::size_t bytes, std::chrono::seconds age) {
            auto pch = lru_dir.file(std::format("{}.pch", stem), std::string(bytes, 'P'));
            (void)lru_dir.file(std::format("{}.hpp", stem), "#pragma once\n");
            std::error_code tec;
            std::filesystem::last_write_time(pch, std::filesystem::file_time_type::clock::now() - age, tec);
        };
        write_entry("cfire_rel_oldest00000001", 1024, std::chrono::seconds(300));
        write_entry("cfire_rel_middle00000002", 1024, std::chrono::seconds(150));
        write_entry("cfire_rel_newest00000003", 1024, std::chrono::seconds(10));

        cfire::build::evict_pch_cache_to_budget(lru_dir.path, 1500, "cfire_rel_newest00000003");
        std::error_code eec;
        require(!std::filesystem::exists(lru_dir.path / "cfire_rel_oldest00000001.pch", eec) &&
                    !std::filesystem::exists(lru_dir.path / "cfire_rel_oldest00000001.hpp", eec),
            "evict_pch_cache_to_budget removes oldest .pch and .hpp first");
        require(std::filesystem::exists(lru_dir.path / "cfire_rel_newest00000003.pch", eec) &&
                    std::filesystem::exists(lru_dir.path / "cfire_rel_newest00000003.hpp", eec),
            "evict_pch_cache_to_budget preserves active newest stem within budget");
        uint64_t remaining_pch_bytes = 0;
        for (const auto& de : std::filesystem::directory_iterator(lru_dir.path, eec)) {
            if (de.is_regular_file(eec) && de.path().extension() == ".pch") {
                remaining_pch_bytes += de.file_size(eec);
            }
        }
        require(remaining_pch_bytes <= 1500,
            std::format("evict_pch_cache_to_budget enforces byte budget (remaining={})", remaining_pch_bytes));
    }

    // Parallel test runner CLI flags (--list and --run-single).
    {
        std::error_code rec;
        const std::filesystem::path runner_exe = std::filesystem::current_path() /
#if defined(_WIN32)
            ".cfire_test_runner.exe";
#else
            ".cfire_test_runner";
#endif
        if (std::filesystem::exists(runner_exe, rec)) {
            auto lr = run_process({runner_exe.string(), "--list", "fast"}, std::filesystem::current_path(), 30);
            require(!lr.spawn_failed && !lr.timed_out && lr.exit_code == 0 &&
                        lr.out.find("test_sema_intent_and_move_diagnostics") != std::string::npos &&
                        lr.out.find("test_end_to_end_canonical_examples") == std::string::npos,
                "--list fast lists offline compiler suites and omits loopback/e2e suites");
            auto sr = run_process({runner_exe.string(), "--run-single", "test_sema_intent_and_move_diagnostics"},
                                  std::filesystem::current_path(), 60);
            require(!sr.spawn_failed && !sr.timed_out && sr.exit_code == 0,
                "--run-single executes a single unit suite cleanly");
        }
    }
}
