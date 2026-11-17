#pragma once
// tests/unit/test_tooling_docs_polish.hpp — Formatter, rename scope, and documentation integrity regression tests.

#include "../test_harness.hpp"
#include "../process_run.hpp"
#include "../../src/formatter.hpp"
#include "../../src/refactor.hpp"
#include "../../src/docgen/catalog.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <vector>

namespace tooling_polish_detail {
[[nodiscard]] inline auto wildcard_match(std::string_view pat, std::string_view text) -> bool {
    std::size_t pi = 0, ti = 0, star = std::string_view::npos, match_ti = 0;
    while (ti < text.size()) {
        if (pi < pat.size() && pat[pi] == text[ti]) {
            ++pi;
            ++ti;
        } else if (pi < pat.size() && pat[pi] == '*') {
            star = pi++;
            match_ti = ti;
        } else if (star != std::string_view::npos) {
            pi = star + 1;
            ti = ++match_ti;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '*') ++pi;
    return pi == pat.size();
}

[[nodiscard]] inline auto expand_single_braces(std::string_view tok) -> std::vector<std::string> {
    const auto lb = tok.find('{');
    const auto rb = tok.find('}', lb == std::string_view::npos ? 0 : lb + 1);
    if (lb == std::string_view::npos || rb == std::string_view::npos) {
        return {std::string(tok)};
    }
    const std::string_view prefix = tok.substr(0, lb);
    const std::string_view suffix = tok.substr(rb + 1);
    const std::string_view inner = tok.substr(lb + 1, rb - lb - 1);
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= inner.size()) {
        auto comma = inner.find(',', start);
        if (comma == std::string_view::npos) comma = inner.size();
        out.push_back(std::format("{}{}{}", prefix, inner.substr(start, comma - start), suffix));
        if (comma == inner.size()) break;
        start = comma + 1;
    }
    return out;
}

[[nodiscard]] inline auto repo_path_matches_exists(const std::filesystem::path& repo_root, std::string_view raw_tok) -> bool {
    std::error_code ec;
    for (const auto& expanded : expand_single_braces(raw_tok)) {
        std::string_view sv = expanded;
        while (sv.size() > 1 && sv.back() == '/') sv.remove_suffix(1);
        if (!sv.contains('*')) {
            if (!std::filesystem::exists(repo_root / sv, ec)) return false;
            continue;
        }
        std::vector<std::string_view> parts;
        std::size_t pos = 0;
        while (pos < sv.size()) {
            auto sl = sv.find('/', pos);
            if (sl == std::string_view::npos) sl = sv.size();
            if (sl > pos) parts.push_back(sv.substr(pos, sl - pos));
            pos = sl + 1;
        }
        auto match_rec = [&](auto& self, const std::filesystem::path& cur, std::size_t idx) -> bool {
            if (idx == parts.size()) return std::filesystem::exists(cur, ec);
            const std::string_view part = parts[idx];
            if (!part.contains('*')) {
                return self(self, cur / part, idx + 1);
            }
            if (!std::filesystem::is_directory(cur, ec)) return false;
            for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
                if (ec) break;
                const std::string fn = entry.path().filename().string();
                if (wildcard_match(part, fn)) {
                    if (idx + 1 == parts.size() || self(self, entry.path(), idx + 1)) return true;
                }
            }
            return false;
        };
        if (!match_rec(match_rec, repo_root, 0)) return false;
    }
    return true;
}

