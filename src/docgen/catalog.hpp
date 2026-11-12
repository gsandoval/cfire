#pragma once
// src/docgen/catalog.hpp — Single-Source-Of-Truth Metadata Catalog (`core:*` Stdlib & Semantic Diagnostics)

#include "catalog_types.hpp"
#include "catalog_examples.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema/builtin_methods.hpp"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cfire::doc {

// W4: the builtin-type surface (methods, associated items, `make`), rendered from src/sema/builtin_methods.hpp
// as a documentation-only module. It is not importable and is not part of stdlib_catalog().
[[nodiscard]] inline auto builtin_types_doc() -> const StdModuleDoc& {
    static const StdModuleDoc doc = [] {
        StdModuleDoc m;
        m.import_path = "builtin";
        m.ns_alias = "builtin";
        m.title = "Builtin Types: Methods, Associated Items & make";
        m.link_flags = "(always available)";
        m.overview = "Operations on values of builtin types are methods on the value (Q7): `s.trim()`, `xs.sort()`, `m.keys()`, `x.abs()`, `f.sqrt()`. "
                     "Searches return `T?`, never `-1` (Q8). Limits and parsing belong to the type (Q10): `i32.MAX`, `f64.NAN`, `i32.parse(s) -> i32?`. "
                     "`make` is the one construction builtin, and the type decides where memory lives (Q25): `make(T, args)` is a value, "
                     "`make(own[T], args)` / `make(shared[T], args)` are heap-owned, and `make([]T, len[, cap])` is a slice. In the table, `Self` is the receiver type, "
                     "`T` the element type and `K`/`V` the map key/value types.";
        struct Kind { std::uint8_t bit; std::string_view recv; std::string_view cpp; };
        constexpr Kind kinds[] = {
            {cfire::frontend::BmInt, "i64", "x"}, {cfire::frontend::BmFloat, "f64", "f"}, {cfire::frontend::BmStr, "str", "s"},
            {cfire::frontend::BmSlice, "[]T", "xs"}, {cfire::frontend::BmArray, "[N]T", "arr"}, {cfire::frontend::BmMap, "map[K]V", "m"},
        };
        for (const auto& k : kinds) {
            for (const auto& bm : cfire::frontend::kBuiltinMethods) {
                if ((bm.recv & k.bit) == 0) continue;
                std::string sig = cfire::frontend::builtin_method_signature(bm);
                sig.insert(3, std::format("({}: {}) ", k.cpp, k.recv));
                std::string lowered = bm.lowered ? std::format("cfire::bm::{}({}, ...)", bm.name, k.cpp) : std::string("(dedicated lowering)");
                m.functions.push_back({std::format("{}.{}", k.recv, bm.name), sig, lowered, std::string(bm.summary), std::string(bm.example)});
            }
        }
        for (const auto& c : cfire::frontend::kBuiltinAssocConsts) {
            const bool is_float = (c.recv & cfire::frontend::BmFloat) != 0;
            std::string_view t = is_float ? "f64" : "i64";
            std::string_view cpp_t = is_float ? std::string_view("double") : std::string_view("std::int64_t");
            std::string lowered = std::vformat(c.cpp, std::make_format_args(cpp_t));
            m.functions.push_back({std::format("{}.{}", t, c.name), std::format("const {}.{}: {}", is_float ? "<float>" : "<int>", c.name, c.name == "BITS" ? "u32" : "Self"),
                                   lowered, std::string(c.summary), std::format("v := {}.{}", is_float ? "f64" : "i32", c.name)});
        }
        m.functions.push_back({"T.parse", "fn T.parse(s: str) -> T?", "cfire::bm::parse<T>(s)",
                               "Parses a complete literal of any numeric type or `bool` (surrounding whitespace ignored; `bool` accepts `true`/`false`/`1`/`0`); `none` on invalid or out-of-range input.",
                               "port := u16.parse(arg) or 8080"});
        m.functions.push_back({"make(T)", "make(T) / make(T, args...)", "T{} / cfire_ctor_T(args...)",
                               "A value of `T`: default field values, or the type's constructor `fn (T) make(...) -> T` (one per type).",
                               "p := make(Point, 3, 4)"});
        m.functions.push_back({"make(own[T])", "make(own[T], args...) / make(shared[T], args...)", "cfire::make_own<T>(...) / cfire::make_shared_ptr<T>(...)",
                               "A heap-owned `T` (`unique_ptr` / `shared_ptr`), built from one `T` value or the constructor's arguments.",
                               "node := make(own[Node], Node{v: 1})"});
        m.functions.push_back({"make([]T)", "make([]T, len) / make([]T, len, cap)", "cfire::make_slice<T>(len[, cap])",
                               "`len` zero-filled elements, optionally with `cap` reserved; a negative size or `cap < len` panics.",
                               "buf := make([]byte, 0, 4096)"});
        m.functions.push_back({"make(map[K]V)", "make(map[K]V)", "cfire::Map<K, V>{}", "An empty map (iteration order is unspecified, Q27).",
                               "counts := make(map[str]i32)"});
        m.functions.push_back({"own.release", "fn (o: own[T]) release() -> *mut T", "o.release()",
                               "Gives up ownership for C interop (CF-181): returns the raw pointer and moves `o`, so any later use is `cfire/USE-AFTER-MOVE`. Not available on `shared[T]`.",
                               "c_api_take(buf.release())"});
        m.functions.push_back({"own.from_raw", "fn own[T].from_raw(p: *mut T) -> own[T]", "std::unique_ptr<T>(p)",
                               "Takes ownership of a pointer produced by `release` (CF-181). Adopting any other pointer is undefined behavior.",
                               "buf := own[Buffer].from_raw(c_api_give())"});
        return m;
    }();
    return doc;
}

