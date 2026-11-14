// src/tu_docgen.cpp — Translation Unit for `cfire doc` (CF-198)

#define CFIRE_TU_DOCGEN_IMPL 1
#include "cli/dispatch.hpp"
#include "docgen.hpp"

#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

namespace cfire::cli::tu {

auto run_docgen_tu(int argc, char** argv, const cfire::build::ToolchainLayout& layout) -> int {
    bool check_only = false;
    bool llm_mode = false;
    std::filesystem::path target_cf;
    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--check") {
            check_only = true;
        } else if (arg == "--llm") {
            llm_mode = true;
        } else {
            target_cf = arg;
        }
    }
    if (llm_mode) {
        std::println("{}", cfire::doc::generate_llm_reference());
        return 0;
    }
    if (!target_cf.empty() && target_cf.extension() == ".cf") {
        auto md = cfire::doc::extract_file_markdown_doc(target_cf);
        if (!md) {
            std::println(stderr, "{}", md.error());
            return 1;
        }
        std::print("{}", *md);
        return 0;
    }
    auto docs_dir = layout.docs_dir;
    auto res = cfire::doc::sync_docs_directory(docs_dir, check_only, layout.runtime_dir);
    if (!res) {
        std::println(stderr, "{}", res.error());
        return 1;
    }
    if (check_only) {
        std::println("✓ Documentation in {} is 100% up-to-date with the compiler", docs_dir.string());
    } else {
        std::println("✓ Generated landing page (index.html), guide (guide.html, GUIDE.md), and stdlib reference (reference.html, STDLIB_REFERENCE.md) in {}", docs_dir.string());
    }
    return 0;
}

} // namespace cfire::cli::tu