[[nodiscard]] inline auto strip_line_col_and_punct(std::string_view tok) -> std::string_view {
    while (!tok.empty() && (tok.front() == '(' || tok.front() == '"' || tok.front() == '\'')) tok.remove_prefix(1);
    while (!tok.empty() && (tok.back() == ')' || tok.back() == ',' || tok.back() == ';' || tok.back() == '"' || tok.back() == '\'')) {
        tok.remove_suffix(1);
    }
    while (true) {
        auto col = tok.rfind(':');
        if (col == std::string_view::npos || col + 1 >= tok.size()) break;
        std::string_view tail = tok.substr(col + 1);
        const bool all_digits = std::all_of(tail.begin(), tail.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        if (!all_digits) break;
        tok = tok.substr(0, col);
    }
    return tok;
}

[[nodiscard]] inline auto is_repo_prefixed_token(std::string_view tok, bool exact_root_allowed) -> bool {
    if (tok.empty() || tok.contains("://") || tok.contains('<') || tok.contains('>')) return false;
    for (std::string_view pfx : {"src/", "runtime/", "docs/", "examples/", "tests/", "scripts/", "tools/vmtest", ".github/", ".githooks/", ".claude-plugin/"}) {
        if (tok.starts_with(pfx)) return true;
    }
    if (exact_root_allowed) {
        for (std::string_view exact : {"Makefile", "Dockerfile", "VERSION", "README.md", "AGENTS.md"}) {
            if (tok == exact) return true;
        }
    }
    return false;
}

[[nodiscard]] inline auto collect_missing_repo_paths(
    const std::filesystem::path& repo_root,
    const std::filesystem::path& doc_file,
    std::string_view markdown
) -> std::vector<std::string> {
    std::vector<std::string> missing;
    std::set<std::string> seen;
    auto record_missing = [&](std::string msg) {
        if (seen.insert(msg).second) missing.push_back(std::move(msg));
    };
    const std::string doc_rel = doc_file.lexically_relative(repo_root).empty()
        ? doc_file.filename().string()
        : doc_file.lexically_relative(repo_root).generic_string();

    bool in_fence = false;
    std::string fence_lang;
    std::size_t pos = 0;
    int line_no = 0;
    while (pos < markdown.size()) {
        ++line_no;
        auto nl = markdown.find('\n', pos);
        if (nl == std::string_view::npos) nl = markdown.size();
        std::string_view line = markdown.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = nl + 1;

        const auto non_ws = line.find_first_not_of(" \t");
        if (non_ws != std::string_view::npos && line.substr(non_ws).starts_with("```")) {
            if (!in_fence) {
                in_fence = true;
                std::string_view lang = line.substr(non_ws + 3);
                while (!lang.empty() && std::isspace(static_cast<unsigned char>(lang.front()))) lang.remove_prefix(1);
                while (!lang.empty() && std::isspace(static_cast<unsigned char>(lang.back()))) lang.remove_suffix(1);
                fence_lang = std::string(lang);
            } else {
                in_fence = false;
                fence_lang.clear();
            }
            continue;
        }

        if (in_fence) {
            if (fence_lang == "bash" || fence_lang == "sh") {
                std::size_t i = 0;
                while (i < line.size()) {
                    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) || line[i] == '"' || line[i] == '\'' || line[i] == '`')) ++i;
                    if (i >= line.size() || line[i] == '#') break;
                    std::size_t j = i;
                    while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j])) && line[j] != '"' && line[j] != '\'' && line[j] != '`') ++j;
                    std::string_view tok = strip_line_col_and_punct(line.substr(i, j - i));
                    if (tok.starts_with("examples/") && !repo_path_matches_exists(repo_root, tok)) {
                        record_missing(std::format("{}:{}: missing bash-example path `{}`", doc_rel, line_no, tok));
                    }
                    i = j;
                }
            }
            continue;
        }

        // Outside fenced blocks: extract inline backtick spans and record character mask
        std::vector<bool> in_code(line.size(), false);
        std::size_t bi = 0;
        while (bi < line.size()) {
            if (line[bi] != '`') {
                ++bi;
                continue;
            }
            std::size_t tick_len = 0;
            while (bi + tick_len < line.size() && line[bi + tick_len] == '`') ++tick_len;
            std::size_t end_tick = bi + tick_len;
            bool closed = false;
            while (end_tick < line.size()) {
                if (line[end_tick] == '`') {
                    std::size_t run = 0;
                    while (end_tick + run < line.size() && line[end_tick + run] == '`') ++run;
                    if (run == tick_len) {
                        closed = true;
                        break;
                    }
                    end_tick += run;
                } else {
                    ++end_tick;
                }
            }
            if (!closed) break;
            for (std::size_t k = bi; k < end_tick + tick_len; ++k) in_code[k] = true;
            std::string_view span = line.substr(bi + tick_len, end_tick - (bi + tick_len));
            std::size_t ti = 0;
            while (ti < span.size()) {
                while (ti < span.size() && std::isspace(static_cast<unsigned char>(span[ti]))) ++ti;
                if (ti >= span.size()) break;
                std::size_t tj = ti;
                while (tj < span.size() && !std::isspace(static_cast<unsigned char>(span[tj]))) ++tj;
                std::string_view tok = strip_line_col_and_punct(span.substr(ti, tj - ti));
                if (is_repo_prefixed_token(tok, true) && !repo_path_matches_exists(repo_root, tok)) {
                    record_missing(std::format("{}:{}: missing backtick path `{}`", doc_rel, line_no, tok));
                }
                ti = tj;
            }
            bi = end_tick + tick_len;
        }

        // Outside fenced blocks & inline code spans: extract relative Markdown links `[label](target)`
        std::size_t li = 0;
        while (li < line.size()) {
            auto br = line.find("](", li);
            if (br == std::string_view::npos) break;
            if (in_code[br] || in_code[br + 1]) {
                li = br + 2;
                continue;
            }
            auto close = line.find(')', br + 2);
            if (close == std::string_view::npos) break;
            std::string_view target = line.substr(br + 2, close - (br + 2));
            while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front()))) target.remove_prefix(1);
            if (auto ws = target.find_first_of(" \t"); ws != std::string_view::npos) target = target.substr(0, ws);
            if (!target.empty() && !target.starts_with('#') && !target.starts_with("http://") &&
                !target.starts_with("https://") && !target.starts_with("mailto:") && !target.contains('<')) {
                if (auto hash = target.find('#'); hash != std::string_view::npos) target = target.substr(0, hash);
                if (!target.empty()) {
                    std::error_code ec;
                    const auto resolved = (doc_file.parent_path() / target).lexically_normal();
                    if (!std::filesystem::exists(resolved, ec)) {
                        record_missing(std::format("{}:{}: missing link target `{}`", doc_rel, line_no, target));
                    }
                }
            }
            li = close + 1;
        }
    }
    return missing;
}

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
} // namespace tooling_polish_detail

