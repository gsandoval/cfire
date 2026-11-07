#pragma once
// src/docgen/catalog.hpp — Single-Source-Of-Truth Metadata Catalog (`core:*` Stdlib & Semantic Diagnostics)

#include "catalog_types.hpp"
#include "catalog_examples.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cfire::doc {

struct DiscoveredModule {
    std::string import_path;
    std::string header_rel_path;
    bool is_vendor{false};
};

[[nodiscard]] inline auto discover_runtime_modules(const std::filesystem::path& runtime_dir) -> std::vector<DiscoveredModule> {
    std::vector<DiscoveredModule> discovered;
    std::error_code ec;

    // 1. Scan runtime/core
    auto core_dir = runtime_dir / "core";
    if (std::filesystem::exists(core_dir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(core_dir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".hpp") continue;
            auto rel = std::filesystem::relative(entry.path(), core_dir).string();
            if (rel == "cli.hpp") discovered.push_back({"core:cli", "core/" + rel, false});
            else if (rel == "ios_raylib.hpp" || rel == "desktop_raylib.hpp") continue; // Built-in platform backends for vendor:raylib
            else if (rel == "compress.hpp") discovered.push_back({"ext:compress/zstd", "core/" + rel, false});
            else if (rel == "fs.hpp") discovered.push_back({"core:fs", "core/" + rel, false});
            else if (rel == "json.hpp") discovered.push_back({"ext:json", "core/" + rel, false});
            else if (rel == "log.hpp") discovered.push_back({"core:log", "core/" + rel, false});
            else if (rel == "net.hpp") discovered.push_back({"ext:net", "core/" + rel, false});
            else if (rel == "net/http.hpp") discovered.push_back({"ext:net/http", "core/" + rel, false});
            else if (rel == "net/tls.hpp") discovered.push_back({"ext:net/tls", "core/" + rel, false});
            else if (rel == "net/url.hpp") discovered.push_back({"ext:net/url", "core/" + rel, false});
            else if (rel == "net/websocket.hpp") discovered.push_back({"ext:net/websocket", "core/" + rel, false});
            else if (rel == "ssh.hpp") discovered.push_back({"ext:net/ssh", "core/" + rel, false});
            else if (rel == "os.hpp") discovered.push_back({"core:os", "core/" + rel, false});
            else if (rel == "regex.hpp") discovered.push_back({"core:regex", "core/" + rel, false});
            else if (rel == "sqlite.hpp") discovered.push_back({"ext:db/sqlite", "core/" + rel, false});
            else if (rel == "str.hpp") discovered.push_back({"core:str", "core/" + rel, false});
            else if (rel == "time.hpp") discovered.push_back({"core:time", "core/" + rel, false});
            else if (rel == "unicode.hpp") discovered.push_back({"core:unicode", "core/" + rel, false});
            else if (rel == "audio.hpp") discovered.push_back({"ext:audio", "core/" + rel, false});
            else if (rel == "ai.hpp") discovered.push_back({"ext:ai", "core/" + rel, false});
            else if (rel == "webui.hpp") discovered.push_back({"ext:webui", "core/" + rel, false});
            else if (rel == "sodium.hpp") discovered.push_back({"ext:crypto/sodium", "core/" + rel, false});
            else if (rel == "keychain.hpp") discovered.push_back({"ext:keychain", "core/" + rel, false});
            else if (rel == "crypto.hpp") {
                discovered.push_back({"core:crypto/sha256", "core/" + rel, false});
                discovered.push_back({"core:crypto/hmac", "core/" + rel, false});
                discovered.push_back({"core:crypto/aes", "core/" + rel, false});
                discovered.push_back({"core:crypto/blake3", "core/" + rel, false});
            } else if (rel == "encoding.hpp") {
                discovered.push_back({"core:encoding/base64", "core/" + rel, false});
                discovered.push_back({"core:encoding/hex", "core/" + rel, false});
                discovered.push_back({"core:encoding/csv", "core/" + rel, false});
            } else if (rel == "math.hpp") {
                discovered.push_back({"core:math", "core/" + rel, false});
                discovered.push_back({"core:rand", "core/" + rel, false});
            } else if (rel == "collections.hpp") {
                discovered.push_back({"core:collections", "core/" + rel, false});
                discovered.push_back({"core:sort", "core/" + rel, false});
            } else if (rel == "sync.hpp") {
                discovered.push_back({"core:sync", "core/" + rel, false});
                discovered.push_back({"core:sync/context", "core/" + rel, false});
            }
        }
    }

    // 2. Scan runtime/vendor
    auto vendor_dir = runtime_dir / "vendor";
    if (std::filesystem::exists(vendor_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(vendor_dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto stem = entry.path().stem().string();
            discovered.push_back({"vendor:" + stem, "vendor/" + entry.path().filename().string(), true});
        }
    }

    // 3. Known built-in vendor packages recognized by buildsys
    discovered.push_back({"vendor:raylib", "(built-in C bridge)", true});

    return discovered;
}

[[nodiscard]] inline auto audit_documentation_catalog(const std::filesystem::path& runtime_dir) -> std::expected<void, std::string> {
    auto discovered = discover_runtime_modules(runtime_dir);
    const auto& catalog = stdlib_catalog();

    // 0. Verify stdlib_catalog is strictly sorted alphabetically by import_path
    for (size_t i = 1; i < catalog.size(); ++i) {
        if (catalog[i - 1].import_path >= catalog[i].import_path) {
            return std::unexpected(std::format(
                "Documentation audit failure: stdlib_catalog() must be strictly sorted alphabetically by import_path.\n"
                "Found '{}' before '{}'. Please keep entries in src/docgen/catalog.hpp sorted alphabetically.",
                catalog[i - 1].import_path, catalog[i].import_path));
        }
    }

    std::unordered_map<std::string, const StdModuleDoc*> cat_map;
    for (const auto& mod : catalog) {
        cat_map[mod.import_path] = &mod;
    }

    // 1. Verify every discovered module has a catalog entry
    for (const auto& disc : discovered) {
        if (!cat_map.contains(disc.import_path)) {
            return std::unexpected(std::format(
                "Documentation audit failure: module '{}' (from {}) exists in runtime but is missing from stdlib_catalog() in src/docgen/catalog.hpp.\n"
                "To fix: register '{}' in stdlib_catalog() in src/docgen/catalog_types.hpp with functions, signatures, summaries, and examples.",
                disc.import_path, disc.header_rel_path, disc.import_path));
        }
    }

    // 2. Verify every catalog module has functions, examples, and descriptions
    for (const auto& mod : catalog) {
        if (mod.functions.empty()) {
            return std::unexpected(std::format(
                "Documentation audit failure: module '{}' has 0 functions documented in stdlib_catalog().\n"
                "To fix: document public functions with signatures, lowering, summaries, and examples in src/docgen/catalog_types.hpp.",
                mod.import_path));
        }
        for (const auto& fn : mod.functions) {
            if (fn.name.empty() || fn.cf_sig.empty() || fn.summary.empty() || fn.example.empty()) {
                return std::unexpected(std::format(
                    "Documentation audit failure: function '{}.{}' has incomplete documentation in stdlib_catalog().\n"
                    "Name, signature, summary, and example must all be non-empty.",
                    mod.import_path, fn.name));
            }
        }

        // 3. Verify practical runnable code example exists
        auto practical_ex = detail::module_practical_example(mod.import_path);
        if (practical_ex.empty()) {
            return std::unexpected(std::format(
                "Documentation audit failure: module '{}' has no practical code example in detail::module_practical_example().\n"
                "To fix: add a complete, runnable Cfire example in src/docgen/catalog_examples.hpp detail::module_practical_example().",
                mod.import_path));
        }

        // 4. Verify practical code example is syntactically valid Cfire code
        auto toks = frontend::tokenize(mod.import_path + ".cf", practical_ex);
        if (!toks) {
            return std::unexpected(std::format(
                "Documentation audit failure: practical example for module '{}' failed lexing:\n{}\nSource:\n{}",
                mod.import_path, toks.error(), practical_ex));
        }
        frontend::Parser parser(std::string(mod.import_path) + ".cf", *toks);
        auto parse_res = parser.parse_file_result();
        if (!parse_res.ok()) {
            std::string errs;
            for (const auto& e : parse_res.errors) errs += "  - " + e + "\n";
            return std::unexpected(std::format(
                "Documentation audit failure: practical example for module '{}' has syntax errors:\n{}Source:\n{}",
                mod.import_path, errs, practical_ex));
        }
    }

    return {};
}

} // namespace cfire::doc
