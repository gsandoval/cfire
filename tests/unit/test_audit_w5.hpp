#pragma once
// tests/unit/test_audit_w5.hpp — Wave W5 (tooling & docs polish) regression tests.

#include "../test_harness.hpp"
#include "../../src/formatter.hpp"
#include "../../src/refactor.hpp"
#include "../../src/docgen/catalog.hpp"
#include <regex>
#include <set>

namespace w5_detail {
[[nodiscard]] inline auto strip_line_directives(std::string_view cpp) -> std::string {
    std::string out;
    std::size_t pos = 0;
    while (pos < cpp.size()) {
        auto nl = cpp.find('\n', pos);
        std::string_view line = cpp.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        if (line.find_first_not_of(' ') == std::string_view::npos || !line.substr(line.find_first_not_of(' ')).starts_with("#line")) {
            out.append(line);
            out.push_back('\n');
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    // Source locations passed to runtime checks (`index_at(xs, i, "f.cf", 18)`) are positions, like #line.
    return std::regex_replace(out, std::regex(R"((\.cf", )[0-9]+)"), "$1L");
}
} // namespace w5_detail

// Q34 (CF-156): the in/out corpus round-trips, formatting is idempotent, and it never changes the lowered C++.
void test_cf156_formatter_corpus([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf156_formatter_corpus (CF-156)");
    std::size_t pairs = 0;
    for (const auto& entry : std::filesystem::directory_iterator("tests/fmt/in")) {
        if (entry.path().extension() != ".cf") continue;
        ++pairs;
        const auto name = entry.path().filename().string();
        auto in = cfire::build::read_file_text(entry.path());
        auto want = cfire::build::read_file_text(std::filesystem::path("tests/fmt/out") / name);
        require(in.has_value() && want.has_value(), std::format("CF-156: corpus pair {} readable", name));
        if (!in || !want) continue;
        auto got = cfire::fmt::format_source(*in, name);
        require(got.has_value() && *got == *want, std::format("CF-156: {} formats to tests/fmt/out/{}, got:\n{}", name, name, got ? *got : got.error()));
        auto again = cfire::fmt::format_source(*want, name);
        require(again.has_value() && *again == *want, std::format("CF-156: tests/fmt/out/{} is a fixed point", name));
        // The lowered C++ is identical modulo #line.
        auto lower = [&](const std::string& src) -> std::string {
            auto ast = cfire::frontend::parse_source(name, src);
            if (!ast) return "parse error: " + ast.error();
            auto checked = cfire::frontend::analyze_resilient(std::move(*ast));
            return w5_detail::strip_line_directives(cfire::frontend::emit_cpp(checked));
        };
        require(lower(*in) == lower(*want), std::format("CF-156: formatting {} does not change generated C++", name));
    }
    require(pairs >= 3, "CF-156: tests/fmt corpus has pairs");
    // Specific canonical forms from the audit (C2).
    auto f = cfire::fmt::format_source("package main\ntype   P struct{\n    b : str\n}\nfn  f( mut p:P)->i32{ return 1; }\nfn main() { x := 1; y := x }\n", "c2.cf");
    require(f.has_value() && f->contains("type P struct {\n    b: str\n}") && f->contains("fn f(mut p: P) -> i32 {") &&
            f->contains("fn main() {\n    x := 1\n    y := x\n}"), std::format("CF-156: canonical spacing and split one-liners, got: {}", f ? *f : f.error()));
    auto c = cfire::fmt::format_source("package main\n/* keep  this */\nfn main() {\n    x := 1 /* a+b */ + 2 // c  d\n}\n", "comments.cf");
    require(c.has_value() && c->contains("/* keep  this */") && c->contains("x := 1 /* a+b */ + 2 // c  d"), std::format("CF-156: comments are kept verbatim, got: {}", c ? *c : c.error()));
    std::println("--- PASS: test_cf156_formatter_corpus (CF-156)");
}

// CF-155: closure parameters and match bindings are separate rename scopes; diffs apply with `git apply`; `--help` per command.
void test_cf155_rename_scopes_and_diff([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf155_rename_scopes_and_diff (CF-155)");
    ScopedTempDir dir("cfire_cf155");
    std::filesystem::create_directory(dir.path / ".git");
    auto file = dir.file("main.cf", R"(package main

type Opt enum {
    Some(i32)
    Nothing
}

fn main() {
    count := 10
    f := |count: i32| { return count * 2 }
    o := Opt::Some(4)
    r := match o {
        Opt::Some(count) => count + 1,
        Opt::Nothing => count
    }
    println("{} {} {}", count, f(3), r)
}
)");
    auto outer = cfire::refactor::plan_rename(file, 9, 5, "n");
    require(outer.success && outer.total_occurrences == 3, std::format("CF-155: outer rename touches 3 occurrences, got {} ({})", outer.total_occurrences, outer.error));
    if (outer.success && !outer.modifications.empty()) {
        const auto& m = outer.modifications[0].modified_content;
        require(m.contains("|count: i32| { return count * 2 }") && m.contains("Opt::Some(count) => count + 1") && m.contains("Opt::Nothing => n"),
            "CF-155: closure parameter and match binding keep their names");
    }
    auto inner = cfire::refactor::plan_rename(file, 10, 11, "c");
    require(inner.success && inner.total_occurrences == 2, std::format("CF-155: closure-parameter rename stays in the closure, got {}", inner.total_occurrences));
    // The diff uses a/ b/ relative paths and applies with git apply.
    auto diff = run_cfire(std::format("refactor rename \"{}:9:5\" n --diff", file.string()));
    require(diff.output.contains("--- a/") && !diff.output.contains("a//"), "CF-155: diff paths are a/<path>, never a//");
    auto help = run_cfire("refactor rename --help");
    require(help.output.contains("Usage: cfire refactor rename") && !help.output.contains("Commands:"), "CF-155: refactor --help prints the command's own usage");
    std::println("--- PASS: test_cf155_rename_scopes_and_diff (CF-155)");
}

// CF-158: the GUIDE's diagnostics table covers every code the compiler emits, and docs hold no local links.
void test_cf158_doc_integrity([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf158_doc_integrity (CF-158)");
    std::set<std::string> emitted;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("src")) {
        if (!entry.is_regular_file() || (entry.path().extension() != ".hpp" && entry.path().extension() != ".cpp")) continue;
        auto txt = cfire::build::read_file_text(entry.path());
        if (!txt) continue;
        for (std::size_t p = txt->find("\"cfire/"); p != std::string::npos; p = txt->find("\"cfire/", p + 1)) {
            auto e = txt->find('"', p + 1);
            std::string code = txt->substr(p + 1, e - p - 1);
            if (code.size() > 6 && std::all_of(code.begin() + 6, code.end(), [](char ch) { return std::isupper(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)) || ch == '-'; })) {
                emitted.insert(code);
            }
        }
    }
    const auto& reg = cfire::doc::diagnostic_code_registry();
    for (const auto& code : emitted) {
        require(std::any_of(reg.begin(), reg.end(), [&](const auto& d) { return d.code == code; }),
            std::format("CF-158: {} is emitted by the compiler but missing from diagnostic_code_registry()", code));
    }
    auto guide = cfire::build::read_file_text("docs/GUIDE.md");
    require(guide && !guide->contains("file://") && !guide->contains("](/Users/"), "CF-158: GUIDE has no local file links");
    require(guide && guide->contains("| `cfire/RETIRED-API` |") && guide->contains("| `cfire/FIELD-NAME-CASE` |"), "CF-158: GUIDE diagnostics table is generated from the registry");
    std::println("--- PASS: test_cf158_doc_integrity (CF-158)");
}
