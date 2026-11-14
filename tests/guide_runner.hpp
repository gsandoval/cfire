#pragma once
// tests/guide_runner.hpp — Doc-tests for docs/GUIDE.md (C++ since CF-186).
//
// 1. Every ```cfire block followed by a ```text block starting with `# Output:` must run and print that output.
// 2. CF-148: every complete ```cfire block (one with `package`) must type-check without cfire/UNKNOWN-TYPE.
// 3. CF-158: in ```bash blocks, each `cfire eval ...` line followed by `# Output: <text>` must print <text>.
//
// Usage: .cfire_test_runner guide [--cfire path] [--jobs N]

#include "process_run.hpp"
#include "test_harness.hpp"

namespace guide {

struct Block {
    std::string lang;
    std::string code;
    std::size_t end = 0;  // offset just past the closing fence
};

// The fenced blocks of `text`, in order. A fence is a line that is "```" after optional indentation (inside a list);
// the opening one carries the language. Backticks elsewhere on a line (prose, `/// ```cf` in a doc comment) are not
// fences.
[[nodiscard]] inline auto fenced_blocks(std::string_view text) -> std::vector<Block> {
    std::vector<Block> out;
    std::size_t pos = 0;
    std::optional<Block> open;
    std::size_t body_start = 0;
    while (pos < text.size()) {
        auto nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = text.substr(pos, nl - pos);
        const auto first = line.find_first_not_of(" \t");
        const bool fence = first != std::string_view::npos && line.substr(first).starts_with("```");
        if (fence && !open) {
            std::string lang(line.substr(first + 3));
            while (!lang.empty() && (lang.back() == '\r' || lang.back() == ' ')) lang.pop_back();
            open = Block{std::move(lang), {}, 0};
            body_start = std::min(nl + 1, text.size());
        } else if (fence && open && line.substr(first).find_first_not_of("`\r ") == std::string_view::npos) {
            const std::size_t body_end = pos > body_start ? pos - 1 : body_start;
            open->code = std::string(text.substr(body_start, body_end - body_start));
            open->end = nl;
            out.push_back(std::move(*open));
            open.reset();
        }
        pos = nl + 1;
    }
    return out;
}

// The ```cfire blocks with an expected-output block right after them.
[[nodiscard]] inline auto output_tests(std::string_view text) -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> out;
    const auto blocks = fenced_blocks(text);
    for (std::size_t i = 0; i + 1 < blocks.size(); ++i) {
        if (blocks[i].lang != "cfire" || blocks[i + 1].lang != "text" || !blocks[i + 1].code.starts_with("# Output:\n")) continue;
        auto between = text.substr(blocks[i].end, text.find("```", blocks[i].end) - blocks[i].end);
        if (between.find_first_not_of(" \t\r\n") != std::string_view::npos) continue;
        out.emplace_back(blocks[i].code, blocks[i + 1].code.substr(std::string_view("# Output:\n").size()));
        ++i;
    }
    return out;
}

// shlex.split for the eval lines: whitespace-separated words with '...' / "..." quoting and backslash escapes.
[[nodiscard]] inline auto shell_words(std::string_view s) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string cur;
    bool in_word = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\'') {
            in_word = true;
            for (++i; i < s.size() && s[i] != '\''; ++i) cur.push_back(s[i]);
        } else if (c == '"') {
            in_word = true;
            for (++i; i < s.size() && s[i] != '"'; ++i) {
                if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '"' || s[i + 1] == '\\' || s[i + 1] == '$' || s[i + 1] == '`')) ++i;
                cur.push_back(s[i]);
            }
        } else if (c == '\\' && i + 1 < s.size()) {
            in_word = true;
            cur.push_back(s[++i]);
        } else if (c == ' ' || c == '\t') {
            if (in_word) out.push_back(std::move(cur)), cur.clear(), in_word = false;
        } else {
            in_word = true;
            cur.push_back(c);
        }
    }
    if (in_word) out.push_back(std::move(cur));
    return out;
}

[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        auto nl = text.find('\n', start);
        if (nl == std::string_view::npos) nl = text.size();
        std::string line(text.substr(start, nl - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        start = nl + 1;
    }
    return out;
}

[[nodiscard]] inline auto strip(std::string_view s) -> std::string {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return "";
    return std::string(s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1));
}

