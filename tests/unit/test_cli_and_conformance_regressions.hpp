#pragma once
// tests/unit/test_cli_and_conformance_regressions.hpp — CLI script discovery and formatter flag regressions. Language behavior is
// pinned by the conformance corpus (`syntax/014`-`015`, `types/026`-`029`, `arith/023`-`025`, `stdlib/...`).

#include "../test_harness.hpp"

void test_standalone_scripts_and_fmt_flags([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_standalone_scripts_and_fmt_flags");
    // a folder of standalone scripts (every file has its own `fn main`) runs one file without the helper-note warning.
    {
        ScopedTempDir dir("cfire_standalone_scripts");
        auto a = dir.file("day1_01.cf", "package main\n\nfn main() {\n    println(\"one\")\n}\n");
        dir.file("day1_02.cf", "package main\n\nfn main() {\n    println(\"two\")\n}\n");
        auto r = run_cfire(std::format("run \"{}\"", a.string()));
        require(r.ok() && r.output.contains("one"), std::format("a standalone script runs, got: {}", r.output));
        require(!r.output.contains("note: compiling only"), "no note when every sibling has its own fn main");
    }
    // ...but a sibling without `fn main` is part of the package, so the note stays.
    {
        ScopedTempDir dir("cfire_pkg_scripts");
        auto m = dir.file("main.cf", "package main\n\nfn main() {\n    println(\"main\")\n}\n");
        dir.file("helper.cf", "package main\n\nfn helper() -> i64 {\n    return 1\n}\n");
        auto r = run_cfire(std::format("run \"{}\"", m.string()));
        require(r.output.contains("note: compiling only main.cf"), std::format("the helper note stays for a package helper, got: {}", r.output));
    }
    // `cfire fmt` rejects an unknown flag instead of rewriting files in place.
    {
        ScopedTempDir dir("cfire_fmt_flags");
        const std::string src = "package main\n\nfn main() {\n    mut g: [][] i64 = []\n    println(\"{}\", g.len())\n}\n";
        auto f = dir.file("main.cf", src);
        auto r = run_cfire(std::format("fmt --diff \"{}\"", f.string()));
        require(!r.ok() && r.output.contains("unknown flag '--diff'"), std::format("fmt rejects --diff, got: {}", r.output));
        require(cfire::fs::read_text(f).value_or("") == src, "a rejected fmt call leaves the file untouched");
        auto out = run_cfire(std::format("fmt --stdout \"{}\"", f.string()));
        require(out.output.contains("mut g: [][]i64 = []"), "fmt keeps nested slice types together");
    }
    std::println("--- PASS: test_standalone_scripts_and_fmt_flags");
}
