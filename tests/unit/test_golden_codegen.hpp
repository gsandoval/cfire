#pragma once
// tests/unit/test_golden_codegen.hpp — Golden Codegen Snapshot & #line Verification Suite (CF-092)

#include "../test_harness.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

void test_golden_codegen_snapshots(const std::filesystem::path& repo_root) {
    std::println("=== RUN   test_golden_codegen_snapshots (CF-092)");

    auto golden_dir = repo_root / "tests" / "golden";
    require(std::filesystem::exists(golden_dir), std::format("tests/golden exists at {}", golden_dir.string()));
    if (!std::filesystem::exists(golden_dir)) return;

    bool update_golden = false;
    if (const char* env = std::getenv("UPDATE_GOLDEN")) {
        update_golden = (std::string(env) == "1");
    }

    std::vector<std::filesystem::path> cf_files;
    for (const auto& entry : std::filesystem::directory_iterator(golden_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cf") {
            cf_files.push_back(entry.path());
        }
    }
    std::sort(cf_files.begin(), cf_files.end());

    require(!cf_files.empty(), "at least one golden .cf test file found");

    int verified_count = 0;

    for (const auto& cf_path : cf_files) {
        std::string filename = cf_path.filename().string();
        auto expected_path = cf_path.parent_path() / (cf_path.stem().string() + ".expected.cpp");

        auto src_opt = cfire::fs::read_text(cf_path);
        require(src_opt.has_value(), std::format("read golden cf file: {}", filename));
        if (!src_opt) continue;

        auto parse_res = cfire::frontend::parse_source(filename, *src_opt);
        require(parse_res.has_value(), std::format("parse golden file: {}", filename));
        if (!parse_res) continue;

        auto check_res = cfire::frontend::analyze(std::move(*parse_res));
        if (!check_res) {
            require(false, std::format("analyze golden file {}: {}", filename, check_res.error()));
            continue;
        }

        std::string actual_cpp = cfire::frontend::emit_cpp(*check_res, false);

        // 1. Verify #line directive fidelity (CF-092)
        std::string line_pattern = std::format("#line [0-9]+ \"{}\"", filename);
        bool has_line_directives = std::regex_search(actual_cpp, std::regex(line_pattern));
        require(has_line_directives, std::format("{} must emit #line directives referencing \"{}\"", filename, filename));

        // 2. Snapshot comparison against .expected.cpp
        if (update_golden) {
            (void)cfire::fs::write_text(expected_path, actual_cpp);
            std::println("  [UPDATE_GOLDEN] Wrote snapshot {}", expected_path.filename().string());
            ++verified_count;
            continue;
        }

        auto expected_opt = cfire::fs::read_text(expected_path);
        if (!expected_opt) {
            require(false, std::format("missing golden snapshot: {} (run with UPDATE_GOLDEN=1 to create)", expected_path.string()));
            continue;
        }

        const std::string& expected_cpp = *expected_opt;
        if (actual_cpp != expected_cpp) {
            std::size_t diff_pos = 0;
            while (diff_pos < actual_cpp.size() && diff_pos < expected_cpp.size() &&
                   actual_cpp[diff_pos] == expected_cpp[diff_pos]) {
                ++diff_pos;
            }
            std::size_t line_no = 1;
            for (std::size_t i = 0; i < diff_pos && i < actual_cpp.size(); ++i) {
                if (actual_cpp[i] == '\n') ++line_no;
            }

            require(false, std::format("golden snapshot mismatch for {} at byte offset {} (line {}): actual {} bytes vs expected {} bytes",
                filename, diff_pos, line_no, actual_cpp.size(), expected_cpp.size()));

            std::size_t start = (diff_pos > 30) ? diff_pos - 30 : 0;
            std::string exp_snippet = expected_cpp.substr(start, std::min<std::size_t>(60, expected_cpp.size() - start));
            std::string act_snippet = actual_cpp.substr(start, std::min<std::size_t>(60, actual_cpp.size() - start));
            std::println(stderr, "    Expected around line {}: \"{}\"", line_no, exp_snippet);
            std::println(stderr, "    Actual   around line {}: \"{}\"", line_no, act_snippet);
        } else {
            ++verified_count;
        }
    }

    std::println("  [PASS] Verified {}/{} golden codegen snapshots with #line fidelity", verified_count, cf_files.size());
}