// the in/out corpus round-trips, formatting is idempotent, and it never changes the lowered C++.
void test_formatter_roundtrip_corpus([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_formatter_roundtrip_corpus");
    std::size_t pairs = 0;
    for (const auto& entry : std::filesystem::directory_iterator("tests/fmt/in")) {
        if (entry.path().extension() != ".cf") continue;
        ++pairs;
        const auto name = entry.path().filename().string();
        auto in = cfire::build::read_file_text(entry.path());
        auto want = cfire::build::read_file_text(std::filesystem::path("tests/fmt/out") / name);
        require(in.has_value() && want.has_value(), std::format("corpus pair {} readable", name));
        if (!in || !want) continue;
        auto got = cfire::fmt::format_source(*in, name);
        require(got.has_value() && *got == *want, std::format("{} formats to tests/fmt/out/{}, got:\n{}", name, name, got ? *got : got.error()));
        auto again = cfire::fmt::format_source(*want, name);
        require(again.has_value() && *again == *want, std::format("tests/fmt/out/{} is a fixed point", name));
        // The lowered C++ is identical modulo #line.
        auto lower = [&](const std::string& src) -> std::string {
            auto ast = cfire::frontend::parse_source(name, src);
            if (!ast) return "parse error: " + ast.error();
            auto checked = cfire::frontend::analyze_resilient(std::move(*ast));
            return tooling_polish_detail::strip_line_directives(cfire::frontend::emit_cpp(checked));
        };
        require(lower(*in) == lower(*want), std::format("formatting {} does not change generated C++", name));
    }
    require(pairs >= 3, "tests/fmt corpus has pairs");
    // Specific canonical forms from the audit (spacing and formatting).
    auto f = cfire::fmt::format_source("package main\ntype   P struct{\n    b : str\n}\nfn  f( mut p:P)->i32{ return 1; }\nfn main() { x := 1; y := x }\n", "c2.cf");
    require(f.has_value() && f->contains("type P struct {\n    b: str\n}") && f->contains("fn f(mut p: P) -> i32 {") &&
            f->contains("fn main() {\n    x := 1\n    y := x\n}"), std::format("canonical spacing and split one-liners, got: {}", f ? *f : f.error()));
    auto c = cfire::fmt::format_source("package main\n/* keep  this */\nfn main() {\n    x := 1 /* a+b */ + 2 // c  d\n}\n", "comments.cf");
    require(c.has_value() && c->contains("/* keep  this */") && c->contains("x := 1 /* a+b */ + 2 // c  d"), std::format("comments are kept verbatim, got: {}", c ? *c : c.error()));
    std::println("--- PASS: test_formatter_roundtrip_corpus");
}

// closure parameters and match bindings are separate rename scopes; diffs apply with `git apply`; `--help` per command.
void test_rename_scopes_and_diff([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_rename_scopes_and_diff");
    ScopedTempDir dir("cfire_rename_diff");
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
    require(outer.success && outer.total_occurrences == 3, std::format("outer rename touches 3 occurrences, got {} ({})", outer.total_occurrences, outer.error));
    if (outer.success && !outer.modifications.empty()) {
        const auto& m = outer.modifications[0].modified_content;
        require(m.contains("|count: i32| { return count * 2 }") && m.contains("Opt::Some(count) => count + 1") && m.contains("Opt::Nothing => n"),
            "closure parameter and match binding keep their names");
    }
    auto inner = cfire::refactor::plan_rename(file, 10, 11, "c");
    require(inner.success && inner.total_occurrences == 2, std::format("closure-parameter rename stays in the closure, got {}", inner.total_occurrences));
    // The diff uses a/ b/ relative paths and applies with git apply.
    auto diff = run_cfire(std::format("refactor rename \"{}:9:5\" n --diff", file.string()));
    require(diff.output.contains("--- a/") && !diff.output.contains("a//"), "diff paths are a/<path>, never a//");
    auto help = run_cfire("refactor rename --help");
    require(help.output.contains("Usage: cfire refactor rename") && !help.output.contains("Commands:"), "refactor --help prints the command's own usage");
    std::println("--- PASS: test_rename_scopes_and_diff");
}

// the GUIDE's diagnostics table covers every code the compiler emits, docs hold no local links,
// documented repo paths exist on disk, root GitHub Pages leftovers stay removed, and Ch11 C++ RAII works end-to-end.
void test_generated_doc_integrity(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_generated_doc_integrity");
    std::error_code rec;
    std::filesystem::path repo_root = std::filesystem::current_path(rec);
    if (!rt_dir.empty() && std::filesystem::exists(rt_dir.parent_path() / "README.md", rec)) {
        repo_root = rt_dir.parent_path();
    }

    std::set<std::string> emitted;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(repo_root / "src")) {
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
            std::format("{} is emitted by the compiler but missing from diagnostic_code_registry()", code));
    }
    auto guide = cfire::build::read_file_text(repo_root / "docs/GUIDE.md");
    require(guide && !guide->contains("file://") && !guide->contains("](/Users/"), "GUIDE has no local file links");
    require(guide && guide->contains("| `cfire/RETIRED-API` |") && guide->contains("| `cfire/FIELD-NAME-CASE` |"), "GUIDE diagnostics table is generated from the registry");

    // 1. Repository path integrity audit across authoritative docs (excluding historical planning logs).
    for (const std::string_view rel_doc : {
             std::string_view("README.md"),
             std::string_view("docs/GUIDE.md"),
             std::string_view("docs/SENIOR_ENGINEER_REVIEW_GUIDELINES.md"),
             std::string_view("AGENTS.md"),
         }) {
        const auto doc_path = repo_root / rel_doc;
        auto doc_txt = cfire::build::read_file_text(doc_path);
        require(doc_txt.has_value(), std::format("readable documentation file {}", doc_path.string()));
        if (!doc_txt) continue;
        const auto missing = tooling_polish_detail::collect_missing_repo_paths(repo_root, doc_path, *doc_txt);
        std::string joined;
        for (const auto& m : missing) joined += "\n  - " + m;
        require(missing.empty(), std::format("{} has broken repository paths:{}", rel_doc, joined));
    }

    // 2. Negative self-check on the doc-path integrity scanner itself.
    {
        const std::string broken_md =
            "Review `docs/planning/architectural-backlog.md` and `runtime/core_tako.hpp`.\n"
            "```bash\n"
            "./cfire run --ios examples/13-ios-native-ui\n"
            "```\n"
            "See [missing](docs/nonexistent_doc_target.md) for details.\n";
        const auto caught = tooling_polish_detail::collect_missing_repo_paths(repo_root, repo_root / "README.md", broken_md);
        auto has_substr = [&](std::string_view needle) {
            return std::any_of(caught.begin(), caught.end(), [&](const std::string& s) { return s.contains(needle); });
        };
        require(has_substr("docs/planning/architectural-backlog.md"), "doc-path scanner flags missing backlog path");
        require(has_substr("runtime/core_tako.hpp"), "doc-path scanner flags missing runtime header path");
        require(has_substr("examples/13-ios-native-ui"), "doc-path scanner flags missing bash example path");
        require(has_substr("docs/nonexistent_doc_target.md"), "doc-path scanner flags missing relative link");
    }

    // 3. Root GitHub Pages leftovers deleted; docs/ copies intact.
    require(!std::filesystem::exists(repo_root / "CNAME", rec), "root CNAME removed");
    require(!std::filesystem::exists(repo_root / ".nojekyll", rec), "root .nojekyll removed");
    require(std::filesystem::is_regular_file(repo_root / "docs" / "CNAME", rec), "docs/CNAME preserved");
    require(std::filesystem::is_regular_file(repo_root / "docs" / ".nojekyll", rec), "docs/.nojekyll preserved");

    // 4. End-to-end verification of GUIDE Ch11 move-only C++23 RAII custom-deleter handle wrapper.
    {
        ScopedTempDir dir("ch11_cpp_raii");
        dir.file("csrc/cipher_handle.hpp", R"HPP(#pragma once
#include <utility>

struct DemoCSession {
    int seed;
};

inline int g_demo_sessions_freed = 0;

[[nodiscard]] inline auto demo_session_open(int seed) -> DemoCSession* {
    return new DemoCSession{seed};
}

inline void demo_session_free(DemoCSession* ctx) {
    if (ctx != nullptr) {
        delete ctx;
        ++g_demo_sessions_freed;
    }
}

class CipherHandle {
public:
    CipherHandle() = default;
    explicit CipherHandle(DemoCSession* raw) noexcept : ptr_(raw) {}
    ~CipherHandle() { reset(); }

    CipherHandle(const CipherHandle&) = delete;
    auto operator=(const CipherHandle&) -> CipherHandle& = delete;

    CipherHandle(CipherHandle&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)) {}
    auto operator=(CipherHandle&& other) noexcept -> CipherHandle& {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    void reset() noexcept {
        if (ptr_ != nullptr) {
            demo_session_free(std::exchange(ptr_, nullptr));
        }
    }
    [[nodiscard]] auto valid() const noexcept -> bool { return ptr_ != nullptr; }

private:
    DemoCSession* ptr_{nullptr};
};

[[nodiscard]] inline auto open_cipher(int seed) -> CipherHandle {
    return CipherHandle(demo_session_open(seed));
}

[[nodiscard]] inline auto demo_freed_count() -> int {
    return g_demo_sessions_freed;
}
)HPP");
        dir.file("build.cf", R"CF(package build

import "core:build"

app := build.Executable{
    Name:     "cipher_raii_demo",
    Entry:    "main.cf",
    Includes: ["csrc"],
}
)CF");
        dir.file("main.cf", R"CF(package main

import cpp "cipher_handle.hpp"

fn open_and_drop() -> bool {
    mut handle := cpp.open_cipher(7)
    ok := handle.valid()
    return ok
}

fn main() {
    was_valid := open_and_drop()
    println("valid={} freed={}", was_valid, cpp.demo_freed_count())
}
)CF");
        const std::string cfire = std::filesystem::absolute(cfire_exe()).string();
        auto chk = run_process({cfire, "check", "main.cf"}, dir.path, 120);
        require(chk.exit_code == 0, std::format("Ch11 move-only C++ RAII project checks cleanly (rc={}): {}{}", chk.exit_code, chk.out, chk.err));
        auto run = run_process({cfire, "run", "main.cf"}, dir.path, 120);
        require(run.exit_code == 0 && (run.out + run.err).contains("valid=true freed=1"),
            std::format("Ch11 move-only C++ RAII handle runs and frees once on scope exit (rc={}): {}{}", run.exit_code, run.out, run.err));
    }

    std::println("--- PASS: test_generated_doc_integrity");
}
