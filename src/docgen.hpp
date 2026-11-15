#pragma once
// src/docgen.hpp — Umbrella header for Single-Source-Of-Truth Metadata Catalog & Documentation Generator (`cfire doc`)
// Sub-modules:
//   - src/docgen/catalog.hpp  (`stdlib_catalog()`, `diagnostics_catalog()`, `module_practical_example()`)
//   - src/docgen/styles.hpp   (`shared_css()`, `navbar_html()`, `html_escape()`)
//   - src/docgen/markdown.hpp (`render_guide_markdown()`, `generate_reference_markdown()`, `extract_file_markdown_doc()`, `generate_llm_reference()`)
//   - src/docgen/html.hpp     (`generate_landing_html()`, `generate_guide_html()`, `generate_reference_html()`, `sync_docs_directory()`)

#if defined(CFIRE_SPLIT_TUS) && !defined(CFIRE_DOCGEN_IMPL) && !defined(CFIRE_DOCGEN_MD_IMPL)

#include "docgen/catalog_types.hpp"
#include <expected>
#include <filesystem>
#include <string>

namespace cfire::doc {

[[nodiscard]] auto generate_llm_reference() -> std::string;

[[nodiscard]] auto extract_file_markdown_doc(const std::filesystem::path& cf_file)
    -> std::expected<std::string, std::string>;

[[nodiscard]] auto sync_docs_directory(
    const std::filesystem::path& docs_dir,
    bool check_only,
    const std::filesystem::path& runtime_dir = {}
) -> std::expected<void, std::string>;

} // namespace cfire::doc

#else

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_DOCGEN_INLINE
#else
#define CFIRE_DOCGEN_INLINE inline
#endif

#include "docgen/catalog.hpp"
#include "docgen/styles.hpp"
#include "docgen/markdown.hpp"
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_DOCGEN_IMPL)
#include "docgen/html.hpp"
#endif

#undef CFIRE_DOCGEN_INLINE
#endif
