#pragma once
// src/docgen.hpp — Umbrella header for Single-Source-Of-Truth Metadata Catalog & Documentation Generator (`cfire doc`)
// Sub-modules:
//   - src/docgen/catalog.hpp  (`stdlib_catalog()`, `diagnostics_catalog()`, `module_practical_example()`)
//   - src/docgen/styles.hpp   (`shared_css()`, `navbar_html()`, `html_escape()`)
//   - src/docgen/markdown.hpp (`render_guide_markdown()`, `generate_reference_markdown()`, `extract_file_markdown_doc()`, `generate_llm_reference()`)
//   - src/docgen/html.hpp     (`generate_landing_html()`, `generate_guide_html()`, `generate_reference_html()`, `sync_docs_directory()`)

#include "docgen/catalog.hpp"
#include "docgen/styles.hpp"
#include "docgen/markdown.hpp"
#include "docgen/html.hpp"
