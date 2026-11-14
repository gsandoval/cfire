#pragma once
// src/docgen/catalog_types.hpp — Lightweight Metadata Catalog Types & Static Tables (`stdlib_catalog` & `diagnostics_catalog`)

#include "catalog_type_docs.hpp"
#include "sema/builtin_methods.hpp"
#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace cfire::doc {

struct StdFnDoc {
    std::string name;
    std::string cf_sig;
    std::string cpp_lowering;
    std::string summary;
    std::string example;
    std::vector<std::size_t> escaping_param_indices{};
};

struct StdModuleDoc {
    std::string import_path; // e.g. "core:fs"
    std::string ns_alias;    // e.g. "fs"
    std::string title;
    std::string link_flags;
    std::string overview;
    std::vector<StdFnDoc> functions;
    std::vector<StdTypeDoc> type_docs{};  // CF-188: one per name in `types`, filled from catalog_type_docs.hpp

    // Authoritative unified metadata for compilation, sema, module registry & codegen:
    std::string header_path{};
    std::string core_key{};
    std::vector<std::string> extra_core_keys{};
    std::vector<std::string> types{};
    std::vector<std::string> defines{};
    std::vector<std::string> link_tags{};
    std::vector<std::string> aliases{};
    bool is_vendor = false;
    std::vector<std::string> implied_vendors{};
};

struct DiagnosticDoc {
    std::string code;
    std::string title;
    std::string rule;
    std::string bad_example;
    std::string good_example;
};

// CF-158: one line for every diagnostic code the compiler can emit. Codes with a detailed entry in
// diagnostics_catalog() take their summary from there; the unit test `test_cf158_doc_integrity` fails when the
// sources emit a code that is in neither list. The GUIDE's diagnostics table is generated from this.
struct DiagnosticCodeDoc {
    std::string code;
    std::string category;
    std::string summary;
};

#if defined(CFIRE_SPLIT_TUS)
#define CFIRE_CATALOG_INLINE
#else
#define CFIRE_CATALOG_INLINE inline
#endif

[[nodiscard]] CFIRE_CATALOG_INLINE auto stdlib_catalog() -> const std::vector<StdModuleDoc>&;
[[nodiscard]] CFIRE_CATALOG_INLINE auto diagnostics_catalog() -> const std::vector<DiagnosticDoc>&;
[[nodiscard]] CFIRE_CATALOG_INLINE auto diagnostic_code_registry() -> const std::vector<DiagnosticCodeDoc>&;

#undef CFIRE_CATALOG_INLINE

} // namespace cfire::doc

#if !defined(CFIRE_SPLIT_TUS)
#include "catalog_types_impl.inc"
#endif