// The modules shown in the reference: the builtin-type surface first, then stdlib_catalog().
[[nodiscard]] inline auto reference_modules() -> std::vector<StdModuleDoc> {
    std::vector<StdModuleDoc> out;
    out.push_back(builtin_types_doc());
    for (const auto& m : stdlib_catalog()) out.push_back(m);
    return out;
}

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
            else if (rel == "json.hpp") discovered.push_back({"core:json", "core/" + rel, false});
            else if (rel == "log.hpp") discovered.push_back({"core:log", "core/" + rel, false});
            else if (rel == "net.hpp") discovered.push_back({"core:net", "core/" + rel, false});
            else if (rel == "net/http.hpp") discovered.push_back({"core:net/http", "core/" + rel, false});
            else if (rel == "net/tls.hpp") discovered.push_back({"core:net/tls", "core/" + rel, false});
            else if (rel == "net/url.hpp") discovered.push_back({"core:net/url", "core/" + rel, false});
            else if (rel == "net/websocket.hpp") discovered.push_back({"core:net/websocket", "core/" + rel, false});
            else if (rel == "ssh.hpp") discovered.push_back({"ext:net/ssh", "core/" + rel, false});
            else if (rel == "os.hpp") discovered.push_back({"core:os", "core/" + rel, false});
            else if (rel == "regex.hpp") discovered.push_back({"core:regex", "core/" + rel, false});
            else if (rel == "sqlite.hpp") discovered.push_back({"ext:db/sqlite", "core/" + rel, false});
            else if (rel == "time.hpp") discovered.push_back({"core:time", "core/" + rel, false});
            else if (rel == "unicode.hpp") discovered.push_back({"core:unicode", "core/" + rel, false});
            else if (rel == "audio.hpp") discovered.push_back({"core:audio", "core/" + rel, false});
            else if (rel == "ai.hpp") discovered.push_back({"ext:ai", "core/" + rel, false});
            else if (rel == "webui.hpp") discovered.push_back({"ext:webui", "core/" + rel, false});
            else if (rel == "sodium.hpp") discovered.push_back({"ext:crypto/sodium", "core/" + rel, false});
            else if (rel == "keychain.hpp") discovered.push_back({"core:keychain", "core/" + rel, false});
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