inline auto run_guide(const std::string& cfire, const std::filesystem::path& repo, std::size_t jobs) -> int {
    const auto guide_path = repo / "docs" / "GUIDE.md";
    auto text_r = cfire::fs::read_text(guide_path);
    if (!text_r) {
        std::println(stderr, "Error: cannot read {}", guide_path.string());
        return 1;
    }
    const std::string text(*text_r);
    const auto tests = output_tests(text);
    std::println("=== Found {} testable GUIDE.md code blocks ===\n", tests.size());

    // 1. Output doc-tests.
    std::vector<std::string> failures(tests.size());
    parallel_for_each_index(tests.size(), jobs, [&](std::size_t i) {
        ScopedTempDir dir("cfire_guide");
        auto file = dir.file("main.cf", tests[i].first);
        auto r = run_process({cfire, "run", file.string()}, repo, 120);
        const std::string want = strip(tests[i].second);
        const std::string got = strip(r.out + (r.err.empty() ? "" : "\n" + r.err));
        if (r.timed_out) failures[i] = "TIMEOUT";
        else if (r.exit_code != 0) failures[i] = std::format("FAIL (exit code {})\n       STDERR: {}", r.exit_code, strip(r.err).substr(0, 300));
        else if (!got.contains(want)) failures[i] = std::format("FAIL\n       EXPECTED: {}\n       ACTUAL:   {}", want, got);
    });
    int passed = 0, failed = 0;
    for (std::size_t i = 0; i < tests.size(); ++i) {
        if (failures[i].empty()) {
            ++passed;
            std::println("  [{:02}/{:02}] PASS", i + 1, tests.size());
        } else {
            ++failed;
            std::println("  [{:02}/{:02}] {}", i + 1, tests.size(), failures[i]);
        }
    }

    // 2. CF-148: complete blocks must be fully typed.
    std::vector<std::string> complete;
    for (const auto& b : fenced_blocks(text)) {
        if (b.lang == "cfire" && b.code.contains("package ")) complete.push_back(b.code);
    }
    std::vector<std::string> untyped(complete.size());
    parallel_for_each_index(complete.size(), jobs, [&](std::size_t i) {
        ScopedTempDir dir("cfire_guide_types");
        auto file = dir.file(complete[i].contains("test \"") ? "main_test.cf" : "main.cf", complete[i]);
        auto r = run_process({cfire, "check", file.string()}, repo, 120);
        for (const auto& line : split_lines(r.out + r.err)) {
            if (line.contains("cfire/UNKNOWN-TYPE")) {
                const auto colon = line.find(": ");
                untyped[i] = (colon == std::string::npos ? line : line.substr(colon + 2)).substr(0, 160);
                break;
            }
        }
    });
    int unknown_blocks = 0;
    for (std::size_t i = 0; i < complete.size(); ++i) {
        if (untyped[i].empty()) continue;
        ++unknown_blocks;
        std::println("  [types] block {}: {}", i, untyped[i]);
    }
    if (unknown_blocks > 0) {
        std::println("  {} GUIDE block(s) have expressions Sema cannot type (cfire/UNKNOWN-TYPE)", unknown_blocks);
        failed += unknown_blocks;
    }

    // 3. CF-158: `cfire eval` examples.
    int eval_checked = 0;
    for (const auto& b : fenced_blocks(text)) {
        if (b.lang != "bash") continue;
        const auto lines = split_lines(b.code);
        for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
            if (!lines[i].starts_with("cfire eval ") || !lines[i + 1].starts_with("# Output: ")) continue;
            const std::string want = lines[i + 1].substr(std::string_view("# Output: ").size());
            auto words = shell_words(lines[i]);
            std::vector<std::string> argv{cfire, "eval"};
            argv.insert(argv.end(), words.begin() + 2, words.end());
            auto r = run_process(argv, repo, 180);
            const std::string out = strip(r.out);
            const std::string got = out.empty() ? "" : out.substr(out.rfind('\n') == std::string::npos ? 0 : out.rfind('\n') + 1);
            ++eval_checked;
            if (r.exit_code != 0 || got != want) {
                ++failed;
                std::println("  [eval] {}\n       want: {}\n       got:  {}", lines[i], want, got.empty() ? strip(r.out + r.err).substr(0, 200) : got);
            } else {
                ++passed;
            }
        }
    }
    std::println("  {} `cfire eval` example(s) checked", eval_checked);
    std::println("\n==========================================");
    std::println("GUIDE Doc-Test Results: {} passed, {} failed (total {})", passed, failed, tests.size());
    return failed > 0 ? 1 : 0;
}

} // namespace guide
